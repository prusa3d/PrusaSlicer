#include "WebViewPanel.hpp"

#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/UserAccount.hpp"
#include "slic3r/GUI/format.hpp"
#include "slic3r/GUI/WebView.hpp"
#include "slic3r/GUI/WebViewPlatformUtils.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/Field.hpp"
#include "slic3r/Utils/Http.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Config.hpp"

#include <libslic3r/PresetBundle.hpp> // IWYU pragma: keep

#include <wx/webview.h>
#include <wx/url.h>
#include <curl/curl.h>
#include <boost/log/trivial.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <regex>

// if set to 1 the fetch() JS function gets override to include JWT in authorization header
// if set to 0, the /slicer/login is invoked from WebKit (passing JWT token only to this request)
// to set authorization cookie for all WebKit requests to Connect
#define AUTH_VIA_FETCH_OVERRIDE 0

wxDEFINE_EVENT(EVT_PRINTABLES_CONNECT_PRINT, wxCommandEvent);

namespace pt = boost::property_tree;

namespace Slic3r::GUI {

WebViewPanel::~WebViewPanel()
{
    SetEvtHandlerEnabled(false);
#ifdef DEBUG_URL_PANEL
    delete m_tools_menu;
#endif
}

void WebViewPanel::destroy_browser()
{
    if (!m_browser || m_do_late_webview_create) {
        return;
    }
    topsizer->Detach(m_browser);
    m_browser->Destroy();
    m_browser = nullptr;
}


void WebViewPanel::load_url(const wxString& url)
{
    if (!m_browser)
        return;

    this->on_page_will_load();

    this->Show();
    this->Raise();
#ifdef DEBUG_URL_PANEL
    m_url->SetLabelText(url);
#endif
    wxString correct_url = url.empty() ? wxString("") : wxURI(url).BuildURI();
    m_browser->LoadURL(correct_url);
    m_browser->SetFocus();
}


WebViewPanel::WebViewPanel(wxWindow *parent, const wxString& default_url, const std::vector<std::string>& message_handler_names, const std::string& loading_html, const std::string& error_html, bool do_create)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
    , m_default_url (default_url)
    , m_loading_html(loading_html)
    , m_error_html(error_html)
    , m_script_message_hadler_names(message_handler_names)
{
    topsizer = new wxBoxSizer(wxVERTICAL);
    m_sizer_top = new wxBoxSizer(wxHORIZONTAL);
    topsizer->Add(m_sizer_top, 0, wxEXPAND, 0);

#ifdef DEBUG_URL_PANEL
    // Create the button
    bSizer_toolbar = new wxBoxSizer(wxHORIZONTAL);

    m_button_back = new wxButton(this, wxID_ANY, wxT("Back"), wxDefaultPosition, wxDefaultSize, 0);
    //m_button_back->Enable(false);
    bSizer_toolbar->Add(m_button_back, 0, wxALL, 5);

    m_button_forward = new wxButton(this, wxID_ANY, wxT("Forward"), wxDefaultPosition, wxDefaultSize, 0);
    //m_button_forward->Enable(false);
    bSizer_toolbar->Add(m_button_forward, 0, wxALL, 5);

    m_button_stop = new wxButton(this, wxID_ANY, wxT("Stop"), wxDefaultPosition, wxDefaultSize, 0);

    bSizer_toolbar->Add(m_button_stop, 0, wxALL, 5);

    m_button_reload = new wxButton(this, wxID_ANY, wxT("Reload"), wxDefaultPosition, wxDefaultSize, 0);
    bSizer_toolbar->Add(m_button_reload, 0, wxALL, 5);

    m_url = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    bSizer_toolbar->Add(m_url, 1, wxALL | wxEXPAND, 5);

    m_button_tools = new wxButton(this, wxID_ANY, wxT("Tools"), wxDefaultPosition, wxDefaultSize, 0);
    bSizer_toolbar->Add(m_button_tools, 0, wxALL, 5);

    // Create panel for find toolbar.
    wxPanel* panel = new wxPanel(this);
    topsizer->Add(bSizer_toolbar, 0, wxEXPAND, 0);
    topsizer->Add(panel, wxSizerFlags().Expand());

    // Create sizer for panel.
    wxBoxSizer* panel_sizer = new wxBoxSizer(wxVERTICAL);
    panel->SetSizer(panel_sizer);

    // Create the info panel
    m_info = new wxInfoBar(this);
    topsizer->Add(m_info, wxSizerFlags().Expand());
#endif

    SetSizer(topsizer);
    
    Bind(wxEVT_SHOW, &WebViewPanel::on_show, this);
    Bind(wxEVT_IDLE, &WebViewPanel::on_idle, this);

#ifdef DEBUG_URL_PANEL
    // Create the Tools menu
    m_tools_menu = new wxMenu();
    wxMenuItem* viewSource = m_tools_menu->Append(wxID_ANY, "View Source");
    wxMenuItem* viewText = m_tools_menu->Append(wxID_ANY, "View Text");
    m_tools_menu->AppendSeparator();

    wxMenu* script_menu = new wxMenu;

    m_script_custom = script_menu->Append(wxID_ANY, "Custom script");
    m_tools_menu->AppendSubMenu(script_menu, "Run Script");
    wxMenuItem* addUserScript = m_tools_menu->Append(wxID_ANY, "Add user script");
    wxMenuItem* setCustomUserAgent = m_tools_menu->Append(wxID_ANY, "Set custom user agent");

    m_context_menu = m_tools_menu->AppendCheckItem(wxID_ANY, "Enable Context Menu");
    m_dev_tools = m_tools_menu->AppendCheckItem(wxID_ANY, "Enable Dev Tools");

    // Connect the button events
    Bind(wxEVT_BUTTON, &WebViewPanel::on_back_button, this, m_button_back->GetId());
    Bind(wxEVT_BUTTON, &WebViewPanel::on_forward_button, this, m_button_forward->GetId());
    Bind(wxEVT_BUTTON, &WebViewPanel::on_stop_button, this, m_button_stop->GetId());
    Bind(wxEVT_BUTTON, &WebViewPanel::on_reload_button, this, m_button_reload->GetId());
    Bind(wxEVT_BUTTON, &WebViewPanel::on_tools_clicked, this, m_button_tools->GetId());
    Bind(wxEVT_TEXT_ENTER, &WebViewPanel::on_url, this, m_url->GetId());

    // Connect the menu events
    Bind(wxEVT_MENU, &WebViewPanel::on_view_source_request, this, viewSource->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::on_view_text_request, this, viewText->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::On_enable_context_menu, this, m_context_menu->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::On_enable_dev_tools, this, m_dev_tools->GetId());

    Bind(wxEVT_MENU, &WebViewPanel::on_run_script_custom, this, m_script_custom->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::on_add_user_script, this, addUserScript->GetId());
#endif

    // Create the webview
    if (!do_create) {
        m_do_late_webview_create = true;
        return;
    }
    m_do_late_webview_create = false;
    late_create();
}

void WebViewPanel::late_create()
{
    m_do_late_webview_create = false;
    m_browser = WebView::webview_new();
   
    if (!m_browser) {
        wxStaticText* text = new wxStaticText(this, wxID_ANY, _L("Failed to load a web browser."));
        topsizer->Add(text, 0, wxALIGN_LEFT | wxBOTTOM, 10);
        return;
    }
    WebView::webview_create(m_browser,this, GUI::format_wxstr("file://%1%/web/%2%.html", boost::filesystem::path(resources_dir()).generic_string(), m_loading_html), m_script_message_hadler_names);
 
    if (Utils::ServiceConfig::instance().webdev_enabled()) {
        m_browser->EnableContextMenu();
        m_browser->EnableAccessToDevTools();
    }
    topsizer->Add(m_browser, wxSizerFlags().Expand().Proportion(1));

    // Connect the webview events
    Bind(wxEVT_WEBVIEW_ERROR, &WebViewPanel::on_error, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &WebViewPanel::on_script_message, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_NAVIGATING, &WebViewPanel::on_navigation_request, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_LOADED, &WebViewPanel::on_loaded, this, m_browser->GetId());
    Layout();
}

void WebViewPanel::load_default_url_delayed()
{
    assert(!m_default_url.empty());
    m_load_default_url = true;
}

void WebViewPanel::load_error_page()
{
    if (!m_browser || m_do_late_webview_create) {
        return;
    }

    m_browser->Stop();
    m_load_error_page = true;    
}

void WebViewPanel::on_show(wxShowEvent& evt)
{
    m_shown = evt.IsShown();
    if (!m_shown) {
        wxSetCursor(wxNullCursor);
        return;
    }
    if (m_do_late_webview_create) {
        m_do_late_webview_create = false;
        late_create();
        return;
    }
    if (m_load_default_url) {
        m_load_default_url = false;
        load_default_url();
        return;
    }

    if (m_after_show_func_prohibited_once) {
        m_after_show_func_prohibited_once = false;
        return;
    }
    after_on_show(evt);
}

void WebViewPanel::on_idle(wxIdleEvent& WXUNUSED(evt))
{
    if (!m_browser || m_do_late_webview_create)
        return;

    // The busy cursor on webview is switched off on Linux.
    // Because m_browser->IsBusy() is almost always true on Printables / Connect.
#ifndef __linux__
    if (m_shown) {
        if (m_browser->IsBusy()) {
            wxSetCursor(wxCURSOR_ARROWWAIT);
        } else {
            wxSetCursor(wxNullCursor);
        }
    }
#endif // !__linux__

    if (m_shown && m_load_error_page && !m_browser->IsBusy()) {
        m_load_error_page = false;
        if (m_load_default_url_on_next_error) {
            m_load_default_url_on_next_error = false;
            load_default_url();
        } else { 
            load_url(GUI::format_wxstr("file://%1%/web/%2%.html", boost::filesystem::path(resources_dir()).generic_string(), m_error_html));
            // This is a fix of broken message handling after error.
            // F.e. if there is an error but we do AddUserScript & Reload, the handling will break.
            // So we just reset the handler here.
            if (!m_script_message_hadler_names.empty()) {
                m_browser->RemoveScriptMessageHandler(Slic3r::GUI::from_u8(m_script_message_hadler_names.front()));
                m_browser->AddScriptMessageHandler(Slic3r::GUI::from_u8(m_script_message_hadler_names.front()));
            } 
        }
    }
    
#ifdef DEBUG_URL_PANEL
    m_button_stop->Enable(m_browser->IsBusy());
#endif
}

void WebViewPanel::on_loaded(wxWebViewEvent& evt)
{
    if (evt.GetURL().IsEmpty())
        return;

    if (evt.GetURL().StartsWith(m_default_url)) {
        define_css();
    } else {
        m_styles_defined = false;
    }

    m_load_default_url_on_next_error = false;
    if (evt.GetURL().Find(GUI::format_wxstr("/web/%1%.html", m_loading_html)) != wxNOT_FOUND && m_load_default_url) {
        m_load_default_url = false;
        load_default_url();
    }
}

/**
    * Callback invoked when user entered an URL and pressed enter
    */
void WebViewPanel::on_url(wxCommandEvent& WXUNUSED(evt))
{
    if (!m_browser)
        return;
#ifdef DEBUG_URL_PANEL
    m_browser->LoadURL(m_url->GetValue());
    m_browser->SetFocus();
#endif
}

/**
    * Callback invoked when user pressed the "back" button
    */
void WebViewPanel::on_back_button(wxCommandEvent& WXUNUSED(evt))
{
    if (!m_browser)
        return;
    if (!m_browser->CanGoBack())
        return;
    m_browser->GoBack();
}

/**
    * Callback invoked when user pressed the "forward" button
    */
void WebViewPanel::on_forward_button(wxCommandEvent& WXUNUSED(evt))
{
    if (!m_browser)
        return;
    if (!m_browser->CanGoForward())
        return;
    m_browser->GoForward();
}

/**
    * Callback invoked when user pressed the "stop" button
    */
void WebViewPanel::on_stop_button(wxCommandEvent& WXUNUSED(evt))
{
    if (!m_browser)
        return;
    m_browser->Stop();
}

/**
    * Callback invoked when user pressed the "reload" button
    */
void WebViewPanel::on_reload_button(wxCommandEvent& WXUNUSED(evt))
{
    if (!m_browser)
        return;
    m_browser->Reload();
}

void WebViewPanel::on_script_message(wxWebViewEvent& evt)
{
    BOOST_LOG_TRIVIAL(error) << "unhandled script message: " << evt.GetString();
}

void WebViewPanel::on_navigation_request(wxWebViewEvent &evt) 
{
}

void WebViewPanel::on_page_will_load()
{
}

/**
    * Invoked when user selects the "View Source" menu item
    */
void WebViewPanel::on_view_source_request(wxCommandEvent& WXUNUSED(evt))
{
    if (!m_browser)
        return;

    SourceViewDialog dlg(this, m_browser->GetPageSource());
    dlg.ShowModal();
}

/**
    * Invoked when user selects the "View Text" menu item
    */
void WebViewPanel::on_view_text_request(wxCommandEvent& WXUNUSED(evt))
{
    if (!m_browser)
        return;

    wxDialog textViewDialog(this, wxID_ANY, "Page Text",
        wxDefaultPosition, wxSize(700, 500),
        wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);

    wxTextCtrl* text = new wxTextCtrl(this, wxID_ANY, m_browser->GetPageText(),
        wxDefaultPosition, wxDefaultSize,
        wxTE_MULTILINE |
        wxTE_RICH |
        wxTE_READONLY);

    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(text, 1, wxEXPAND);
    SetSizer(sizer);
    textViewDialog.ShowModal();
}

/**
    * Invoked when user selects the "Menu" item
    */
void WebViewPanel::on_tools_clicked(wxCommandEvent& WXUNUSED(evt))
{
    if (!m_browser)
        return;

#ifdef DEBUG_URL_PANEL
    m_context_menu->Check(m_browser->IsContextMenuEnabled());
    m_dev_tools->Check(m_browser->IsAccessToDevToolsEnabled());

    wxPoint position = ScreenToClient(wxGetMousePosition());
    PopupMenu(m_tools_menu, position.x, position.y);
#endif
}

void WebViewPanel::run_script(const wxString& javascript)
{
    if (!m_browser || !m_shown)
        return;
    // Remember the script we run in any case, so the next time the user opens
    // the "Run Script" dialog box, it is shown there for convenient updating.
    m_javascript = javascript;
    BOOST_LOG_TRIVIAL(trace) << "RunScript " << javascript << "\n";
    m_browser->RunScriptAsync(javascript);
}


void WebViewPanel::on_run_script_custom(wxCommandEvent& WXUNUSED(evt))
{
    wxTextEntryDialog dialog
    (
        this,
        "Please enter JavaScript code to execute",
        wxGetTextFromUserPromptStr,
        m_javascript,
        wxOK | wxCANCEL | wxCENTRE | wxTE_MULTILINE
    );
    if (dialog.ShowModal() != wxID_OK)
        return;

    run_script(dialog.GetValue());
}

void WebViewPanel::on_add_user_script(wxCommandEvent& WXUNUSED(evt))
{
    if (!m_browser) {
        return;
    }
    wxString userScript = "window.wx_test_var = 'wxWidgets webview sample';";
    wxTextEntryDialog dialog
    (
        this,
        "Enter the JavaScript code to run as the initialization script that runs before any script in the HTML document.",
        wxGetTextFromUserPromptStr,
        userScript,
        wxOK | wxCANCEL | wxCENTRE | wxTE_MULTILINE
    );
    if (dialog.ShowModal() != wxID_OK)
        return;

    const wxString& javascript = dialog.GetValue();
    BOOST_LOG_TRIVIAL(debug) << "RunScript " << javascript << "\n";
    if (!m_browser->AddUserScript(javascript))
        wxLogError("Could not add user script");
}

void WebViewPanel::on_set_custom_user_agent(wxCommandEvent& WXUNUSED(evt))
{
    if (!m_browser)
        return;

    wxString customUserAgent = "Mozilla/5.0 (iPhone; CPU iPhone OS 13_1_3 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/13.0.1 Mobile/15E148 Safari/604.1";
    wxTextEntryDialog dialog
    (
        this,
        "Enter the custom user agent string you would like to use.",
        wxGetTextFromUserPromptStr,
        customUserAgent,
        wxOK | wxCANCEL | wxCENTRE
    );
    if (dialog.ShowModal() != wxID_OK)
        return;

    if (!m_browser->SetUserAgent(customUserAgent))
        wxLogError("Could not set custom user agent");
}

void WebViewPanel::on_clear_selection(wxCommandEvent& WXUNUSED(evt))
{
    if (!m_browser)
        return;

    m_browser->ClearSelection();
}

void WebViewPanel::on_delete_selection(wxCommandEvent& WXUNUSED(evt))
{
    if (!m_browser)
        return;

    m_browser->DeleteSelection();
}

void WebViewPanel::on_select_all(wxCommandEvent& WXUNUSED(evt))
{
    if (!m_browser)
        return;

    m_browser->SelectAll();
}

void WebViewPanel::On_enable_context_menu(wxCommandEvent& evt)
{
    if (!m_browser)
        return;

    m_browser->EnableContextMenu(evt.IsChecked());
}
void WebViewPanel::On_enable_dev_tools(wxCommandEvent& evt)
{
    if (!m_browser)
        return;

    m_browser->EnableAccessToDevTools(evt.IsChecked());
}

/**
    * Callback invoked when a loading error occurs
    */
void WebViewPanel::on_error(wxWebViewEvent& evt)
{
#define WX_ERROR_CASE(type) \
case type: \
    category = #type; \
    break;

    wxString category;
    switch (evt.GetInt())
    {
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_CONNECTION);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_CERTIFICATE);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_AUTH);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_SECURITY);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_NOT_FOUND);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_REQUEST);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_USER_CANCELLED);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_OTHER);
    }

    BOOST_LOG_TRIVIAL(error) << this <<" WebViewPanel error: " << category << " url: " << evt.GetURL();
    load_error_page();
