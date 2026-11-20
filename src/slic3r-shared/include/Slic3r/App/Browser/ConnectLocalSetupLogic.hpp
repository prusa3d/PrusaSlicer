#pragma once

#include "Slic3r/Biz/Lifetime.hpp"
#include "Slic3r/App/Browser/BrowserLogicCommand.hpp"
#include "Slic3r/App/Browser/ConnectServiceSwitch.hpp"

#include "Slic3r/Biz/Network/ServerTrustProbe.hpp"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Slic3r::Biz::ConnectLocal {
class DiscoveryLoop;
} // namespace Slic3r::Biz::ConnectLocal

namespace Slic3r::Biz::UserAccount {
class UserAccountInteractor;
} // namespace Slic3r::Biz::UserAccount

namespace Slic3r::App::Browser {

/**
 * @brief The Connect Local server setup of the Connect tab (Connect Local).
 *
 * Drives the slicer's own pages of the tab that choose the server: the setup page
 * (resources/web/connect_local_setup.html) with the LAN discovery on request and the certificate of
 * an https server, and the logged-out page (connect_local_logged_out.html) that leads to it.
 * Choosing a server writes it to the app config; the Connect tab is then created again. The
 * Connect Local page logic (BrowserLogicConnectLocalPage) owns the tab and loads the pages.
 */
class ConnectLocalSetupLogic
{
public:
    /**
     * @brief What the setup needs from the Connect page logic.
     */
    struct Page
    {
        /** @brief Sends commands outside of a webview event (results of background checks). */
        std::function<void(std::vector<BrowserLogicCommand>)> push_commands;
        /** @brief Commands loading the setup page. */
        std::function<std::vector<BrowserLogicCommand>()> setup_page;
        /** @brief Commands loading the Connect page when signed in, else the logged-out page. */
        std::function<std::vector<BrowserLogicCommand>()> home_page;
        /**
         * @brief Commands loading the logged-out page (the setup page while wants_setup_page()).
         */
        std::function<std::vector<BrowserLogicCommand>()> logged_out_page;
    };

    using Actions =
        std::map<std::string, std::function<std::vector<BrowserLogicCommand>(const std::string&)>>;

    /** @brief Resources page that chooses the server. */
    static constexpr std::string_view SETUP_PAGE = "connect_local_setup";
    /**
     * @brief Connect Local replacement of connect_logged_out: signs in to the server, not to Prusa
     * Account.
     */
    static constexpr std::string_view LOGGED_OUT_PAGE = "connect_local_logged_out";
    /**
     * @brief Passed to the Connect tab as the page to show next: the setup page, asked for from
     * outside of the tab (the account menu, Preferences).
     */
    static constexpr std::string_view SETUP_REQUEST = "prusaslicer://connect-server-setup";

    ConnectLocalSetupLogic(Biz::UserAccount::UserAccountInteractor& user_account, Page page);
    ~ConnectLocalSetupLogic();

    ConnectLocalSetupLogic(const ConnectLocalSetupLogic&)            = delete;
    ConnectLocalSetupLogic& operator=(const ConnectLocalSetupLogic&) = delete;

    /**
     * @brief Connect Local without a valid server: the tab can only show the setup page.
     */
    static bool needs_server_setup();

    /**
     * @brief Whether the tab shows the setup page instead of the logged-out page.
     */
    bool wants_setup_page() const;

    /**
     * @brief The person asked to change the server from outside of the tab.
     */
    void request_setup()
    {
        m_setup_requested = true;
    }

    /**
     * @brief Adds the handlers of the messages of the setup and logged-out pages.
     */
    void add_actions(Actions& actions);

    /**
     * @brief A page finished loading. Returns the commands that fill the setup or a logged-out
     * page; other pages stop the discovery. The setup page does not start it, the user does.
     */
    std::vector<BrowserLogicCommand> on_page_loaded(const std::string& url);

    /**
     * @brief The tab is about to navigate: messages of the next page are not trusted unless it
     * is one of the slicer's pages again.
     */
    void on_navigation_request(const std::string& new_url);

    /**
     * @brief The tab was shown or hidden. No discovery runs while nobody looks at the list.
     */
    void set_shown(bool shown);

