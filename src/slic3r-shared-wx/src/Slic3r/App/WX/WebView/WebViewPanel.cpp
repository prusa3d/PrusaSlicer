#include "Slic3r/App/WX/WebView/WebViewPanel.hpp"

#include "WebViewPlatformUtils.hpp"

#include "Slic3r/App/WX/WebView/WebView.hpp"
#include "Slic3r/App/WX/StringConversions.hpp"
#include "Slic3r/App/WX/I18N.hpp"
#include "Slic3r/Biz/Network/ServiceConfig.hpp"
#include "Slic3r/App/WX/format.hpp"
#include "Slic3r/App/WX/WidgetsConfig.hpp"

#include "Slic3r/Log.hpp"
#include <Slic3r/Assert.hpp>
#include <Slic3r/Directories.hpp>

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textdlg.h>
#include <wx/uri.h>

#include "nlohmann/json.hpp"

namespace Slic3r::App::WX::WebView {

WebViewPanel::WebViewPanel(wxWindow* parent,  int id, std::unique_ptr<App::Browser::AbstractBrowserLogic>&& logic, bool do_create) :
    AbstractWebViewPanel(parent, id),
    m_logic(std::move(logic))
{
    topsizer    = new wxBoxSizer(wxVERTICAL);
    m_sizer_top = new wxBoxSizer(wxHORIZONTAL);
    topsizer->Add(m_sizer_top, 0, wxEXPAND, 0);

#ifdef DEBUG_URL_PANEL
    // Create the button
    bSizer_toolbar = new wxBoxSizer(wxHORIZONTAL);

    m_button_back = new wxButton(this, wxID_ANY, wxT("Back"), wxDefaultPosition, wxDefaultSize, 0);
    // m_button_back->Enable(false);
    bSizer_toolbar->Add(m_button_back, 0, wxALL, 5);

    m_button_forward = new wxButton(this, wxID_ANY, wxT("Forward"), wxDefaultPosition, wxDefaultSize, 0);
    // m_button_forward->Enable(false);
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
    /*
    // Create the Tools menu
    m_tools_menu         = new wxMenu();
    wxMenuItem* viewText = m_tools_menu->Append(wxID_ANY, "View Text");
    m_tools_menu->AppendSeparator();

    wxMenu* script_menu = new wxMenu;

    m_script_custom = script_menu->Append(wxID_ANY, "Custom script");
    m_tools_menu->AppendSubMenu(script_menu, "Run Script");
    wxMenuItem* addUserScript      = m_tools_menu->Append(wxID_ANY, "Add user script");
    wxMenuItem* setCustomUserAgent = m_tools_menu->Append(wxID_ANY, "Set custom user agent");

    m_context_menu = m_tools_menu->AppendCheckItem(wxID_ANY, "Enable Context Menu");
    m_dev_tools    = m_tools_menu->AppendCheckItem(wxID_ANY, "Enable Dev Tools");

    // Connect the button events
    Bind(wxEVT_BUTTON, &WebViewPanel::on_back_button, this, m_button_back->GetId());
    Bind(wxEVT_BUTTON, &WebViewPanel::on_forward_button, this, m_button_forward->GetId());
    Bind(wxEVT_BUTTON, &WebViewPanel::on_stop_button, this, m_button_stop->GetId());
    Bind(wxEVT_BUTTON, &WebViewPanel::on_reload_button, this, m_button_reload->GetId());
    Bind(wxEVT_BUTTON, &WebViewPanel::on_tools_clicked, this, m_button_tools->GetId());
    Bind(wxEVT_TEXT_ENTER, &WebViewPanel::on_url, this, m_url->GetId());

    // Connect the menu events
    Bind(wxEVT_MENU, &WebViewPanel::on_view_text_request, this, viewText->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::On_enable_context_menu, this, m_context_menu->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::On_enable_dev_tools, this, m_dev_tools->GetId());

    Bind(wxEVT_MENU, &WebViewPanel::on_run_script_custom, this, m_script_custom->GetId());
    Bind(wxEVT_MENU, &WebViewPanel::on_add_user_script, this, addUserScript->GetId());
    */
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
    m_web_view               = WebView::web_view_new();

    if (!m_web_view) {
        wxStaticText* text = new wxStaticText(this, wxID_ANY, _L("Failed to load a web browser."));
        topsizer->Add(text, 0, wxALIGN_LEFT | wxBOTTOM, 10);
        return;
    }
    WebView::web_view_create(
        m_web_view,
        this,
        format_wxstr(
            "file://%1%/web/%2%%3%.html",
            boost::filesystem::path(resources_dir()).generic_string(),
            m_logic->loading_html(),
            w_config()->dark_mode() ? "_dark" : ""
        ),
        m_logic->script_message_handler_names()
    );

    if (Biz::Network::ServiceConfig::instance().webdev_enabled()) {
        setup_webview_devtools(m_web_view);
    }
    topsizer->Add(m_web_view, wxSizerFlags().Expand().Proportion(1));

    // Connect the webview events
    Bind(wxEVT_WEBVIEW_ERROR, &WebViewPanel::on_error, this, m_web_view->GetId());
    Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &WebViewPanel::on_script_message, this, m_web_view->GetId());
    Bind(wxEVT_WEBVIEW_NAVIGATING, &WebViewPanel::on_navigation_request, this, m_web_view->GetId());
    Bind(wxEVT_WEBVIEW_LOADED, &WebViewPanel::on_loaded, this, m_web_view->GetId());
    Bind(wxEVT_WEBVIEW_CREATED, &WebViewPanel::on_created, this, m_web_view->GetId());

