#include "Slic3r/Biz/Network/MockHttp.hpp"
#include "Slic3r/Biz/Network/MockHttpFactory.hpp"

#include "Slic3r/App/Platform/StdMainThreadDispatcher.hpp"
#include "Slic3r/Biz/Platform/ISecretStore.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Biz/UserAccount/UserAccountSession.hpp"
#include "Slic3r/Biz/UserAccount/IUserAccountSessionListener.hpp"
#include "Slic3r/Biz/UserAccount/UserAccountTokenStore.hpp"

#include <boost/beast/core/detail/base64.hpp>
#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/trompeloeil.hpp>

#include <ctime>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace Slic3r::Biz;
using namespace Slic3r::Biz::UserAccount;

namespace {

class MemorySecretStore : public Platform::ISecretStore
{
public:
    bool save_secret(const std::string& opt, const std::string& usr, const std::string& psswd) override
    {
        m_secrets[opt] = {usr, psswd};
        return true;
    }

    bool load_secret(const std::string& opt, std::string& usr, std::string& psswd) override
    {
        const auto it = m_secrets.find(opt);
        if (it == m_secrets.end()) {
            return false;
        }
        usr    = it->second.first;
        psswd  = it->second.second;
        return true;
    }

private:
    std::map<std::string, std::pair<std::string, std::string>> m_secrets;
};

class AccountMockHttp : public Network::MockHttp
{
public:
    AccountMockHttp(std::string url, Network::IHttp::RetryFn retryfn) :
        MockHttp(std::move(url), std::move(retryfn))
    {}

    bool poll_cancel()
    {
        bool cancel = false;
        if (retryfn) {
            retryfn(Network::IHttp::Retry{1, 0, true}, cancel);
        }
        return cancel;
    }

    std::vector<std::unique_ptr<trompeloeil::expectation>> allowances;
};

class RecordingSessionListener : public IUserAccountSessionListener
{
public:
    struct LoggedOut
    {
        bool notify_owner;
        bool was_logged_in;
    };

    void on_action_retry(const Network::IHttp::Retry&) override {}
    void on_action_success(ActionSuccessType, std::string) override {}
    void on_action_fail(ActionFailType, std::string) override {}
    void on_enqueued_refresh() override {}
    void on_new_refresh_time(long long) override {}
    void on_race_lost(const std::string&) override {}
    void on_printables_secret_token(const std::string&) override {}

    void on_logged_out(bool notify_owner, bool was_logged_in) override
    {
        logged_out.push_back({notify_owner, was_logged_in});
    }

    std::vector<LoggedOut> logged_out;
};

std::string make_access_token(int valid_for_seconds)
{
    nlohmann::json payload;
    payload["exp"] = static_cast<long long>(std::time(nullptr)) + valid_for_seconds;
    const std::string json = payload.dump();

    std::string encoded;
    encoded.resize(boost::beast::detail::base64::encoded_size(json.size()));
    const size_t written = boost::beast::detail::base64::encode(encoded.data(), json.data(), json.size());
    encoded.resize(written);

    return "header." + encoded + ".signature";
}

std::string make_token_response(const std::string& access_token, const std::string& refresh_token, const std::string& shared_session_key)
{
    nlohmann::json body;
    body["access_token"]       = access_token;
    body["refresh_token"]      = refresh_token;
    body["shared_session_key"] = shared_session_key;
    return body.dump();
}

struct SessionFixture
{
    SessionFixture()
    {
        Network::configure_http_factory_with_mock();

        auto store   = std::make_unique<MemorySecretStore>();
        secret_store = store.get();
        Platform::PlatformServices::instance().set_secret_store(std::move(store));

        token_store_available = TokenStore::save_tokens({});

        Network::MockHttpProvider::get().set_create_fn(
            [this](Network::IHttp::RequestMethod, const std::string& url, Network::IHttp::RetryFn retryfn)
            {
                auto http = std::make_unique<AccountMockHttp>(url, std::move(retryfn));
                http->allowances.push_back(NAMED_ALLOW_CALL(*http, header_mock(trompeloeil::_, trompeloeil::_)));
                http->allowances.push_back(NAMED_ALLOW_CALL(*http, set_post_body_str_mock(trompeloeil::_)));
                http->set_perform_override_fn(
                    [this, url](Network::MockHttp* performed, const Network::HttpRetryOpt&)
                    {
                        performed_urls.push_back(url);
                        if (on_perform) {
                            on_perform(*performed);
                        }
                    }
                );
                return http;
            }
        );
    }

