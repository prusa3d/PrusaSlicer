#include "Slic3r/App/WX/WebView/WebViewDialog.hpp"

#include "WebViewPlatformUtils.hpp"

#include "Slic3r/App/WX/WebView/WebView.hpp"
#include "Slic3r/App/WX/StringConversions.hpp"
#include "Slic3r/App/WX/I18N.hpp"
#include "Slic3r/Biz/Network/ServiceConfig.hpp"
#include "Slic3r/App/WX/format.hpp"
#include "Slic3r/App/WX/WidgetsConfig.hpp"

#include "Slic3r/Assert.hpp"
#include "Slic3r/Log.hpp"
#include "Slic3r/Directories.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textdlg.h>
#include <wx/display.h>

namespace Slic3r::App::WX::WebView {

WebViewDialog::WebViewDialog(std::unique_ptr<App::Browser::AbstractBrowserLogic>&& logic) :
    AbstractWebViewDialog(
        nullptr,
        wxID_ANY,
        from_u8(logic->title()),
        wxDefaultPosition,
        wxSize(logic->size(w_config()->em_unit()).first, logic->size(w_config()->em_unit()).second),
        wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER
    ),
    m_logic(std::move(logic)),
    m_web_view(WebView::web_view_new())
{
    ASSERT(m_logic);
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
    
    auto [req_w, req_h] = m_logic->size(w_config()->em_unit());
    auto [min_w, min_h] = m_logic->min_size(w_config()->em_unit());

    wxRect client_area = wxDisplay(wxDisplay::GetFromWindow(this)).GetClientArea();
    if (client_area.IsEmpty()) {
        // Fallback if display detection fails
        int scr_w, scr_h;
        wxDisplaySize(&scr_w, &scr_h);
        client_area = wxRect(0, 0, scr_w, scr_h);
    }

    int max_safe_w = static_cast<int>(client_area.GetWidth() * 0.90);
    int max_safe_h = static_cast<int>(client_area.GetHeight() * 0.85);
    int final_w = std::min(req_w, max_safe_w);
    int final_h = std::min(req_h, max_safe_h);
    min_w = std::min(min_w, final_w);
    min_h = std::min(min_h, final_h);

    SetMinSize(wxSize(min_w, min_h));
    SetSize(wxSize(final_w, final_h));
    SetSizer(topsizer);

    this->CenterOnParent();

    // Create the webview
    if (!m_web_view) {
        wxStaticText* text = new wxStaticText(this, wxID_ANY, _L("Failed to load a web browser."));
        topsizer->Add(text, 0, wxALIGN_LEFT | wxBOTTOM, 10);
        return;
    }
    if (!m_logic->url().empty()) {
        WebView::web_view_create(m_web_view, this, from_u8(m_logic->url()), m_logic->script_message_handler_names());
    } else {
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
    }

    if (Biz::Network::ServiceConfig::instance().webdev_enabled()) {
        setup_webview_devtools(m_web_view);
    }

    topsizer->Add(m_web_view, wxSizerFlags().Expand().Proportion(1));

#ifdef DEBUG_URL_PANEL
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

#endif

    Bind(wxEVT_SHOW, &WebViewDialog::on_show, this);
    Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &WebViewDialog::on_script_message, this, m_web_view->GetId());

    // Connect the webview events
    Bind(wxEVT_WEBVIEW_ERROR, &WebViewDialog::on_error, this, m_web_view->GetId());
    // Connect the idle events
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
    Bind(wxEVT_MENU, &WebViewDialog::on_view_text_request, this, viewText->GetId());
    Bind(wxEVT_MENU, &WebViewDialog::On_enable_context_menu, this, m_context_menu->GetId());
    Bind(wxEVT_MENU, &WebViewDialog::On_enable_dev_tools, this, m_dev_tools->GetId());

    Bind(wxEVT_MENU, &WebViewDialog::on_run_script_custom, this, m_script_custom->GetId());
    Bind(wxEVT_MENU, &WebViewDialog::on_add_user_script, this, addUserScript->GetId());
#endif
    Bind(wxEVT_WEBVIEW_NAVIGATING, &WebViewDialog::on_navigation_request, this, m_web_view->GetId());
    Bind(wxEVT_WEBVIEW_LOADED, &WebViewDialog::on_loaded, this, m_web_view->GetId());

    Bind(wxEVT_CLOSE_WINDOW, ([this](wxCloseEvent& evt) { EndModal(wxID_CANCEL); }));

#ifdef DEBUG_URL_PANEL
    m_url->SetLabelText(url);
#endif

    [[maybe_unused]] bool b = process_logic_command_vector(m_logic->on_webview_created());
    DEBUG_ASSERT(b, "Cant veto in non event callback function.");
}

constexpr bool is_linux =
#if defined(__linux__)
    true;
#else
    false;
#endif