    Layout();

#ifdef DEBUG_URL_PANEL
    setup_webview_devtools(m_web_view);
#endif
}

void WebViewPanel::on_user_account_id_success(bool is_refresh, const std::string& username)
{
    if (!m_web_view)
        return;
    [[maybe_unused]] bool b = process_logic_command_vector(m_logic->on_user_account_id_success(is_refresh, into_u8(m_web_view->GetCurrentURL())));
    DEBUG_ASSERT(b, "False return value signals Veto which cannot be done here.");
}

void WebViewPanel::on_user_account_logged_out()
{
    if (!m_web_view)
        return;
    [[maybe_unused]] bool b = process_logic_command_vector(m_logic->on_user_account_logged_out(into_u8(m_web_view->GetCurrentURL())));
    DEBUG_ASSERT(b, "False return value signals Veto which cannot be done here.");
}

void WebViewPanel::on_user_account_will_refresh()
{
    if (!m_web_view)
        return;
    [[maybe_unused]] bool b = process_logic_command_vector(m_logic->on_user_account_will_refresh());
    DEBUG_ASSERT(b, "False return value signals Veto which cannot be done here.");
}

void WebViewPanel::on_printables_secret_token(const std::string& body) 
{
    [[maybe_unused]] bool b = process_logic_command_vector(m_logic->on_printables_secret_token(body));
    DEBUG_ASSERT(b, "False return value signals Veto which cannot be done here.");
}

void WebViewPanel::load_url(const wxString& url)
{
    if (!m_web_view)
        return;

    this->on_page_will_load();

    // this->Show();
    // this->Raise();
#ifdef DEBUG_URL_PANEL
    m_url->SetLabelText(url);
#endif
    wxString correct_url = url.empty() ? wxString(L"") : wxURI(url).BuildURI();
    m_web_view->LoadURL(correct_url);
    m_web_view->SetFocus();
}

void WebViewPanel::load_error_page()
{
    if (!m_web_view || m_do_late_webview_create) {
        return;
    }

    m_web_view->Stop();
    m_load_error_page = true;
}

void WebViewPanel::load_default_url()
{
    if (!m_web_view || m_do_late_webview_create) {
        return;
    }

    process_logic_command_vector(m_logic->on_load_default_url());
}

void WebViewPanel::run_script(const wxString& javascript)
{
    if (!m_web_view || !m_shown)
        return;
    // SPDLOG_DEBUG("RunScript: {}", into_u8(javascript));
    m_web_view->RunScriptAsync(javascript);
}