    ~SessionFixture()
    {
        dispatcher.close();
    }

    TokenStore::StoreData stored_tokens() const
    {
        TokenStore::StoreData data;
        TokenStore::load_tokens(data);
        return data;
    }

    MemorySecretStore* secret_store{nullptr};
    bool token_store_available{false};
    std::vector<std::string> performed_urls;
    std::function<void(Network::MockHttp&)> on_perform;

    Slic3r::App::Platform::StdMainThreadDispatcher dispatcher;
    UserAccountSession session{dispatcher};
};

} // namespace

TEST_CASE_METHOD(SessionFixture, "Stopped session does not start another request", "[user_account]")
{
    session.set_tokens("access-token", "refresh-token", "session-key", std::time(nullptr) + 3600);
    session.enqueue_test_with_refresh();

    on_perform = [this](Network::MockHttp& http)
    {
        session.stop();
        http.invoke_error({}, "aborted", 0);
    };

    session.process_action_queue();

    CHECK(performed_urls.size() == 1);
}

TEST_CASE_METHOD(SessionFixture, "Stopped session discards a token response", "[user_account]")
{
    session.set_tokens("access-token", "refresh-token", "session-key", std::time(nullptr) + 3600);
    session.enqueue_test_with_refresh();

    const TokenStore::StoreData before = stored_tokens();

    on_perform = [this](Network::MockHttp& http)
    {
        if (performed_urls.size() == 1) {
            http.invoke_error({}, "offline", 0);
            return;
        }
        session.stop();
        http.invoke_complete(
            make_token_response(make_access_token(3600), "rotated-refresh-token", "new-session-key"),
            200
        );
    };

    session.process_action_queue();

    CHECK(performed_urls.size() == 2);
    CHECK(session.get_refresh_token() == "refresh-token");
    if (token_store_available) {
        CHECK(stored_tokens().refresh_token == before.refresh_token);
    }
}

TEST_CASE_METHOD(SessionFixture, "Refreshed tokens are stored before the user id request", "[user_account]")
{
    if (!token_store_available) {
        SUCCEED("Token store is not compiled in this build.");
        return;
    }

    session.set_tokens("access-token", "refresh-token", "session-key", std::time(nullptr) + 3600);
    session.enqueue_test_with_refresh();

    std::string refresh_token_in_store_at_user_id;

    on_perform = [&](Network::MockHttp& http)
    {
        switch (performed_urls.size()) {
        case 1:
            http.invoke_error({}, "expired", 401);
            break;
        case 2:
            http.invoke_complete(
                make_token_response(make_access_token(3600), "rotated-refresh-token", "new-session-key"),
                200
            );
            break;
        default:
            refresh_token_in_store_at_user_id = stored_tokens().refresh_token;
            break;
        }
    };

    session.process_action_queue();

    CHECK(refresh_token_in_store_at_user_id == "rotated-refresh-token");
    CHECK(stored_tokens().access_token == session.get_access_token());
}

TEST_CASE_METHOD(SessionFixture, "Cancel applies to the whole queue drain", "[user_account]")
{
    session.set_tokens("access-token", "refresh-token", "session-key", std::time(nullptr) + 3600);
    session.enqueue_test_with_refresh();

    bool follow_up_was_cancelled = false;

    on_perform = [&](Network::MockHttp& http)
    {
        auto& mock = static_cast<AccountMockHttp&>(http);
        if (performed_urls.size() == 1) {
            session.cancel_ongoing_session_action();
            CHECK(mock.poll_cancel());
        } else {
            follow_up_was_cancelled = mock.poll_cancel();
        }
        http.invoke_error({}, "cancelled", 0);
    };

    session.process_action_queue();

    CHECK(performed_urls.size() == 2);
    CHECK(follow_up_was_cancelled);
}

TEST_CASE_METHOD(SessionFixture, "Logout reports whether a session was signed in", "[user_account]")
{
    RecordingSessionListener listener;
    session.add_listener<IUserAccountSessionListener>(&listener);

    session.set_tokens("access-token", "refresh-token", "session-key", std::time(nullptr) + 3600);
    session.do_clear(true);
    dispatcher.dispatch_enqueued();

    REQUIRE(listener.logged_out.size() == 1);
    CHECK_FALSE(listener.logged_out[0].was_logged_in);

    session.set_has_username(true);
    session.do_clear(true);
    dispatcher.dispatch_enqueued();

    REQUIRE(listener.logged_out.size() == 2);
    CHECK(listener.logged_out[1].was_logged_in);

    session.remove_listener<IUserAccountSessionListener>(&listener);
}
