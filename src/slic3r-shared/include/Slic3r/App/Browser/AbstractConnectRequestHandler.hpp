#pragma once

#include "Slic3r/App/Browser/BrowserLogicCommand.hpp"

#include <map>
#include <memory>
#include <string>
#include <functional>
#include <vector>

namespace Slic3r::Biz {
class ProjectInteractor;
}

namespace Slic3r::App::Browser {
class AbstractBrowserLogic;

class AbstractConnectRequestHandler
{
public:
    /**
     * @param logic The logic this handler is part of, it gets the commands of page sessions.
     */
    AbstractConnectRequestHandler(
        Biz::ProjectInteractor& project_interactor,
        AbstractBrowserLogic& logic
    );
    virtual ~AbstractConnectRequestHandler() = default;

protected:
    Biz::ProjectInteractor& m_project_interactor;
    std::vector<BrowserLogicCommand> handle_message(const std::string& message);
    std::vector<BrowserLogicCommand> resend_config();
    std::string get_engine_check_script() const;
    /**
     * @brief JavaScript condition that is true when the page is the Connect web app and not one of
     * our local resource pages. Online it requires https, a Connect Local server may be served over
     * http.
     */
    static std::string connect_page_js_condition();

    /** @brief What with_page_session() does when no session can be opened. */
    enum class OnSessionFailure
    {
        /** @brief Push `then` anyway (e.g. load the page, it asks again). */
        Then,
        /** @brief Push page_session_failed_commands(): the page's session was refused already. */
        ShowError,
    };

    /**
     * @brief Offline (Connect Local): opens a page session for the server's pages and pushes a
     * SetCookie command with its cookie followed by `then`; the view runs `then` once the cookie
     * store has the cookie. The pages never see the credential. A refused credential (401) logs
     * out instead.
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
     * @brief Offline: shown in place of the page when its page session was refused and no new
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
     * @brief REQUEST_CONFIG from the page. Offline (Connect Local) the answer waits for the page
     * session cookie: the first request of a page gets a recent session (each one counts against
     * the server's limits), a page that asks again had its session refused (401) and gets a new
     * one.
     */
    std::vector<BrowserLogicCommand>
    on_connect_action_request_config_message(const std::string& message_data);

    AbstractBrowserLogic& m_logic;
    std::shared_ptr<char> m_page_session_alive{std::make_shared<char>(0)};
    /** @brief Offline: the current page asked for its config already. */
    bool m_page_configured{false};
};

} // namespace Slic3r::App::Browser
