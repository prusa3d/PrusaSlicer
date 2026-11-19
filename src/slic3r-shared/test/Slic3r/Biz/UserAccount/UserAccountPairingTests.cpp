#include "Slic3r/Biz/Network/MockHttp.hpp"
#include "Slic3r/Biz/Network/MockHttpFactory.hpp"

#include "Slic3r/Biz/Network/ServiceConfig.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Biz/UserAccount/IPairingCommunication.hpp"
#include "Slic3r/Biz/UserAccount/IUserAccountSessionListener.hpp"
#include "Slic3r/Biz/UserAccount/UserAccountCommunicationPairing.hpp"
#include "Slic3r/Biz/UserAccount/UserAccountPairing.hpp"
#include "Slic3r/Biz/UserAccount/UserAccountTokenStore.hpp"
#include "Slic3r/TestUtils/MemorySecretStore.hpp"

#include <boost/beast/core/detail/base64.hpp>
#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/trompeloeil.hpp>

#include <atomic>
#include <chrono>
#include <ctime>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace Slic3r::Biz;
using namespace Slic3r::Biz::UserAccount;
using Step = Pairing::Poller::Step;

namespace {

const std::string SERVER = "http://192.168.1.10:8080";

std::string make_jwt(long long exp)
{
    const std::string json = nlohmann::json{{"exp", exp}}.dump();
    std::string encoded;
    encoded.resize(boost::beast::detail::base64::encoded_size(json.size()));
    encoded.resize(boost::beast::detail::base64::encode(encoded.data(), json.data(), json.size()));
    return "header." + encoded + ".signature";
}

std::string error_body(const std::string& code)
{
    return nlohmann::json{{"code", code}, {"message", "whatever"}}.dump();
}

std::string credential_body(
    const std::string& token,
    std::vector<std::string> scopes = {"operate"},
    const std::string& id           = "cred-1"
)
{
    return nlohmann::json{
        {"id", id},
        {"name", "PrusaSlicer"},
        {"subject", "user"},
        {"created_at", "2026-11-13T10:00:00Z"},
        {"expires_at", "2027-02-11T10:00:00Z"},
        {"scopes", scopes},
        {"origin", "app_pairing"},
        {"token", "pcl_opaque"},
        {"access_token", token},
    }
        .dump();
}

Pairing::Started started(int expires_in = 600, int interval = 5)
{
    return {"device-secret", "BCDF-GHJK", expires_in, interval};
}

} // namespace


TEST_CASE("Pairing request bodies follow the server contract", "[user_account][pairing]")
{
    const nlohmann::json start = nlohmann::json::parse(Pairing::start_request_body());
    CHECK(start["app_name"] == "PrusaSlicer");
    CHECK(start["scopes"] == nlohmann::json::array({"operate"}));

    const nlohmann::json poll = nlohmann::json::parse(Pairing::poll_request_body("abc"));
    CHECK(poll["device_code"] == "abc");
}

TEST_CASE("Pairing start reply is parsed", "[user_account][pairing]")
{
    const std::optional<Pairing::Started> parsed = Pairing::parse_started(
        nlohmann::json{
            {"device_code", "dev"},
            {"user_code", "BCDF-GHJK"},
            {"verification_uri", "http://evil/pair"},
            {"verification_uri_complete", "http://evil/pair?code=BCDF-GHJK"},
            {"expires_in", 600},
            {"interval", 5},
        }
            .dump()
    );
    REQUIRE(parsed);
    CHECK(parsed->device_code == "dev");
    CHECK(parsed->user_code == "BCDF-GHJK");
    CHECK(parsed->expires_in == 600);
    CHECK(parsed->interval == 5);

    CHECK_FALSE(Pairing::parse_started("{}"));
    CHECK_FALSE(Pairing::parse_started("not json"));
    CHECK_FALSE(Pairing::parse_started(nlohmann::json{{"user_code", "X"}}.dump()));
}

TEST_CASE("Page session errors are classified", "[user_account][page_session]")
{
    using Pairing::PageSessionError;
    CHECK(Pairing::classify_page_session_error(401, "") == PageSessionError::Unauthorized);
    CHECK(
        Pairing::classify_page_session_error(400, error_body("core.auth/TOKEN_INVALID"))
        == PageSessionError::Unauthorized
    );
    CHECK(
        Pairing::classify_page_session_error(403, error_body("core.auth/PAGE_SESSION_REFUSED"))
        == PageSessionError::Refused
    );
    CHECK(
        Pairing::classify_page_session_error(429, error_body("core.auth/PAGE_SESSION_LIMITED"))
        == PageSessionError::Limited
    );
    CHECK(Pairing::classify_page_session_error(0, "") == PageSessionError::Failed);
    CHECK(Pairing::classify_page_session_error(500, "") == PageSessionError::Failed);
}