void WebViewPanel::do_reload()
{
    if (!m_web_view) {
        return;
    }
    // IsBusy on Linux very often returns true due to loading about:blank after loading requested url.
#ifndef __linux__
    if (m_web_view->IsBusy()) {
        return;
    }
#endif
    const wxString current_url = m_web_view->GetCurrentURL();
    if (current_url.StartsWith(from_u8(m_logic->url()))) {
        m_web_view->Reload();
        return;
    }
    load_default_url();
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
    [[maybe_unused]] bool b = process_logic_command_vector(std::move(m_logic->on_show_webview_event(evt.IsShown())));
    DEBUG_ASSERT(b, "False return value signals Veto which cannot be done here.");
}

void WebViewPanel::on_script_message(wxWebViewEvent& evt)
{
    if (!process_logic_command_vector(std::move(m_logic->on_script_message_webview_event(into_u8(evt.GetString())))))
    {
        evt.Veto();
    }
}

void WebViewPanel::on_navigation_request(wxWebViewEvent& evt)
{
    if (!process_logic_command_vector(
            std::move(m_logic->on_navigation_request_webview_event(
                into_u8(evt.GetURL()),
                into_u8(m_web_view->GetCurrentURL())
            ))
        ))
    {
        evt.Veto();
    }
}

void WebViewPanel::on_loaded(wxWebViewEvent& evt)
{
    if (!process_logic_command_vector(std::move(m_logic->on_loaded_webview_event(into_u8(evt.GetURL())))))
    {
        evt.Veto();
    }
#ifdef DEBUG_URL_PANEL
    m_url->SetLabelText(evt.GetURL());
#endif
}

void WebViewPanel::on_page_will_load()
{
    [[maybe_unused]] bool b = process_logic_command_vector(std::move(m_logic->on_page_will_load_webview_event()));
    DEBUG_ASSERT(b, "False return value signals Veto which cannot be done here.");
}

void WebViewPanel::on_created(wxWebViewEvent& evt)
{
    [[maybe_unused]] bool b = process_logic_command_vector(m_logic->on_webview_created());
    DEBUG_ASSERT(b, "False return value signals Veto which cannot be done here.");
}

void WebViewPanel::on_error(wxWebViewEvent& evt)
{
#define WX_ERROR_CASE(type) \
case type: \
    category = #type; \
    break;

    std::string category;
    switch (evt.GetInt()) {
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_CONNECTION);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_CERTIFICATE);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_AUTH);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_SECURITY);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_NOT_FOUND);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_REQUEST);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_USER_CANCELLED);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_OTHER);
    }

    SPDLOG_ERROR("WebViewDialog error: {}", category);
    load_error_page();
}

void WebViewPanel::on_idle(wxIdleEvent& evt)
{
    if (!m_web_view || m_do_late_webview_create)
        return;

    bool is_busy = m_web_view->IsBusy();

    // The busy cursor on webview is switched off on Linux.
    // Because m_browser->IsBusy() is almost always true on Printables / Connect.
#ifndef __linux__
    if (m_shown) {
        // Use window-specific SetCursor instead of global wxSetCursor to allow parent frame resizing
        if (is_busy && !m_busy_cursor_set) {
            this->SetCursor(wxCURSOR_ARROWWAIT);
            m_busy_cursor_set = true;
            SPDLOG_INFO("busy cursor");
        } else if (!is_busy && m_busy_cursor_set) {
            this->SetCursor(wxNullCursor);
            m_busy_cursor_set = false;
            SPDLOG_INFO("normal cursor");
        }
    }
#endif // !__linux__

    if (m_shown && m_load_error_page && !is_busy) {
        m_load_error_page = false;
        if (m_load_default_url_on_next_error) {
            m_load_default_url_on_next_error = false;
            load_default_url();
        } else {
            load_url(format_wxstr(
                "file://%1%/web/%2%%3%.html",
                boost::filesystem::path(resources_dir()).generic_string(),
                m_logic->error_html(),
                w_config()->dark_mode() ? "_dark" : ""
            ));
            // This is a fix of broken message handling after error.
            // F.e. if there is an error but we do AddUserScript & Reload, the handling will break.
            // So we just reset the handler here.
            if (!m_script_message_hadler_names.empty()) {
                m_web_view->RemoveScriptMessageHandler(from_u8(m_script_message_hadler_names.front()));
                m_web_view->AddScriptMessageHandler(from_u8(m_script_message_hadler_names.front()));
            }
        }
    }

#ifdef DEBUG_URL_PANEL
    m_button_stop->Enable(is_busy);
#endif
}

