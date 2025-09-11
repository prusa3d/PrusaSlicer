#ifndef slic3r_WebViewPanel_hpp_
#define slic3r_WebViewPanel_hpp_

#include <map>
#include <wx/wx.h>
#include <wx/event.h>

#include "GUI_Utils.hpp"
#include "UserAccountSession.hpp"
#include "ConnectRequestHandler.hpp"
#include "slic3r/Utils/ServiceConfig.hpp"

#ifdef DEBUG_URL_PANEL
#include <wx/infobar.h>
#endif

class wxWebView;
class wxWebViewEvent;

wxDECLARE_EVENT(EVT_PRINTABLES_CONNECT_PRINT, wxCommandEvent);

namespace Slic3r::GUI {

class WebViewPanel : public wxPanel
{
public:
    WebViewPanel(wxWindow *parent, const wxString& default_url, const std::vector<std::string>& message_handler_names, const std::string& loading_html, const std::string& error_html, bool do_create);
    virtual ~WebViewPanel();
    void destroy_browser();
    void set_create_browser() {m_do_late_webview_create = true; m_load_default_url = true; }

    void load_url(const wxString& url);
    void load_default_url_delayed();
    void load_error_page();

    // Let WebViewPanel do on_show so it can create webview properly
    // and load default page
    // override after_on_show for more actions in on_show
    void on_show(wxShowEvent& evt);
    virtual void after_on_show(wxShowEvent& evt) {}

    virtual void on_script_message(wxWebViewEvent& evt);

    void on_idle(wxIdleEvent& evt);
    virtual void on_loaded(wxWebViewEvent& evt);
    void on_url(wxCommandEvent& evt);
    virtual void on_back_button(wxCommandEvent& evt);
    virtual void on_forward_button(wxCommandEvent& evt);
    void on_stop_button(wxCommandEvent& evt);
    virtual void on_reload_button(wxCommandEvent& evt);
    
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

    virtual wxString get_default_url() const { return m_default_url; }
    void set_default_url(const wxString& url) { m_default_url = url; }
    virtual void do_reload();
    virtual void load_default_url();

    virtual void sys_color_changed();

    void set_load_default_url_on_next_error(bool val) { m_load_default_url_on_next_error = val; }
   
    void on_app_quit_event(const std::string& message_data);
    void on_app_minimize_event(const std::string& message_data);
    void prohibit_after_show_func_once() {m_after_show_func_prohibited_once = true; }
protected:
    virtual void late_create();
    virtual void define_css();
    virtual void on_page_will_load();


    wxWebView* m_browser { nullptr };
    bool m_load_default_url { false };

    wxBoxSizer* topsizer;
    wxBoxSizer* m_sizer_top;
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
    bool m_reached_default_url {false};
    bool m_after_show_func_prohibited_once {false};

    std::string m_loading_html;
    std::string m_error_html;
    //DECLARE_EVENT_TABLE()

    bool m_load_error_page { false };
    bool m_shown { false };
    bool m_load_default_url_on_next_error { false };
    bool m_do_late_webview_create {false};
    bool m_styles_defined {false};

    std::vector<std::string> m_script_message_hadler_names;
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
    void late_create() override;
    void on_connect_action_request_login(const std::string &message_data) override;
    void on_connect_action_select_printer(const std::string& message_data) override;
    void on_connect_action_print(const std::string& message_data) override;
    void on_connect_action_webapp_ready(const std::string& message_data) override {}
    void run_script_bridge(const wxString& script) override {run_script(script); }
    void on_page_will_load() override;
    void on_connect_action_error(const std::string &message_data) override;
    void on_reload_event(const std::string& message_data) override;
    void on_connect_action_close_dialog(const std::string& message_data) override {assert(false);}
    void on_user_token(UserAccountSuccessEvent& e);
    void define_css() override;
    void after_on_show(wxShowEvent& evt) override;
private:
    static wxString get_login_script(bool refresh);
    static wxString get_logout_script();
    void on_user_logged_out(UserAccountSuccessEvent& e);
};

class PrinterWebViewPanel : public WebViewPanel
{
public:
    PrinterWebViewPanel(wxWindow* parent, const wxString& default_url);
    
    void on_loaded(wxWebViewEvent& evt) override;
    void on_script_message(wxWebViewEvent& evt) override;
    void on_navigation_request(wxWebViewEvent &evt) override;
    void send_api_key();
    void send_credentials();
    void set_api_key(const std::string &key)
    {
        clear();
        m_api_key = key;
    }
    void set_credentials(const std::string &usr, const std::string &psk)
    {
        clear();
        m_usr = usr;
        m_psk = psk;
    }
    void clear() { m_api_key.clear(); m_usr.clear(); m_psk.clear(); m_api_key_sent = false; }

    void on_reload_event(const std::string& message_data);
protected:
    void define_css() override;
private:
    std::string m_api_key;
    std::string m_usr;
    std::string m_psk;
    bool m_api_key_sent {false};

    void handle_message(const std::string& message);
    std::map<std::string, std::function<void(const std::string&)>> m_events;
};

class PrintablesWebViewPanel : public WebViewPanel
{
public:
    PrintablesWebViewPanel(wxWindow* parent);
    void on_navigation_request(wxWebViewEvent &evt) override;
    void on_loaded(wxWebViewEvent& evt) override;
    void after_on_show(wxShowEvent& evt) override;
    void on_script_message(wxWebViewEvent& evt) override;
    void sys_color_changed() override;

    void logout(const std::string& override_url = std::string());
    void login(const std::string& access_token, const std::string& override_url = std::string());
    void send_refreshed_token(const std::string& access_token);
    void send_will_refresh();
    wxString get_default_url() const override;
    void set_next_show_url(const std::string& url) {m_next_show_url = Utils::ServiceConfig::instance().printables_url() + url; }
protected:
    void define_css() override;
private:
     void handle_message(const std::string& message);
     void on_printables_event_dummy(const std::string& message_data) {}
     void on_printables_event_access_token_expired(const std::string& message_data);
     void on_reload_event(const std::string& message_data);
     void on_printables_event_print_gcode(const std::string& message_data);
     void on_printables_event_download_file(const std::string& message_data);
     void on_printables_event_slice_file(const std::string& message_data);
     void on_printables_event_required_login(const std::string& message_data);
     void on_printables_event_open_url(const std::string& message_data);
     void on_dummy_event(const std::string& message_data) {}
     void load_default_url() override;
     std::string get_url_lang_theme(const wxString& url) const;
     void show_download_notification(const std::string& filename);
     void show_loading_overlay();
     void hide_loading_overlay();

     std::map<std::string, std::function<void(const std::string&)>> m_events;
     std::string m_next_show_url;

     bool m_refreshing_token {false};
#ifdef _WIN32
     bool m_remove_request_auth { false };
#endif
/*
Eventy Slicer -> Printables
accessTokenWillChange
WebUI zavola event predtim nez udela refresh access tokenu proti Prusa Accountu na Printables to bude znamenat pozastaveni requestu Mobile app muze chtit udelat refresh i bez explicitni predchozi printables zadosti skrz accessTokenExpired event
accessTokenChange
window postMessage JSON stringify { event 'accessTokenChange' token 'eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVC' }
volani po uspesne rotaci tokenu
historyBack
navigace zpet triggerovana z mobilni aplikace
historyForward
navigace vpred triggerovana z mobilni aplikace
*/
};
} // namespace Slic3r::GUI

#endif /* slic3r_WebViewPanel_hpp_ */