#ifdef DEBUG_URL_PANEL
    m_info->ShowMessage(wxString("An error occurred loading ") + evt.GetURL() + "\n" +
        "'" + category + "'", wxICON_ERROR);
#endif
}

void WebViewPanel::do_reload()
{
    if (!m_browser) {
        return;
    }   
    // IsBusy on Linux very often returns true due to loading about:blank after loading requested url.
#ifndef __linux__
    if (m_browser->IsBusy()) {
        return;
    }
#endif
    const wxString current_url = m_browser->GetCurrentURL();
    if (current_url.StartsWith(m_default_url)) {
        m_browser->Reload();
        return;
    }
    load_default_url();
}

void WebViewPanel::load_default_url()
{
     if (!m_browser || m_do_late_webview_create) {
        return;
    }
    m_styles_defined = false;
    load_url(m_default_url);
}

void WebViewPanel::sys_color_changed()
{
#ifdef _WIN32
    wxGetApp().UpdateDarkUI(this);
#endif
}

void WebViewPanel::on_app_quit_event(const std::string& message_data)
{
    // MacOS only suplement for cmd+Q
    if (wxGetApp().mainframe) {
         wxGetApp().mainframe->Close();
    }
}
void WebViewPanel::on_app_minimize_event(const std::string& message_data)
{
    // MacOS only suplement for cmd+M
     wxGetApp().mainframe->Iconize(true);
}
void WebViewPanel::define_css()
{
    assert(false);
}