TEST_CASE("Failed pairing requests are told apart", "[user_account][pairing]")
{
    CHECK(
        Pairing::failure_of(400, error_body("core.auth/APP_PAIRING_DENIED"))
        == PairingFailure::Denied
    );
    CHECK(
        Pairing::failure_of(400, error_body("core.auth/APP_PAIRING_EXPIRED"))
        == PairingFailure::Expired
    );
    CHECK(
        Pairing::failure_of(400, error_body("core.auth/APP_PAIRING_UNKNOWN"))
        == PairingFailure::Unknown
    );
    CHECK(
        Pairing::failure_of(429, error_body("core.auth/APP_PAIRING_LIMITED"))
        == PairingFailure::Limited
    );
    CHECK(Pairing::failure_of(429, "") == PairingFailure::Limited);
    CHECK(Pairing::failure_of(0, "") == PairingFailure::Unreachable);
    CHECK(Pairing::failure_of(502, "<html>") == PairingFailure::Unreachable);
    CHECK(
        Pairing::failure_of(400, error_body("core.auth/SOMETHING_ELSE"))
        == PairingFailure::ServerError
    );
}

TEST_CASE("Pending replies keep polling at the interval", "[user_account][pairing]")
{
    Pairing::Poller poller;
    poller.start(started(600, 5), 1000);

    const Step step = poller.on_reply(400, error_body("core.auth/APP_PAIRING_PENDING"), 1005);
    CHECK(step.kind == Step::Kind::PollIn);
    CHECK(step.seconds == 5);

    CHECK(poller.on_reply(0, "", 1010).kind == Step::Kind::PollIn);
    CHECK(poller.on_reply(502, "", 1015).kind == Step::Kind::PollIn);
}

TEST_CASE("Slow down raises the interval by five seconds each time", "[user_account][pairing]")
{
    Pairing::Poller poller;
    poller.start(started(600, 5), 1000);

    const std::string slow_down = error_body("core.auth/APP_PAIRING_SLOW_DOWN");
    CHECK(poller.on_reply(400, slow_down, 1005).seconds == 10);
    CHECK(poller.on_reply(400, slow_down, 1015).seconds == 15);
    CHECK(poller.on_reply(400, error_body("core.auth/APP_PAIRING_PENDING"), 1030).seconds == 15);
}

TEST_CASE(
    "Denied, expired, unknown, limited and unexpected replies end the pairing",
    "[user_account][pairing]"
)
{
    const std::vector<std::pair<std::string, PairingFailure>> cases{
        {"core.auth/APP_PAIRING_DENIED", PairingFailure::Denied},
        {"core.auth/APP_PAIRING_EXPIRED", PairingFailure::Expired},
        {"core.auth/APP_PAIRING_UNKNOWN", PairingFailure::Unknown},
        {"core.auth/APP_PAIRING_LIMITED", PairingFailure::Limited},
        {"core.auth/SOMETHING_ELSE", PairingFailure::ServerError},
    };
    for (const auto& [code, failure] : cases) {
        Pairing::Poller poller;
        poller.start(started(), 1000);
        const Step step = poller.on_reply(400, error_body(code), 1005);
        CHECK(step.kind == Step::Kind::Failed);
        CHECK(step.failure == failure);
        CHECK_FALSE(step.credential);
    }
}

TEST_CASE("Pairing expires locally at its deadline", "[user_account][pairing]")
{
    Pairing::Poller poller;
    poller.start(started(600, 5), 1000);
    CHECK(
        poller.on_reply(400, error_body("core.auth/APP_PAIRING_PENDING"), 1599).kind
        == Step::Kind::PollIn
    );

    const Step step = poller.on_reply(400, error_body("core.auth/APP_PAIRING_PENDING"), 1600);
    CHECK(step.kind == Step::Kind::Failed);
    CHECK(step.failure == PairingFailure::Expired);
}

TEST_CASE("An approved pairing yields the credential", "[user_account][pairing]")
{
    Pairing::Poller poller;
    poller.start(started(), 1000);
    const std::string token = make_jwt(std::time(nullptr) + 90 * 24 * 3600);

    const Step step = poller.on_reply(201, credential_body(token), 1005);
    CHECK(step.kind == Step::Kind::Succeeded);
    REQUIRE(step.credential);
    CHECK(step.credential->id == "cred-1");
    CHECK(step.credential->token == token);
    CHECK(step.credential->expires_at > std::time(nullptr) + 89 * 24 * 3600);

    CHECK(poller.on_reply(200, "{}", 1010).failure == PairingFailure::ServerError);
}