    /**
     * @brief A page of the server failed on its certificate. Returns the commands showing the
     * setup page with the certificate step, none when the failure is not the server's.
     */
    std::vector<BrowserLogicCommand> on_certificate_error(const std::string& url);

private:
    enum class ProbeKind
    {
        /** @brief The setup page shows "checking" and waits for the result. */
        Foreground,
        /**
         * @brief A check of the current server, the page shows its certificate only when refused.
         */
        Background
    };

    using MessageHandler =
        std::vector<BrowserLogicCommand> (ConnectLocalSetupLogic::*)(const std::string&);

    std::string setup_page_init_script() const;
    std::string logged_out_page_init_script() const;

    void start_discovery();
    void stop_discovery();
    std::vector<BrowserLogicCommand> discovery_state_commands() const;

    std::vector<BrowserLogicCommand> on_set_url(const std::string& message_data);
    std::vector<BrowserLogicCommand> on_show_setup(const std::string& message_data);
    std::vector<BrowserLogicCommand> on_cancel_setup(const std::string& message_data);
    std::vector<BrowserLogicCommand> on_discovery(const std::string& message_data);
    std::vector<BrowserLogicCommand> on_trust_certificate(const std::string& message_data);
    std::vector<BrowserLogicCommand> on_load_certificate(const std::string& message_data);
    std::vector<BrowserLogicCommand> on_forget_certificate(const std::string& message_data);

    /**
     * @brief Saves the server (and Connect Local); the mode switch reloads the tab, or this leaves
     * the setup page when nothing changes.
     */
    std::vector<BrowserLogicCommand> use_server(const std::string& normalized_url);

    /**
     * @brief Checks the server certificate off the main thread; done gets the result on the main
     * thread, unless this logic is gone or a newer check of the same kind started.
     */
    void probe_certificate(
        const std::string& normalized_url,
        ProbeKind kind,
        const Biz::Network::ServerTrust& trust,
        std::function<void(Biz::Network::TlsProbeResult)> done
    );

    /**
     * @brief Shows "checking" and probes the server; the result uses it, shows its certificate or
     * why it failed.
     */
    std::vector<BrowserLogicCommand> check_and_use_server(
        const std::string& normalized_url,
        std::optional<Biz::Network::ServerTrust> candidate_trust = std::nullopt
    );

    /**
     * @brief The page commands after a check the page waits for.
     */
    std::vector<BrowserLogicCommand>
    finish_foreground_probe(
        const std::string& server,
        const Biz::Network::TlsProbeResult& result,
        const std::optional<Biz::Network::ServerTrust>& candidate_trust
    );

    /**
     * @brief The page commands showing a refused certificate (and the trust buttons).
     */
    std::vector<BrowserLogicCommand> show_certificate_commands(
        const std::string& normalized_url,
        const Biz::Network::TlsProbeResult& result,
        const Biz::Network::ServerTrust& trust
    );

    /**
     * @brief Probes the current https server and shows its certificate if refused.
     */
    void check_current_server_certificate();

    /**
     * @brief The logged-out page checks the certificate of an https server once per server; a
     * refused one opens the setup page, since signing in would fail.
     */
    void check_certificate_before_sign_in();

    Biz::UserAccount::UserAccountInteractor& m_user_account;
    Page m_page;

    bool m_shown{true};
    /** @brief Knows whether one of the app's pages is loaded, the only ones allowed to act. */
    ConnectServiceSwitch m_service_switch;
    /** @brief The user asked to change a valid server from the logged-out page. */
    bool m_setup_requested{false};
    /** @brief The setup page was shown because the server page failed on its certificate. */
    bool m_certificate_setup{false};
    /** @brief The server whose certificate the logged-out page checked. */
    std::string m_certificate_checked_server;

    std::unique_ptr<Biz::ConnectLocal::DiscoveryLoop> m_discovery;

    /** @brief The certificate shown on the setup page, the only one the page can ask to trust. */
    std::optional<Biz::Network::CertificateInfo> m_presented_certificate;
    std::string m_presented_certificate_url;
    Biz::Network::ServerTrustProbe m_foreground_probe{"Connect Local certificate check"};
    Biz::Network::ServerTrustProbe m_background_probe{"Connect Local certificate check"};
    Biz::Lifetime m_lifetime;
};

} // namespace Slic3r::App::Browser
