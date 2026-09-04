#pragma once

#include "Slic3r/App/Browser/AbstractBrowserLogic.hpp"
#include "Slic3r/App/Browser/AbstractConnectRequestHandler.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
}
namespace Slic3r::App::Browser {

class BrowserLogicConnectPage final : public AbstractBrowserLogic, public AbstractConnectRequestHandler
{
public:
    BrowserLogicConnectPage(Biz::ProjectInteractor& project_interactor);
    
    // AbstractBrowserLogic
    std::string access_token() override;
    std::vector<BrowserLogicCommand> on_script_message_webview_event(const std::string& message) override;
    std::vector<BrowserLogicCommand> on_show_webview_event(bool show) override;
    std::vector<BrowserLogicCommand> on_navigation_request_webview_event(const std::string& new_url, const std::string& current_url) override;
    std::vector<BrowserLogicCommand> on_load_default_url() override;
    std::vector<BrowserLogicCommand> on_page_will_load_webview_event() override;
    std::vector<BrowserLogicCommand> on_webview_created() override;
    std::vector<BrowserLogicCommand> on_loaded_webview_event(const std::string& url) override;
    std::vector<BrowserLogicCommand> on_user_account_id_success(bool is_refresh, const std::string& current_url) override;
    std::vector<BrowserLogicCommand> on_user_account_logged_out(const std::string& current_url) override;

    /**
     * @brief call when connect panel is being removed 
     */
    std::vector<BrowserLogicCommand> logout();

protected:
    // AbstractConnectRequestHandler
    std::vector<BrowserLogicCommand> on_connect_action_select_printer(const std::string& message_data) override;
    std::vector<BrowserLogicCommand> on_connect_action_print(const std::string& message_data) override;
    std::vector<BrowserLogicCommand> on_connect_action_webapp_ready(const std::string& message_data) override;
    std::vector<BrowserLogicCommand> on_webview_reload_event(const std::string& message_data) override;
    std::vector<BrowserLogicCommand> on_connect_action_close_dialog(const std::string& message_data) override;
    std::vector<BrowserLogicCommand> on_connect_action_request_login(const std::string &message_data) override;
    std::vector<BrowserLogicCommand> on_connect_action_error(const std::string &message_data) override;
    std::vector<BrowserLogicCommand> on_connect_action_log_in_in_browser(const std::string& message_data) override;

private:
    bool m_styles_defined {false};
    bool m_reached_default_url {false};
    bool m_load_default_url { true };
    bool m_logged_out {false};

    void emplace_load_default_url_commands(std::vector<BrowserLogicCommand>& res);
    void emplace_load_logged_out_page_commands(std::vector<BrowserLogicCommand>& res);
    void emplace_define_css_commands(std::vector<BrowserLogicCommand>& res);
    std::string get_login_script(bool refresh, const std::string& access_token) const;
    std::string get_logout_script() const;

};
} // namespace Slic3r::App::Browser 