void WebViewPanel::on_url(wxCommandEvent& evt)
{
    if (!m_web_view)
        return;
#ifdef DEBUG_URL_PANEL
    m_web_view->LoadURL(m_url->GetValue());
    m_web_view->SetFocus();
#endif
}

void WebViewPanel::on_back_button(wxCommandEvent& evt)
{
    if (!m_web_view)
        return;
    if (!m_web_view->CanGoBack())
        return;
    m_web_view->GoBack();
}

void WebViewPanel::on_forward_button(wxCommandEvent& evt)
{
    if (!m_web_view)
        return;
    if (!m_web_view->CanGoForward())
        return;
    m_web_view->GoForward();
}

void WebViewPanel::on_stop_button(wxCommandEvent& evt)
{
    if (!m_web_view)
        return;
    m_web_view->Stop();
}

void WebViewPanel::on_reload_button(wxCommandEvent& evt)
{
    if (!m_web_view)
        return;
    m_web_view->Reload();
}

void WebViewPanel::on_view_text_request(wxCommandEvent& evt)
{
    if (!m_web_view)
        return;

    wxDialog textViewDialog(this, wxID_ANY, L"Page Text", wxDefaultPosition, wxSize(700, 500), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);

    wxTextCtrl* text = new wxTextCtrl(this, wxID_ANY, m_web_view->GetPageText(), wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_RICH | wxTE_READONLY);

    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(text, 1, wxEXPAND);
    SetSizer(sizer);
    textViewDialog.ShowModal();
}

void WebViewPanel::on_tools_clicked(wxCommandEvent& evt)
{
    if (!m_web_view)
        return;

#ifdef DEBUG_URL_PANEL
    m_context_menu->Check(m_web_view->IsContextMenuEnabled());
    m_dev_tools->Check(m_web_view->IsAccessToDevToolsEnabled());

    wxPoint position = ScreenToClient(wxGetMousePosition());
    PopupMenu(m_tools_menu, position.x, position.y);
#endif
}

void WebViewPanel::on_run_script_custom(wxCommandEvent& evt)
{
    wxTextEntryDialog dialog(this, L"Please enter JavaScript code to execute", from_u8(wxGetTextFromUserPromptStr), L"", wxOK | wxCANCEL | wxCENTRE | wxTE_MULTILINE);
    if (dialog.ShowModal() != wxID_OK)
        return;

    run_script(dialog.GetValue());
}

void WebViewPanel::on_add_user_script(wxCommandEvent& evt)
{
    if (!m_web_view) {
        return;
    }
    wxString userScript;
    wxTextEntryDialog dialog(this, L"Enter the JavaScript code to run as the initialization script that runs before any script in the HTML document.", from_u8(wxGetTextFromUserPromptStr), userScript, wxOK | wxCANCEL | wxCENTRE | wxTE_MULTILINE);
    if (dialog.ShowModal() != wxID_OK)
        return;

    const wxString& javascript = dialog.GetValue();
    if (!m_web_view->AddUserScript(javascript))
        SPDLOG_ERROR("Could not add user script");
}

void WebViewPanel::on_set_custom_user_agent(wxCommandEvent& evt)
{
    if (!m_web_view)
        return;

    wxString customUserAgent;
    wxTextEntryDialog dialog(this, L"Enter the custom user agent string you would like to use.", from_u8(wxGetTextFromUserPromptStr), customUserAgent, wxOK | wxCANCEL | wxCENTRE);
    if (dialog.ShowModal() != wxID_OK)
        return;

    if (!m_web_view->SetUserAgent(customUserAgent))
        SPDLOG_ERROR("Could not set custom user agent");
}

void WebViewPanel::on_clear_selection(wxCommandEvent& evt)
{
    if (!m_web_view)
        return;

    m_web_view->ClearSelection();
}

void WebViewPanel::on_delete_selection(wxCommandEvent& evt)
{
    if (!m_web_view)
        return;

    m_web_view->DeleteSelection();
}