TEST_CASE("A credential without the operate scope is refused", "[user_account][pairing]")
{
    const std::string token = make_jwt(std::time(nullptr) + 3600);
    Pairing::Poller poller;
    poller.start(started(), 1000);
    const Step step = poller.on_reply(201, credential_body(token, {"read"}), 1005);
    CHECK(step.kind == Step::Kind::Failed);
    CHECK(step.failure == PairingFailure::NoOperateScope);
    REQUIRE(step.credential);
    CHECK(step.credential->id == "cred-1");

    nlohmann::json without_scopes = nlohmann::json::parse(credential_body(token));
    without_scopes.erase("scopes");
    CHECK(
        poller.on_reply(201, without_scopes.dump(), 1010).failure == PairingFailure::NoOperateScope
    );
}


TEST_CASE("Offline credential record round-trips", "[user_account][pairing]")
{
    const TokenStore::ConnectLocalCredential
        credential{"tok.en.jwt", "cred-1", 1800000000, "https://pcl.local:8443/a|b"};
    const std::string record = TokenStore::serialize_connect_local_credential(credential);
    CHECK(record == "tok.en.jwt|cred-1|1800000000|https://pcl.local:8443/a|b");

    const std::optional<TokenStore::ConnectLocalCredential> parsed =
        TokenStore::parse_connect_local_credential(record);
    REQUIRE(parsed);
    CHECK(parsed->token == credential.token);
    CHECK(parsed->credential_id == credential.credential_id);
    CHECK(parsed->expires_at == credential.expires_at);
    CHECK(parsed->server_url == credential.server_url);

    CHECK_FALSE(TokenStore::parse_connect_local_credential(""));
    CHECK_FALSE(TokenStore::parse_connect_local_credential("|||"));
    CHECK_FALSE(TokenStore::parse_connect_local_credential("||||"));
    CHECK_FALSE(TokenStore::parse_connect_local_credential("tok|id|notanumber|http://x"));
    CHECK_FALSE(TokenStore::parse_connect_local_credential("tok|id|1"));
}


namespace {

class PairingMockHttp : public Network::MockHttp
{
public:
    PairingMockHttp(std::string url, Network::IHttp::RetryFn retryfn) :
        MockHttp(std::move(url), std::move(retryfn))
    {}

    std::vector<std::unique_ptr<trompeloeil::expectation>> allowances;
};

struct Request
{
    Network::IHttp::RequestMethod method;
    std::string url;
    std::map<std::string, std::string> headers;
};

using Responder = std::function<void(const Request&, Network::MockHttp&)>;

class RecordingListener : public IUserAccountSessionListener, public IPairingListener
{
public:
    void on_action_retry(const Network::IHttp::Retry&) override {}

    void on_action_success(ActionSuccessType type, std::string) override
    {
        successes.push_back(type);
    }

    void on_action_fail(ActionFailType, std::string) override {}

    void on_enqueued_refresh() override {}

    void on_new_refresh_time(long long) override {}

    void on_race_lost(const std::string&) override {}

    void on_logged_out(bool, bool) override
    {
        ++logged_out;
    }

    void on_printables_secret_token(const std::string&) override {}

    void on_pairing_ready(const std::string& page_url, const std::string& user_code) override
    {
        ready.emplace_back(page_url, user_code);
    }

    void on_pairing_failed(PairingFailure failure) override
    {
        failures.push_back(failure);
    }

    void on_pairing_approved() override
    {
        approved_before_account = successes.empty();
        ++approved;
    }

    void on_credential_expires_soon(std::time_t expires_at) override
    {
        expiring.push_back(expires_at);
    }

