#include "Slic3r/Biz/ConnectLocal/ConnectLocalDiscovery.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <vector>

using namespace Slic3r::Biz::ConnectLocal;

TEST_CASE("An announced instance is read into something addressable", "[connect-local]")
{
    const auto found = discovered_from_reply(
        "Workshop",
        "workshop.local.",
        "192.168.1.50",
        8080,
        {{TXT_NAME, "Workshop"}, {TXT_VERSION, "0.1.0"}, {TXT_TLS, "0"}}
    );

    REQUIRE(found.has_value());
    CHECK(found->name == "Workshop");
    CHECK(found->hostname == "workshop.local");
    CHECK(found->address == "192.168.1.50");
    CHECK(found->port == 8080);
    CHECK_FALSE(found->tls);
    CHECK(found->version == "0.1.0");
    CHECK(found->base_url() == "http://workshop.local:8080");
}

TEST_CASE("What an instance publishes decides how it is reached", "[connect-local]")
{
    SECTION("tls switches the scheme")
    {
        const auto found =
            discovered_from_reply("Secure", "secure.local", "10.0.0.2", 8443, {{TXT_TLS, "1"}});
        REQUIRE(found.has_value());
        CHECK(found->tls);
        CHECK(found->base_url() == "https://secure.local:8443");
    }

    SECTION("an instance with no name of its own is shown under its service name")
    {
        const auto found = discovered_from_reply("Kitchen Pi", "kitchen.local", "10.0.0.3", 80, {});
        REQUIRE(found.has_value());
        CHECK(found->name == "Kitchen Pi");
        CHECK(found->base_url() == "http://kitchen.local:80");
    }

    SECTION("an instance that published no host name falls back to its address")
    {
        const auto found = discovered_from_reply("Nameless", "", "10.0.0.4", 8080, {});
        REQUIRE(found.has_value());
        CHECK(found->base_url() == "http://10.0.0.4:8080");
    }
}

TEST_CASE("An answer that cannot be connected to is not offered", "[connect-local]")
{
    CHECK_FALSE(discovered_from_reply("No port", "a.local", "10.0.0.5", 0, {}).has_value());
    CHECK_FALSE(discovered_from_reply("Nowhere", "", "", 8080, {}).has_value());
    CHECK_FALSE(discovered_from_reply("Dots only", ".", "", 8080, {}).has_value());
}

TEST_CASE("The service label fits what DNS-SD allows", "[connect-local]")
{
    CHECK(std::string{SERVICE_NAME}.size() <= 15);
}

TEST_CASE("An instance answering again is listed once", "[connect-local]")
{
    DiscoveredList list;
    auto a = discovered_from_reply("A", "a.local", "10.0.0.1", 8080, {{TXT_VERSION, "1"}});
    auto b = discovered_from_reply("B", "b.local", "10.0.0.2", 8080, {});
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());

    CHECK(list.add(*a));
    CHECK(list.add(*b));
    CHECK_FALSE(list.add(*a));
    CHECK(list.instances().size() == 2);

    auto a2 = discovered_from_reply("A", "a.local", "10.0.0.1", 8080, {{TXT_VERSION, "2"}});
    REQUIRE(a2.has_value());
    CHECK(list.add(*a2));
    REQUIRE(list.instances().size() == 2);
    CHECK(list.instances()[0].version == "2");
}

TEST_CASE("An instance that stops answering is dropped after two rounds", "[connect-local]")
{
    DiscoveredList list;
    auto old_name = discovered_from_reply("Demo", "host.local", "127.0.0.1", 18398, {});
    auto new_name = discovered_from_reply("Demo", "host-2.local", "127.0.0.1", 18398, {});
    REQUIRE(old_name.has_value());
    REQUIRE(new_name.has_value());

    list.add(*old_name);
    CHECK_FALSE(list.end_round());
    list.add(*new_name);
    CHECK_FALSE(list.end_round());
    REQUIRE(list.instances().size() == 2);
    list.add(*new_name);
    CHECK(list.end_round());
    REQUIRE(list.instances().size() == 1);
    CHECK(list.instances()[0].hostname == "host-2.local");

    list.restart();
    CHECK_FALSE(list.end_round());
    CHECK(list.end_round());
    CHECK(list.instances().empty());
}

TEST_CASE("The setup page receives escaped JSON", "[connect-local]")
{
    auto evil = discovered_from_reply(
        "Evil",
        "evil.local",
        "10.0.0.9",
        8080,
        {{TXT_NAME, "x'); alert(1); //</script>\"\\"}, {TXT_VERSION, "\xC3\xA9\xFF"}}
    );
    REQUIRE(evil.has_value());
    const std::string json = discovered_list_json({*evil});

    const nlohmann::json parsed = nlohmann::json::parse(json);
    REQUIRE(parsed.is_array());
    REQUIRE(parsed.size() == 1);
    CHECK(parsed[0]["name"] == "x'); alert(1); //</script>\"\\");
    CHECK(parsed[0]["hostname"] == "evil.local");
    CHECK(parsed[0]["address"] == "10.0.0.9");
    CHECK(parsed[0]["port"] == 8080);
    CHECK(parsed[0]["tls"] == false);
    CHECK(parsed[0]["url"] == "http://evil.local:8080");
    CHECK(std::ranges::all_of(json, [](unsigned char c) { return c < 0x80; }));
    CHECK(json.find("\\\"\\\\") != std::string::npos);

    CHECK(discovered_list_json({}) == "[]");
}

TEST_CASE("An instance known by its IPv6 address gets a bracketed URL", "[connect-local]")
{
    Discovered found;
    found.address = "fe80::1";
    found.port    = 8080;
    CHECK(found.base_url() == "http://[fe80::1]:8080");

    found.tls = true;
    CHECK(found.base_url() == "https://[fe80::1]:8080");

    found.hostname = "workshop.local";
    CHECK(found.base_url() == "https://workshop.local:8080");
}