void WebViewDialog::on_idle(wxIdleEvent& WXUNUSED(evt))
{
    if (!m_web_view)
        return;

    bool is_busy = m_web_view->IsBusy();

    if constexpr (!is_linux) {
        if (is_busy && !m_busy_cursor_set) {
            wxSetCursor(wxCURSOR_ARROWWAIT);
            m_busy_cursor_set = true;
        } else if (!is_busy && m_busy_cursor_set) {
            wxSetCursor(wxNullCursor);
            m_busy_cursor_set = false;
        }
    }

    if (!is_busy) {
        if (m_load_error_page) {
            m_load_error_page = false;
            m_web_view->LoadURL(format_wxstr(
                "file://%1%/web/error_no_reload%2%.html",
                boost::filesystem::path(resources_dir()).generic_string(),
                w_config()->dark_mode() ? "_dark" : ""
            ));
        }
        if (m_waiting_for_counters && m_atomic_counter == m_counter_to_match) {
            EndModal(wxID_OK);
        }
        if (m_force_close) {
            EndModal(wxID_OK);
        }
    }
    
#ifdef DEBUG_URL_PANEL
    m_button_stop->Enable(is_busy);
#endif
}

void WebViewDialog::on_url(wxCommandEvent& WXUNUSED(evt))
{
    if (!m_web_view)
        return;
#ifdef DEBUG_URL_PANEL
    m_web_view->LoadURL(m_url->GetValue());
    m_web_view->SetFocus();
#endif
}

void WebViewDialog::on_back_button(wxCommandEvent& WXUNUSED(evt))
{
    if (!m_web_view)
        return;
    m_web_view->GoBack();
}

void WebViewDialog::on_forward_button(wxCommandEvent& WXUNUSED(evt))
{
    if (!m_web_view)
        return;
    m_web_view->GoForward();
}

void WebViewDialog::on_stop_button(wxCommandEvent& WXUNUSED(evt))
{
    if (!m_web_view)
        return;
    m_web_view->Stop();
}

void WebViewDialog::on_reload_button(wxCommandEvent& WXUNUSED(evt))
{
    if (!m_web_view)
        return;
    m_web_view->Reload();
}

void WebViewDialog::on_navigation_request(wxWebViewEvent& evt)
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

void WebViewDialog::on_loaded(wxWebViewEvent& evt)
{
    if (!process_logic_command_vector(std::move(m_logic->on_loaded_webview_event(into_u8(evt.GetURL())))))
    {
        evt.Veto();
    }
}

void WebViewDialog::on_script_message(wxWebViewEvent& evt)
{
    if (!process_logic_command_vector(std::move(m_logic->on_script_message_webview_event(into_u8(evt.GetString())))))
    {
        ;
        evt.Veto();
    }
}

void WebViewDialog::on_show(wxShowEvent& evt)
{
    [[maybe_unused]] bool b = process_logic_command_vector(std::move(m_logic->on_show_webview_event(evt.IsShown())));
    DEBUG_ASSERT(b, "Can't veto wxShowEvent.");
}

void WebViewDialog::on_view_text_request(wxCommandEvent& WXUNUSED(evt))
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

void WebViewDialog::on_tools_clicked(wxCommandEvent& WXUNUSED(evt))
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

void WebViewDialog::on_run_script_custom(wxCommandEvent& WXUNUSED(evt))
{
    wxTextEntryDialog dialog(this, L"Please enter JavaScript code to execute", from_u8(wxGetTextFromUserPromptStr), L"", wxOK | wxCANCEL | wxCENTRE | wxTE_MULTILINE);
    if (dialog.ShowModal() != wxID_OK)
        return;

    run_script(dialog.GetValue());
}

void WebViewDialog::on_add_user_script(wxCommandEvent& WXUNUSED(evt))
{
    wxString userScript = L"window.wx_test_var = 'wxWidgets webview sample';";
    wxTextEntryDialog dialog(this, L"Enter the JavaScript code to run as the initialization script that runs before any script in the HTML document.", from_u8(wxGetTextFromUserPromptStr), userScript, wxOK | wxCANCEL | wxCENTRE | wxTE_MULTILINE);
    if (dialog.ShowModal() != wxID_OK)
        return;

    const wxString& javascript = dialog.GetValue();

    if (!m_web_view->AddUserScript(javascript))
        SPDLOG_ERROR("Could not add user script");
}

void WebViewDialog::on_set_custom_user_agent(wxCommandEvent& WXUNUSED(evt))
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

void WebViewDialog::on_clear_selection(wxCommandEvent& WXUNUSED(evt))
{
    if (!m_web_view)
        return;

    m_web_view->ClearSelection();
}

void WebViewDialog::on_delete_selection(wxCommandEvent& WXUNUSED(evt))
{
    if (!m_web_view)
        return;

    m_web_view->DeleteSelection();
}

void WebViewDialog::on_select_all(wxCommandEvent& WXUNUSED(evt))
{
    if (!m_web_view)
        return;

    m_web_view->SelectAll();
}

void WebViewDialog::On_enable_context_menu(wxCommandEvent& evt)
{
    if (!m_web_view)
        return;

    m_web_view->EnableContextMenu(evt.IsChecked());
}

void WebViewDialog::On_enable_dev_tools(wxCommandEvent& evt)
{
    if (!m_web_view)
        return;

    m_web_view->EnableAccessToDevTools(evt.IsChecked());
}