    std::vector<ActionSuccessType> successes;
    int logged_out{0};
    std::vector<std::pair<std::string, std::string>> ready;
    std::vector<PairingFailure> failures;
    int approved{0};
    bool approved_before_account{false};
    std::vector<std::time_t> expiring;
};

/**
 * @brief The communication runs its requests on its own threads (session, revocation) and its
 * timers and results on the app's main thread dispatcher, which the test thread drains.
 */
struct PairingFixture
{
    PairingFixture()
    {
        Network::configure_http_factory_with_mock();
        Network::ServiceConfig::instance().set_mode(Network::ServiceMode::ConnectLocal, SERVER);

        auto store = std::make_unique<Tests::MemorySecretStore>();
        secrets    = store.get();
        Platform::PlatformServices::instance().set_secret_store(std::move(store));
        store_available = TokenStore::save_connect_local_credential({});

        Network::MockHttpProvider::get().set_create_fn(
            [this](
                Network::IHttp::RequestMethod method,
                const std::string& url,
                Network::IHttp::RetryFn retryfn
            )
            {
                auto http    = std::make_unique<PairingMockHttp>(url, std::move(retryfn));
                size_t index = 0;
                {
                    std::lock_guard lock(mutex);
                    index = requests.size();
                    requests.push_back({method, url, {}});
                }
                http->allowances.push_back(
                    NAMED_ALLOW_CALL(*http, header_mock(trompeloeil::_, trompeloeil::_))
                        .SIDE_EFFECT(record_header(index, _1, _2))
                );
                http->allowances.push_back(
                    NAMED_ALLOW_CALL(*http, set_post_body_str_mock(trompeloeil::_))
                );
                http->allowances.push_back(
                    NAMED_ALLOW_CALL(*http, timeout_connection_mock(trompeloeil::_))
                );
                http->allowances.push_back(
                    NAMED_ALLOW_CALL(*http, timeout_total_mock(trompeloeil::_))
                );
                http->set_perform_override_fn(
                    [this, index](Network::MockHttp* performed, const Network::HttpRetryOpt&)
                    {
                        Responder responder;
                        Request performed_request;
                        {
                            std::lock_guard lock(mutex);
                            responder         = respond;
                            performed_request = requests[index];
                        }
                        if (responder) {
                            responder(performed_request, *performed);
                        }
                        ++responses;
                    }
                );
                return http;
            }
        );
    }

    ~PairingFixture()
    {
        communication.reset();
        dispatcher.dispatch_enqueued();
        Network::ServiceConfig::instance().set_mode(Network::ServiceMode::Cloud);
    }

    void create()
    {
        communication = std::make_unique<UserAccountCommunicationPairing>(dispatcher);
        communication->add_session_listener(&listener);
        REQUIRE(communication->pairing() == communication.get());
        communication->pairing()->set_pairing_listener(&listener);
    }

    void set_respond(Responder responder)
    {
        std::lock_guard lock(mutex);
        respond = std::move(responder);
    }

    void record_header(size_t index, const std::string& key, const std::string& value)
    {
        std::lock_guard lock(mutex);
        requests[index].headers[key] = value;
    }

    void store(
        const std::string& token,
        const std::string& server = SERVER,
        const std::string& id     = "cred-1"
    )
    {
        TokenStore::save_connect_local_credential({token, id, std::time(nullptr) + 3600, server});
    }

    std::optional<TokenStore::ConnectLocalCredential> stored() const
    {
        return TokenStore::read_connect_local_credential().credential;
    }

    /** @brief Runs the main thread events (results, timers) until pred holds. */
    bool run_until(
        const std::function<bool()>& pred,
        std::chrono::milliseconds timeout = std::chrono::seconds(5)
    )
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            dispatcher.dispatch_enqueued();
            if (pred()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    }

    /** @brief Waits until `count` requests were answered in total and their results are handled. */
    bool answered(size_t count)
    {
        const bool done = run_until([&]() { return responses >= count; });
        dispatcher.dispatch_enqueued();
        return done;
    }

    std::vector<Request> all_requests() const
    {
        std::lock_guard lock(mutex);
        return requests;
    }

    size_t count(Network::IHttp::RequestMethod method, const std::string& url) const
    {
        size_t n = 0;
        for (const Request& request : all_requests()) {
            n += request.method == method && request.url == url ? 1 : 0;
        }
        return n;
    }

