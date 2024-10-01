#include "WebViewDialog.hpp"

#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/UserAccount.hpp"
#include "slic3r/GUI/format.hpp"
#include "slic3r/GUI/WebView.hpp"
#include "slic3r/GUI/WebViewPlatformUtils.hpp"
#include "slic3r/Utils/ServiceConfig.hpp"

#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/Field.hpp"

#include <libslic3r/PresetBundle.hpp> // IWYU pragma: keep


#include <wx/webview.h>

#include <boost/log/trivial.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

// if set to 1 the fetch() JS function gets override to include JWT in authorization header
// if set to 0, the /slicer/login is invoked from WebKit (passing JWT token only to this request)
// to set authorization cookie for all WebKit requests to Connect
#define AUTH_VIA_FETCH_OVERRIDE 0

wxDEFINE_EVENT(EVT_OPEN_EXTERNAL_LOGIN, wxCommandEvent);

namespace pt = boost::property_tree;

namespace Slic3r {
namespace GUI {


WebViewPanel::~WebViewPanel()
{
    SetEvtHandlerEnabled(false);
#ifdef DEBUG_URL_PANEL
    delete m_tools_menu;
#endif
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
    m_browser->LoadURL(url);
    m_browser->SetFocus();
}


WebViewPanel::WebViewPanel(wxWindow *parent, const wxString& default_url, const std::vector<std::string>& message_handler_names, const std::string& loading_html/* = "loading"*/)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
    , m_default_url (default_url)
    , m_loading_html(loading_html)
    , m_script_message_hadler_names(message_handler_names)
{
    wxBoxSizer* topsizer = new wxBoxSizer(wxVERTICAL);
#ifdef DEBUG_URL_PANEL
    // Create the button
    bSizer_toolbar = new wxBoxSizer(wxHORIZONTAL);

    m_button_back = new wxButton(this, wxID_ANY, wxT("Back"), wxDefaultPosition, wxDefaultSize, 0);
    m_button_back->Enable(false);
    bSizer_toolbar->Add(m_button_back, 0, wxALL, 5);

    m_button_forward = new wxButton(this, wxID_ANY, wxT("Forward"), wxDefaultPosition, wxDefaultSize, 0);
    m_button_forward->Enable(false);
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

    // Create the webview
    m_browser = WebView::CreateWebView(this, /*m_default_url*/ GUI::format_wxstr("file://%1%/web/%2%.html", boost::filesystem::path(resources_dir()).generic_string(), m_loading_html), m_script_message_hadler_names);
    if (Utils::ServiceConfig::instance().webdev_enabled()) {
        m_browser->EnableContextMenu();
        m_browser->EnableAccessToDevTools();
    }
    if (!m_browser) {
        wxStaticText* text = new wxStaticText(this, wxID_ANY, _L("Failed to load a web browser."));
        topsizer->Add(text, 0, wxALIGN_LEFT | wxBOTTOM, 10);
        return;
    }
    topsizer->Add(m_browser, wxSizerFlags().Expand().Proportion(1));
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

#endif

    Bind(wxEVT_SHOW, &WebViewPanel::on_show, this);

    // Connect the webview events
    Bind(wxEVT_WEBVIEW_ERROR, &WebViewPanel::on_error, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &WebViewPanel::on_script_message, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_NAVIGATING, &WebViewPanel::on_navigation_request, this, m_browser->GetId());

#ifdef DEBUG_URL_PANEL
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
    //Connect the idle events
    Bind(wxEVT_IDLE, &WebViewPanel::on_idle, this);
}

void WebViewPanel::load_default_url_delayed()
{
    assert(!m_default_url.empty());
    m_load_default_url = true;
}

void WebViewPanel::load_error_page()
{
    if (!m_browser)
        return;

    m_browser->Stop();
    m_load_error_page = true;    
}

void WebViewPanel::on_show(wxShowEvent& evt)
{
    m_shown = evt.IsShown();
    if (evt.IsShown() && m_load_default_url) {
        m_load_default_url = false;
        load_url(m_default_url);
    }
}

void WebViewPanel::on_idle(wxIdleEvent& WXUNUSED(evt))
{
    if (!m_browser)
        return;
    if (m_browser->IsBusy()) {
        wxSetCursor(wxCURSOR_ARROWWAIT);
    } else {
        wxSetCursor(wxNullCursor);

        if (m_shown && m_load_error_page) {
            m_load_error_page = false;
            load_url(GUI::format_wxstr("file://%1%/web/connection_failed.html", boost::filesystem::path(resources_dir()).generic_string()));
        }
    }
#ifdef DEBUG_URL_PANEL
    m_button_stop->Enable(m_browser->IsBusy());
#endif
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
    m_browser->GoBack();
}

/**
    * Callback invoked when user pressed the "forward" button
    */
void WebViewPanel::on_forward_button(wxCommandEvent& WXUNUSED(evt))
{
    if (!m_browser)
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
    BOOST_LOG_TRIVIAL(debug) << "RunScript " << javascript << "\n";
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

    BOOST_LOG_TRIVIAL(error) << "WebViewPanel error: " << category;
    load_error_page();
#ifdef DEBUG_URL_PANEL
    m_info->ShowMessage(wxString("An error occurred loading ") + evt.GetURL() + "\n" +
        "'" + category + "'", wxICON_ERROR);
#endif
}

void WebViewPanel::sys_color_changed()
{
#ifdef _WIN32
    wxGetApp().UpdateDarkUI(this);
#endif
}

SourceViewDialog::SourceViewDialog(wxWindow* parent, wxString source) :
                  wxDialog(parent, wxID_ANY, "Source Code",
                           wxDefaultPosition, wxSize(700,500),
                           wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    wxTextCtrl* text = new wxTextCtrl(this, wxID_ANY, source,
                                      wxDefaultPosition, wxDefaultSize,
                                      wxTE_MULTILINE |
                                      wxTE_RICH |
                                      wxTE_READONLY);

    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(text, 1, wxEXPAND);
    SetSizer(sizer);
}

ConnectRequestHandler::ConnectRequestHandler()
{
    m_actions["REQUEST_LOGIN"] = std::bind(&ConnectRequestHandler::on_connect_action_request_login, this, std::placeholders::_1);
    m_actions["REQUEST_CONFIG"] = std::bind(&ConnectRequestHandler::on_connect_action_request_config, this, std::placeholders::_1);
    m_actions["WEBAPP_READY"] = std::bind(&ConnectRequestHandler::on_connect_action_webapp_ready,this, std::placeholders::_1);
    m_actions["SELECT_PRINTER"] = std::bind(&ConnectRequestHandler::on_connect_action_select_printer, this, std::placeholders::_1);
    m_actions["PRINT"] = std::bind(&ConnectRequestHandler::on_connect_action_print, this, std::placeholders::_1);
    m_actions["REQUEST_OPEN_IN_BROWSER"] = std::bind(&ConnectRequestHandler::on_connect_action_request_open_in_browser, this, std::placeholders::_1);
    m_actions["ERROR"] = std::bind(&ConnectRequestHandler::on_connect_action_error, this, std::placeholders::_1);
    m_actions["LOG"] = std::bind(&ConnectRequestHandler::on_connect_action_log, this, std::placeholders::_1);
}
ConnectRequestHandler::~ConnectRequestHandler()
{
}
void ConnectRequestHandler::handle_message(const std::string& message)
{
    // read msg and choose action
    /*
    v0:
    {"type":"request","detail":{"action":"requestAccessToken"}}
    v1:
    {"action":"REQUEST_ACCESS_TOKEN"}
    */
    std::string action_string;
    try {
        std::stringstream ss(message);
        pt::ptree ptree;
        pt::read_json(ss, ptree);
        // v1:
        if (const auto action = ptree.get_optional<std::string>("action"); action) {
            action_string = *action;
        }
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Could not parse _prusaConnect message. " << e.what();
        return;
    }

    if (action_string.empty()) {
        BOOST_LOG_TRIVIAL(error) << "Recieved invalid message from _prusaConnect (missing action). Message: " << message;
        return;
    }
    assert(m_actions.find(action_string) != m_actions.end()); // this assert means there is a action that has no handling.
    if (m_actions.find(action_string) != m_actions.end()) {
        m_actions[action_string](message);
    }
}

void ConnectRequestHandler::on_connect_action_error(const std::string &message_data)
{
    BOOST_LOG_TRIVIAL(error) << "WebView runtime error: " << message_data;
}

void ConnectRequestHandler::resend_config()
{
    on_connect_action_request_config({});
}

void ConnectRequestHandler::on_connect_action_log(const std::string& message_data)
{
    BOOST_LOG_TRIVIAL(info) << "WebView log: " << message_data;
}

void ConnectRequestHandler::on_connect_action_request_login(const std::string &message_data)
{}


void ConnectRequestHandler::on_connect_action_request_config(const std::string& message_data)
{
    /*
    accessToken?: string;
    clientVersion?: string;
    colorMode?: "LIGHT" | "DARK";
    language?: ConnectLanguage;
    sessionId?: string;
    */
    const std::string token = wxGetApp().plater()->get_user_account()->get_access_token();
    //const std::string sesh = wxGetApp().plater()->get_user_account()->get_shared_session_key();
    const std::string dark_mode = wxGetApp().dark_mode() ? "DARK" : "LIGHT";
    wxString language = GUI::wxGetApp().current_language_code();
    language = language.SubString(0, 1);
    const std::string init_options = GUI::format("{\"accessToken\": \"%4%\",\"clientVersion\": \"%1%\", \"colorMode\": \"%2%\", \"language\": \"%3%\"}", SLIC3R_VERSION, dark_mode, language, token );  
    wxString script = GUI::format_wxstr("window._prusaConnect_v1.init(%1%)", init_options);
    run_script_bridge(script);
    
}
void ConnectRequestHandler::on_connect_action_request_open_in_browser(const std::string& message_data) 
{
    try {
        std::stringstream ss(message_data);
        pt::ptree ptree;
        pt::read_json(ss, ptree);
        if (const auto url = ptree.get_optional<std::string>("url"); url) {
            wxGetApp().open_browser_with_warning_dialog(GUI::from_u8(*url));
        }
    } catch (const std::exception &e) {
        BOOST_LOG_TRIVIAL(error) << "Could not parse _prusaConnect message. " << e.what();
        return;
    }    
}

ConnectWebViewPanel::ConnectWebViewPanel(wxWindow* parent)
    : WebViewPanel(parent, GUI::from_u8(Utils::ServiceConfig::instance().connect_url()), { "_prusaSlicer" }, "connect_loading")
{  
    // m_browser->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new WebViewHandler("https")));

    auto* plater = wxGetApp().plater();
    plater->Bind(EVT_UA_ID_USER_SUCCESS, &ConnectWebViewPanel::on_user_token, this);
    plater->Bind(EVT_UA_LOGGEDOUT, &ConnectWebViewPanel::on_user_logged_out, this);
}

ConnectWebViewPanel::~ConnectWebViewPanel()
{
    m_browser->Unbind(EVT_UA_ID_USER_SUCCESS, &ConnectWebViewPanel::on_user_token, this);
}

wxString ConnectWebViewPanel::get_login_script(bool refresh)
{
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
            const maxBackoff = 64000;
            do {

                let error = false;

                try {
                    _prusaSlicer_log('Slicer Login request ' + token.substring(token.length - 8));
                    let resp = await fetch('/slicer/login', {method: 'POST', headers: {Authorization: 'Bearer ' + token}});
                    let body = await resp.text();
                    _prusaSlicer_log('Slicer Login resp ' + resp.status + ' (' + token.substring(token.length - 8) + ') body: ' + body);
                    if (resp.status >= 500 || resp.status == 408) {
                        retry = true;
                    } else {
                        retry = false;
                        if (resp.status >= 400)
                            _prusaSlicer_errorHandler({status: resp.status, body});
                    }
                } catch (e) {
                    _prusaSlicer_log('Slicer Login failed: ' + e.toString());
                    console.error('Slicer Login failed', e.toString());
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
    auto javascript = get_login_script(false);
    BOOST_LOG_TRIVIAL(debug) << "RunScript " << javascript << "\n";
    m_browser->AddUserScript(javascript);
}

void ConnectWebViewPanel::on_user_token(UserAccountSuccessEvent& e)
{
    e.Skip();
    auto access_token = wxGetApp().plater()->get_user_account()->get_access_token();
    assert(!access_token.empty());

    wxString javascript = get_login_script(true);
    BOOST_LOG_TRIVIAL(debug) << "RunScript " << javascript << "\n";
    m_browser->RunScriptAsync(javascript);
    resend_config();
}

void ConnectWebViewPanel::on_user_logged_out(UserAccountSuccessEvent& e)
{
    e.Skip();
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
    BOOST_LOG_TRIVIAL(debug) << "Navigation requested to: " << into_u8(evt.GetURL());
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
    }
}

void ConnectWebViewPanel::on_connect_action_error(const std::string &message_data)
{
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

void ConnectWebViewPanel::logout()
{
    wxString script = L"window._prusaConnect_v1.logout()";
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
    assert(true);
}

PrinterWebViewPanel::PrinterWebViewPanel(wxWindow* parent, const wxString& default_url)
    : WebViewPanel(parent, default_url, {})
{
    if (!m_browser)
        return;

    m_browser->Bind(wxEVT_WEBVIEW_LOADED, &PrinterWebViewPanel::on_loaded, this);
#ifndef NDEBUG
    m_browser->EnableAccessToDevTools();
    m_browser->EnableContextMenu();
#endif
}

void PrinterWebViewPanel::on_loaded(wxWebViewEvent& evt)
{
    if (evt.GetURL().IsEmpty())
        return;
    if (!m_api_key.empty()) {
        send_api_key();
    } else if (!m_usr.empty() && !m_psk.empty()) {
        send_credentials();
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
    m_browser->Reload();
    m_api_key_sent = true;
    setup_webview_with_credentials(m_browser, m_usr, m_psk);
}

void PrinterWebViewPanel::sys_color_changed()
{
}

WebViewDialog::WebViewDialog(wxWindow* parent, const wxString& url, const wxString& dialog_name, const wxSize& size, const std::vector<std::string>& message_handler_names, const std::string& loading_html/* = "loading"*/)
    : DPIDialog(parent, wxID_ANY, dialog_name, wxDefaultPosition, size, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , m_loading_html(loading_html)
    , m_script_message_hadler_names (message_handler_names)
{
    wxBoxSizer* topsizer = new wxBoxSizer(wxVERTICAL);
#ifdef DEBUG_URL_PANEL
    // Create the button
    bSizer_toolbar = new wxBoxSizer(wxHORIZONTAL);

    m_button_back = new wxButton(this, wxID_ANY, wxT("Back"), wxDefaultPosition, wxDefaultSize, 0);
    m_button_back->Enable(false);
    bSizer_toolbar->Add(m_button_back, 0, wxALL, 5);

    m_button_forward = new wxButton(this, wxID_ANY, wxT("Forward"), wxDefaultPosition, wxDefaultSize, 0);
    m_button_forward->Enable(false);
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
#endif
    topsizer->SetMinSize(size);
    SetSizerAndFit(topsizer);

    // Create the webview
    m_browser = WebView::CreateWebView(this, GUI::format_wxstr("file://%1%/web/%2%.html", boost::filesystem::path(resources_dir()).generic_string(), m_loading_html), m_script_message_hadler_names);
    if (Utils::ServiceConfig::instance().webdev_enabled()) {
        m_browser->EnableContextMenu();
        m_browser->EnableAccessToDevTools();
    }
    if (!m_browser) {
        wxStaticText* text = new wxStaticText(this, wxID_ANY, _L("Failed to load a web browser."));
        topsizer->Add(text, 0, wxALIGN_LEFT | wxBOTTOM, 10);
        return;
    }

    topsizer->Add(m_browser, wxSizerFlags().Expand().Proportion(1));

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

#endif
    
    Bind(wxEVT_SHOW, &WebViewDialog::on_show, this);
    Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &WebViewDialog::on_script_message, this, m_browser->GetId());
    
    // Connect the webview events
    Bind(wxEVT_WEBVIEW_ERROR, &WebViewDialog::on_error, this, m_browser->GetId());
    //Connect the idle events
    Bind(wxEVT_IDLE, &WebViewDialog::on_idle, this);
#ifdef DEBUG_URL_PANEL
    // Connect the button events
    Bind(wxEVT_BUTTON, &WebViewDialog::on_back_button, this, m_button_back->GetId());
    Bind(wxEVT_BUTTON, &WebViewDialog::on_forward_button, this, m_button_forward->GetId());
    Bind(wxEVT_BUTTON, &WebViewDialog::on_stop_button, this, m_button_stop->GetId());
    Bind(wxEVT_BUTTON, &WebViewDialog::on_reload_button, this, m_button_reload->GetId());
    Bind(wxEVT_BUTTON, &WebViewDialog::on_tools_clicked, this, m_button_tools->GetId());
    Bind(wxEVT_TEXT_ENTER, &WebViewDialog::on_url, this, m_url->GetId());
    
    // Connect the menu events
    Bind(wxEVT_MENU, &WebViewDialog::on_view_source_request, this, viewSource->GetId());
    Bind(wxEVT_MENU, &WebViewDialog::on_view_text_request, this, viewText->GetId());
    Bind(wxEVT_MENU, &WebViewDialog::On_enable_context_menu, this, m_context_menu->GetId());
    Bind(wxEVT_MENU, &WebViewDialog::On_enable_dev_tools, this, m_dev_tools->GetId());

    Bind(wxEVT_MENU, &WebViewDialog::on_run_script_custom, this, m_script_custom->GetId());
    Bind(wxEVT_MENU, &WebViewDialog::on_add_user_script, this, addUserScript->GetId());
#endif
    Bind(wxEVT_WEBVIEW_NAVIGATING, &WebViewDialog::on_navigation_request, this, m_browser->GetId());
    Bind(wxEVT_WEBVIEW_LOADED, &WebViewDialog::on_loaded, this, m_browser->GetId());

    Bind(wxEVT_CLOSE_WINDOW, ([this](wxCloseEvent& evt) { EndModal(wxID_CANCEL); }));

    m_browser->LoadURL(url);   
#ifdef DEBUG_URL_PANEL
    m_url->SetLabelText(url);
#endif
}
WebViewDialog::~WebViewDialog()
{
}

void WebViewDialog::on_idle(wxIdleEvent& WXUNUSED(evt))
{
    if (!m_browser)
        return;
    if (m_browser->IsBusy()) {
        wxSetCursor(wxCURSOR_ARROWWAIT);
    }  else {
        wxSetCursor(wxNullCursor);
        if (m_load_error_page) {
            m_load_error_page = false;
            m_browser->LoadURL(GUI::format_wxstr("file://%1%/web/connection_failed.html", boost::filesystem::path(resources_dir()).generic_string()));
        }
    }
#ifdef DEBUG_URL_PANEL
    m_button_stop->Enable(m_browser->IsBusy());
#endif
}

/**
    * Callback invoked when user entered an URL and pressed enter
    */
void WebViewDialog::on_url(wxCommandEvent& WXUNUSED(evt))
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
void WebViewDialog::on_back_button(wxCommandEvent& WXUNUSED(evt))
{
    if (!m_browser)
        return;
    m_browser->GoBack();
}

/**
    * Callback invoked when user pressed the "forward" button
    */
void WebViewDialog::on_forward_button(wxCommandEvent& WXUNUSED(evt))
{
    if (!m_browser)
        return;
    m_browser->GoForward();
}

/**
    * Callback invoked when user pressed the "stop" button
    */
void WebViewDialog::on_stop_button(wxCommandEvent& WXUNUSED(evt))
{
    if (!m_browser)
        return;
    m_browser->Stop();
}

/**
    * Callback invoked when user pressed the "reload" button
    */
void WebViewDialog::on_reload_button(wxCommandEvent& WXUNUSED(evt))
{
    if (!m_browser)
        return;
    m_browser->Reload();
}


void WebViewDialog::on_navigation_request(wxWebViewEvent &evt)
{
}

void WebViewDialog::on_script_message(wxWebViewEvent& evt)
{
}

/**
    * Invoked when user selects the "View Source" menu item
    */
void WebViewDialog::on_view_source_request(wxCommandEvent& WXUNUSED(evt))
{
    if (!m_browser)
        return;

    SourceViewDialog dlg(this, m_browser->GetPageSource());
    dlg.ShowModal();
}

/**
    * Invoked when user selects the "View Text" menu item
    */
void WebViewDialog::on_view_text_request(wxCommandEvent& WXUNUSED(evt))
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
void WebViewDialog::on_tools_clicked(wxCommandEvent& WXUNUSED(evt))
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


void WebViewDialog::on_run_script_custom(wxCommandEvent& WXUNUSED(evt))
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

void WebViewDialog::on_add_user_script(wxCommandEvent& WXUNUSED(evt))
{
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
    BOOST_LOG_TRIVIAL(debug) << "RunScript " << javascript <<"\n";
    if (!m_browser->AddUserScript(javascript))
        wxLogError("Could not add user script");
}

void WebViewDialog::on_set_custom_user_agent(wxCommandEvent& WXUNUSED(evt))
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

void WebViewDialog::on_clear_selection(wxCommandEvent& WXUNUSED(evt))
{
    if (!m_browser)
        return;

    m_browser->ClearSelection();
}

void WebViewDialog::on_delete_selection(wxCommandEvent& WXUNUSED(evt))
{
    if (!m_browser)
        return;

    m_browser->DeleteSelection();
}

void WebViewDialog::on_select_all(wxCommandEvent& WXUNUSED(evt))
{
    if (!m_browser)
        return;

    m_browser->SelectAll();
}

void WebViewDialog::On_enable_context_menu(wxCommandEvent& evt)
{
    if (!m_browser)
        return;

    m_browser->EnableContextMenu(evt.IsChecked());
}
void WebViewDialog::On_enable_dev_tools(wxCommandEvent& evt)
{
    if (!m_browser)
        return;

    m_browser->EnableAccessToDevTools(evt.IsChecked());
}

/**
    * Callback invoked when a loading error occurs
    */
void WebViewDialog::on_error(wxWebViewEvent& evt)
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

    BOOST_LOG_TRIVIAL(error) << "WebViewDialog error: " << category;
    load_error_page();
}

void WebViewDialog::load_error_page()
{
    if (!m_browser)
        return;

    m_browser->Stop();
    m_load_error_page = true;
}

void WebViewDialog::run_script(const wxString& javascript)
{
    if (!m_browser)
        return;
    // Remember the script we run in any case, so the next time the user opens
    // the "Run Script" dialog box, it is shown there for convenient updating.
    m_javascript = javascript;
    BOOST_LOG_TRIVIAL(debug) << "RunScript " << javascript << "\n";
    m_browser->RunScriptAsync(javascript);
}

void WebViewDialog::EndModal(int retCode)
{
    if (m_browser) {
        for (const std::string& handler : m_script_message_hadler_names) {
            m_browser->RemoveScriptMessageHandler(GUI::into_u8(handler));
        }
    }
    
    wxDialog::EndModal(retCode);
}

PrinterPickWebViewDialog::PrinterPickWebViewDialog(wxWindow* parent, std::string& ret_val)
    : WebViewDialog(parent
        , GUI::from_u8(Utils::ServiceConfig::instance().connect_select_printer_url())
        , _L("Choose a printer")
        , wxSize(std::max(parent->GetClientSize().x / 2, 100 * wxGetApp().em_unit()), std::max(parent->GetClientSize().y / 2, 50 * wxGetApp().em_unit()))
        ,{"_prusaSlicer"}
        , "connect_loading")
    , m_ret_val(ret_val)
{
    Centre();
}
void PrinterPickWebViewDialog::on_show(wxShowEvent& evt)
{
    /*
    if (evt.IsShown()) {
        std::string token = wxGetApp().plater()->get_user_account()->get_access_token();
        wxString script = GUI::format_wxstr("window.setAccessToken(\'%1%\')", token);
        // TODO: should this be happening every OnShow?
        run_script(script);
    }
    */
}
void PrinterPickWebViewDialog::on_script_message(wxWebViewEvent& evt)
{
    handle_message(into_u8(evt.GetString()));
}

void PrinterPickWebViewDialog::on_connect_action_select_printer(const std::string& message_data)
{
    // SELECT_PRINTER request is not defined for PrinterPickWebViewDialog
    assert(true);
}
void PrinterPickWebViewDialog::on_connect_action_print(const std::string& message_data)
{
    m_ret_val = message_data;
    this->EndModal(wxID_OK);
}

void PrinterPickWebViewDialog::on_connect_action_webapp_ready(const std::string& message_data)
{
    
    if (Preset::printer_technology(wxGetApp().preset_bundle->printers.get_selected_preset().config) == ptFFF) {
        request_compatible_printers_FFF();
    } else {
        request_compatible_printers_SLA();
    }
}

void PrinterPickWebViewDialog::request_compatible_printers_FFF() {
// {
//  "printerUuid": "",
//  "printerModel": "MK4S",
//  "filename": "Shape-Box_0.4n_0.2mm_{..}_MK4S_{..}.bgcode",
//  "nozzle_diameter": [0.4],     // array float
//  "material": ["PLA", "ASA"],   // array string
//  "filament_abrasive": [false], // array boolean
//  "high_flow": [false],         // array boolean
//}
    const Preset &selected_printer = wxGetApp().preset_bundle->printers.get_selected_preset();
    const DynamicPrintConfig full_config = wxGetApp().preset_bundle->full_config();
    
    wxString nozzle_diameter_serialized = "[";
    const auto nozzle_diameter_floats = static_cast<const ConfigOptionFloats *>(full_config.option("nozzle_diameter"));
    for (size_t i = 0; i < nozzle_diameter_floats->size(); i++) {
        double nozzle_diameter = nozzle_diameter_floats->values[i];
        wxString value = double_to_string(nozzle_diameter);
        value.Replace(L",", L".");
        nozzle_diameter_serialized += value;
        if (i <  nozzle_diameter_floats->size() - 1) {
            nozzle_diameter_serialized += ", ";
        }
    }
    nozzle_diameter_serialized += "]";
    
    std::string filament_type_serialized = "[";
    const auto filament_type_strings =  static_cast<const ConfigOptionStrings *>(full_config.option("filament_type"));
    for (size_t i = 0; i < filament_type_strings->size(); i++) {
        std::string value = filament_type_strings->values[0];
        filament_type_serialized += "\"" + value + "\"";
        if (i <  filament_type_strings->size() - 1)
        {
            filament_type_serialized += ", ";
        }
    }
    filament_type_serialized += "]";
    
    wxString nozzle_high_flow_serialized = "[";
    const auto nozzle_high_flow_bools =  static_cast<const ConfigOptionBools *>(full_config.option("nozzle_high_flow"));
    for (size_t i = 0; i < nozzle_high_flow_bools->size(); i++) {
        wxString value = nozzle_high_flow_bools->values[0] ? "true" : "false";
        nozzle_high_flow_serialized += value;
        if (i <  nozzle_high_flow_bools->size() - 1) {
            nozzle_high_flow_serialized += ", ";
        }
    }
    nozzle_high_flow_serialized += "]";

    wxString filament_abrasive_serialized = "[";
    const auto filament_abrasive_bools =  static_cast<const ConfigOptionBools *>(full_config.option("filament_abrasive"));
    for (size_t i = 0; i < filament_abrasive_bools->size(); i++) {
        wxString value = filament_abrasive_bools->values[0] ? "true" : "false";
        filament_abrasive_serialized += value;
        if (i <  filament_abrasive_bools->size() - 1) {
            filament_abrasive_serialized += ", ";
        }
    }
    filament_abrasive_serialized += "]";
    
    std::string printer_model_serialized = full_config.option("printer_model")->serialize();
    std::string vendor_repo_prefix;
    if (selected_printer.vendor) {
        vendor_repo_prefix = selected_printer.vendor->repo_prefix;
    } else if (std::string inherits = selected_printer.inherits(); !inherits.empty()) {
        const Preset *parent = wxGetApp().preset_bundle->printers.find_preset(inherits);
        if (parent && parent->vendor) {
            vendor_repo_prefix = parent->vendor->repo_prefix;
        }
    }
    if (printer_model_serialized.find(vendor_repo_prefix) == 0) {
        printer_model_serialized = printer_model_serialized.substr(vendor_repo_prefix.size());
        boost::trim_left(printer_model_serialized);
    }

    const std::string uuid = wxGetApp().plater()->get_user_account()->get_current_printer_uuid_from_connect(printer_model_serialized);
    const std::string filename = wxGetApp().plater()->get_upload_filename();
    //filament_abrasive
    std::string request = GUI::format(
        "{"
        "\"printerUuid\": \"%4%\", "
        "\"printerModel\": \"%3%\", "
        "\"nozzle_diameter\": %2%, "
        "\"material\": %1%, "
        "\"filename\": \"%5%\", "
        "\"filament_abrasive\": %6%,"
        "\"high_flow\": %7%"
        "}"
        , filament_type_serialized, nozzle_diameter_serialized, printer_model_serialized, uuid, filename, nozzle_high_flow_serialized, filament_abrasive_serialized);

    wxString script = GUI::format_wxstr("window._prusaConnect_v1.requestCompatiblePrinter(%1%)", request);
    run_script(script);
}
void PrinterPickWebViewDialog::request_compatible_printers_SLA()
{
    const Preset& selected_printer = wxGetApp().preset_bundle->printers.get_selected_preset();
    std::string printer_model_serialized = selected_printer.config.option("printer_model")->serialize();
    
    std::string vendor_repo_prefix;
    if (selected_printer.vendor) {
        vendor_repo_prefix = selected_printer.vendor->repo_prefix;
    } else if (std::string inherits = selected_printer.inherits(); !inherits.empty()) {
        const Preset *parent = wxGetApp().preset_bundle->printers.find_preset(inherits);
        if (parent && parent->vendor) {
            vendor_repo_prefix = parent->vendor->repo_prefix;
        }
    }
    if (printer_model_serialized.find(vendor_repo_prefix) == 0) {
        printer_model_serialized = printer_model_serialized.substr(vendor_repo_prefix.size());
        boost::trim_left(printer_model_serialized);
    }
    const Preset& selected_material = wxGetApp().preset_bundle->sla_materials.get_selected_preset();
    const std::string material_type_serialized = selected_material.config.option("material_type")->serialize();
    const std::string uuid = wxGetApp().plater()->get_user_account()->get_current_printer_uuid_from_connect(printer_model_serialized);
    const std::string filename = wxGetApp().plater()->get_upload_filename();
    const std::string request = GUI::format(
        "{"
        "\"printerUuid\": \"%3%\", "
        "\"material\": \"%1%\", "
        "\"printerModel\": \"%2%\", "
        "\"filename\": \"%4%\" "
        "}", material_type_serialized, printer_model_serialized, uuid, filename);

    wxString script = GUI::format_wxstr("window._prusaConnect_v1.requestCompatiblePrinter(%1%)", request);
    run_script(script);
}
void PrinterPickWebViewDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    wxWindow *parent = GetParent();
    const wxSize &size = wxSize(
        std::max(parent->GetClientSize().x / 2, 100 * wxGetApp().em_unit()),
        std::max(parent->GetClientSize().y / 2, 50 * wxGetApp().em_unit())
    );
    SetMinSize(size);
    Fit();
    Refresh();
}

LoginWebViewDialog::LoginWebViewDialog(wxWindow *parent, std::string &ret_val, const wxString& url, wxEvtHandler* evt_handler)
    : WebViewDialog(parent, url, _L("Log in dialog"), wxSize(50 * wxGetApp().em_unit(), 80 * wxGetApp().em_unit()), {})
    , m_ret_val(ret_val)
    , p_evt_handler(evt_handler)
{
    Centre();
}
void LoginWebViewDialog::on_navigation_request(wxWebViewEvent &evt)
{
    wxString url = evt.GetURL();
    if (url.starts_with(L"prusaslicer")) {
        delete_cookies(m_browser, "https://account.prusa3d.com");
        delete_cookies(m_browser, "https://accounts.google.com");
        delete_cookies(m_browser, "https://appleid.apple.com");
        delete_cookies(m_browser, "https://facebook.com");
        evt.Veto();
        m_ret_val = into_u8(url);
        EndModal(wxID_OK);
    } else if (url.Find(L"accounts.google.com") != wxString::npos
        || url.Find(L"appleid.apple.com") != wxString::npos
        || url.Find(L"facebook.com") != wxString::npos) {         
        auto& sc = Utils::ServiceConfig::instance();
        if (!m_evt_sent && !url.starts_with(GUI::from_u8(sc.account_url()))) {
            wxCommandEvent* evt = new wxCommandEvent(EVT_OPEN_EXTERNAL_LOGIN);
            evt->SetString(url);
            p_evt_handler->QueueEvent(evt);
            m_evt_sent = true;
        }
    }
}

void LoginWebViewDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    const wxSize &size = wxSize(50 * wxGetApp().em_unit(), 80 * wxGetApp().em_unit());
    SetMinSize(size);
    Fit();
    Refresh();
}
} // GUI
} // Slic3r