void WebViewDialog::on_error(wxWebViewEvent& evt)
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

void WebViewDialog::load_error_page()
{
    if (!m_web_view)
        return;

    m_web_view->Stop();
    m_load_error_page = true;
}

void WebViewDialog::run_script(const wxString& javascript)
{
    if (!m_web_view)
        return;

    //SPDLOG_INFO("RunScript {}\n", into_u8(javascript));
    m_web_view->RunScriptAsync(javascript);
}

void WebViewDialog::EndModal(int retCode)
{
    if (m_web_view) {
        for (const std::string& handler : m_logic->script_message_handler_names()) {
            m_web_view->RemoveScriptMessageHandler(from_u8(handler));
        }
    }

    wxDialog::EndModal(retCode);
}

void WebViewDialog::do_reload()
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
    m_web_view->LoadURL(from_u8(m_logic->url()));
}

void WebViewDialog::on_user_account_id_success(bool is_refresh, const std::string& username)
{
    [[maybe_unused]] bool r = process_logic_command_vector(m_logic->on_user_account_id_success(is_refresh, into_u8(m_web_view->GetCurrentURL())));
    DEBUG_ASSERT(r, "False return value signals Veto which cannot be done here.");
}

void WebViewDialog::on_user_account_logged_out()
{
    [[maybe_unused]] bool r = process_logic_command_vector(m_logic->on_user_account_logged_out(into_u8(m_web_view->GetCurrentURL())));
    DEBUG_ASSERT(r, "False return value signals Veto which cannot be done here.");
}

void WebViewDialog::on_user_account_will_refresh()
{
    [[maybe_unused]] bool r = process_logic_command_vector(m_logic->on_user_account_will_refresh());
    DEBUG_ASSERT(r, "False return value signals Veto which cannot be done here.");
}

bool WebViewDialog::handle_logic_command_LoadURL(const std::string& data)
{
    m_web_view->LoadURL(from_u8(data));
    return true;
}

bool WebViewDialog::handle_logic_command_LoadRequest(const std::string& data)
{
    load_request(m_web_view, data, m_logic->access_token());
    return true;
}

bool WebViewDialog::handle_logic_command_RunScript(const std::string& data)
{
    run_script(from_u8(data));
    return true;
}

bool WebViewDialog::handle_logic_command_EndModalOK(const std::string& data)
{
    EndModal(wxID_OK);
    return true;
}

bool WebViewDialog::handle_logic_command_EndModalCancel(const std::string& data)
{
    EndModal(wxID_CANCEL);
    return true;
}

bool WebViewDialog::handle_logic_command_DeleteCookies(const std::string& data)
{
    delete_cookies(m_web_view, data);
    return true;
}

bool WebViewDialog::handle_logic_command_DeleteCookiesWithCounter(const std::string& data)
{
    m_waiting_for_counters = true;
    m_atomic_counter       = 0;
    m_counter_to_match++;
    delete_cookies_with_counter(m_web_view, data, m_atomic_counter);
    return true;
}

bool WebViewDialog::handle_logic_command_Veto(const std::string& data)
{
    // veto is passed up as return value. Not all events can Veto.
    return false;
}

bool WebViewDialog::handle_logic_command_DoReload(const std::string& data)
{
    do_reload();
    return true;
}

bool WebViewDialog::handle_logic_command_AddUserScript(const std::string& data)
{
    m_web_view->AddUserScript(from_u8(data));
    return true;
}

bool WebViewDialog::handle_logic_command_LoadResourcesPage(const std::string& data)
{
    m_web_view->LoadURL(format_wxstr(
        "file://%1%/web/%2%%3%.html",
        boost::filesystem::path(resources_dir()).generic_string(),
        data,
        w_config()->dark_mode() ? "_dark" : ""
    ));
    return true;
}

bool WebViewDialog::handle_logic_command_OpenExternalBrowser(const std::string& data)
{
    wxLaunchDefaultBrowser(from_u8(data), 0);
    return true;
}

bool WebViewDialog::handle_logic_command_RegisterPrusaSlicerURL(const std::string& data)
{
    register_prusaslicer_url();
    return true;
}

bool WebViewDialog::handle_logic_command_SetLoadDefaultURLOnErrorTrue(const std::string& data)
{
    DEBUG_ASSERT(false, "Command not implmented on WebViewDialog.");
    return true;
}

bool WebViewDialog::handle_logic_command_SetLoadDefaultURLOnErrorFalse(const std::string& data)
{
    DEBUG_ASSERT(false, "Command not implmented on WebViewDialog.");
    return true;
}

bool WebViewDialog::handle_logic_command_SwitchToSlicing(const std::string& data)
{
    DEBUG_ASSERT(false, "Command not implmented on WebViewDialog.");
    return true;
}

bool WebViewDialog::handle_logic_command_SetBasicAuth(const std::string& data)
{
    return false;
}

bool WebViewDialog::handle_logic_command_ClearBasicAuth(const std::string& data)
{
    return false;
}

} // namespace Slic3r::App::WX::WebView
