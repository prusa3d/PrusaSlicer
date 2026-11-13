#include "Slic3r/App/Browser/BrowserCookie.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace Slic3r::App::Browser;

TEST_CASE("Cookie commands carry every attribute", "[page_session]")
{
    BrowserCookie cookie;
    cookie.url       = "https://pcl.local:8443/base";
    cookie.name      = "pcl_session";
    cookie.value     = "pcl_\"quoted\"";
    cookie.path      = "/base";
    cookie.http_only = true;
    cookie.secure    = true;
    cookie.same_site = "Lax";

    const std::optional<BrowserCookie> parsed =
        parse_cookie_set_command_data(cookie_set_command_data(cookie));
    REQUIRE(parsed);
    CHECK(parsed->url == cookie.url);
    CHECK(parsed->name == cookie.name);
    CHECK(parsed->value == cookie.value);
    CHECK(parsed->path == "/base");
    CHECK(parsed->http_only);
    CHECK(parsed->secure);
    CHECK(parsed->same_site == "Lax");

    CHECK_FALSE(parse_cookie_set_command_data("{}"));
    CHECK_FALSE(parse_cookie_set_command_data("not json"));
}

TEST_CASE("A page session reply is parsed", "[page_session]")
{
    const std::string body = R"({
        "expires_at": "2026-11-13T12:00:00Z",
        "cookie": {"name": "pcl_session", "value": "pcl_abc", "path": "/",
                   "expires": "2026-11-13T12:00:00Z", "http_only": true,
                   "same_site": "Strict", "secure": true}
    })";

    const std::optional<BrowserCookie> cookie = parse_page_session(body, "https://pcl.local:8443");
    REQUIRE(cookie);
    CHECK(cookie->url == "https://pcl.local:8443");
    CHECK(cookie->name == CONNECT_LOCAL_SESSION_COOKIE);
    CHECK(cookie->value == "pcl_abc");
    CHECK(cookie->path == "/");
    CHECK(cookie->http_only);
    CHECK(cookie->same_site == "Strict");
    CHECK(cookie->secure);

    CHECK_FALSE(parse_page_session("{}", "https://pcl.local"));
    CHECK_FALSE(parse_page_session(R"({"cookie":{"name":"pcl_session"}})", "https://pcl.local"));
    CHECK_FALSE(parse_page_session(R"({"cookie":"pcl_session"})", "https://pcl.local"));
    CHECK_FALSE(parse_page_session(
        R"({"cookie":{"name":"other_session","value":"pcl_abc"}})",
        "https://pcl.local"
    ));
    CHECK_FALSE(parse_page_session("not json", "https://pcl.local"));
}