    Platform::IMainThreadDispatcher& dispatcher{
        Platform::PlatformServices::instance().main_thread_dispatcher()
    };
    Tests::MemorySecretStore* secrets{nullptr};
    bool store_available{false};
    mutable std::mutex mutex;
    std::vector<Request> requests;
    Responder respond;
    std::atomic<size_t> responses{0};
    RecordingListener listener;
    std::unique_ptr<UserAccountCommunicationPairing> communication;
};

const std::string ME_URL =
    SERVER + "/api/v1/modules/com.github.aux.prusa3d.prusaslicer3-compat/account/api/v1/me/";
const std::string PAIRING_URL       = SERVER + "/api/v1/auth/app-pairings";
const std::string PAIRING_TOKEN_URL = PAIRING_URL + "/token";
const std::string PAGE_SESSION_URL  = PAIRING_URL + "/session";
constexpr auto DELETE               = Network::IHttp::RequestMethod::Delete;
constexpr auto GET                  = Network::IHttp::RequestMethod::Get;
constexpr auto POST                 = Network::IHttp::RequestMethod::Post;

std::string credential_url(const std::string& id)
{
    return SERVER + "/api/v1/auth/credentials/" + id;
}

std::string started_body()
{
    return nlohmann::json{
        {"device_code", "dev"},
        {"user_code", "BCDF-GHJK"},
        {"expires_in", 600},
        {"interval", 1}
    }
        .dump();
}

void me_ok(Network::MockHttp& http)
{
    http.invoke_complete(R"({"public_username":"pat","id":"1"})", 200);
}

void unreachable(Network::MockHttp& http)
{
    http.invoke_error("", "could not connect", 0);
}

void unauthorized(Network::MockHttp& http)
{
    http.invoke_error("{}", "unauthorized", 401);
}

/**
 * @brief Answers the pairing endpoints with an approval of `token`, /me with `me`, DELETE with 204.
 */
Responder pairing_server(
    const std::string& token,
    std::function<void(Network::MockHttp&)> me,
    const std::string& id = "cred-2"
)
{
    return [token, me, id](const Request& request, Network::MockHttp& http)
    {
        if (request.method == DELETE) {
            http.invoke_complete("", 204);
        } else if (request.url == PAIRING_URL) {
            http.invoke_complete(started_body(), 201);
        } else if (request.url == PAIRING_TOKEN_URL) {
            http.invoke_complete(credential_body(token, {"operate"}, id), 201);
        } else if (request.url == ME_URL) {
            me(http);
        }
    };
}

/** @brief Answers every request with `answer`. */
Responder all(std::function<void(Network::MockHttp&)> answer)
{
    return [answer](const Request&, Network::MockHttp& http) { answer(http); };
}

} // namespace

TEST_CASE_METHOD(
    PairingFixture,
    "A stored credential of another server is kept but not used",
    "[user_account][pairing]"
)
{
    if (!store_available) {
        SKIP("Token store is not compiled in this build.");
    }
    store(make_jwt(std::time(nullptr) + 3600), "http://other-server:8080");
    create();
    communication->init();

    CHECK(communication->access_token().empty());
    REQUIRE(stored());
    CHECK(stored()->server_url == "http://other-server:8080");
    CHECK(all_requests().empty());
}

TEST_CASE_METHOD(
    PairingFixture,
    "A stored credential is tested with the server on start",
    "[user_account][pairing]"
)
{
    if (!store_available) {
        SKIP("Token store is not compiled in this build.");
    }
    const std::string token = make_jwt(std::time(nullptr) + 3600);
    store(token);
    set_respond(all(me_ok));
    create();
    communication->init();
    CHECK(communication->access_token() == token);

    REQUIRE(answered(1));
    const std::vector<Request> requests = all_requests();
    REQUIRE(requests.size() == 1);
    CHECK(requests[0].url == ME_URL);
    CHECK(requests[0].headers.at("Authorization") == "Bearer " + token);
    CHECK(listener.successes == std::vector<ActionSuccessType>{ActionSuccessType::UserID});
}

TEST_CASE_METHOD(
    PairingFixture,
    "A refused credential logs out without a revoke",
    "[user_account][pairing]"
)
{
    if (!store_available) {
        SKIP("Token store is not compiled in this build.");
    }
    store(make_jwt(std::time(nullptr) + 3600));
    set_respond(all(unauthorized));
    create();
    communication->init();

    REQUIRE(run_until([&]() { return listener.logged_out == 1; }));
    CHECK(communication->access_token().empty());
    CHECK_FALSE(stored());
    CHECK(count(DELETE, credential_url("cred-1")) == 0);
}

TEST_CASE_METHOD(
    PairingFixture,
    "An unreachable server keeps the credential",
    "[user_account][pairing]"
)
{
    if (!store_available) {
        SKIP("Token store is not compiled in this build.");
    }
    const std::string token = make_jwt(std::time(nullptr) + 3600);
    store(token);
    set_respond(all(unreachable));
    create();
    communication->init();

    REQUIRE(answered(1));
    CHECK(communication->access_token() == token);
    CHECK(stored());
    CHECK(listener.logged_out == 0);
}

TEST_CASE_METHOD(
    PairingFixture,
    "A stored credential without an account is kept for another check",
    "[user_account][pairing]"
)
{
    if (!store_available) {
        SKIP("Token store is not compiled in this build.");
    }
    const std::string token = make_jwt(std::time(nullptr) + 3600);
    store(token);
    set_respond(all([](Network::MockHttp& http) { http.invoke_complete(R"({"id":"1"})", 200); }));
    create();
    communication->init();
    REQUIRE(answered(1));

    communication->on_user_id_unusable();
    CHECK_FALSE(communication->is_logged_in());
    CHECK(communication->access_token() == token);
    CHECK(stored());
    CHECK(count(DELETE, credential_url("cred-1")) == 0);
}