ConnectWebViewPanel::ConnectWebViewPanel(wxWindow* parent)
    : WebViewPanel(parent, GUI::from_u8(Utils::ServiceConfig::instance().connect_url()), { "_prusaSlicer" }, "connect_loading", "connect_error", false)
{  
    auto* plater = wxGetApp().plater();
    plater->Bind(EVT_UA_LOGGEDOUT, &ConnectWebViewPanel::on_user_logged_out, this);
    plater->Bind(EVT_UA_ID_USER_SUCCESS, &ConnectWebViewPanel::on_user_token, this);
    plater->Bind(EVT_UA_ID_USER_SUCCESS_AFTER_TOKEN_SUCCESS, &ConnectWebViewPanel::on_user_token, this);

    m_actions["appQuit"] = std::bind(&WebViewPanel::on_app_quit_event, this, std::placeholders::_1);
    m_actions["appMinimize"] = std::bind(&WebViewPanel::on_app_minimize_event, this, std::placeholders::_1);
    m_actions["reloadHomePage"] = std::bind(&ConnectWebViewPanel::on_reload_event, this, std::placeholders::_1);

}

void ConnectWebViewPanel::late_create()
{
    WebViewPanel::late_create();
    if (!m_browser) {
        return;
    }
    
    // This code used to be inside plater->Bind(EVT_UA_ID_USER_SUCCESS, &ConnectWebViewPanel::on_user_token, this)
    auto access_token = wxGetApp().plater()->get_user_account()->get_access_token();
    assert(!access_token.empty());

    wxString javascript = get_login_script(true);
    BOOST_LOG_TRIVIAL(debug) << "RunScript " << javascript << "\n";
    m_browser->RunScriptAsync(javascript);
    resend_config();
}

void ConnectWebViewPanel::on_user_token(UserAccountSuccessEvent& e)
{
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__;
    e.Skip();
    if (!m_browser) {
        return;
    }
    auto access_token = wxGetApp().plater()->get_user_account()->get_access_token();
    assert(!access_token.empty());

    wxString javascript = get_login_script(true);
    BOOST_LOG_TRIVIAL(debug) << "RunScript " << javascript << "\n";
    m_browser->RunScriptAsync(javascript);
    resend_config();
}

ConnectWebViewPanel::~ConnectWebViewPanel()
{
}

