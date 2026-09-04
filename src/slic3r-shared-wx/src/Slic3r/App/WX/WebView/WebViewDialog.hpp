#pragma once

#include "Slic3r/App/Browser/AbstractBrowserLogic.hpp"
#include "Slic3r/App/WX/WebView/AbstractWebViewDialog.hpp"
#include "Slic3r/App/Browser/AbstractBrowserLogicCommandHandler.hpp"

#include <wx/webview.h>
#include <memory>
#include <atomic>

namespace Slic3r::App::WX::WebView {

class WebViewDialog : public AbstractWebViewDialog, public App::Browser::AbstractBrowserLogicCommandHandler
{
public:
    WebViewDialog(std::unique_ptr<App::Browser::AbstractBrowserLogic>&& logic);
    ~WebViewDialog() = default;

    // IUserAccountListener
    void on_user_account_id_success(bool is_refresh, const std::string& username) override;
    void on_user_account_logged_out() override;
    void on_user_account_will_refresh() override;

    void on_user_account_action_retry(const Biz::Network::IHttp::Retry& retry, std::function<void(void)> cancel_callback) override
    { /*unused*/
    }

private:
    std::unique_ptr<App::Browser::AbstractBrowserLogic> m_logic;
    wxWebView* m_web_view;

    void on_show(wxShowEvent& evt);
    void on_script_message(wxWebViewEvent& evt);
    void on_idle(wxIdleEvent& evt);
    void on_url(wxCommandEvent& evt);
    void on_back_button(wxCommandEvent& evt);
    void on_forward_button(wxCommandEvent& evt);
    void on_stop_button(wxCommandEvent& evt);
    void on_reload_button(wxCommandEvent& evt);
    void on_view_text_request(wxCommandEvent& evt);
    void on_tools_clicked(wxCommandEvent& evt);
    void on_error(wxWebViewEvent& evt);
    void on_run_script_custom(wxCommandEvent& evt);
    void on_add_user_script(wxCommandEvent& evt);
    void on_set_custom_user_agent(wxCommandEvent& evt);
    void on_clear_selection(wxCommandEvent& evt);
    void on_delete_selection(wxCommandEvent& evt);
    void on_select_all(wxCommandEvent& evt);
    void On_enable_context_menu(wxCommandEvent& evt);
    void On_enable_dev_tools(wxCommandEvent& evt);
    void on_navigation_request(wxWebViewEvent& evt);
    void on_loaded(wxWebViewEvent& evt);

    void run_script(const wxString& javascript);
    void load_error_page();
    void EndModal(int retCode) wxOVERRIDE;
    void do_reload();

#ifdef DEBUG_URL_PANEL
    wxBoxSizer* bSizer_toolbar;
    wxButton* m_button_back;
    wxButton* m_button_forward;
    wxButton* m_button_stop;
    wxButton* m_button_reload;
    wxTextCtrl* m_url;
    wxButton* m_button_tools;
    wxMenu* m_tools_menu;
    wxMenuItem* m_script_custom;
    wxStaticText* m_info_text;
    wxMenuItem* m_context_menu;
    wxMenuItem* m_dev_tools;
#endif

    std::string m_loading_html;
    bool m_load_error_page{false};

    wxString m_response_js;
    wxString m_default_url;

    bool m_waiting_for_counters{false};
    std::atomic<size_t> m_atomic_counter{0};
    size_t m_counter_to_match{0};
    bool m_force_close{false};
    bool m_busy_cursor_set {false};

protected:
    bool handle_logic_command_LoadURL(const std::string& data) override;
    bool handle_logic_command_LoadRequest(const std::string& data) override;
    bool handle_logic_command_RunScript(const std::string& data) override;
    bool handle_logic_command_EndModalOK(const std::string& data) override;
    bool handle_logic_command_EndModalCancel(const std::string& data) override;
    bool handle_logic_command_DeleteCookies(const std::string& data) override;
    bool handle_logic_command_DeleteCookiesWithCounter(const std::string& data) override;
    bool handle_logic_command_Veto(const std::string& data) override;
    bool handle_logic_command_DoReload(const std::string& data) override;
    bool handle_logic_command_AddUserScript(const std::string& data) override;
    bool handle_logic_command_LoadResourcesPage(const std::string& data) override;
    bool handle_logic_command_OpenExternalBrowser(const std::string& data) override;
    bool handle_logic_command_RegisterPrusaSlicerURL(const std::string& data) override;
    bool handle_logic_command_SetLoadDefaultURLOnErrorTrue(const std::string& data) override;
    bool handle_logic_command_SetLoadDefaultURLOnErrorFalse(const std::string& data) override;
    bool handle_logic_command_SwitchToSlicing(const std::string& data) override;
    bool handle_logic_command_SetBasicAuth(const std::string& data) override;
    bool handle_logic_command_ClearBasicAuth(const std::string& data) override;
};

} // namespace Slic3r::App::WX::WebView
