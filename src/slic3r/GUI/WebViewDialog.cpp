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
#include <wx/display.h>

#include <boost/log/trivial.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

wxDEFINE_EVENT(EVT_OPEN_EXTERNAL_LOGIN, wxCommandEvent);

namespace pt = boost::property_tree;

namespace Slic3r {
namespace GUI {

WebViewDialog::WebViewDialog(wxWindow* parent, const wxString& url, const wxString& dialog_name, const wxSize& size, const std::vector<std::string>& message_handler_names, const std::string& loading_html/* = "other_loading"*/)
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
    m_browser = WebView::webview_new();
    if (!m_browser) {
        wxStaticText* text = new wxStaticText(this, wxID_ANY, _L("Failed to load a web browser."));
        topsizer->Add(text, 0, wxALIGN_LEFT | wxBOTTOM, 10);
        return;
    }
    WebView::webview_create(m_browser, this, url, m_script_message_hadler_names);
    
    if (Utils::ServiceConfig::instance().webdev_enabled()) {
        m_browser->EnableContextMenu();
        m_browser->EnableAccessToDevTools();
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

#ifdef DEBUG_URL_PANEL
    m_url->SetLabelText(url);
#endif
}
WebViewDialog::~WebViewDialog()
{
}

constexpr bool is_linux =
#if defined(__linux__)
true;
#else
false;
#endif

void WebViewDialog::on_idle(wxIdleEvent& WXUNUSED(evt))
{
    if (!m_browser)
        return;
    if (m_browser->IsBusy()) {
       if constexpr (!is_linux) { 
            wxSetCursor(wxCURSOR_ARROWWAIT);
        }
    } else {
        if constexpr (!is_linux) { 
            wxSetCursor(wxNullCursor);
        }
        if (m_load_error_page) {
            m_load_error_page = false;
            m_browser->LoadURL(GUI::format_wxstr("file://%1%/web/error_no_reload.html", boost::filesystem::path(resources_dir()).generic_string()));
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
    BOOST_LOG_TRIVIAL(debug) << "WebViewDialog::on_navigation_request " << evt.GetURL();
}

void WebViewDialog::on_loaded(wxWebViewEvent &evt)
{
    BOOST_LOG_TRIVIAL(debug) << "WebViewDialog::on_loaded " << evt.GetURL();
}


void WebViewDialog::on_script_message(wxWebViewEvent& evt)
{
    BOOST_LOG_TRIVIAL(error) << "Unhandled script message: " << evt.GetString();
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

PrinterPickWebViewDialog::PrinterPickWebViewDialog(wxWindow* parent, std::string& ret_val, bool multiple_beds)
    : WebViewDialog(parent
        , GUI::from_u8(Utils::ServiceConfig::instance().connect_select_printer_url())
        , _L("Choose a printer")
        , wxSize(parent->GetClientSize().x / 4 * 3, parent->GetClientSize().y/ 4 * 3)
        ,{"_prusaSlicer"}
        , "connect_loading")
        , m_multiple_beds(multiple_beds)
    , m_ret_val(ret_val)
{
    
    wxDisplay display(wxDisplay::GetFromWindow(this));
    wxRect geometry = display.GetGeometry();
    SetMinSize(wxSize(geometry.GetWidth() / 2, geometry.GetHeight() / 2));
    Centre();
}

void PrinterPickWebViewDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    wxDisplay display(wxDisplay::GetFromWindow(this));
    wxRect geometry = display.GetGeometry();
    SetMinSize(wxSize(geometry.GetWidth() / 2, geometry.GetHeight() / 2));
    Fit();
    Refresh();
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
    assert(false);
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
    const PresetWithVendorProfile& printer_with_vendor = wxGetApp().preset_bundle->printers.get_preset_with_vendor_profile(selected_printer);
    printer_model_serialized = selected_printer.trim_vendor_repo_prefix(printer_model_serialized, printer_with_vendor.vendor);

    const std::string uuid = wxGetApp().plater()->get_user_account()->get_current_printer_uuid_from_connect(printer_model_serialized);
    const std::string filename = wxGetApp().plater()->get_upload_filename();

    const std::string multiple_beds_value = m_multiple_beds ? "true" : "false";

    std::string request = GUI::format(
        "{"
        "\"printerUuid\": \"%4%\", "
        "\"printerModel\": \"%3%\", "
        "\"nozzle_diameter\": %2%, "
        "\"material\": %1%, "
        "\"filename\": \"%5%\", "
        "\"filament_abrasive\": %6%,"
        "\"high_flow\": %7%,"
        "\"multiple_beds\": %8%"
        "}"
        , filament_type_serialized, nozzle_diameter_serialized, printer_model_serialized, uuid, filename, filament_abrasive_serialized, nozzle_high_flow_serialized, multiple_beds_value);

    wxString script = GUI::format_wxstr("window._prusaConnect_v2.requestCompatiblePrinter(%1%)", request);
    run_script(script);
}
void PrinterPickWebViewDialog::request_compatible_printers_SLA()
{
    const Preset& selected_printer = wxGetApp().preset_bundle->printers.get_selected_preset();
    std::string printer_model_serialized = selected_printer.config.option("printer_model")->serialize();
    
    std::string vendor_repo_prefix;
    const PresetWithVendorProfile& printer_with_vendor = wxGetApp().preset_bundle->printers.get_preset_with_vendor_profile(selected_printer);
    printer_model_serialized = selected_printer.trim_vendor_repo_prefix(printer_model_serialized, printer_with_vendor.vendor);

    const Preset& selected_material = wxGetApp().preset_bundle->sla_materials.get_selected_preset();
    const std::string material_type_serialized = selected_material.config.option("material_type")->serialize();
    const std::string uuid = wxGetApp().plater()->get_user_account()->get_current_printer_uuid_from_connect(printer_model_serialized);
    const std::string filename = wxGetApp().plater()->get_upload_filename();
    const std::string multiple_beds_value = m_multiple_beds ? "true" : "false";
    const std::string request = GUI::format(
        "{"
        "\"printerUuid\": \"%3%\", "
        "\"material\": \"%1%\", "
        "\"printerModel\": \"%2%\", "
        "\"filename\": \"%4%\", "
        "\"multiple_beds\": \"%5%\" "
        "}", material_type_serialized, printer_model_serialized, uuid, filename, multiple_beds_value);

    wxString script = GUI::format_wxstr("window._prusaConnect_v2.requestCompatiblePrinter(%1%)", request);
    run_script(script);
}


void PrinterPickWebViewDialog::on_reload_event(const std::string& message_data)
{
    if (!m_browser) {
        return;
    }
    m_browser->LoadURL(m_default_url);
}


PrintablesConnectUploadDialog::PrintablesConnectUploadDialog(wxWindow* parent, const std::string url)
    : WebViewDialog(parent
    , GUI::from_u8(url)
    , _L("Choose a printer")
    , wxSize(parent->GetClientSize().x / 4 * 3, parent->GetClientSize().y/ 4 * 3)
    ,{"_prusaSlicer"}
    , "connect_loading")     
{
    wxDisplay display(wxDisplay::GetFromWindow(this));
    wxRect geometry = display.GetGeometry();
    SetMinSize(wxSize(geometry.GetWidth() / 2, geometry.GetHeight() / 2));
    Centre();
}

void PrintablesConnectUploadDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    wxDisplay display(wxDisplay::GetFromWindow(this));
    wxRect geometry = display.GetGeometry();
    SetMinSize(wxSize(geometry.GetWidth() / 2, geometry.GetHeight() / 2));
    Fit();
    Refresh();
}

void PrintablesConnectUploadDialog::on_script_message(wxWebViewEvent& evt)
{
    handle_message(into_u8(evt.GetString()));
}

void PrintablesConnectUploadDialog::on_connect_action_select_printer(const std::string& message_data)
{
    // SELECT_PRINTER request is not defined for PrintablesConnectUploadDialog
    assert(false);
}
void PrintablesConnectUploadDialog::on_connect_action_print(const std::string& message_data)
{
     assert(false);
}

void PrintablesConnectUploadDialog::on_connect_action_webapp_ready(const std::string& message_data)
{
    // WEBAPP_READY request is not defined for PrintablesConnectUploadDialog
    assert(false);
}

void PrintablesConnectUploadDialog::on_reload_event(const std::string& message_data)
{
    if (!m_browser) {
        return;
    }
    m_browser->LoadURL(m_default_url);
}

void PrintablesConnectUploadDialog::on_connect_action_close_dialog(const std::string& message_data)
{
    this->EndModal(wxID_OK);
}

LoginWebViewDialog::LoginWebViewDialog(wxWindow *parent, std::string &ret_val, const wxString& url, wxEvtHandler* evt_handler)
    : WebViewDialog(parent, url, _L("Log in dialog"), wxSize(50 * wxGetApp().em_unit(), 80 * wxGetApp().em_unit()), {})
    , m_ret_val(ret_val)
    , p_evt_handler(evt_handler)
{
    m_force_quit_timer.SetOwner(this, 0);
    Bind(wxEVT_TIMER, [this](wxTimerEvent &evt)
    {
        m_force_quit = true;
    });
    Centre();
}
void LoginWebViewDialog::on_navigation_request(wxWebViewEvent &evt)
{
    wxString url = evt.GetURL();
    if (url.starts_with(L"prusaslicer")) {
        m_waiting_for_counters = true;
        m_atomic_counter = 0;
        m_counter_to_match = 4;
        delete_cookies_with_counter(m_browser, Utils::ServiceConfig::instance().account_url(), m_atomic_counter);
        delete_cookies_with_counter(m_browser, "https://accounts.google.com", m_atomic_counter);
        delete_cookies_with_counter(m_browser, "https://appleid.apple.com", m_atomic_counter);
        delete_cookies_with_counter(m_browser, "https://facebook.com", m_atomic_counter);
        evt.Veto();
        m_ret_val = into_u8(url);
        m_force_quit_timer.Start(2000, wxTIMER_ONE_SHOT);
        // End modal is moved to on_idle        
    } else if (url.Find(L"accounts.google.com") != wxNOT_FOUND
        || url.Find(L"appleid.apple.com") != wxNOT_FOUND
        || url.Find(L"facebook.com") != wxNOT_FOUND) {         
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

void LoginWebViewDialog::on_idle(wxIdleEvent& WXUNUSED(evt))
{
    if (!m_browser)
        return;
    if (m_browser->IsBusy()) {
       if constexpr (!is_linux) { 
            wxSetCursor(wxCURSOR_ARROWWAIT);
        }
    } else {
        if constexpr (!is_linux) { 
            wxSetCursor(wxNullCursor);
        }
        if (m_load_error_page) {
            m_load_error_page = false;
            m_browser->LoadURL(GUI::format_wxstr("file://%1%/web/error_no_reload.html", boost::filesystem::path(resources_dir()).generic_string()));
        }
        if (m_waiting_for_counters && m_atomic_counter == m_counter_to_match)
        {
            EndModal(wxID_OK);
        }
        if (m_force_quit)
        {
            EndModal(wxID_OK);
        }
    }
#ifdef DEBUG_URL_PANEL
    m_button_stop->Enable(m_browser->IsBusy());
#endif
}

} // GUI
} // Slic3r