TEST_CASE_METHOD(
    PairingFixture,
    "A 401 elsewhere logs out only for the current token",
    "[user_account][pairing]"
)
{
    if (!store_available) {
        SKIP("Token store is not compiled in this build.");
    }
    const std::string token = make_jwt(std::time(nullptr) + 3600);
    store(token);
    create();
    communication->init();

    communication->on_unauthorized("an-older-token");
    CHECK(communication->access_token() == token);

    communication->on_unauthorized(token);
    CHECK(communication->access_token().empty());
    CHECK_FALSE(stored());
}

TEST_CASE_METHOD(
    PairingFixture,
    "A page session is opened with the credential as bearer and reused",
    "[user_account][page_session]"
)
{
    if (!store_available) {
        SKIP("Token store is not compiled in this build.");
    }
    const std::string token = make_jwt(std::time(nullptr) + 3600);
    store(token);
    set_respond(all(me_ok));
    create();
    communication->init();
    REQUIRE(answered(1));

    std::vector<std::optional<std::string>> replies;
    const auto record = [&replies](std::optional<std::string> reply)
    { replies.push_back(std::move(reply)); };
    set_respond(
        all([](Network::MockHttp& http) { http.invoke_complete(R"({"cookie":{}})", 200); })
    );
    communication->enqueue_page_session_action(record, false);
    REQUIRE(answered(2));
    const std::vector<Request> requests = all_requests();
    CHECK(requests.back().method == POST);
    CHECK(requests.back().url == PAGE_SESSION_URL);
    CHECK(requests.back().headers.at("Authorization") == "Bearer " + token);
    REQUIRE(replies.size() == 1);
    CHECK(replies[0] == R"({"cookie":{}})");

    communication->enqueue_page_session_action(record, false);
    REQUIRE(run_until([&]() { return replies.size() == 2; }));
    CHECK(replies[1] == R"({"cookie":{}})");
    CHECK(count(POST, PAGE_SESSION_URL) == 1);

    set_respond(all([](Network::MockHttp& http) { http.invoke_error("{}", "limited", 429); }));
    communication->enqueue_page_session_action(record, true);
    REQUIRE(answered(3));
    REQUIRE(replies.size() == 3);
    CHECK_FALSE(replies[2]);
    CHECK(communication->access_token() == token);

    set_respond(all(unauthorized));
    communication->enqueue_page_session_action(record, true);
    REQUIRE(answered(4));
    REQUIRE(run_until([&]() { return communication->access_token().empty(); }));
    CHECK(replies.size() == 3);
}

TEST_CASE_METHOD(
    PairingFixture,
    "Logging out revokes the credential and empties the store",
    "[user_account][pairing]"
)
{
    if (!store_available) {
        SKIP("Token store is not compiled in this build.");
    }
    const std::string token = make_jwt(std::time(nullptr) + 3600);
    store(token);
    set_respond(all(unreachable));
    create();
    communication->init();
    REQUIRE(answered(1));

    set_respond(all([](Network::MockHttp& http) { http.invoke_complete("", 204); }));
    communication->do_log_out(true);

    CHECK(communication->access_token().empty());
    CHECK_FALSE(stored());
    REQUIRE(answered(2));
    CHECK(count(DELETE, credential_url("cred-1")) == 1);
    for (const Request& request : all_requests()) {
        if (request.method == DELETE) {
            CHECK(request.headers.at("Authorization") == "Bearer " + token);
        }
    }
}

TEST_CASE_METHOD(
    PairingFixture,
    "Pairing shows the page built from the server URL",
    "[user_account][pairing]"
)
{
    set_respond(
        [](const Request& request, Network::MockHttp& http)
        {
            http.invoke_complete(
                nlohmann::json{
                    {"device_code", "dev"},
                    {"user_code", "BCDF-GHJK"},
                    {"verification_uri", "http://spoofed-host/pair"},
                    {"expires_in", 600},
                    {"interval", 5},
                }
                    .dump(),
                201
            );
        }
    );
    create();
    communication->init();
    communication->start_pairing();
    REQUIRE(answered(1));

    const std::vector<Request> requests = all_requests();
    REQUIRE(requests.size() == 1);
    CHECK(requests[0].method == POST);
    CHECK(requests[0].url == PAIRING_URL);
    CHECK(requests[0].headers.count("Authorization") == 0);
    CHECK(requests[0].headers.at("Content-Type") == "application/json");
    REQUIRE(listener.ready.size() == 1);
    CHECK(listener.ready[0].first == SERVER + "/pair?code=BCDF-GHJK");
    CHECK(listener.ready[0].second == "BCDF-GHJK");
}