wxString ConnectWebViewPanel::get_login_script(bool refresh)
{
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__;
    Plater* plater = wxGetApp().plater();
    const std::string& access_token = plater->get_user_account()->get_access_token();
    assert(!access_token.empty());
    auto javascript = wxString::Format(

#if AUTH_VIA_FETCH_OVERRIDE
        refresh
            ?
            "window.__access_token = '%s';window.__access_token_version = (window.__access_token_version || 0) + 1;console.log('Updated Auth token', window.__access_token);"
            :
        /*
         * Notes:
         * - The fetch() function has two distinct prototypes (i.e. input args):
         *    1. fetch(url: string, options: object | undefined)
         *    2. fetch(req: Request, options: object | undefined)
         * - For some reason I can't explain the headers can be extended only via Request object
         *   i.e. the fetch prototype (2). So we need to convert (1) call into (2) before
         *
         */
        R"(
            if (window.__fetch === undefined) {
                window.__fetch = fetch;
                window.fetch = function(req, opts = {}) {
                    if (typeof req === 'string') {
                        req = new Request(req, opts);
                        opts = {};
                    }
                    if (window.__access_token && (req.url[0] == '/' || req.url.indexOf('prusa3d.com') > 0)) {
                        req.headers.set('Authorization', 'Bearer ' + window.__access_token);
                        console.log('Header updated: ', req.headers.get('Authorization'));
                        console.log('AT Version: ', __access_token_version);
                    }
                    //console.log('Injected fetch used', req, opts);
                    return __fetch(req, opts);
                };
            }
            window.__access_token = '%s';
            window.__access_token_version = 0;
        )",
#else
        refresh
        ?
        R"(
        if (location.protocol === 'https:') {
            if (window._prusaSlicer_initLogin !== undefined) {
                console.log('Init login');
                if (window._prusaSlicer !== undefined)
                    _prusaSlicer.postMessage({action: 'LOG', message: 'Refreshing login'});
                _prusaSlicer_initLogin('%s');
            } else {
                console.log('Refreshing login skipped as no _prusaSlicer_login defined (yet?)');
                if (window._prusaSlicer === undefined) {
                    console.log('Message handler _prusaSlicer not defined yet');
                } else {
                    _prusaSlicer.postMessage({action: 'LOG', message: 'Refreshing login skipped as no _prusaSlicer_initLogin defined (yet?)'});
                }
            }
        }
        )"
        :
        R"(
        function _prusaSlicer_log(msg) {
            console.log(msg);
            if (window._prusaSlicer !== undefined)
                _prusaSlicer.postMessage({action: 'LOG', message: msg});
        }
        function _prusaSlicer_errorHandler(err) {
            const msg = {
                action: 'ERROR',
                error: typeof(err) === 'string' ? err : JSON.stringify(err),
                critical: false
            };
            console.error('Login error occurred', msg);
            window._prusaSlicer.postMessage(msg);
        };

        function _prusaSlicer_delay(ms) {
            return new Promise((resolve, reject) => {
                setTimeout(resolve, ms);
            });
        }

        async function _prusaSlicer_initLogin(token) {
            const parts = token.split('.');
            const claims = JSON.parse(atob(parts[1]));
            const now = new Date().getTime() / 1000;
            if (claims.exp <= now) {
                _prusaSlicer_log('Skipping initLogin as token is expired');
                return;
            }

            let retry = false;
            let backoff = 1000;
            const maxBackoff = 64000 * 4;
            const maxRetries = 16;
            let numRetries = 0;
            do {

                let error = false;

                try {
                    _prusaSlicer_log('Slicer Login request ' + token.substring(token.length - 8));
                    let resp = await fetch('/slicer/login', {method: 'POST', headers: {Authorization: 'Bearer ' + token}});
                    let body = await resp.text();
                    _prusaSlicer_log('Slicer Login resp ' + resp.status + ' (' + token.substring(token.length - 8) + ') body: ' + body);
                    if (resp.status >= 500 || resp.status == 408) {
                        numRetries++;
                        retry = maxRetries <= 0 || numRetries <= maxRetries;
                    } else {
                        retry = false;
                        if (resp.status >= 400)
                            _prusaSlicer_errorHandler({status: resp.status, body});
                    }
                } catch (e) {
                    _prusaSlicer_log('Slicer Login failed: ' + e.toString());
                    console.error('Slicer Login failed', e.toString());
                    // intentionally not taking care about max retry count, as this is not server error but likely being offline
                    retry = true;
                }

                if (retry) {
                    await _prusaSlicer_delay(backoff + 1000 * Math.random());
                    if (backoff < maxBackoff) {
                        backoff *= 2;
                    }
                }
            } while (retry);
        }

        if (location.protocol === 'https:' && window._prusaSlicer) {
            _prusaSlicer_log('Requesting login');
            _prusaSlicer.postMessage({action: 'REQUEST_LOGIN'});
        }
        )",
#endif
        access_token
    );
    return javascript;
}

wxString ConnectWebViewPanel::get_logout_script()
{
    return "sessionStorage.removeItem('_slicer_token');";
}

void ConnectWebViewPanel::on_page_will_load()
{
    if (!m_browser) {
        return;
    }
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__;
    auto javascript = get_login_script(false);
    BOOST_LOG_TRIVIAL(debug) << "RunScript " << javascript << "\n";
    m_browser->AddUserScript(javascript);
}

void ConnectWebViewPanel::on_user_logged_out(UserAccountSuccessEvent& e)
{
    e.Skip();
    if (!m_browser)
        return;
    // clear token from session storage
    m_browser->RunScriptAsync(get_logout_script());
}

void ConnectWebViewPanel::on_script_message(wxWebViewEvent& evt)
{
    BOOST_LOG_TRIVIAL(debug) << "received message from Prusa Connect FE: " << evt.GetString();
    handle_message(into_u8(evt.GetString()));
}
void ConnectWebViewPanel::on_navigation_request(wxWebViewEvent &evt) 
{
#ifdef DEBUG_URL_PANEL
    m_url->SetValue(evt.GetURL());
#endif
    BOOST_LOG_TRIVIAL(debug) << "Navigation requested to: " << into_u8(evt.GetURL());

    // we need to do this to redefine css when reload is hit
    if (evt.GetURL().StartsWith(m_default_url) && evt.GetURL() == m_browser->GetCurrentURL()) {
        m_styles_defined = false;
    }

    if (evt.GetURL() == m_default_url) {
        m_reached_default_url = true;
        return;
    }
    if (evt.GetURL() == (GUI::format_wxstr("file:///%1%/web/connection_failed.html", boost::filesystem::path(resources_dir()).generic_string()))) {
        return;
    }
    if (m_reached_default_url && !evt.GetURL().StartsWith(m_default_url)) {
        BOOST_LOG_TRIVIAL(info) << evt.GetURL() <<  " does not start with default url. Vetoing.";
        evt.Veto();
    } else if (m_reached_default_url && evt.GetURL().Find(GUI::format_wxstr("/web/%1%.html", m_loading_html)) != wxNOT_FOUND) {
        // Do not allow back button to loading screen
        evt.Veto();
    }
}

void ConnectWebViewPanel::on_connect_action_error(const std::string &message_data)
{
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__;
    ConnectRequestHandler::on_connect_action_error(message_data);
    // TODO: make this more user friendly (and make sure only once opened if multiple errors happen)
//    MessageDialog dialog(
//        this,
//        GUI::format_wxstr(_L("WebKit Runtime Error encountered:\n\n%s"), message_data),
//        "WebKit Runtime Error",
//        wxOK
//    );
//    dialog.ShowModal();

}

void ConnectWebViewPanel::on_reload_event(const std::string& message_data)
{
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__;
    // Event from our error page button or keyboard shortcut 
    m_styles_defined = false;
    try {
        std::stringstream ss(message_data);
        pt::ptree ptree;
        pt::read_json(ss, ptree);
        if (const auto keyboard = ptree.get_optional<bool>("fromKeyboard"); keyboard && *keyboard) {
            do_reload();
        } else {
            // On error page do load of default url.
            load_default_url();
        }
    } catch (const std::exception &e) {
        BOOST_LOG_TRIVIAL(error) << "Could not parse printables message. " << e.what();
        return;
    }
}

void ConnectWebViewPanel::after_on_show(wxShowEvent& evt)
{
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__;
    run_script("window.location.reload();");
}

void ConnectWebViewPanel::logout()
{
    if (!m_browser || m_do_late_webview_create) {
        return;
    }
    wxString script = L"window._prusaConnect_v2.logout()";
    run_script(script);

    Plater* plater = wxGetApp().plater();
    auto javascript = wxString::Format(
                     R"(
            console.log('Preparing logout');
            window.fetch('/slicer/logout', {method: 'POST', headers: {Authorization: 'Bearer %s'}})
                .then(function (resp){
                    console.log('Logout resp', resp);
                    resp.text().then(function (json) { console.log('Logout resp body', json) });
                });
        )",
                     plater->get_user_account()->get_access_token()
    );
    BOOST_LOG_TRIVIAL(debug) << "RunScript " << javascript << "\n";
    m_browser->RunScript(javascript);
}