void WebViewPanel::on_select_all(wxCommandEvent& evt)
{
    if (!m_web_view)
        return;

    m_web_view->SelectAll();
}

void WebViewPanel::on_enable_context_menu(wxCommandEvent& evt)
{
    if (!m_web_view)
        return;

    m_web_view->EnableContextMenu(evt.IsChecked());
}

void WebViewPanel::on_enable_dev_tools(wxCommandEvent& evt)
{
    if (!m_web_view)
        return;

    m_web_view->EnableAccessToDevTools(evt.IsChecked());
}

bool WebViewPanel::handle_logic_command_LoadURL(const std::string& data)
{
    load_url(from_u8(data));
    return true;
}

bool WebViewPanel::handle_logic_command_LoadRequest(const std::string& data)
{
    load_request(m_web_view, data, m_logic->access_token());
    return true;
}

bool WebViewPanel::handle_logic_command_RunScript(const std::string& data)
{
    run_script(from_u8(data));
    return true;
}

bool WebViewPanel::handle_logic_command_EndModalOK(const std::string& data)
{
    DEBUG_ASSERT(false, "EndModal not supported for WebViewTab.");
    return true;
}

bool WebViewPanel::handle_logic_command_EndModalCancel(const std::string& data)
{
    DEBUG_ASSERT(false, "EndModal not supported for WebViewTab.");
    return true;
}

bool WebViewPanel::handle_logic_command_DeleteCookies(const std::string& data)
{
    delete_cookies(m_web_view, data);
    return true;
}

bool WebViewPanel::handle_logic_command_DeleteCookiesWithCounter(const std::string& data)
{
    DEBUG_ASSERT(false, "DeleteCookiesWithCounter not supported for WebViewTab.");
    return true;
}

bool WebViewPanel::handle_logic_command_Veto(const std::string& data)
{
    // veto is passed up as return value. Not all events can Veto.
    return false;
}

bool WebViewPanel::handle_logic_command_DoReload(const std::string& data)
{
    do_reload();
    return true;
}

bool WebViewPanel::handle_logic_command_AddUserScript(const std::string& data)
{
    m_web_view->AddUserScript(from_u8(data));
    return true;
}

bool WebViewPanel::handle_logic_command_LoadResourcesPage(const std::string& data)
{
    load_url(format_wxstr(
        "file://%1%/web/%2%%3%.html",
        boost::filesystem::path(resources_dir()).generic_string(),
        data,
        w_config()->dark_mode() ? "_dark" : ""
    ));
    return true;
}

bool WebViewPanel::handle_logic_command_OpenExternalBrowser(const std::string& data)
{
    wxLaunchDefaultBrowser(from_u8(data), 0);
    return true;
}

bool WebViewPanel::handle_logic_command_RegisterPrusaSlicerURL(const std::string& data)
{
    register_prusaslicer_url();
    return true;
}

bool WebViewPanel::handle_logic_command_SetLoadDefaultURLOnErrorTrue(const std::string& data)
{
    m_load_default_url_on_next_error = true;
    return true;
}

bool WebViewPanel::handle_logic_command_SetLoadDefaultURLOnErrorFalse(const std::string& data)
{
    m_load_default_url_on_next_error = false;
    return true;
}

bool WebViewPanel::handle_logic_command_SwitchToSlicing(const std::string& data)
{
    if (m_switch_left_tab_fn) {
        m_switch_left_tab_fn(LeftBarTabs::Slicing, {});
    }
    return true;
}

bool WebViewPanel::handle_logic_command_SetBasicAuth(const std::string& data)
{
    std::string usr;
    std::string pwd;
    try {
        nlohmann::json j = nlohmann::json::parse(data);
        usr = j["username"].get<std::string>();
        pwd = j["password"].get<std::string>();
    } catch (const nlohmann::json::exception& e) {
        SPDLOG_ERROR("Could not parse json message. {}", e.what());
        return false;
    }

    setup_webview_with_credentials(m_web_view, usr, pwd);
    return true;
}

bool WebViewPanel::handle_logic_command_ClearBasicAuth(const std::string& data)
{
    remove_webview_credentials(m_web_view);
    return true;
}

} // namespace Slic3r::App::WX::WebView
