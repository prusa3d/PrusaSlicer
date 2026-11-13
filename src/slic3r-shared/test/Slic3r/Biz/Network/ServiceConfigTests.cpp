#include "Slic3r/Biz/Network/ServiceConfig.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <optional>
#include <string>

using namespace Slic3r::Biz;
using Network::ServiceConfig;
using Network::ServiceMode;

namespace {

/** @brief Puts the singleton back to the online mode when a test ends, whatever happens. */
struct ModeGuard
{
    ~ModeGuard()
    {
        ServiceConfig::instance().set_mode(ServiceMode::Online);
    }
};

bool online_urls_overridden()
{
    for (const char* name :
         {"PRUSA_CONNECT_URL",
          "PRUSA_CONNECT_API_URL",
          "PRUSA_ACCOUNT_URL",
          "PRUSA_ACCOUNT_CLIENT_ID"})
    {
        const char* value = std::getenv(name);
        if (value != nullptr && *value != '\0') {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("Offline server URL is normalized without the domain whitelist", "[ServiceConfig]")
{
    using R = std::optional<std::string>;
    auto n  = [](std::string_view url) { return ServiceConfig::normalize_offline_server_url(url); };

    // LAN IP, hostname, .local, localhost, plain http, no whitelist.
    CHECK(n("http://192.168.1.10:8080") == R{"http://192.168.1.10:8080"});
    CHECK(n("http://connect-local:8080/") == R{"http://connect-local:8080"});
    CHECK(n("http://printfarm.local") == R{"http://printfarm.local"});
    CHECK(n("http://localhost:8080") == R{"http://localhost:8080"});
    CHECK(n("https://connect.example.org") == R{"https://connect.example.org"});
    CHECK(n("http://[fd00::1]:8080") == R{"http://[fd00::1]:8080"});

    // Normalization.
    CHECK(n("  192.168.1.10:8080  ") == R{"http://192.168.1.10:8080"});
    CHECK(n("HTTP://Printfarm.LOCAL:8080//") == R{"http://printfarm.local:8080"});
    CHECK(n("http://host:8080/prefix/") == R{"http://host:8080/prefix"});
    // An empty port is the default one (RFC 3986).
    CHECK(n("http://host:") == R{"http://host"});
    CHECK(n("http://[FE80::1%25en0]:8080") == R{"http://[fe80::1%25en0]:8080"});

    // Rejected.
    CHECK(n("") == std::nullopt);
    CHECK(n("   ") == std::nullopt);
    CHECK(n("ftp://host") == std::nullopt);
    CHECK(n("file:///etc/passwd") == std::nullopt);
    CHECK(n("http://") == std::nullopt);
    CHECK(n("http://:8080") == std::nullopt);
    CHECK(n("http://host:99999") == std::nullopt);
    CHECK(n("http://host:80a") == std::nullopt);
    CHECK(n("http://user@host") == std::nullopt);
    CHECK(n("http://host/?q=1") == std::nullopt);
    CHECK(n("http://host/#x") == std::nullopt);
    CHECK(n("http://ho st") == std::nullopt);
    CHECK(n("http://host:0") == std::nullopt);
    CHECK(n("http://ho~st") == std::nullopt);
    CHECK(n("http://host\\path") == std::nullopt);
}

TEST_CASE("Online service URLs are unchanged", "[ServiceConfig]")
{
    ModeGuard guard;
    ServiceConfig& sc = ServiceConfig::instance();
    sc.set_mode(ServiceMode::Online);

    CHECK(sc.mode() == ServiceMode::Online);
    CHECK(sc.offline_server_url().empty());

    if (online_urls_overridden()) {
        SKIP("Online URLs are overridden by the environment.");
    }

    CHECK(sc.account_url() == "https://account.prusa3d.com");
    CHECK(sc.account_client_id() == "oamhmhZez7opFosnwzElIgE2oGgI2iJORSkw587O");
    CHECK(sc.account_token_url() == "https://account.prusa3d.com/o/token/");
    CHECK(sc.account_me_url() == "https://account.prusa3d.com/api/v1/me/");

    CHECK(sc.connect_url() == "https://connect.prusa3d.com");
    CHECK(sc.connect_page_url() == "https://connect.prusa3d.com");
    CHECK(sc.connect_printer_list_url() == "https://connect-api.prusa3d.com/printer/slicer/v1/printers");
    CHECK(sc.connect_select_printer_url() == "https://connect.prusa3d.com/slicer-select-printer");
    CHECK(sc.connect_printables_print_url() == "https://connect.prusa3d.com/slicer-print");
    CHECK(sc.connect_graphql_url() == "https://connect-api.prusa3d.com/graphql");
}

TEST_CASE("Offline service URLs are derived from the Connect Local server", "[ServiceConfig]")
{
    ModeGuard guard;
    ServiceConfig& sc           = ServiceConfig::instance();
    const std::string client_id = sc.account_client_id();

    sc.set_mode(ServiceMode::Offline, "http://192.168.1.10:8080/");

    const std::string S = "http://192.168.1.10:8080";
    const std::string M = S + "/api/v1/modules/com.github.aux.prusa-research.prusaslicer3-compat";

    CHECK(sc.mode() == ServiceMode::Offline);
    CHECK(sc.is_offline());
    CHECK(sc.offline_server_url() == S);

    // Same client id as online.
    CHECK(sc.account_client_id() == client_id);

    // Connect Local signs in by app pairing, there is no OAuth flow offline.
    CHECK(sc.account_token_url().empty());
    CHECK(sc.account_url() == M + "/account");
    CHECK(sc.account_me_url() == M + "/account/api/v1/me/");

    CHECK(sc.offline_pairing_url() == S + "/api/v1/auth/app-pairings");
    CHECK(sc.offline_pairing_token_url() == S + "/api/v1/auth/app-pairings/token");
    CHECK(sc.offline_page_session_url() == S + "/api/v1/auth/app-pairings/session");
    CHECK(sc.offline_pairing_page_url("BCDF-GHJK") == S + "/pair?code=BCDF-GHJK");
    CHECK(sc.offline_credential_url("c0ffee-01") == S + "/api/v1/auth/credentials/c0ffee-01");

    CHECK(sc.connect_url() == M + "/connect");
    CHECK(sc.connect_page_url() == S + "/");
    CHECK(
        sc.connect_select_printer_url()
        == S + "/m/com.github.aux.prusa-research.prusaslicer3-compat/select-printer?embed=1"
    );

    // Services outside Account and Connect stay where they are.
    sc.set_mode(ServiceMode::Online);
    const std::string printables = sc.printables_url();
    sc.set_mode(ServiceMode::Offline, S);
    CHECK(sc.printables_url() == printables);
}

TEST_CASE(
    "Pairing URLs exist only offline and take nothing unsafe from the server",
    "[ServiceConfig][connect-local]"
)
{
    ModeGuard guard;
    ServiceConfig& sc = ServiceConfig::instance();

    sc.set_mode(ServiceMode::Online);
    CHECK(sc.offline_pairing_url().empty());
    CHECK(sc.offline_pairing_token_url().empty());
    CHECK(sc.offline_pairing_page_url("BCDF-GHJK").empty());
    CHECK(sc.offline_credential_url("1").empty());
    CHECK(sc.offline_page_session_url().empty());

    sc.set_mode(ServiceMode::Offline, "https://pcl.local:8443/base/");
    CHECK(
        sc.offline_pairing_page_url("BCDF-GHJK")
        == "https://pcl.local:8443/base/pair?code=BCDF-GHJK"
    );
    CHECK(sc.offline_pairing_page_url("").empty());
    CHECK(sc.offline_pairing_page_url("AB&next=https://evil").empty());
    CHECK(sc.offline_pairing_page_url("AB CD").empty());
    CHECK(sc.offline_pairing_page_url("AB\xC3\x81").empty());
    CHECK(sc.offline_credential_url("id\xE9").empty());
    CHECK(sc.offline_credential_url("").empty());
    CHECK(sc.offline_credential_url("../me").empty());
    CHECK(sc.offline_credential_url("1?x=y").empty());
}

TEST_CASE("Printer page URL follows the service mode", "[ServiceConfig]")
{
    ModeGuard guard;
    ServiceConfig& sc      = ServiceConfig::instance();
    const std::string uuid = "c0ffee00-1234-4abc-8def-0123456789ab";

    sc.set_mode(ServiceMode::Online);
    CHECK(sc.connect_printer_page_url(uuid) == sc.connect_url() + "/printer/" + uuid + "/dashboard");

    sc.set_mode(ServiceMode::Offline, "https://pcl.local:8443/base/");
    CHECK(sc.connect_printer_page_url(uuid) == "https://pcl.local:8443/base/printers/" + uuid);

    // Nothing but one path segment is taken from the printer list.
    CHECK(sc.connect_printer_page_url("").empty());
    CHECK(sc.connect_printer_page_url("../me").empty());
    CHECK(sc.connect_printer_page_url("id?x=y").empty());
    CHECK(sc.connect_printer_page_url("id\xE9").empty());

    sc.set_mode(ServiceMode::Offline, "not a url");
    CHECK(sc.connect_printer_page_url(uuid).empty());
}

TEST_CASE("Offline mode without a valid server has no Account URLs", "[ServiceConfig]")
{
    ModeGuard guard;
    ServiceConfig& sc = ServiceConfig::instance();

    sc.set_mode(ServiceMode::Offline, "not a url");

    CHECK(sc.is_offline());
    CHECK(sc.offline_server_url().empty());
    CHECK(sc.account_url().empty());
    CHECK(sc.connect_url().empty());
    CHECK(sc.connect_page_url().empty());
    CHECK(sc.connect_select_printer_url().empty());
    CHECK(sc.offline_pairing_url().empty());
    CHECK(sc.offline_pairing_token_url().empty());
    CHECK(sc.offline_pairing_page_url("BCDF-GHJK").empty());
    CHECK(sc.offline_credential_url("1").empty());

    sc.set_mode(ServiceMode::Online, "http://192.168.1.10:8080");
    CHECK(sc.offline_server_url().empty());
    CHECK_FALSE(sc.account_url().empty());
}