void ConnectWebViewPanel::sys_color_changed()
{
    resend_config();
}

void ConnectWebViewPanel::on_connect_action_request_login(const std::string &message_data)
{
    run_script_bridge(get_login_script(true));
}


void ConnectWebViewPanel::on_connect_action_select_printer(const std::string& message_data)
{
    assert(!message_data.empty());
    wxGetApp().handle_connect_request_printer_select(message_data);
}
void ConnectWebViewPanel::on_connect_action_print(const std::string& message_data)
{
    // PRINT request is not defined for ConnectWebViewPanel
    assert(false);
}

void ConnectWebViewPanel::define_css()
{
    
    if (m_styles_defined) {
        return;
    }
    m_styles_defined = true;
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__;
#if defined(__APPLE__) 
    // WebView on Windows does read keyboard shortcuts
    // Thus doing f.e. Reload twice would make the oparation to fail
    std::string script = R"(
        document.addEventListener('keydown', function (event) {
            if (event.key === 'F5' || (event.ctrlKey && event.key === 'r') || (event.metaKey && event.key === 'r')) {
                 window.webkit.messageHandlers._prusaSlicer.postMessage(JSON.stringify({ action: 'reloadHomePage', fromKeyboard: 1}));
            }
            if (event.metaKey && event.key === 'q') {
                 window.webkit.messageHandlers._prusaSlicer.postMessage(JSON.stringify({ action: 'appQuit'}));
            }
            if (event.metaKey && event.key === 'm') {
                 window.webkit.messageHandlers._prusaSlicer.postMessage(JSON.stringify({ action: 'appMinimize'}));
            }
        });
    )";
    run_script(script);

#endif // defined(__APPLE__)
}

PrinterWebViewPanel::PrinterWebViewPanel(wxWindow* parent, const wxString& default_url)
    : WebViewPanel(parent, default_url, {"ExternalApp"}, "other_loading", "other_error", false)
{
    m_events["reloadHomePage"] = std::bind(&PrinterWebViewPanel::on_reload_event, this, std::placeholders::_1);
    m_events["appQuit"] = std::bind(&WebViewPanel::on_app_quit_event, this, std::placeholders::_1);
    m_events["appMinimize"] = std::bind(&WebViewPanel::on_app_minimize_event, this, std::placeholders::_1);
}

void PrinterWebViewPanel::on_navigation_request(wxWebViewEvent &evt)
{
    const wxString url = evt.GetURL();
    if (url.StartsWith(m_default_url)) {
        m_reached_default_url = true;
        if (url == m_browser->GetCurrentURL()) {
            // we need to do this to redefine css when reload is hit
            m_styles_defined = false;
        }

        if ( !m_api_key_sent) {
            if (!m_usr.empty() && !m_psk.empty()) {
                send_credentials();
            }
        }
    } else if (m_reached_default_url && evt.GetURL().Find(GUI::format_wxstr("/web/%1%.html", m_loading_html)) != wxNOT_FOUND) {
        // Do not allow back button to loading screen
        evt.Veto();
    }
}

void PrinterWebViewPanel::on_loaded(wxWebViewEvent& evt)
{
    if (evt.GetURL().IsEmpty())
        return;

    if (evt.GetURL().StartsWith(m_default_url)) {
        define_css();
    } else {
        m_styles_defined = false;
    }

    m_load_default_url_on_next_error = false;
    if (evt.GetURL().Find(GUI::format_wxstr("/web/%1%.html", m_loading_html)) != wxNOT_FOUND && m_load_default_url) {
        m_load_default_url = false;
        load_default_url();
        return;
    }
    if (!m_api_key.empty()) {
        send_api_key();
    }
}
void PrinterWebViewPanel::on_script_message(wxWebViewEvent& evt)
{
    BOOST_LOG_TRIVIAL(debug) << "received message from Physical printer page: " << evt.GetString();
    handle_message(into_u8(evt.GetString()));
}

void PrinterWebViewPanel::handle_message(const std::string& message)
{

    std::string event_string;
    try {
        std::stringstream ss(message);
        pt::ptree ptree;
        pt::read_json(ss, ptree);
        if (const auto action = ptree.get_optional<std::string>("event"); action) {
            event_string = *action;
        }
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Could not parse printables message. " << e.what();
        return;
    }

    if (event_string.empty()) {
        BOOST_LOG_TRIVIAL(error) << "Received invalid message from printables (missing event). Message: " << message;
        return;
    }
    assert(m_events.find(event_string) != m_events.end()); // this assert means there is an event that has no handling.
    if (m_events.find(event_string) != m_events.end()) {
        m_events[event_string](message);
    }
}

void PrinterWebViewPanel::send_api_key()
{
    if (!m_browser || m_api_key_sent)
        return;
    m_api_key_sent = true;
    wxString key = from_u8(m_api_key);
    wxString script = wxString::Format(R"(
    // Check if window.fetch exists before overriding
    if (window.originalFetch === undefined) {
        console.log('Patching fetch with API key');
        window.originalFetch = window.fetch;
        window.fetch = function(input, init = {}) {
            init.headers = init.headers || {};
            init.headers['X-Api-Key'] = sessionStorage.getItem('apiKey');
            console.log('Patched fetch', input, init);
            return window.originalFetch(input, init);
        };
    }
    sessionStorage.setItem('authType', 'ApiKey');
    sessionStorage.setItem('apiKey', '%s');
)",
    key);
    m_browser->RemoveAllUserScripts();
    BOOST_LOG_TRIVIAL(debug) << "RunScript " << script << "\n";
    m_browser->AddUserScript(script);
    m_browser->Reload();
    remove_webview_credentials(m_browser);
}

void PrinterWebViewPanel::send_credentials()
{
    if (!m_browser || m_api_key_sent)
        return;
    m_browser->RemoveAllUserScripts();
    m_browser->AddUserScript("sessionStorage.removeItem('authType'); sessionStorage.removeItem('apiKey'); console.log('Session Storage cleared');");
    // reload would be done only if called from on_loaded
    //m_browser->Reload();
    m_api_key_sent = true;
    setup_webview_with_credentials(m_browser, m_usr, m_psk);
}

void PrinterWebViewPanel::define_css()
{
    
    if (m_styles_defined) {
        return;
    }
    m_styles_defined = true;
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__;
#if defined(__APPLE__) 
    // WebView on Windows does read keyboard shortcuts
    // Thus doing f.e. Reload twice would make the oparation to fail
    std::string script = R"(
        document.addEventListener('keydown', function (event) {
            if (event.key === 'F5' || (event.ctrlKey && event.key === 'r') || (event.metaKey && event.key === 'r')) {
                 window.webkit.messageHandlers.ExternalApp.postMessage(JSON.stringify({ event: 'reloadHomePage', fromKeyboard: 1}));
            }
            if (event.metaKey && event.key === 'q') {
                 window.webkit.messageHandlers.ExternalApp.postMessage(JSON.stringify({ event: 'appQuit'}));
            }
            if (event.metaKey && event.key === 'm') {
                 window.webkit.messageHandlers.ExternalApp.postMessage(JSON.stringify({ event: 'appMinimize'}));
            }
        });
    )";
    run_script(script);

#endif // defined(__APPLE__)
}

void PrinterWebViewPanel::on_reload_event(const std::string& message_data)
{
    // Event from our error page button or keyboard shortcut 
    m_styles_defined = false;
    try {
        std::stringstream ss(message_data);
        pt::ptree ptree;
        pt::read_json(ss, ptree);
        if (const auto keyboard = ptree.get_optional<bool>("fromKeyboard"); keyboard && *keyboard) {
            do_reload();
        } else {
            // On error page do load of default url.
            load_default_url();
        }
    } catch (const std::exception &e) {
        BOOST_LOG_TRIVIAL(error) << "Could not parse message. " << e.what();
        return;
    }
}

