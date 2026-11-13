#include "Slic3r/Biz/Network/Url.hpp"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

using namespace Slic3r::Biz::Network;

TEST_CASE("Parts of a URL", "[Url]")
{
    std::optional<UrlParts> url = parse_url("HTTPS://PrintFarm.local:8443/X?y#z");
    REQUIRE(url);
    CHECK(url->scheme == "https");
    CHECK(url->host == "printfarm.local");
    CHECK(url->port == 8443);
    CHECK(url->explicit_port);
    CHECK(url->path == "/X");
    CHECK(url->origin() == "https://printfarm.local:8443");

    // The default port of the scheme.
    url = parse_url("https://192.168.1.10");
    REQUIRE(url);
    CHECK(url->port == 443);
    CHECK_FALSE(url->explicit_port);
    CHECK(url->path == "/");
    CHECK(url->origin() == "https://192.168.1.10");

    // IPv6 without brackets, origin with them.
    url = parse_url("http://[FE80::1]/a/../b");
    REQUIRE(url);
    CHECK(url->host == "fe80::1");
    CHECK(url->is_ipv6());
    CHECK(url->port == 80);
    CHECK(url->path == "/a/../b");
    CHECK(url->origin() == "http://[fe80::1]");
    url = parse_url("http://[fe80::1%25en0]:8080");
    REQUIRE(url);
    CHECK(url->host == "fe80::1");
    CHECK(url->zone_id == "en0");
    CHECK(url->origin() == "http://[fe80::1%25en0]:8080");

    // Any scheme.
    url = parse_url("ssl://[::1]:8883");
    REQUIRE(url);
    CHECK(url->scheme == "ssl");
    CHECK(url->host == "::1");
    CHECK(url->port == 8883);

    CHECK_FALSE(parse_url("printfarm.local").has_value());
    CHECK_FALSE(parse_url("https://:443").has_value());
    CHECK_FALSE(parse_url("https://host:99999").has_value());
    CHECK_FALSE(parse_url("https://host:0").has_value());
    CHECK_FALSE(parse_url("https://ho st").has_value());
    CHECK_FALSE(parse_url("file:///etc/passwd").has_value());
    CHECK_FALSE(parse_url("").has_value());
}

TEST_CASE("URLs under the Connect Local server", "[Url]")
{
    const std::string server = "https://printfarm.local:8443";
    CHECK(url_belongs_to_server(server, server));
    CHECK(url_belongs_to_server(server + "/", server));
    CHECK(url_belongs_to_server(server + "/api/v1/modules/x/connect/slicer/mqtt", server));
    CHECK(url_belongs_to_server(server + "?a=b", server));
    CHECK(url_belongs_to_server("HTTPS://PrintFarm.Local:8443/Path", server));
    CHECK_FALSE(url_belongs_to_server("https://printfarm.local:84430/", server));
    CHECK_FALSE(url_belongs_to_server("https://printfarm.local.evil.com:8443/", server));
    CHECK_FALSE(url_belongs_to_server("https://user@printfarm.local.evil.com:8443/", server));
    CHECK_FALSE(url_belongs_to_server("http://printfarm.local:8443/", server));
    CHECK_FALSE(url_belongs_to_server("https://printfarm.local/", server));
    CHECK_FALSE(url_belongs_to_server("https://connect.prusa3d.com/", server));
    CHECK_FALSE(url_belongs_to_server(server, ""));
    // The default port, written out or not.
    CHECK(url_belongs_to_server("https://printfarm.local:443/x", "https://printfarm.local"));

    const std::string with_path = "https://10.0.0.5/connect";
    CHECK(url_belongs_to_server("https://10.0.0.5/connect/api", with_path));
    CHECK(url_belongs_to_server("https://10.0.0.5/connect#x", with_path));
    CHECK_FALSE(url_belongs_to_server("https://10.0.0.5/connector", with_path));
    CHECK_FALSE(url_belongs_to_server("https://10.0.0.5/Connect/api", with_path));
}

TEST_CASE("Resolving a URL reference", "[Url]")
{
    const std::string me = "https://printfarm.local:8443/account/api/v1/me/";
    // Absolute references pass through unchanged.
    CHECK(resolve_url("https://cdn.prusa3d.com/a/B.png", me) == "https://cdn.prusa3d.com/a/B.png");
    CHECK(resolve_url("HTTP://Other.local/x/../y.png?q", me) == "HTTP://Other.local/x/../y.png?q");
    // Relative references resolve under the base, the query kept.
    CHECK(
        resolve_url("avatar.png?i=D&c=fa6831", me)
        == "https://printfarm.local:8443/account/api/v1/me/avatar.png?i=D&c=fa6831"
    );
    CHECK(resolve_url("../x.png", me) == "https://printfarm.local:8443/account/api/v1/x.png");
    CHECK(resolve_url("/avatar.png", me) == "https://printfarm.local:8443/avatar.png");
    CHECK(resolve_url("avatar.png", "https://h/me") == "https://h/avatar.png");
    // Nothing to resolve.
    CHECK(resolve_url("", me).empty());
    CHECK(resolve_url("avatar.png", "").empty());
}