TEST_CASE_METHOD(
    PairingFixture,
    "A rate limited pairing start is reported",
    "[user_account][pairing]"
)
{
    set_respond(all(
        [](Network::MockHttp& http)
        { http.invoke_error(error_body("core.auth/APP_PAIRING_LIMITED"), "too many", 429); }
    ));
    create();
    communication->init();
    communication->start_pairing();

    REQUIRE(run_until([&]() { return !listener.failures.empty(); }));
    CHECK(listener.ready.empty());
    CHECK(listener.failures == std::vector<PairingFailure>{PairingFailure::Limited});
}

TEST_CASE_METHOD(
    PairingFixture,
    "Another instance's credential is adopted on a store message",
    "[user_account][pairing]"
)
{
    if (!store_available) {
        SKIP("Token store is not compiled in this build.");
    }
    create();
    communication->init();
    CHECK(communication->access_token().empty());

    const std::string token = make_jwt(std::time(nullptr) + 3600);
    store(token);
    communication->on_read_token_store_message();
    CHECK(communication->access_token() == token);

    TokenStore::clear_connect_local_credential();
    communication->on_read_token_store_message();
    CHECK(communication->access_token().empty());
    CHECK(count(DELETE, credential_url("cred-1")) == 0);
}

TEST_CASE_METHOD(
    PairingFixture,
    "Signing in with an unconfirmed credential checks it again",
    "[user_account][pairing]"
)
{
    if (!store_available) {
        SKIP("Token store is not compiled in this build.");
    }
    const std::string token = make_jwt(std::time(nullptr) + 3600);
    store(token);
    set_respond(pairing_server(make_jwt(std::time(nullptr) + 7200), unreachable));
    create();
    communication->init();
    REQUIRE(answered(1));
    REQUIRE(communication->access_token() == token);

    communication->start_pairing();

    REQUIRE(run_until([&]() { return !listener.failures.empty(); }));
    CHECK(listener.failures == std::vector<PairingFailure>{PairingFailure::AccountUnavailable});
    CHECK(communication->access_token() == token);
    REQUIRE(stored());
    CHECK(stored()->token == token);
    CHECK(count(DELETE, credential_url("cred-1")) == 0);
    CHECK(count(POST, PAIRING_URL) == 0);
}

TEST_CASE_METHOD(
    PairingFixture,
    "An unreachable account after pairing keeps the credential",
    "[user_account][pairing]"
)
{
    if (!store_available) {
        SKIP("Token store is not compiled in this build.");
    }
    const std::string token = make_jwt(std::time(nullptr) + 3600);
    set_respond(pairing_server(
        token,
        [](Network::MockHttp& http) { http.invoke_error("", "bad gateway", 502); }
    ));
    create();
    communication->init();
    communication->start_pairing();

    REQUIRE(run_until([&]() { return !listener.failures.empty(); }));
    CHECK(listener.failures == std::vector<PairingFailure>{PairingFailure::AccountUnavailable});
    CHECK(communication->access_token() == token);
    REQUIRE(stored());
    CHECK(count(DELETE, credential_url("cred-2")) == 0);
}

TEST_CASE_METHOD(
    PairingFixture,
    "A new credential refused by /me ends the sign-in",
    "[user_account][pairing]"
)
{
    if (!store_available) {
        SKIP("Token store is not compiled in this build.");
    }
    set_respond(pairing_server(make_jwt(std::time(nullptr) + 3600), unauthorized));
    create();
    communication->init();
    communication->start_pairing();

    REQUIRE(run_until([&]() { return !listener.failures.empty(); }));
    CHECK(listener.failures == std::vector<PairingFailure>{PairingFailure::CredentialLost});
    CHECK(communication->access_token().empty());
    CHECK_FALSE(stored());
}

TEST_CASE_METHOD(
    PairingFixture,
    "A new credential without an account is revoked",
    "[user_account][pairing]"
)
{
    if (!store_available) {
        SKIP("Token store is not compiled in this build.");
    }
    set_respond(pairing_server(
        make_jwt(std::time(nullptr) + 3600),
        [](Network::MockHttp& http) { http.invoke_complete(R"({"id":"1"})", 200); }
    ));
    create();
    communication->init();
    communication->start_pairing();
    REQUIRE(run_until([&]() { return !listener.successes.empty(); }));

    communication->on_user_id_unusable();
    CHECK(listener.failures == std::vector<PairingFailure>{PairingFailure::NoAccount});
    CHECK(communication->access_token().empty());
    CHECK_FALSE(stored());
    CHECK(run_until([&]() { return count(DELETE, credential_url("cred-2")) == 1; }));
}