PrintablesWebViewPanel::PrintablesWebViewPanel(wxWindow* parent)
    : WebViewPanel(parent, GUI::from_u8(Utils::ServiceConfig::instance().printables_url()), { "ExternalApp" }, "other_loading", "other_error", false)
{  
    m_events["accessTokenExpired"] = std::bind(&PrintablesWebViewPanel::on_printables_event_access_token_expired, this, std::placeholders::_1);
    m_events["printGcode"] = std::bind(&PrintablesWebViewPanel::on_printables_event_print_gcode, this, std::placeholders::_1);
    m_events["downloadFile"] = std::bind(&PrintablesWebViewPanel::on_printables_event_download_file, this, std::placeholders::_1);
    m_events["sliceFile"] = std::bind(&PrintablesWebViewPanel::on_printables_event_slice_file, this, std::placeholders::_1);
    m_events["requiredLogin"] = std::bind(&PrintablesWebViewPanel::on_printables_event_required_login, this, std::placeholders::_1);
    m_events["openExternalUrl"] = std::bind(&PrintablesWebViewPanel::on_printables_event_open_url, this, std::placeholders::_1);
    m_events["reloadHomePage"] = std::bind(&PrintablesWebViewPanel::on_reload_event, this, std::placeholders::_1);
    m_events["appQuit"] = std::bind(&WebViewPanel::on_app_quit_event, this, std::placeholders::_1);
    m_events["appMinimize"] = std::bind(&WebViewPanel::on_app_minimize_event, this, std::placeholders::_1);
    m_events["ready"] = std::bind(&PrintablesWebViewPanel::on_printables_event_dummy, this, std::placeholders::_1);
}

void PrintablesWebViewPanel::handle_message(const std::string& message)
{

    std::string event_string;
    try {
        std::stringstream ss(message);
        pt::ptree ptree;
        pt::read_json(ss, ptree);
        if (const auto action = ptree.get_optional<std::string>("event"); action) {
            event_string = *action;
        }
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Could not parse printables message. " << e.what();
        return;
    }

    if (event_string.empty()) {
        BOOST_LOG_TRIVIAL(error) << "Received invalid message from printables (missing event). Message: " << message;
        return;
    }
    assert(m_events.find(event_string) != m_events.end()); // this assert means there is an event that has no handling.
    if (m_events.find(event_string) != m_events.end()) {
        m_events[event_string](message);
    }
}

void PrintablesWebViewPanel::on_navigation_request(wxWebViewEvent &evt)
{
    const wxString url = evt.GetURL();   
    if (url.StartsWith(m_default_url)) {
        m_reached_default_url = true;
        if (url == m_browser->GetCurrentURL()) {
            // we need to do this to redefine css when reload is hit
            m_styles_defined = false;
        }
    } else if (m_reached_default_url && url.StartsWith("http")) {
        BOOST_LOG_TRIVIAL(info) << evt.GetURL() <<  " does not start with default url. Vetoing.";
        evt.Veto();
    } else if (m_reached_default_url && evt.GetURL().Find(GUI::format_wxstr("/web/%1%.html", m_loading_html)) != wxNOT_FOUND) {
        // Do not allow back button to loading screen
        evt.Veto();
    }
}

wxString PrintablesWebViewPanel::get_default_url() const
{
    return GUI::from_u8(get_url_lang_theme(GUI::from_u8(Utils::ServiceConfig::instance().printables_url() + "/homepage")));
}

void PrintablesWebViewPanel::on_loaded(wxWebViewEvent& evt)
{
    if (evt.GetURL().Find(GUI::format_wxstr("/web/%1%.html", m_loading_html)) != wxNOT_FOUND && m_load_default_url) {
        m_load_default_url = false;
        load_default_url();
        return;
    }
    if (evt.GetURL().StartsWith(m_default_url)) {
        define_css();
    } else {
        m_styles_defined = false;
    }
#ifdef _WIN32
    // This is needed only once after add_request_authorization
    if (m_remove_request_auth) {
        m_remove_request_auth = false;
        remove_request_authorization(m_browser);
    }
#endif
    m_load_default_url_on_next_error = false;
}

std::string PrintablesWebViewPanel::get_url_lang_theme(const wxString& url) const
{
    // situations and reaction:
    // 1) url is just a path (no query no fragment) -> query with lang and theme is added 
    // 2) url has query that contains lang and theme -> query and lang values are modified 
    // 3) url has query with just one of lang or theme -> query is modified and missing value is added 
    // 4) url has query of query and fragment without lang and theme -> query with lang and theme is added to the end of query 

    std::string url_string = into_u8(url);
    std::string theme = wxGetApp().dark_mode() ? "dark" : "light";
    wxString language = GUI::wxGetApp().current_language_code();
    if (language.size() > 2)
        language = language.SubString(0, 1);

    // Replace lang and theme if already in url
    bool lang_found = false;
    std::regex lang_regex(R"((lang=)[^&#]*)");
    if (std::regex_search(url_string, lang_regex)) {
        url_string = std::regex_replace(url_string, lang_regex, "$1" + into_u8(language));
        lang_found = true;
    }
    bool theme_found = false;
    std::regex theme_regex(R"((theme=)[^&#]*)");
    if (std::regex_search(url_string, theme_regex)) {
        url_string = std::regex_replace(url_string, theme_regex, "$1" + theme);
        theme_found = true;
    }
    if (lang_found && theme_found) 
        return url_string;

    // missing params string
    std::string new_params = lang_found ?  GUI::format("theme=%1%", theme)
        : theme_found ?  GUI::format("lang=%1%", language)
        : GUI::format("lang=%1%&theme=%2%", language, theme);

    // Regex to capture query and optional fragment
    std::regex query_regex(R"((\?.*?)(#.*)?$)");
    
    if (std::regex_search(url_string, query_regex)) {
        // Append params before the fragment (if it exists)
        return std::regex_replace(url_string, query_regex, "$1&" + new_params + "$2");
    } 
    std::regex fragment_regex(R"(#.*$)");
    if (std::regex_search(url_string, fragment_regex)) {
        // Add params before the fragment
        return std::regex_replace(url_string, fragment_regex, "?" + new_params + "$&");
    } 

    return url_string + "?" + new_params;
}

void PrintablesWebViewPanel::after_on_show(wxShowEvent& evt)
{
    // in case login changed, resend login / logout
    // DK1: it seems to me, it is safer to do login / logout (where logout means requesting the page again)
    // on every show of panel,
    // than to keep information if we have printables page in same state as slicer in terms of login
    // But im afraid it will be concidered not pretty...
    const std::string access_token = wxGetApp().plater()->get_user_account()->get_access_token();
    if (access_token.empty()) {
        logout(m_next_show_url);
    } else {
        login(access_token, m_next_show_url);
    }
    m_next_show_url.clear();
}

