#ifndef slic3r_WebViewDialog_hpp_
#define slic3r_WebViewDialog_hpp_

//#define DEBUG_URL_PANEL

#include <map>
#include <wx/wx.h>
#include <wx/event.h>

#include "GUI_Utils.hpp"
#include "UserAccountSession.hpp"

#ifdef DEBUG_URL_PANEL
#include <wx/infobar.h>
#endif

class wxWebView;
class wxWebViewEvent;

wxDECLARE_EVENT(EVT_OPEN_EXTERNAL_LOGIN, wxCommandEvent);

namespace Slic3r {
namespace GUI {

class WebViewPanel : public wxPanel
{
public:
    WebViewPanel(wxWindow *parent, const wxString& default_url, const std::vector<std::string>& message_handler_names, const std::string& loading_html = "loading");
    virtual ~WebViewPanel();

    void load_url(const wxString& url);
    void load_default_url_delayed();
    void load_error_page();

    void on_show(wxShowEvent& evt);
    virtual void on_script_message(wxWebViewEvent& evt);

    void on_idle(wxIdleEvent& evt);
    void on_url(wxCommandEvent& evt);
    void on_back_button(wxCommandEvent& evt);
    void on_forward_button(wxCommandEvent& evt);
    void on_stop_button(wxCommandEvent& evt);
    void on_reload_button(wxCommandEvent& evt);
    
    void on_view_source_request(wxCommandEvent& evt);
    void on_view_text_request(wxCommandEvent& evt);
    void on_tools_clicked(wxCommandEvent& evt);
    void on_error(wxWebViewEvent& evt);
   
    void run_script(const wxString& javascript);
    void on_run_script_custom(wxCommandEvent& evt);
    void on_add_user_script(wxCommandEvent& evt);
    void on_set_custom_user_agent(wxCommandEvent& evt);
    void on_clear_selection(wxCommandEvent& evt);
    void on_delete_selection(wxCommandEvent& evt);
    void on_select_all(wxCommandEvent& evt);
    void On_enable_context_menu(wxCommandEvent& evt);
    void On_enable_dev_tools(wxCommandEvent& evt);
    virtual void on_navigation_request(wxWebViewEvent &evt);

    wxString get_default_url() const { return m_default_url; }
    void set_default_url(const wxString& url) { m_default_url = url; }

    virtual void sys_color_changed();
protected:

    virtual void on_page_will_load();

protected:

    wxWebView* m_browser { nullptr };
    bool m_load_default_url { false };
#ifdef DEBUG_URL_PANEL
    
    wxBoxSizer *bSizer_toolbar;
    wxButton *  m_button_back;
    wxButton *  m_button_forward;
    wxButton *  m_button_stop;
    wxButton *  m_button_reload;
    wxTextCtrl *m_url;
    wxButton *  m_button_tools;

    wxMenu* m_tools_menu;
    wxMenuItem* m_script_custom;
    
    wxInfoBar *m_info;
    wxStaticText* m_info_text;

    wxMenuItem* m_context_menu;
    wxMenuItem* m_dev_tools;
#endif

    // Last executed JavaScript snippet, for convenience.
    wxString m_javascript;
    wxString m_response_js;
    wxString m_default_url;

    std::string m_loading_html;
    //DECLARE_EVENT_TABLE()

    bool m_load_error_page { false };
    bool m_shown { false };

    std::vector<std::string> m_script_message_hadler_names;
}; 


class WebViewDialog : public DPIDialog
{
public:
    WebViewDialog(wxWindow* parent, const wxString& url, const wxString& dialog_name, const wxSize& size, const std::vector<std::string>& message_handler_names, const std::string& loading_html = "loading");
    virtual ~WebViewDialog();

    virtual void on_show(wxShowEvent& evt) {};
    virtual void on_script_message(wxWebViewEvent& evt);

    void on_idle(wxIdleEvent& evt);
    void on_url(wxCommandEvent& evt);
    void on_back_button(wxCommandEvent& evt);
    void on_forward_button(wxCommandEvent& evt);
    void on_stop_button(wxCommandEvent& evt);
    void on_reload_button(wxCommandEvent& evt);

    void on_view_source_request(wxCommandEvent& evt);
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
    
    virtual void on_navigation_request(wxWebViewEvent &evt);
    virtual void on_loaded(wxWebViewEvent &evt) {}

    void run_script(const wxString& javascript);
   
    void load_error_page();

    virtual void EndModal(int retCode) wxOVERRIDE;
protected:
    wxWebView* m_browser {nullptr};
    std::string m_loading_html;