TEST_CASE_METHOD(
    PairingFixture,
    "A pairing approved after it was cancelled is revoked",
    "[user_account][pairing]"
)
{
    const std::string token = make_jwt(std::time(nullptr) + 3600);
    set_respond(
        [&](const Request& request, Network::MockHttp& http)
        {
            if (request.url == PAIRING_URL) {
                http.invoke_complete(started_body(), 201);
            } else if (request.url == PAIRING_TOKEN_URL) {
                dispatcher.dispatch_on_main_thread([this]() { communication->cancel_pairing(); });
                http.invoke_complete(credential_body(token, {"operate"}, "cred-2"), 201);
            } else {
                http.invoke_complete("", 204);
            }
        }
    );
    create();
    communication->init();
    communication->start_pairing();

    REQUIRE(run_until([&]() { return count(DELETE, credential_url("cred-2")) == 1; }));
    CHECK(communication->access_token().empty());
}

TEST_CASE_METHOD(
    PairingFixture,
    "A refused credential gives way to another valid one in the store",
    "[user_account][pairing]"
)
{
    if (!store_available) {
        SKIP("Token store is not compiled in this build.");
    }
    const std::string old_token = make_jwt(std::time(nullptr) + 3600);
    store(old_token);
    create();
    communication->init();

    const std::string other_token = make_jwt(std::time(nullptr) + 7200);
    store(other_token, SERVER, "cred-2");
    communication->on_unauthorized(old_token);

    CHECK(communication->access_token() == other_token);
    CHECK(listener.logged_out == 0);
    CHECK(stored()->token == other_token);
}

TEST_CASE_METHOD(
    PairingFixture,
    "An unreadable store neither logs out nor drops anything",
    "[user_account][pairing]"
)
{
    if (!store_available) {
        SKIP("Token store is not compiled in this build.");
    }
    const std::string token = make_jwt(std::time(nullptr) + 3600);
    store(token);
    create();
    communication->init();

    secrets->fail_loading = true;
    communication->on_read_token_store_message();
    CHECK(communication->access_token() == token);

    secrets->fail_loading = false;
    CHECK(stored()->token == token);
}

TEST_CASE_METHOD(
    PairingFixture,
    "An unreadable store at start keeps its record",
    "[user_account][pairing]"
)
{
    if (!store_available) {
        SKIP("Token store is not compiled in this build.");
    }
    store(make_jwt(std::time(nullptr) + 3600));
    secrets->fail_loading = true;
    create();
    communication->init();
    CHECK(communication->access_token().empty());
    secrets->fail_loading = false;
    CHECK(stored());
}

TEST_CASE_METHOD(
    PairingFixture,
    "An account without a credential is ignored",
    "[user_account][pairing]"
)
{
    create();
    communication->init();
    communication->on_username_changed("pat", "1", true);
    CHECK_FALSE(communication->is_logged_in());
}

TEST_CASE_METHOD(
    PairingFixture,
    "Requested credential checks are rate limited",
    "[user_account][pairing]"
)
{
    if (!store_available) {
        SKIP("Token store is not compiled in this build.");
    }
    store(make_jwt(std::time(nullptr) + 3600));
    set_respond(all(me_ok));
    create();
    communication->init();
    REQUIRE(answered(1));
    REQUIRE(count(GET, ME_URL) == 1);

    communication->request_refresh();
    REQUIRE(answered(2));
    CHECK(count(GET, ME_URL) == 2);
    communication->request_refresh();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(count(GET, ME_URL) == 2);
}

TEST_CASE_METHOD(
    PairingFixture,
    "Replacing the communication neither revokes nor clears the store",
    "[user_account][pairing]"
)
{
    if (!store_available) {
        SKIP("Token store is not compiled in this build.");
    }
    const std::string token = make_jwt(std::time(nullptr) + 3600);
    store(token);
    create();
    communication->init();

    communication->log_out_locally(true);
    CHECK(communication->access_token().empty());
    communication.reset();
    CHECK(count(DELETE, credential_url("cred-1")) == 0);
    CHECK(stored()->token == token);
}

TEST_CASE_METHOD(
    PairingFixture,
    "The approval is announced before the account",
    "[user_account][pairing]"
)
{
    if (!store_available) {
        SKIP("Token store is not compiled in this build.");
    }
    set_respond(pairing_server(make_jwt(std::time(nullptr) + 3600), me_ok));
    create();
    communication->init();
    communication->start_pairing();

    REQUIRE(run_until([&]() { return !listener.successes.empty(); }));
    CHECK(listener.approved == 1);
    CHECK(listener.approved_before_account);
}
