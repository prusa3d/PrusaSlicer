#pragma once

#include "Slic3r/App/IAppConfigChangedListener.hpp"
#include "Slic3r/Biz/Platform/WithListeners.hpp"

#include <functional>
#include <string>
#include <utility>

namespace Slic3r::Biz::UserAccount {
class UserAccountInteractor;
} // namespace Slic3r::Biz::UserAccount

namespace Slic3r::App {

class AppConfig;
class AppConfigInteractor;
enum class AccountService;

/**
 * @brief What applying the account service settings changed.
 */
struct AccountServiceChange
{
    /**
     * @brief The active service or its server is another one than before. What was built for
     * the previous one (the Connect tab) has to be built again.
     */
    bool service_changed{false};
    /**
     * @brief Another set of services is enabled than before. What offers the services (the
     * switch on the signed-out pages, the account menu) has to be shown again.
     */
    bool enabled_services_changed{false};
};

class IAccountServiceListener
{
public:
    virtual ~IAccountServiceListener() = default;

    /**
     * @brief The account service settings were applied: the account runs (or not) for the active
     * service of the app config.
     */
    virtual void on_account_service_applied(const AccountServiceChange& change) = 0;
};

/**
 * @brief The one place that changes which account service the app uses and applies the change.
 *
 * Everything that enables a service, makes one active or chooses its server goes through here.
 * A change is applied once, whatever number of settings it writes: the account of the previous
 * service is ended, the service URLs and the server trust are switched, the account of the new
 * service is started and the listeners are told. Settings written by others (the Preferences)
 * are applied the same way.
 */
class AccountServiceSwitcher final :
    public IAppConfigChangedListener,
    public WithListeners<IAccountServiceListener>
{
public:
    /**
     * @brief What applying a change does outside of the settings.
     */
    struct Backend
    {
        std::function<bool()> is_signed_in;
        std::function<bool()> is_account_running;
        /**
         * @brief Ends the account, switches the service URLs and the server trust to the settings
         * and starts the account again if it is usable.
         */
        std::function<void(bool usable)> restart_account;
        /** @brief Starts or ends the account of the unchanged service. */
        std::function<void(bool usable)> run_account;
        /** @brief Drops a server trust that belongs to another server than the configured one. */
        std::function<void()> forget_stale_trust;
        /** @brief Asks whether to go on with a change that signs the person out. */
        std::function<bool()> confirm_sign_out;
    };

    AccountServiceSwitcher(
        AppConfig& app_config,
        AppConfigInteractor& app_config_interactor,
        Backend backend
    );
    ~AccountServiceSwitcher() override;

    AccountServiceSwitcher(const AccountServiceSwitcher&)            = delete;
    AccountServiceSwitcher& operator=(const AccountServiceSwitcher&) = delete;

    /**
     * @brief Makes a service the active one. Asks first when somebody is signed in to another.
     * @return False when the person cancelled; nothing changed then.
     */
    bool choose(AccountService service);

    /**
     * @brief Enables the services and makes one active, as one change.
     */
    void enable_services(bool prusa_account, bool connect_local, AccountService active);

    /**
     * @brief Uses a Prusa Connect on-premise server (a normalized URL) and makes that service the
     * active one, as one change.
     */
    void use_server(const std::string& server_url);

    void on_app_config_changed(const std::string& key) override;

private:
    struct Applied
    {
        bool connect_local{false};
        std::string server;

        bool operator==(const Applied&) const = default;
    };

    Applied configured() const;
    void apply();
    /**
     * @brief Keeps the "account_service" setting equal to the active service, so that enabling
     * the other service later does not make that one active.
     */
    void remember_active_service();

    template <typename Fn>
    void as_one_change(Fn write_settings)
    {
        ++m_open_changes;
        write_settings();
        if (--m_open_changes == 0 && std::exchange(m_settings_written, false)) {
            apply();
        }
    }

    AppConfig& m_app_config;
    AppConfigInteractor& m_app_config_interactor;
    Backend m_backend;
    Applied m_applied;
    bool m_prusa_account_enabled{false};
    bool m_connect_local_enabled{false};
    int m_open_changes{0};
    bool m_settings_written{false};
};

AccountServiceSwitcher::Backend
make_account_service_backend(Biz::UserAccount::UserAccountInteractor& user_account);

} // namespace Slic3r::App