void PrintablesWebViewPanel::logout(const std::string& override_url/* = std::string()*/)
{
    if (!m_shown || !m_browser) {
        return;
    }
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__;
    hide_loading_overlay(); 
    m_styles_defined = false;
    delete_cookies(m_browser, Utils::ServiceConfig::instance().printables_url());
    m_browser->RunScript("localStorage.clear();");

    std::string next_url = override_url.empty() 
        ? get_url_lang_theme(m_browser->GetCurrentURL()) 
        : get_url_lang_theme(from_u8(override_url));
#ifdef _WIN32
    load_url(GUI::from_u8(next_url));
#else
    // We cannot do simple reload here, it would keep the access token in the header
    load_request(m_browser, next_url, std::string());
#endif // 
       
}
void PrintablesWebViewPanel::login(const std::string& access_token, const std::string& override_url/* = std::string()*/)
{
    if (!m_shown) {
        return;
    }
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__;
    hide_loading_overlay();
    m_styles_defined = false;
    // We cannot add token to header as when making the first request.
    // In fact, we shall not do request here, only run scripts.
    // postMessage accessTokenWillChange -> postMessage accessTokenChange -> window.location.reload();
    wxString script = "window.postMessage(JSON.stringify({ event: 'accessTokenWillChange' }))";
    run_script(script);

    script = GUI::format_wxstr("window.postMessage(JSON.stringify({"
        "event: 'accessTokenChange',"
        "token: '%1%'"
        "}));"
        , access_token);
    run_script(script);
    
    if ( override_url.empty()) {
        run_script("window.location.reload();");
    } else {
        load_url(GUI::from_u8(get_url_lang_theme(from_u8(override_url))));
    } 
}

void PrintablesWebViewPanel::load_default_url()
{
    if (!m_browser) {
        return;
    }
    hide_loading_overlay();
    m_styles_defined = false;
    std::string actual_default_url = get_url_lang_theme(from_u8(Utils::ServiceConfig::instance().printables_url() + "/homepage"));
    const std::string access_token = wxGetApp().plater()->get_user_account()->get_access_token();
    // in case of opening printables logged out - delete cookies and localstorage to get rid of last login
    if (access_token.empty())  {
        BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << " logout";
        delete_cookies(m_browser, Utils::ServiceConfig::instance().printables_url());
        m_browser->AddUserScript("localStorage.clear();");
        load_url(actual_default_url);
        return;
    }
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << " login";
    
    // add token to first request
#ifdef _WIN32
    add_request_authorization(m_browser, m_default_url, access_token);
    m_remove_request_auth = true;
    load_url(GUI::from_u8(actual_default_url));
#else
    load_request(m_browser, actual_default_url, access_token);
#endif  
}

void PrintablesWebViewPanel::send_refreshed_token(const std::string& access_token)
{
    if (m_load_default_url) {
        return;
    }
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__;
    hide_loading_overlay();
    wxString script = GUI::format_wxstr("window.postMessage(JSON.stringify({"
        "event: 'accessTokenChange',"
        "token: '%1%'"
        "}));"
        , access_token);
    run_script(script);
}
void PrintablesWebViewPanel::send_will_refresh()
{
    if (m_load_default_url) {
        return;
    }
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__;
    wxString script = "window.postMessage(JSON.stringify({ event: 'accessTokenWillChange' }))";
    run_script(script);
}

void PrintablesWebViewPanel::on_script_message(wxWebViewEvent& evt)
{
    BOOST_LOG_TRIVIAL(debug) << "received message from Printables: " << evt.GetString();
    handle_message(into_u8(evt.GetString()));
}

void PrintablesWebViewPanel::sys_color_changed()
{
    if (m_shown && m_browser) {
        load_url(GUI::from_u8(get_url_lang_theme(m_browser->GetCurrentURL())));
    }
    WebViewPanel::sys_color_changed();
}

void PrintablesWebViewPanel::on_printables_event_access_token_expired(const std::string& message_data)
{
    //  { "event": "accessTokenExpired")
    // There seems to be a situation where we get accessTokenExpired when there is active token from Slicer POW
    // We need get new token and freeze webview until its not refreshed
    if (m_refreshing_token) {
        BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << " already refreshing";
        return;
    }
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__;
    m_refreshing_token = true;
    show_loading_overlay();
   
    wxGetApp().plater()->get_user_account()->request_refresh();
}

void PrintablesWebViewPanel::on_reload_event(const std::string& message_data)
{
    // Event from our error page button or keyboard shortcut 
    m_styles_defined = false;
    try {
        std::stringstream ss(message_data);
        pt::ptree ptree;
        pt::read_json(ss, ptree);
        if (const auto keyboard = ptree.get_optional<bool>("fromKeyboard"); keyboard && *keyboard) {
            do_reload();
        } else {
            // On error page do load of default url.
            load_default_url();
        }
    } catch (const std::exception &e) {
        BOOST_LOG_TRIVIAL(error) << "Could not parse printables message. " << e.what();
        return;
    }
}

namespace {
std::string escape_url(const std::string& unescaped)
{
	std::string ret_val;
	CURL* curl = curl_easy_init();
	if (curl) {
		char* decoded = curl_easy_escape(curl, unescaped.c_str(), unescaped.size());
		if (decoded) {
			ret_val = std::string(decoded);
			curl_free(decoded);
		}
		curl_easy_cleanup(curl);
	}
	return ret_val;
}
}

void PrintablesWebViewPanel::on_printables_event_print_gcode(const std::string& message_data)
{
    // { "event": "downloadFile", "url": "https://media.printables.com/somesecure.stl", "modelUrl": "https://www.printables.com/model/123" }
    std::string download_url;
    std::string model_url;
    try {
        std::stringstream ss(message_data);
        pt::ptree ptree;
        pt::read_json(ss, ptree);
        if (const auto url = ptree.get_optional<std::string>("url"); url) {
            download_url = *url;
        }
        if (const auto url = ptree.get_optional<std::string>("modelUrl"); url) {
            model_url = *url;
        }
    } catch (const std::exception &e) {
        BOOST_LOG_TRIVIAL(error) << "Could not parse printables message. " << e.what();
        return;
    }  
    assert(!download_url.empty() && !model_url.empty());
    wxCommandEvent* evt = new wxCommandEvent(EVT_PRINTABLES_CONNECT_PRINT);
    evt->SetString(from_u8(Utils::ServiceConfig::instance().connect_printables_print_url()  +"?url="  + escape_url(download_url)));
    wxQueueEvent(GUI::wxGetApp().mainframe->m_plater, evt);
}
void PrintablesWebViewPanel::on_printables_event_download_file(const std::string& message_data)
{
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << " " << message_data;
    // { "event": "printGcode", "url": "https://media.printables.com/somesecure.gcode", "modelUrl": "https://www.printables.com/model/123" }
    std::string download_url;
    std::string model_url;
    try {
        std::stringstream ss(message_data);
        pt::ptree ptree;
        pt::read_json(ss, ptree);
        if (const auto url = ptree.get_optional<std::string>("url"); url) {
            download_url = *url;
        }
        if (const auto url = ptree.get_optional<std::string>("modelUrl"); url) {
            model_url = *url;
        }
    } catch (const std::exception &e) {
        BOOST_LOG_TRIVIAL(error) << "Could not parse printables message. " << e.what();
        return;
    }  
    assert(!download_url.empty() && !model_url.empty());
    boost::filesystem::path url_path(download_url);
    show_download_notification(url_path.filename().string());

    wxGetApp().printables_download_request(download_url, model_url); 
}
void PrintablesWebViewPanel::on_printables_event_slice_file(const std::string& message_data)
{
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << " " << message_data;
    // { "event": "sliceFile", "url": "https://media.printables.com/somesecure.zip", "modelUrl": "https://www.printables.com/model/123" }
    std::string download_url;
    std::string model_url;
    try {
        std::stringstream ss(message_data);
        pt::ptree ptree;
        pt::read_json(ss, ptree);
        if (const auto url = ptree.get_optional<std::string>("url"); url) {
            download_url = *url;
        }
        if (const auto url = ptree.get_optional<std::string>("modelUrl"); url) {
            model_url = *url;
        }
    } catch (const std::exception &e) {
        BOOST_LOG_TRIVIAL(error) << "Could not parse printables message. " << e.what();
        return;
    }  
    assert(!download_url.empty() && !model_url.empty());
    
    wxGetApp().printables_slice_request(download_url, model_url);
}

