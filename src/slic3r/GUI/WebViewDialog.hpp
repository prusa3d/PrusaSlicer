#ifndef slic3r_WebViewDialog_hpp_
#define slic3r_WebViewDialog_hpp_

#include <map>
#include <wx/wx.h>
#include <wx/event.h>

#include "GUI_Utils.hpp"
#include "UserAccountSession.hpp"
#include "ConnectRequestHandler.hpp"

#ifdef DEBUG_URL_PANEL
#include <wx/infobar.h>
#endif

#include <atomic>

class wxWebView;
class wxWebViewEvent;

wxDECLARE_EVENT(EVT_OPEN_EXTERNAL_LOGIN, wxCommandEvent);

namespace Slic3r {
namespace GUI {

class WebViewDialog : public DPIDialog
{
public:
    WebViewDialog(wxWindow* parent, const wxString& url, const wxString& dialog_name, const wxSize& size, const std::vector<std::string>& message_handler_names, const std::string& loading_html = "other_loading");
    virtual ~WebViewDialog();

    virtual void on_show(wxShowEvent& evt) {};
    virtual void on_script_message(wxWebViewEvent& evt);

    virtual void on_idle(wxIdleEvent& evt);
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
    virtual void on_loaded(wxWebViewEvent &evt);

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

class PrinterPickWebViewDialog : public WebViewDialog, public ConnectRequestHandler
{
public:
    PrinterPickWebViewDialog(wxWindow* parent, std::string& ret_val, bool multiple_beds);
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
    void on_reload_event(const std::string& message_data) override;
    void on_connect_action_close_dialog(const std::string& message_data) override {assert(false);}
private:
    std::string& m_ret_val;
    bool m_multiple_beds;
};

class PrintablesConnectUploadDialog : public WebViewDialog, public ConnectRequestHandler
{
public:
    PrintablesConnectUploadDialog(wxWindow* parent, const std::string url);
    void on_script_message(wxWebViewEvent& evt) override;
protected:
    void on_dpi_changed(const wxRect &suggested_rect) override;

    void on_connect_action_select_printer(const std::string& message_data) override;
    void on_connect_action_print(const std::string& message_data) override;
    void on_connect_action_webapp_ready(const std::string& message_data) override;
    void on_reload_event(const std::string& message_data) override;
    void run_script_bridge(const wxString &script) override { run_script(script); }
    void on_connect_action_close_dialog(const std::string& message_data) override;
   };

class LoginWebViewDialog : public WebViewDialog
{
public:
    LoginWebViewDialog(wxWindow *parent, std::string &ret_val, const wxString& url, wxEvtHandler* evt_handler);
    void on_navigation_request(wxWebViewEvent &evt) override;
    void on_dpi_changed(const wxRect &suggested_rect) override;
    void on_idle(wxIdleEvent& evt) override;
private:
    std::string&    m_ret_val;
    wxEvtHandler*   p_evt_handler;
    bool            m_evt_sent{ false };

    bool                m_waiting_for_counters {false};
    std::atomic<size_t> m_atomic_counter;
    size_t              m_counter_to_match;
    wxTimer             m_force_quit_timer;
    bool                m_force_quit{false};
};

} // GUI
} // Slic3r

#endif /* slic3r_WebViewDialog_hpp_ */
