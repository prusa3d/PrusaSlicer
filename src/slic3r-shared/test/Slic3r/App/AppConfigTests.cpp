#include "Slic3r/App/AppConfig.hpp"
#include "Slic3r/Biz/Network/ServiceConfig.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using Slic3r::Biz::Network::ServiceMode;

TEST_CASE("The Connect tab follows the account setting, not the server", "[AppConfig]")
{
    Slic3r::App::AppConfig app_config;
    if (!app_config.is_webkit_available()) {
        CHECK_FALSE(app_config.is_connect_tab_enabled());
        CHECK_FALSE(app_config.is_prusa_account_enabled());
        SKIP("Prusa Account needs WebKit.");
    }

    app_config.set<bool>("enable_prusa_account", false);
    CHECK_FALSE(app_config.is_connect_tab_enabled());
    CHECK_FALSE(app_config.is_prusa_account_enabled());

    app_config.set<bool>("enable_prusa_account", true);
    CHECK(app_config.service_mode() == ServiceMode::Online);
    CHECK(app_config.is_connect_tab_enabled());
    CHECK(app_config.is_prusa_account_enabled());

    // Offline without a server: the tab stays (it shows the setup page), the account cannot run.
    app_config.set<ServiceMode>("connect_mode", ServiceMode::Offline);
    app_config.set<std::string>("connect_local_url", "");
    CHECK(app_config.is_connect_tab_enabled());
    CHECK_FALSE(app_config.is_prusa_account_enabled());

    app_config.set<std::string>("connect_local_url", "not a url");
    CHECK(app_config.is_connect_tab_enabled());
    CHECK_FALSE(app_config.is_prusa_account_enabled());

    app_config.set<std::string>("connect_local_url", "http://192.168.1.10:8080");
    CHECK(app_config.is_connect_tab_enabled());
    CHECK(app_config.is_prusa_account_enabled());

    app_config.set<bool>("enable_prusa_account", false);
    CHECK_FALSE(app_config.is_connect_tab_enabled());
    CHECK_FALSE(app_config.is_prusa_account_enabled());
}
