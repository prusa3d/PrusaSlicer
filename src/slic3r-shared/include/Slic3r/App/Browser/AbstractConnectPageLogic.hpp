#pragma once

#include "Slic3r/App/Browser/AbstractBrowserLogic.hpp"
#include "Slic3r/App/Browser/AbstractConnectRequestHandler.hpp"
#include "Slic3r/App/Browser/ConnectNavigationLock.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::Biz {
class ProjectInteractor;
}

namespace Slic3r::App::Browser {

/**
 * @brief What the Connect tab does whatever service it shows: it loads the Connect page when
 * signed in and a logged-out page otherwise, keeps the navigation on the service, and replaces a
 * page the web view engine cannot run. BrowserLogicConnectPage shows Prusa Connect,
 * BrowserLogicConnectLocalPage a Prusa Connect Local server.
 */
class AbstractConnectPageLogic : public AbstractBrowserLogic, public AbstractConnectRequestHandler
{
public:
    AbstractConnectPageLogic(
        Biz::ProjectInteractor& project_interactor,
        const std::string& connect_page_url,
        std::unique_ptr<IConnectPageAuth> page_auth
    );

    std::string access_token() override;
    std::vector<BrowserLogicCommand> on_script_message_webview_event(const std::string& message) override;
    std::vector<BrowserLogicCommand> on_show_webview_event(bool show) override;
    std::vector<BrowserLogicCommand> on_navigation_request_webview_event(
        const std::string& new_url,
        const std::string& current_url
    ) override;
    std::vector<BrowserLogicCommand> on_load_default_url() override;
    std::vector<BrowserLogicCommand> on_page_will_load_webview_event() override;
    std::vector<BrowserLogicCommand> on_webview_created() override;
    std::vector<BrowserLogicCommand> on_loaded_webview_event(const std::string& url) override;
    std::vector<BrowserLogicCommand>
    on_user_account_id_success(bool is_refresh, const std::string& current_url) override;
    std::vector<BrowserLogicCommand>
    on_user_account_logged_out(const std::string& current_url) override;

protected:
    static constexpr const char* LOADING_PAGE     = "connect_loading";
    static constexpr const char* ERROR_PAGE       = "connect_error";
    static constexpr const char* UNSUPPORTED_PAGE = "connect_unsupported";

    std::vector<BrowserLogicCommand> on_connect_action_select_printer(const std::string& message_data) override;
    std::vector<BrowserLogicCommand> on_connect_action_print(const std::string& message_data) override;
    std::vector<BrowserLogicCommand> on_connect_action_webapp_ready(const std::string& message_data) override;
    std::vector<BrowserLogicCommand> on_webview_reload_event(const std::string& message_data) override;
    std::vector<BrowserLogicCommand> on_connect_action_close_dialog(const std::string& message_data) override;
    std::vector<BrowserLogicCommand> on_connect_action_request_login(const std::string& message_data) override;
    std::vector<BrowserLogicCommand> on_connect_action_error(const std::string& message_data) override;
    std::vector<BrowserLogicCommand> on_connect_action_log_in_in_browser(const std::string& message_data) override;
    std::vector<BrowserLogicCommand> on_connect_action_unsupported_webview(const std::string& message_data) override;
    std::vector<BrowserLogicCommand> on_connect_action_open_connect_in_browser(const std::string& message_data) override;

    /** @brief Commands loading `url`, a page of the service, for the signed-in person. */
    virtual void emplace_load_connect_page_commands(
        std::vector<BrowserLogicCommand>& res,
        const std::string& url
    ) = 0;

    /** @brief Commands loading the page shown while nobody is signed in. */
    virtual void emplace_logged_out_page_commands(std::vector<BrowserLogicCommand>& res) = 0;

    /** @brief Scripts every page of the tab gets before it loads, while somebody is signed in. */
    virtual std::vector<BrowserLogicCommand> page_user_script_commands() = 0;

    /** @brief Commands handing a new access token to the loaded page. */
    virtual std::vector<BrowserLogicCommand> sign_in_refresh_commands() = 0;

    /**
     * @brief Commands for the loaded page when the account announced itself and the page was not
     * showing the logged-out state.
     */
    virtual std::vector<BrowserLogicCommand> signed_in_page_commands() = 0;

    /** @brief Commands removing what a page keeps of the person who signed out. */
    virtual std::vector<BrowserLogicCommand> signed_out_cleanup_commands() = 0;

    /** @brief Commands of the derived logic for a page that finished loading. */
    virtual std::vector<BrowserLogicCommand> page_loaded_commands(const std::string& url)
    {
        return {};
    }

    virtual void on_navigating(const std::string& new_url) {}

    /**
     * @brief Commands for a request passed as the page to show next that is not a page of the
     * service, nullopt when the logic knows no such request.
     */
    virtual std::optional<std::vector<BrowserLogicCommand>>
    show_request_commands(const std::string& request)
    {
        return std::nullopt;
    }

    /**
     * @brief Whether the app's own resource pages pass the navigation lock of the tab once the
     * Connect page was reached.
     */
    virtual bool app_pages_pass_navigation_lock() const
    {
        return false;
    }

    bool is_connect_url(const std::string& url) const;

    /** @brief The Connect page when signed in, else the logged-out page. */
    std::vector<BrowserLogicCommand> home_page_commands();
    void emplace_load_logged_out_page_commands(std::vector<BrowserLogicCommand>& res);
    void emplace_load_resources_page_commands(
        std::vector<BrowserLogicCommand>& res,
        const std::string& page
    );

private:
    /** @brief The Connect page, or `page_url` when it is a page of the same service. */
    void emplace_load_default_url_commands(
        std::vector<BrowserLogicCommand>& res,
        const std::string& page_url = {}
    );
    void emplace_load_unsupported_page_commands(std::vector<BrowserLogicCommand>& res);
    void emplace_define_css_commands(std::vector<BrowserLogicCommand>& res);

    bool m_styles_defined{false};
    ConnectNavigationLock m_navigation_lock;
    bool m_load_default_url{true};
    bool m_logged_out{false};
    bool m_unsupported_engine{false};
};

/**
 * @brief The logic of the Connect tab for the current service: Prusa Connect, or the Prusa
 * Connect Local server of the Connect Local. The tab is created again when the service changes.
 */
std::unique_ptr<AbstractBrowserLogic>
make_connect_page_logic(Biz::ProjectInteractor& project_interactor);

} // namespace Slic3r::App::Browser
