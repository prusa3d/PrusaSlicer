#pragma once

#include "Slic3r/App/Browser/BrowserLogicCommand.hpp"
#include "Slic3r/App/Browser/ConnectPageAuth.hpp"

#include <map>
#include <memory>
#include <string>
#include <functional>
#include <vector>

namespace Slic3r::Biz {
class ProjectInteractor;
}

namespace Slic3r::App::Browser {

class AbstractConnectRequestHandler
{
public:
    using PushCommandsFn = std::function<void(std::vector<BrowserLogicCommand>)>;

    /**
     * @param push_commands Sends commands to the hosting view outside of a webview event (the
     * replies of page sessions), see AbstractBrowserLogic::push_commands().
     * @param error_page The resources page shown when a page session cannot be opened.
     */
    AbstractConnectRequestHandler(
        Biz::ProjectInteractor& project_interactor,
        std::unique_ptr<IConnectPageAuth> page_auth,
        PushCommandsFn push_commands,
        std::string error_page
    );
    virtual ~AbstractConnectRequestHandler() = default;

protected:
    Biz::ProjectInteractor& m_project_interactor;
    std::vector<BrowserLogicCommand> handle_message(const std::string& message);
    std::vector<BrowserLogicCommand> resend_config();
    std::string get_engine_check_script() const;

    const IConnectPageAuth& page_auth() const
    {
        return *m_page_auth;
    }

    /** @brief What with_page_session() does when no session can be opened. */
    enum class OnSessionFailure
    {
        /** @brief Push `then` anyway (e.g. load the page, it asks again). */
        Then,
        /** @brief Push page_session_failed_commands(): the page's session was refused already. */
        ShowError,
    };

    /**
     * @brief Pushes `then` once IConnectPageAuth::prepare_page() made the web view ready for it.
     * @param fresh Open a new session, a recent one is not handed out again.
     */
    void with_page_session(
        std::vector<BrowserLogicCommand> then,
        OnSessionFailure on_failure,
        bool fresh = false
    );

    /**
     * @brief A page starts loading (call from on_navigation_request_webview_event()): its next
     * REQUEST_CONFIG is its first one.
     */
    void on_page_navigation()
    {
        m_page_configured = false;
    }

    /**
     * @brief Connect Local: shown in place of the page when its page session was refused and no new
     * one can be opened. The error page of the logic by default.
     */
    virtual std::vector<BrowserLogicCommand> page_session_failed_commands();
    /**
     * @brief What a Connect Local page asking for its configuration gets while nobody is signed
     * in: nothing by default, the page keeps waiting.
     */
    virtual std::vector<BrowserLogicCommand> signed_out_commands();

    // action callbacks stored in m_actions
    virtual std::vector<BrowserLogicCommand> on_connect_action_log(const std::string& message_data);
    virtual std::vector<BrowserLogicCommand> on_connect_action_error(const std::string& message_data);
    virtual std::vector<BrowserLogicCommand> on_connect_action_unsupported_webview(const std::string& message_data);
    virtual std::vector<BrowserLogicCommand> on_connect_action_request_login(const std::string& message_data);
    virtual std::vector<BrowserLogicCommand> on_connect_action_request_config(const std::string& message_data);
    virtual std::vector<BrowserLogicCommand> on_connect_action_request_open_in_browser(const std::string& message_data);
    virtual std::vector<BrowserLogicCommand> on_connect_action_open_connect_in_browser(const std::string& message_data);
    virtual std::vector<BrowserLogicCommand> on_connect_action_select_printer(const std::string& message_data) = 0;
    virtual std::vector<BrowserLogicCommand> on_connect_action_print(const std::string& message_data) = 0;
    virtual std::vector<BrowserLogicCommand> on_connect_action_webapp_ready(const std::string& message_data) = 0;
    virtual std::vector<BrowserLogicCommand> on_connect_action_close_dialog(const std::string& message_data) = 0;
    virtual std::vector<BrowserLogicCommand> on_webview_reload_event(const std::string& message_data) = 0;
    virtual std::vector<BrowserLogicCommand> on_connect_action_log_in_in_browser(const std::string& message_data) = 0;

    std::map<std::string, std::function<std::vector<BrowserLogicCommand>(const std::string&)>> m_actions;

private:
    /**
     * @brief REQUEST_CONFIG from the page. A page that is not handed the token waits for its page
     * session: the first request of a page gets a recent session (each one counts against the
     * server's limits), a page that asks again had its session refused (401) and gets a new one.
     */
    std::vector<BrowserLogicCommand>
    on_connect_action_request_config_message(const std::string& message_data);

    std::unique_ptr<IConnectPageAuth> m_page_auth;
    PushCommandsFn m_push_commands;
    std::string m_error_page;
    /** @brief Connect Local: the current page asked for its config already. */
    bool m_page_configured{false};
};

} // namespace Slic3r::App::Browser