void PrintablesWebViewPanel::on_printables_event_required_login(const std::string& message_data)
{
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << " " << message_data;
    wxGetApp().printables_login_request();
}
void PrintablesWebViewPanel::on_printables_event_open_url(const std::string& message_data)
{
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << " " << message_data;

     try {
        std::stringstream ss(message_data);
        pt::ptree ptree;
        pt::read_json(ss, ptree);
        if (const auto url = ptree.get_optional<std::string>("url"); url) {
            wxGetApp().open_browser_with_warning_dialog(GUI::from_u8(*url));
        }
    } catch (const std::exception &e) {
        BOOST_LOG_TRIVIAL(error) << "Could not parse Printables message. " << e.what();
        return;
    }    
}

void PrintablesWebViewPanel::define_css()
{
    
    if (m_styles_defined) {
        return;
    }
    m_styles_defined = true;
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__;
    std::string script = R"(
        // Loading overlay and Notification style
        var style = document.createElement('style');
        style.innerHTML = `
        body {}
        .slic3r-loading-overlay {
            position: fixed;
            top: 0;
            left: 0;
            right: 0;
            bottom: 0;
            background-color: rgba(127 127 127 / 50%);
            z-index: 50;
            display: flex;
            align-items: center;
            justify-content: center;
        }
        .slic3r-loading-anim {
            width: 60px;
            aspect-ratio: 4;
            --_g: no-repeat radial-gradient(circle closest-side,#000 90%,#0000);
            background:
                    var(--_g) 0%   50%,
                    var(--_g) 50%  50%,
                    var(--_g) 100% 50%;
            background-size: calc(100%/3) 100%;
            animation: slic3r-loading-anim 1s infinite linear;
        }
        @keyframes slic3r-loading-anim {
            33%{background-size:calc(100%/3) 0%  ,calc(100%/3) 100%,calc(100%/3) 100%}
            50%{background-size:calc(100%/3) 100%,calc(100%/3) 0%  ,calc(100%/3) 100%}
            66%{background-size:calc(100%/3) 100%,calc(100%/3) 100%,calc(100%/3) 0%  }
        }
        .notification-popup {
            position: fixed;
            right: 10px;
            bottom: 10px;
            background-color: #333333; /* Dark background */
            padding: 10px;
            border-radius: 6px; /* Slightly rounded corners */
            color: #ffffff; /* White text */
            font-family: Arial, sans-serif;
            font-size: 12px;
            display: flex;
            justify-content: space-between;
            align-items: center;
            box-shadow: 0px 4px 8px rgba(0, 0, 0, 0.3); /* Add a subtle shadow */
            min-width: 350px; 
            max-width: 350px;
            min-height: 50px;
        }
        .notification-popup div {
            white-space: nowrap;
            overflow: hidden;
            text-overflow: ellipsis;
            padding-right: 20px; /* Add padding to make text truncate earlier */
        }
        .notification-popup b {
            color: #ffa500;
        }
        .notification-popup a:hover {
            text-decoration: underline; /* Underline on hover */
        }
        .notification-popup .close-button {
            display: inline-block;
            width: 20px;
            height: 20px;
            border: 2px solid #ffa500; /* Orange border for the button */
            border-radius: 4px;
            text-align: center;
            font-size: 16px;
            line-height: 16px;
            cursor: pointer;
            padding-top: 1px; 
        }
        .notification-popup .close-button:hover {
            background-color: #ffa500; /* Orange background on hover */
            color: #333333; /* Dark color for the "X" on hover */
        }
        .notification-popup .close-button:before {
            content: 'X';
            color: #ffa500; /* Orange "X" */
            font-weight: bold;
        }
        `;
        document.head.appendChild(style); 
    
        // Capture click on hypertext
        // Rewritten from mobileApp code
        (function() {
            const listenerKey = 'custom-click-listener';
            if (!document[listenerKey]) {
                document.addEventListener( 'click', function(event) {
                    const target = event.target.closest('a[href]');
                    if (!target) return; // Ignore clicks that are not on links
                    const url = target.href;
                    // Allow empty iframe navigation
                    if (url === 'about:blank') {
                        return; // Let it proceed
                    }
                    // Debug log for navigation
                    console.log(`Printables:onNavigationRequest: ${url}`);
                    // Handle all non-printables.com domains in an external browser
                    if (!/printables\.com/.test(url)) {
                        window.ExternalApp.postMessage(JSON.stringify({ event: 'openExternalUrl', url }))
                        event.preventDefault();
                    }
                    // Default: Allow navigation to proceed
                },true); // Capture the event during the capture phase
                document[listenerKey] = true;
            }
        })();
    )";
#if defined(__APPLE__) 
    // WebView on Windows does read keyboard shortcuts
    // Thus doing f.e. Reload twice would make the oparation to fail
    script += R"(
        document.addEventListener('keydown', function (event) {
            if (event.key === 'F5' || (event.ctrlKey && event.key === 'r') || (event.metaKey && event.key === 'r')) {
                window.ExternalApp.postMessage(JSON.stringify({ event: 'reloadHomePage', fromKeyboard: 1}));
            }
            if (event.metaKey && event.key === 'q') {
                window.ExternalApp.postMessage(JSON.stringify({ event: 'appQuit'}));
            }
            if (event.metaKey && event.key === 'm') {
                window.ExternalApp.postMessage(JSON.stringify({ event: 'appMinimize'}));
            }
        });
    )";
#endif // defined(__APPLE__)
    run_script(script);
}

void PrintablesWebViewPanel::show_download_notification(const std::string& filename)
{
    // There was a trouble with passing wide characters to the script (it was displayed wrong)
    // Solution is to URL-encode the strings here and pass it.
    // Then inside javascript decodes it.
    const std::string message_filename = Http::url_encode(GUI::format(_u8L("Downloading %1%"),filename));
    const std::string message_dest = Http::url_encode(GUI::format(_u8L("To %1%"), wxGetApp().app_config->get("url_downloader_dest")));
    std::string script = GUI::format(R"(
        function removeNotification() {
            const notifDiv = document.getElementById('slicer-notification');
            if (notifDiv)
                notifDiv.remove();
        }
        function appendNotification() {
        const body = document.getElementsByTagName('body')[0];
        const notifDiv = document.createElement('div');
        notifDiv.innerHTML = `
                    <div>
                    <b>PrusaSlicer: </b>${decodeURIComponent('%1%')}
                    <br>${decodeURIComponent('%2%')}
                    </div>
                `;
        notifDiv.className = 'notification-popup';
        notifDiv.id = 'slicer-notification';
        body.appendChild(notifDiv);

        window.setTimeout(removeNotification, 5000);
    }
        appendNotification();
    )", message_filename, message_dest);
    run_script(script);
}

void PrintablesWebViewPanel::show_loading_overlay()
{
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__;
    std::string script = R"(
        function slic3r_showLoadingOverlay() {
            const body = document.getElementsByTagName('body')[0];
            const overlayDiv = document.createElement('div');
            overlayDiv.className = 'slic3r-loading-overlay'
            overlayDiv.id = 'slic3r-loading-overlay';
            overlayDiv.innerHTML = '<div class="slic3r-loading-anim"></div>';
            body.appendChild(overlayDiv);
        }
        slic3r_showLoadingOverlay();
    )";
    run_script(script);
}

void PrintablesWebViewPanel::hide_loading_overlay()
{
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__;
    m_refreshing_token = false;
    std::string script = R"(
        function slic3r_hideLoadingOverlay() {
            const overlayDiv = document.getElementById('slic3r-loading-overlay');
            if (overlayDiv)
                overlayDiv.remove();
        }
        slic3r_hideLoadingOverlay();
    )";
    run_script(script);
}


} // namespace slic3r::GUI