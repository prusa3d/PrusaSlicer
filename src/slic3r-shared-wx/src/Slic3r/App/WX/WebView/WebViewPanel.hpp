#pragma once

#include "Slic3r/App/Browser/AbstractBrowserLogic.hpp"
#include "Slic3r/App/WX/WebView/AbstractWebViewPanel.hpp"
#include "Slic3r/App/Browser/AbstractBrowserLogicCommandHandler.hpp"

#include <wx/webview.h>
#include <wx/sizer.h>
#include <memory>

#ifdef DEBUG_URL_PANEL
#include <wx/textctrl.h>
#include <wx/infobar.h>
#include <wx/button.h>
#include <wx/menu.h>
#endif

namespace Slic3r::App::WX::WebView {

class WebViewPanel : public AbstractWebViewPanel, public App::Browser::AbstractBrowserLogicCommandHandler
{
public:
    WebViewPanel(wxWindow* parent, int id, std::unique_ptr<App::Browser::AbstractBrowserLogic>&& logic, bool do_create);
    ~WebViewPanel() = default;

    // IUserAccountListener;
    void on_user_account_id_success(bool is_refresh, const std::string& username) override;
    void on_user_account_logged_out() override;
    void on_user_account_will_refresh() override;

    void on_user_account_action_retry(const Biz::Network::IHttp::Retry& retry, std::function<void(void)> cancel_callback) override
    { /*unused*/
    }

    void on_printables_secret_token(const std::string& body) override;

    App::Browser::AbstractBrowserLogic* browser_logic() override
    {
        return m_logic.get();
    }

    void set_next_show_url(const std::string url) override
    {
        browser_logic()->set_next_show_url(url);
    }

private:
    std::unique_ptr<App::Browser::AbstractBrowserLogic> m_logic;
    wxWebView* m_web_view{nullptr};

    std::vector<std::string> m_script_message_hadler_names;


    bool m_do_late_webview_create{false};
    bool m_busy_cursor_set {false};
    bool m_load_error_page{false};
    bool m_shown{false};
    bool m_load_default_url_on_next_error{false};

    void late_create();
    void load_error_page();
    void load_default_url();
    void run_script(const wxString& javascript);
    void do_reload();
    void load_url(const wxString& url);
    // Let WebViewPanel do on_show so it can create webview properly
    // and load default page
    void on_show(wxShowEvent& evt);
    void on_script_message(wxWebViewEvent& evt);
    void on_navigation_request(wxWebViewEvent& evt);
    void on_loaded(wxWebViewEvent& evt);
    void on_page_will_load();
    void on_created(wxWebViewEvent& evt);

    void on_error(wxWebViewEvent& evt);
    void on_idle(wxIdleEvent& evt);

    void on_url(wxCommandEvent& evt);
    void on_back_button(wxCommandEvent& evt);
    void on_forward_button(wxCommandEvent& evt);
    void on_stop_button(wxCommandEvent& evt);
    void on_reload_button(wxCommandEvent& evt);
    void on_view_text_request(wxCommandEvent& evt);
    void on_tools_clicked(wxCommandEvent& evt);
    void on_run_script_custom(wxCommandEvent& evt);
    void on_add_user_script(wxCommandEvent& evt);
    void on_set_custom_user_agent(wxCommandEvent& evt);
    void on_clear_selection(wxCommandEvent& evt);
    void on_delete_selection(wxCommandEvent& evt);
    void on_select_all(wxCommandEvent& evt);
    void on_enable_context_menu(wxCommandEvent& evt);
    void on_enable_dev_tools(wxCommandEvent& evt);

    wxBoxSizer* topsizer;
    wxBoxSizer* m_sizer_top;
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

    wxInfoBar* m_info;
    wxStaticText* m_info_text;

    wxMenuItem* m_context_menu;
    wxMenuItem* m_dev_tools;
#endif

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
