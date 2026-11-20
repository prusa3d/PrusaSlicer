#include "Slic3r/App/AccountServiceSwitcher.hpp"

#include "Slic3r/App/AppConfig.hpp"
#include "Slic3r/App/AppConfigInteractor.hpp"
#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/ConnectLocalTrust.hpp"
#include "Slic3r/App/ConnectServiceName.hpp"
#include "Slic3r/App/IDialogManager.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/UserAccount/UserAccountInteractor.hpp"

#include <fmt/format.h>

namespace Slic3r::App {

namespace {

constexpr const char* CONFIRM_SETTING = "confirm_account_service_switch";

bool is_account_service_setting(const std::string& key)
{
    return key == "enable_prusa_account"
        || key == "enable_connect_local"
        || key == "account_service"
        || key == "connect_local_url";
}

bool ask_to_sign_out()
{
    const AppServices& app_services = AppServices::instance();
    if (!app_services.app_config().get<bool>(CONFIRM_SETTING)) {
        return true;
    }

    bool confirmed      = false;
    bool dont_ask_again = false;
    app_services.dialog_manager().show_rich_yesno_dialog(
        // TRN Title of the question shown before the account service is changed.
        Biz::_u8L("Change account service"),
        fmt::format(
            // TRN {} is the service the person is signed in to, e.g. Prusa Account.
            fmt::runtime(Biz::_u8L("You are signed in to {}. Continuing signs you out. Continue?")),
            account_service_name()
        ),
        Biz::_u8L("Don't ask again"),
        [&confirmed](bool answer) { confirmed = answer; },
        [&dont_ask_again](bool checked) { dont_ask_again = checked; }
    );
    if (confirmed && dont_ask_again) {
        app_services.app_config_interactor()
            .set_item_value(CONFIRM_SETTING, Domain::ConfigValue(false));
    }
    return confirmed;
}

} // namespace

AccountServiceSwitcher::AccountServiceSwitcher(
    AppConfig& app_config,
    AppConfigInteractor& app_config_interactor,
    Backend backend
) :
    m_app_config(app_config),
    m_app_config_interactor(app_config_interactor),
    m_backend(std::move(backend)),
    m_applied(configured()),
    m_prusa_account_enabled(app_config.get<bool>("enable_prusa_account")),
    m_connect_local_enabled(app_config.get<bool>("enable_connect_local"))
{
    m_app_config_interactor.add_listener<IAppConfigChangedListener>(this);
    remember_active_service();
}

AccountServiceSwitcher::~AccountServiceSwitcher()
{
    m_app_config_interactor.remove_listener<IAppConfigChangedListener>(this);
}

AccountServiceSwitcher::Applied AccountServiceSwitcher::configured() const
{
    Applied result;
    result.connect_local = m_app_config.account_service() == AccountService::ConnectLocal;
    if (result.connect_local) {
        result.server = m_app_config.connect_local_server().value_or(std::string());
    }
    return result;
}

bool AccountServiceSwitcher::choose(AccountService service)
{
    if (m_app_config.account_service() == service) {
        return true;
    }
    if (m_backend.is_signed_in() && !m_backend.confirm_sign_out()) {
        return false;
    }
    m_app_config_interactor.set_account_service(service);
    return true;
}

void AccountServiceSwitcher::
    enable_services(bool prusa_account, bool connect_local, AccountService active)
{
    as_one_change(
        [&]()
        {
            m_app_config_interactor
                .set_item_value("enable_prusa_account", Domain::ConfigValue(prusa_account));
            m_app_config_interactor.set_item_value(
                "enable_connect_local",
                Domain::ConfigValue(connect_local)
            );
            m_app_config_interactor.set_account_service(active);
        }
    );
}

void AccountServiceSwitcher::use_server(const std::string& server_url)
{
    as_one_change(
        [&]()
        {
            if (m_app_config.connect_local_url() != server_url) {
                m_app_config_interactor
                    .set_item_value("connect_local_url", Domain::ConfigValue(server_url));
            }
            m_app_config_interactor.set_account_service(AccountService::ConnectLocal);
        }
    );
}

void AccountServiceSwitcher::on_app_config_changed(const std::string& key)
{
    if (!is_account_service_setting(key)) {
        return;
    }
    if (m_open_changes > 0) {
        m_settings_written = true;
        return;
    }
    apply();
}

void AccountServiceSwitcher::remember_active_service()
{
    const AccountService active = m_app_config.account_service();
    if (active == AccountService::None) {
        return;
    }
    const bool settings_were_written = m_settings_written;
    ++m_open_changes;
    m_app_config_interactor.set_account_service(active);
    --m_open_changes;
    m_settings_written = settings_were_written;
}

void AccountServiceSwitcher::apply()
{
    remember_active_service();

    const Applied target       = configured();
    const bool usable          = m_app_config.is_account_usable();
    const bool prusa_account   = m_app_config.get<bool>("enable_prusa_account");
    const bool connect_local   = m_app_config.get<bool>("enable_connect_local");

    AccountServiceChange change;
    change.service_changed          = !(target == m_applied);
    change.enabled_services_changed = prusa_account != m_prusa_account_enabled
        || connect_local != m_connect_local_enabled;
    m_prusa_account_enabled = prusa_account;
    m_connect_local_enabled = connect_local;
    const bool service_changed = change.service_changed;

    m_backend.forget_stale_trust();
    if (service_changed) {
        m_applied = target;
        m_backend.restart_account(usable);
    } else if (usable != m_backend.is_account_running()) {
        m_backend.run_account(usable);
    }

    invoke_listeners<IAccountServiceListener>(
        [&change](IAccountServiceListener* listener)
        { listener->on_account_service_applied(change); }
    );
}

AccountServiceSwitcher::Backend
make_account_service_backend(Biz::UserAccount::UserAccountInteractor& user_account)
{
    return {
        .is_signed_in       = [&user_account]() { return user_account.is_logged_in(); },
        .is_account_running = [&user_account]() { return user_account.is_enabled(); },
        .restart_account =
            [&user_account](bool usable)
        {
            user_account.restart(
                usable,
                []() { AppServices::instance().app_config().apply_service_mode(); }
            );
        },
        .run_account        = [&user_account](bool usable) { user_account.init(usable); },
        .forget_stale_trust = []() { ConnectLocalTrust::forget_if_stale(); },
        .confirm_sign_out   = []() { return ask_to_sign_out(); },
    };
}

} // namespace Slic3r::App
