#pragma once

#include "Slic3r/App/Browser/BrowserLogicCommand.hpp"
#include "Slic3r/Biz/UserAccount/IUserAccountActionCallbacks.hpp"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r::App::Browser {

class AbstractBrowserLogic {
public:
    AbstractBrowserLogic(
        const std::string& url,
        std::vector<std::string>&& message_handler_names,
        const std::string& loading_html = "other_loading",
        const std::string& error_html   = "other_error",
        const std::string& title        = "title"
    ) :
        m_url(url),
        m_loading_html(loading_html),
        m_error_html(error_html),
        m_component_title(title),
        m_script_message_handler_names(std::move(message_handler_names))
    {}
    virtual ~AbstractBrowserLogic() = default;
    
    std::string title() const { return m_component_title; }
    std::string url() const { return m_url; }
    const std::vector<std::string>& script_message_handler_names() const { return m_script_message_handler_names; }
    std::string loading_html() const { return m_loading_html; }
    std::string error_html() const { return m_error_html; }

    virtual bool requires_native_script_run() const { return false; }
    virtual std::pair<int,int> size(int em_unit) const {return std::pair<int,int>(100*em_unit, 100*em_unit); }
    virtual std::pair<int,int> min_size(int em_unit) const {return std::pair<int,int>(40*em_unit, 40*em_unit); }
    virtual std::string access_token() { return {}; }

    virtual std::vector<BrowserLogicCommand> on_navigation_request_webview_event(const std::string& new_url, const std::string& current_url) { return {}; }
    virtual std::vector<BrowserLogicCommand> on_script_message_webview_event(const std::string& message) { return {}; }
    virtual std::vector<BrowserLogicCommand> on_show_webview_event(bool show) { return {}; }
    virtual std::vector<BrowserLogicCommand> on_page_will_load_webview_event() { return {}; }
    virtual std::vector<BrowserLogicCommand> on_webview_created() {return {};}
    virtual std::vector<BrowserLogicCommand> on_load_default_url() { return {{BrowserLogicCommandType::LoadURL, m_url}};}
    virtual std::vector<BrowserLogicCommand> on_loaded_webview_event(const std::string& url) {return {};}
    virtual std::vector<BrowserLogicCommand> on_user_account_id_success(bool is_refresh, const std::string& current_url) {return {};}
    virtual std::vector<BrowserLogicCommand> on_user_account_logged_out(const std::string& current_url) {return {};}
    virtual std::vector<BrowserLogicCommand> on_user_account_will_refresh() {return {};}
    virtual std::vector<BrowserLogicCommand> on_printables_secret_token(const std::string& body) {return {};}

    /**
     * @brief Connect Local app pairing events, see IUserAccountListener.
     */
    virtual std::vector<BrowserLogicCommand>
    on_user_account_pairing_ready(const std::string& page_url, const std::string& user_code)
    {
        return {};
    }

    virtual std::vector<BrowserLogicCommand>
    on_user_account_pairing_failed(Biz::UserAccount::PairingFailure failure)
    {
        return {};
    }

    virtual std::vector<BrowserLogicCommand> on_user_account_pairing_approved()
    {
        return {};
    }

    /**
     * @brief A page failed to load. Commands returned replace the error page of the hosting view,
     * no commands keep it.
     * @param certificate_error The server certificate was refused.
     */
    virtual std::vector<BrowserLogicCommand>
    on_load_error(const std::string& url, bool certificate_error)
    {
        return {};
    }

    /**
     * @brief The hosting dialog is being closed by the person (close button, Esc), not by the
     * logic. The commands run before the dialog ends; the web view still exists.
     */
    virtual std::vector<BrowserLogicCommand> on_dialog_closing()
    {
        return {};
    }

    /**
     * @brief Called when the hosting view is hidden (on_show_webview_event is only called on show).
     */
    virtual void on_hide_webview_event() {}

    /**
     * @brief Called whenever the hosting view becomes visible, also on the first show, when the
     * web view is only being created and on_show_webview_event is not called.
     */
    virtual void on_visible_webview_event() {}

    /**
     * @brief Lets the logic send commands on its own, outside of a webview event (e.g. results of
     * a background task). Set by the hosting view; commands must be pushed on the main thread.
     */
    void set_command_sink(std::function<void(std::vector<BrowserLogicCommand>)> sink)
    {
        m_command_sink = std::move(sink);
    }

    /**
     * @brief Sends commands to the hosting view outside of a webview event, see set_command_sink().
     */
    void push_commands(std::vector<BrowserLogicCommand> commands)
    {
        if (m_command_sink && !commands.empty()) {
            m_command_sink(std::move(commands));
        }
    }

    void set_next_show_url(std::string url) {m_next_show_url = url;}
    void set_title(const std::string& title) { m_component_title = title; }

protected:
    std::string m_url;
    std::string m_loading_html;
    std::string m_error_html;
    std::string m_next_show_url;

private:
    std::string m_component_title;
    std::vector<std::string> m_script_message_handler_names;
    std::function<void(std::vector<BrowserLogicCommand>)> m_command_sink;
};
} // namespace Slic3r::App::Browser 
