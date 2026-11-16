#include "Slic3r/App/AccountServiceSwitcher.hpp"
#include "Slic3r/App/AppConfig.hpp"
#include "Slic3r/App/AppConfigInteractor.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using Slic3r::App::AccountService;
using Slic3r::App::AccountServiceSwitcher;

namespace {

struct RecordingListener : Slic3r::App::IAccountServiceListener
{
    void on_account_service_applied(const Slic3r::App::AccountServiceChange& change) override
    {
        applied.push_back(change.service_changed);
        enabled_services_changed.push_back(change.enabled_services_changed);
    }

    std::vector<bool> applied;
    std::vector<bool> enabled_services_changed;
};

struct SwitcherFixture
{
    AccountServiceSwitcher::Backend backend()
    {
        return {
            .is_signed_in       = [this]() { return signed_in; },
            .is_account_running = [this]() { return account_running; },
            .restart_account =
                [this](bool usable)
            {
                restarts.push_back(usable);
                account_running = usable;
                signed_in       = false;
            },
            .run_account =
                [this](bool usable)
            {
                runs.push_back(usable);
                account_running = usable;
                signed_in       = signed_in && usable;
            },
            .forget_stale_trust = [this]() { ++trust_checks; },
            .confirm_sign_out =
                [this]()
            {
                ++questions;
                return confirm;
            },
        };
    }

    bool webkit() const
    {
        return app_config.is_webkit_available();
    }

    bool signed_in{false};
    bool account_running{false};
    bool confirm{true};
    int questions{0};
    int trust_checks{0};
    std::vector<bool> restarts;
    std::vector<bool> runs;

    bool start_without_services()
    {
        app_config.set<bool>("enable_prusa_account", false);
        app_config.set<bool>("enable_connect_local", false);
        return true;
    }

    Slic3r::App::AppConfig app_config;
    bool started_without_services{start_without_services()};
    Slic3r::App::AppConfigInteractor interactor{app_config.get_config_box_ptr()};
    AccountServiceSwitcher switcher{app_config, interactor, backend()};
    RecordingListener listener;
};

} // namespace

TEST_CASE_METHOD(SwitcherFixture, "Enabling the services is applied as one change", "[account-service]")
{
    if (!webkit()) {
        SKIP("Prusa Account needs WebKit.");
    }
    switcher.add_listener<Slic3r::App::IAccountServiceListener>(&listener);

    switcher.enable_services(true, true, AccountService::PrusaAccount);

    CHECK(app_config.account_service() == AccountService::PrusaAccount);
    CHECK(restarts.empty());
    CHECK(runs == std::vector<bool>{true});
    CHECK(listener.applied == std::vector<bool>{false});
}

TEST_CASE_METHOD(SwitcherFixture, "Using a server restarts the account once", "[account-service]")
{
    if (!webkit()) {
        SKIP("Prusa Account needs WebKit.");
    }
    switcher.enable_services(false, true, AccountService::ConnectLocal);
    REQUIRE(restarts == std::vector<bool>{false});
    switcher.add_listener<Slic3r::App::IAccountServiceListener>(&listener);

    switcher.use_server("http://192.168.1.10:8080");

    CHECK(restarts == std::vector<bool>{false, true});
    CHECK(listener.applied == std::vector<bool>{true});

    switcher.use_server("http://192.168.1.10:8080");

    CHECK(restarts.size() == 2);
}

