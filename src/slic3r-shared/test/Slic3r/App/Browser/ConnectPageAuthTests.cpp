#include "Slic3r/App/Browser/BrowserCookie.hpp"
#include "Slic3r/App/Browser/ConnectPageAuth.hpp"

#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace Slic3r::App::Browser;

namespace {

const std::string SESSION_REPLY = R"({
    "cookie": {"name": "pcl_session", "value": "pcl_abc", "path": "/", "http_only": true,
               "same_site": "Strict", "secure": false}
})";

struct AuthFixture
{
    IConnectPageAuth::Commands load_page() const
    {
        return {{BrowserLogicCommandType::LoadURL, std::string("http://pcl.local:8080/")}};
    }

    IConnectPageAuth::PushFn push()
    {
        return [this](IConnectPageAuth::Commands commands) { pushed.push_back(std::move(commands)); };
    }

    IConnectPageAuth::FallbackFn error_page() const
    {
        return []()
        {
            return IConnectPageAuth::Commands{
                {BrowserLogicCommandType::LoadResourcesPage, std::string("connect_error")}
            };
        };
    }

    std::unique_ptr<ConnectPageSessionAuth> session_auth(const std::string& server_url)
    {
        return std::make_unique<ConnectPageSessionAuth>(
            server_url,
            [this](ConnectPageSessionAuth::SessionReplyFn done, bool fresh)
            {
                requested_fresh.push_back(fresh);
                pending_reply = std::move(done);
            }
        );
    }

    std::vector<IConnectPageAuth::Commands> pushed;
    std::vector<bool> requested_fresh;
    ConnectPageSessionAuth::SessionReplyFn pending_reply;
};

} // namespace

TEST_CASE_METHOD(AuthFixture, "Prusa Connect pages get the token and load at once", "[connect-page]")
{
    ConnectTokenAuth auth;

    CHECK(auth.pages_get_token());
    CHECK(auth.page_js_condition() == "location.protocol === 'https:'");

    auth.prepare_page(load_page(), error_page(), false, push());
    REQUIRE(pushed.size() == 1);
    REQUIRE(pushed[0].size() == 1);
    CHECK(pushed[0][0].type == BrowserLogicCommandType::LoadURL);

    nlohmann::json init_options = {{"language", "en"}};
    auth.add_credentials(init_options, "access-token");
    CHECK(init_options["accessToken"] == "access-token");
}

TEST_CASE_METHOD(AuthFixture, "Connect Local pages never get the credential", "[connect-page]")
{
    const std::unique_ptr<ConnectPageSessionAuth> auth = session_auth("http://pcl.local:8080");

    CHECK_FALSE(auth->pages_get_token());

    nlohmann::json init_options = {{"language", "en"}};
    auth->add_credentials(init_options, "credential");
    CHECK_FALSE(init_options.contains("accessToken"));
}

TEST_CASE_METHOD(AuthFixture, "Connect Local page scripts follow the scheme of the server", "[connect-page]")
{
    CHECK(
        session_auth("http://pcl.local:8080")->page_js_condition()
        == "(location.protocol === 'https:' || location.protocol === 'http:')"
    );
    CHECK(
        session_auth("https://pcl.local:8443")->page_js_condition()
        == "location.protocol === 'https:'"
    );
}

TEST_CASE_METHOD(AuthFixture, "A page session sets its cookie before the page loads", "[connect-page]")
{
    const std::unique_ptr<ConnectPageSessionAuth> auth = session_auth("http://pcl.local:8080");

    auth->prepare_page(load_page(), error_page(), false, push());
    REQUIRE(requested_fresh == std::vector<bool>{false});
    CHECK(pushed.empty());

    pending_reply(SESSION_REPLY);
    REQUIRE(pushed.size() == 1);
    REQUIRE(pushed[0].size() == 2);
    CHECK(pushed[0][0].type == BrowserLogicCommandType::SetCookie);
    CHECK(pushed[0][1].type == BrowserLogicCommandType::LoadURL);

    const std::optional<BrowserCookie> cookie = parse_cookie_set_command_data(pushed[0][0].data);
    REQUIRE(cookie);
    CHECK(cookie->url == "http://pcl.local:8080");
    CHECK(cookie->name == CONNECT_LOCAL_SESSION_COOKIE);
    CHECK(cookie->value == "pcl_abc");
}

TEST_CASE_METHOD(AuthFixture, "A fresh page session is asked for as fresh", "[connect-page]")
{
    const std::unique_ptr<ConnectPageSessionAuth> auth = session_auth("http://pcl.local:8080");

    auth->prepare_page(load_page(), error_page(), true, push());

    CHECK(requested_fresh == std::vector<bool>{true});
}

TEST_CASE_METHOD(AuthFixture, "Without a page session the fallback replaces the page", "[connect-page]")
{
    const std::unique_ptr<ConnectPageSessionAuth> auth = session_auth("http://pcl.local:8080");

    SECTION("the server gave no session")
    {
        auth->prepare_page(load_page(), error_page(), false, push());
        pending_reply(std::nullopt);
    }
    SECTION("the reply is unreadable")
    {
        auth->prepare_page(load_page(), error_page(), false, push());
        pending_reply(std::string("not json"));
    }

    REQUIRE(pushed.size() == 1);
    REQUIRE(pushed[0].size() == 1);
    CHECK(pushed[0][0].type == BrowserLogicCommandType::LoadResourcesPage);
}

TEST_CASE_METHOD(AuthFixture, "Without a page session and a fallback the page loads anyway", "[connect-page]")
{
    const std::unique_ptr<ConnectPageSessionAuth> auth = session_auth("http://pcl.local:8080");

    auth->prepare_page(load_page(), {}, false, push());
    pending_reply(std::nullopt);

    REQUIRE(pushed.size() == 1);
    REQUIRE(pushed[0].size() == 1);
    CHECK(pushed[0][0].type == BrowserLogicCommandType::LoadURL);
}

TEST_CASE_METHOD(AuthFixture, "A page session reply after its logic is gone is dropped", "[connect-page]")
{
    std::unique_ptr<ConnectPageSessionAuth> auth = session_auth("http://pcl.local:8080");

    auth->prepare_page(load_page(), error_page(), false, push());
    auth.reset();
    pending_reply(SESSION_REPLY);

    CHECK(pushed.empty());
}