    bool m_load_error_page{ false };
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
    // Last executed JavaScript snippet, for convenience.
    wxString m_javascript;
    wxString m_response_js;
    wxString m_default_url;

    std::vector<std::string> m_script_message_hadler_names;
};

class ConnectRequestHandler
{
public:
    ConnectRequestHandler();
    ~ConnectRequestHandler();

    void handle_message(const std::string& message);
    void resend_config();
protected:
    // action callbacs stored in m_actions
    virtual void on_connect_action_log(const std::string& message_data);
    virtual void on_connect_action_error(const std::string& message_data);
    virtual void on_connect_action_request_login(const std::string& message_data);
    virtual void on_connect_action_request_config(const std::string& message_data);
    virtual void on_connect_action_request_open_in_browser(const std::string& message_data);
    virtual void on_connect_action_select_printer(const std::string& message_data) = 0;
    virtual void on_connect_action_print(const std::string& message_data) = 0;
    virtual void on_connect_action_webapp_ready(const std::string& message_data) = 0;
    virtual void run_script_bridge(const wxString &script) = 0;

    std::map<std::string, std::function<void(const std::string&)>> m_actions;
};

class ConnectWebViewPanel : public WebViewPanel, public ConnectRequestHandler
{
public:
    ConnectWebViewPanel(wxWindow* parent);
    ~ConnectWebViewPanel() override;
    void on_script_message(wxWebViewEvent& evt) override;
    void logout();
    void sys_color_changed() override;
    void on_navigation_request(wxWebViewEvent &evt) override;
protected:
    void on_connect_action_request_login(const std::string &message_data) override;
    void on_connect_action_select_printer(const std::string& message_data) override;
    void on_connect_action_print(const std::string& message_data) override;
    void on_connect_action_webapp_ready(const std::string& message_data) override {}
    void run_script_bridge(const wxString& script) override {run_script(script); }
    void on_page_will_load() override;
    void on_connect_action_error(const std::string &message_data) override;
private:
    static wxString get_login_script(bool refresh);
    static wxString get_logout_script();
    void on_user_token(UserAccountSuccessEvent& e);
    void on_user_logged_out(UserAccountSuccessEvent& e);
    bool m_reached_default_url {false};
};

class PrinterWebViewPanel : public WebViewPanel
{
public:
    PrinterWebViewPanel(wxWindow* parent, const wxString& default_url);
    
    void on_loaded(wxWebViewEvent& evt);

    void send_api_key();
    void send_credentials();
    void set_api_key(const std::string &key)
    {
        if (m_api_key != key) {
            clear();
            m_api_key = key;
        }
    }
    void set_credentials(const std::string &usr, const std::string &psk)
    {
        if (m_usr != usr || m_psk != psk) {
            clear();
            m_usr = usr;
            m_psk = psk;
        }
    }
    void clear() { m_api_key.clear(); m_usr.clear(); m_psk.clear(); m_api_key_sent = false; }
    void sys_color_changed() override;
private:
    std::string m_api_key;
    std::string m_usr;
    std::string m_psk;
    bool m_api_key_sent {false};
};

class PrinterPickWebViewDialog : public WebViewDialog, public ConnectRequestHandler
{
public:
    PrinterPickWebViewDialog(wxWindow* parent, std::string& ret_val);
    void on_show(wxShowEvent& evt) override;
    void on_script_message(wxWebViewEvent& evt) override;
protected:
    void on_connect_action_select_printer(const std::string& message_data) override;
    void on_connect_action_print(const std::string& message_data) override;
    void on_connect_action_webapp_ready(const std::string& message_data) override;
    void request_compatible_printers_FFF();
    void request_compatible_printers_SLA();
    void run_script_bridge(const wxString& script) override { run_script(script); }
    void on_dpi_changed(const wxRect &suggested_rect) override;

private:
    std::string& m_ret_val;
};

class SourceViewDialog : public wxDialog
{
public:
    SourceViewDialog(wxWindow* parent, wxString source);
};

class LoginWebViewDialog : public WebViewDialog
{
public:
    LoginWebViewDialog(wxWindow *parent, std::string &ret_val, const wxString& url, wxEvtHandler* evt_handler);
    void on_navigation_request(wxWebViewEvent &evt) override;
    void on_dpi_changed(const wxRect &suggested_rect) override;
private:
    std::string&    m_ret_val;
    wxEvtHandler*   p_evt_handler;
    bool            m_evt_sent{ false };
};

} // GUI
} // Slic3r

#endif /* slic3r_Tab_hpp_ */
