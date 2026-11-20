#include "Slic3r/App/AppConfig.hpp"
#include "Slic3r/Biz/Network/ServiceConfig.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using Slic3r::App::AccountService;
using Slic3r::Biz::Network::ServiceMode;

TEST_CASE("The two service switches decide the active account service", "[AppConfig]")
{
    Slic3r::App::AppConfig app_config;
    if (!app_config.is_webkit_available()) {
        CHECK(app_config.account_service() == AccountService::None);
        CHECK_FALSE(app_config.is_account_service_enabled());
        CHECK_FALSE(app_config.is_account_usable());
        SKIP("Prusa Account needs WebKit.");
    }

    CHECK(app_config.get<bool>("enable_connect_local"));
    CHECK(app_config.account_service() == AccountService::ConnectLocal);

    app_config.set<bool>("enable_connect_local", false);
    CHECK(app_config.account_service() == AccountService::None);
    CHECK_FALSE(app_config.is_account_service_enabled());
    CHECK_FALSE(app_config.are_both_account_services_enabled());
    CHECK(app_config.service_mode() == ServiceMode::Cloud);

    app_config.set<bool>("enable_prusa_account", true);
    CHECK(app_config.account_service() == AccountService::PrusaAccount);
    CHECK(app_config.is_account_service_enabled());
    CHECK(app_config.is_account_usable());
    CHECK_FALSE(app_config.are_both_account_services_enabled());

    app_config.set<AccountService>("account_service", AccountService::ConnectLocal);
    CHECK(app_config.account_service() == AccountService::PrusaAccount);

    app_config.set<bool>("enable_connect_local", true);
    CHECK(app_config.are_both_account_services_enabled());
    CHECK(app_config.account_service() == AccountService::ConnectLocal);
    CHECK(app_config.service_mode() == ServiceMode::ConnectLocal);

    app_config.set<AccountService>("account_service", AccountService::PrusaAccount);
    CHECK(app_config.account_service() == AccountService::PrusaAccount);
    CHECK(app_config.service_mode() == ServiceMode::Cloud);

    app_config.set<bool>("enable_prusa_account", false);
    CHECK(app_config.account_service() == AccountService::ConnectLocal);
    CHECK(app_config.is_account_service_enabled());

    app_config.set<bool>("enable_connect_local", false);
    CHECK(app_config.account_service() == AccountService::None);
    CHECK_FALSE(app_config.is_account_service_enabled());
}

TEST_CASE("The on-premise service is usable only with a valid server", "[AppConfig]")
{
    Slic3r::App::AppConfig app_config;
    if (!app_config.is_webkit_available()) {
        SKIP("Prusa Account needs WebKit.");
    }

    app_config.set<bool>("enable_connect_local", true);
    app_config.set<std::string>("connect_local_url", "");
    CHECK(app_config.is_account_service_enabled());
    CHECK_FALSE(app_config.is_account_usable());

    app_config.set<std::string>("connect_local_url", "not a url");
    CHECK_FALSE(app_config.is_account_usable());

    app_config.set<std::string>("connect_local_url", "http://192.168.1.10:8080");
    CHECK(app_config.is_account_usable());
}

TEST_CASE("A config with one account service choice is split into the two switches", "[AppConfig]")
{
    Slic3r::App::AppConfig app_config;
    if (!app_config.is_webkit_available()) {
        SKIP("Prusa Account needs WebKit.");
    }
    app_config.set<bool>("account_service_migrated", true);

    SECTION("on-premise was chosen")
    {
        app_config.set<AccountService>("account_service", AccountService::ConnectLocal);
        Slic3r::App::AppConfig::handle_legacy_config(app_config);
        CHECK_FALSE(app_config.get<bool>("enable_prusa_account"));
        CHECK(app_config.get<bool>("enable_connect_local"));
        CHECK(app_config.account_service() == AccountService::ConnectLocal);
    }
    SECTION("the Prusa Account was chosen")
    {
        app_config.set<AccountService>("account_service", AccountService::PrusaAccount);
        Slic3r::App::AppConfig::handle_legacy_config(app_config);
        CHECK(app_config.get<bool>("enable_prusa_account"));
        CHECK(app_config.get<bool>("enable_connect_local"));
        CHECK(app_config.account_service() == AccountService::PrusaAccount);
    }
    SECTION("no account was chosen")
    {
        app_config.set<AccountService>("account_service", AccountService::None);
        Slic3r::App::AppConfig::handle_legacy_config(app_config);
        CHECK_FALSE(app_config.get<bool>("enable_prusa_account"));
        CHECK(app_config.account_service() == AccountService::ConnectLocal);
    }

    CHECK(app_config.get<bool>("account_services_split"));
}

TEST_CASE("A released config keeps its account switch and gets on-premise enabled", "[AppConfig]")
{
    Slic3r::App::AppConfig app_config;
    if (!app_config.is_webkit_available()) {
        SKIP("Prusa Account needs WebKit.");
    }

    SECTION("the Prusa Account was off")
    {
        app_config.set<bool>("enable_connect_local", false);
        Slic3r::App::AppConfig::handle_legacy_config(app_config);
        CHECK_FALSE(app_config.get<bool>("enable_prusa_account"));
        CHECK(app_config.get<bool>("enable_connect_local"));
        CHECK(app_config.account_service() == AccountService::ConnectLocal);
        return;
    }

    app_config.set<bool>("enable_prusa_account", true);
    app_config.set<bool>("enable_connect_local", false);
    Slic3r::App::AppConfig::handle_legacy_config(app_config);
    CHECK(app_config.get<bool>("enable_connect_local"));
    CHECK(app_config.account_service() == AccountService::PrusaAccount);

    app_config.set<bool>("enable_connect_local", true);
    app_config.set<AccountService>("account_service", AccountService::ConnectLocal);
    Slic3r::App::AppConfig::handle_legacy_config(app_config);
    CHECK(app_config.account_service() == AccountService::ConnectLocal);
}
