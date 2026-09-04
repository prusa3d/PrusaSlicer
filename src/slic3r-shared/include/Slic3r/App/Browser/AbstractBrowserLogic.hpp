#pragma once

#include "Slic3r/App/Browser/BrowserLogicCommand.hpp"

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
};
} // namespace Slic3r::App::Browser 