TEST_CASE_METHOD(SwitcherFixture, "Choosing the other service asks only when signed in", "[account-service]")
{
    if (!webkit()) {
        SKIP("Prusa Account needs WebKit.");
    }
    switcher.enable_services(true, true, AccountService::PrusaAccount);
    app_config.set<std::string>("connect_local_url", "http://192.168.1.10:8080");

    SECTION("signed out")
    {
        CHECK(switcher.choose(AccountService::ConnectLocal));
        CHECK(questions == 0);
        CHECK(app_config.account_service() == AccountService::ConnectLocal);
        CHECK(restarts == std::vector<bool>{true});
    }
    SECTION("signed in and confirmed")
    {
        signed_in = true;
        CHECK(switcher.choose(AccountService::ConnectLocal));
        CHECK(questions == 1);
        CHECK(app_config.account_service() == AccountService::ConnectLocal);
    }
    SECTION("signed in and cancelled")
    {
        signed_in = true;
        confirm   = false;
        CHECK_FALSE(switcher.choose(AccountService::ConnectLocal));
        CHECK(app_config.account_service() == AccountService::PrusaAccount);
        CHECK(restarts.empty());
    }
    SECTION("the active service again")
    {
        signed_in = true;
        CHECK(switcher.choose(AccountService::PrusaAccount));
        CHECK(questions == 0);
        CHECK(restarts.empty());
    }
}

TEST_CASE_METHOD(SwitcherFixture, "Settings written elsewhere are applied too", "[account-service]")
{
    if (!webkit()) {
        SKIP("Prusa Account needs WebKit.");
    }
    switcher.add_listener<Slic3r::App::IAccountServiceListener>(&listener);

    interactor.set_item_value("enable_prusa_account", Slic3r::Domain::ConfigValue(true));
    CHECK(runs == std::vector<bool>{true});

    interactor.set_item_value("enable_prusa_account", Slic3r::Domain::ConfigValue(false));
    CHECK(runs == std::vector<bool>{true, false});
    CHECK(listener.applied == std::vector<bool>{false, false});

    interactor.set_item_value("enable_printables", Slic3r::Domain::ConfigValue(true));
    CHECK(listener.applied.size() == 2);
}

TEST_CASE_METHOD(SwitcherFixture, "Returning to a service keeps what belongs to its server", "[account-service]")
{
    if (!webkit()) {
        SKIP("Prusa Account needs WebKit.");
    }
    app_config.set<std::string>("connect_local_url", "https://192.168.1.10:8443");
    switcher.enable_services(true, true, AccountService::ConnectLocal);
    switcher.add_listener<Slic3r::App::IAccountServiceListener>(&listener);

    CHECK(switcher.choose(AccountService::PrusaAccount));
    CHECK(switcher.choose(AccountService::ConnectLocal));

    CHECK(listener.applied == std::vector<bool>{true, true});
    CHECK(app_config.connect_local_server() == "https://192.168.1.10:8443");
}

TEST_CASE_METHOD(SwitcherFixture, "Enabling the other service keeps the active one", "[account-service]")
{
    if (!webkit()) {
        SKIP("Prusa Account needs WebKit.");
    }
    app_config.set<std::string>("connect_local_url", "http://192.168.1.10:8080");
    switcher.enable_services(false, true, AccountService::PrusaAccount);
    REQUIRE(app_config.account_service() == AccountService::ConnectLocal);
    const size_t restarts_before = restarts.size();
    switcher.add_listener<Slic3r::App::IAccountServiceListener>(&listener);

    interactor.set_item_value("enable_prusa_account", Slic3r::Domain::ConfigValue(true));

    CHECK(app_config.account_service() == AccountService::ConnectLocal);
    CHECK(restarts.size() == restarts_before);
    CHECK(listener.applied == std::vector<bool>{false});
    CHECK(listener.enabled_services_changed == std::vector<bool>{true});

    interactor.set_item_value("enable_prusa_account", Slic3r::Domain::ConfigValue(false));

    CHECK(app_config.account_service() == AccountService::ConnectLocal);
    CHECK(listener.enabled_services_changed == std::vector<bool>{true, true});
}

TEST_CASE_METHOD(SwitcherFixture, "A setting that changes no service reports no change", "[account-service]")
{
    if (!webkit()) {
        SKIP("Prusa Account needs WebKit.");
    }
    switcher.enable_services(true, false, AccountService::PrusaAccount);
    switcher.add_listener<Slic3r::App::IAccountServiceListener>(&listener);

    interactor.set_item_value("connect_local_url", Slic3r::Domain::ConfigValue(std::string("http://a")));

    CHECK(listener.applied == std::vector<bool>{false});
    CHECK(listener.enabled_services_changed == std::vector<bool>{false});
}
