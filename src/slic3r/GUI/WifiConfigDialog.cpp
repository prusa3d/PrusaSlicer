#include "WifiConfigDialog.hpp"

#include "GUI_App.hpp"
#include "GUI.hpp"
#include "I18N.hpp"
#include "format.hpp"
#include "RemovableDriveManager.hpp"
#include "MsgDialog.hpp"

#include <wx/stattext.h>
#include <wx/button.h>
#include <boost/nowide/convert.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/log/trivial.hpp>
#include <boost/filesystem.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/nowide/cstdio.hpp>

#include "Widgets/ComboBox.hpp"

namespace Slic3r {
namespace GUI {

const char* WIFI_CONFIGFILE_NAME = "prusa_printer_settings.ini";

WifiConfigDialog::WifiConfigDialog(wxWindow* parent, std::string& file_path, RemovableDriveManager* removable_manager, const wxString& preffered_drive)
    // TRN: This is the dialog title.
     : DPIDialog(parent, wxID_ANY, _L("Wi-Fi Configuration File Generator"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
     , m_wifi_scanner(new WifiScanner())
     , out_file_path(file_path)
     , m_removable_manager(removable_manager)
{
    // Propagation of error in wifi scanner construtor
    if (!m_wifi_scanner->is_init()) {
        // TRN Error dialog of configuration -> wifi configuration file 
        wxString msg = format_wxstr(L"%1%\n\n%2%", _L("Failed to scan wireless networks. Please fill SSID manually."),
#ifdef _WIN32
            // TRN Windows specific second line of error dialog of configuration -> wifi configuration file 
            _L("Library wlanapi.dll was not loaded.")
#else
            ""
#endif // _WIN32
            );
        show_error(this, msg);
    }
    wxPanel* panel = new wxPanel(this);
    wxBoxSizer* vsizer = new wxBoxSizer(wxVERTICAL);
    panel->SetSizer(vsizer);

    // TRN Wifi config dialog explanation line 1.
    wxStaticText* explain_label1 = new wxStaticText(panel, wxID_ANY, _L("Generate a file to be loaded by a Prusa printer to configure its Wi-Fi connection."));
    // TRN Wifi config dialog explanation line 2.
    wxStaticText* explain_label2 = new wxStaticText(panel, wxID_ANY, GUI::format_wxstr(_L("Write this file on the USB flash drive. Its name will be %1%."), WIFI_CONFIGFILE_NAME));
    // TRN Wifi config dialog explanation line 3.
    wxStaticText* explain_label3 = new wxStaticText(panel, wxID_ANY, _L("Your Prusa printer should load this file automatically."));
    // TRN Wifi config dialog explanation line 4.
    wxStaticText* explain_label4 = new wxStaticText(panel, wxID_ANY, _L("Note: This file will contain the SSID and password in plain text."));

    auto* ssid_sizer = new wxBoxSizer(wxHORIZONTAL);
    // TRN SSID of WiFi network. It is a standard abbreviation which should probably not change in most languages.
    wxStaticText* ssid_label = new wxStaticText(panel, wxID_ANY, GUI::format_wxstr("%1%:", _L("SSID")));
    m_ssid_combo = new ::ComboBox(panel, wxID_ANY, wxString(""), wxDefaultPosition, wxDefaultSize, 0, nullptr, DD_NO_CHECK_ICON);
#if __APPLE__
    m_ssid_combo->SetToolTip(_L("On some versions of MacOS, this only loads SSID of connected network."));
#endif // __APPLE__
    rescan_networks(false);
    m_ssid_button_id = NewControlId();
    // TRN Text of button to rescan visible networks in Wifi Config dialog.
    wxButton* ssid_button = new wxButton(panel, m_ssid_button_id, _(L("Rescan")));
    ssid_sizer->Add(m_ssid_combo, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
    ssid_sizer->Add(ssid_button, 0);

    auto* pass_sizer = new wxBoxSizer(wxHORIZONTAL);
    // TRN Password of WiFi network.
    wxStaticText* password_label = new wxStaticText(panel, wxID_ANY, GUI::format_wxstr("%1%:", _L("Password")));
    m_pass_textctrl = new ::TextInput(panel, "", "", "", wxDefaultPosition, wxDefaultSize);
#if __APPLE__
    pass_sizer->Add(m_pass_textctrl, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
    m_pass_button_id = NewControlId();
    // TRN Text of button to retrieve password from keychain in Wifi Config dialog. Only on Mac.
    wxButton* pass_button = new wxButton(panel, m_pass_button_id, _(L("Retrieve")));
    pass_sizer->Add(pass_button, 0);
    pass_button->Bind(wxEVT_BUTTON, &WifiConfigDialog::on_retrieve_password, this);
#else
    pass_sizer->Add(m_pass_textctrl, 1, wxALIGN_CENTER_VERTICAL, 10);
#endif // __APPLE__
    // show password if current ssid was selected already
    fill_password();

    auto* drive_sizer = new wxBoxSizer(wxHORIZONTAL);
    // TRN description of Combo Box with path to USB drive.
    wxStaticText* drive_label = new wxStaticText(panel, wxID_ANY, GUI::format_wxstr("%1%:", _L("Drive")));
    m_drive_combo = new ::ComboBox(panel, wxID_ANY, wxString(""), wxDefaultPosition, wxDefaultSize, 0, nullptr, DD_NO_CHECK_ICON);
    rescan_drives(preffered_drive);
    m_drive_button_id = NewControlId();
    // TRN Text of button to rescan connect usb drives in Wifi Config dialog.
    wxButton* drive_button = new wxButton(panel, m_drive_button_id, _(L("Rescan")));
    drive_sizer->Add(m_drive_combo, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
    drive_sizer->Add(drive_button, 0);

    // TRN Text of button to write config file in Wifi Config dialog.
    wxButton* ok_button = new wxButton(panel, wxID_OK, _L("Write"));
    wxButton* cancel_button = new wxButton(panel, wxID_CANCEL);

    auto* grid = new wxFlexGridSizer(2, 15, 15);
    grid->AddGrowableCol(1);

    grid->Add(ssid_label, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(ssid_sizer, 0, wxEXPAND);

    grid->Add(password_label, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(pass_sizer, 0, wxEXPAND);

    grid->Add(drive_label, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(drive_sizer, 0, wxEXPAND);

    vsizer->Add(explain_label1, 0, wxALIGN_CENTER_VERTICAL);
    vsizer->Add(explain_label2, 0, wxALIGN_CENTER_VERTICAL);
    vsizer->Add(explain_label3, 0, wxALIGN_CENTER_VERTICAL);
    vsizer->Add(explain_label4, 0, wxALIGN_CENTER_VERTICAL);
    vsizer->Add(grid, 0, wxEXPAND | wxTOP | wxBOTTOM, 15);

    wxBoxSizer* buttons_sizer = new wxBoxSizer(wxHORIZONTAL);
    buttons_sizer->Add(ok_button, 1, wxLEFT);
    buttons_sizer->AddStretchSpacer();
    buttons_sizer->Add(cancel_button, 1, wxRIGHT);

    vsizer->Add(buttons_sizer, 0, wxEXPAND);

    wxBoxSizer* topsizer = new wxBoxSizer(wxVERTICAL);
    topsizer->Add(panel, 1, wxEXPAND | wxALL, 15);
    SetSizerAndFit(topsizer);

    ok_button->Bind(wxEVT_BUTTON, &WifiConfigDialog::on_ok, this);
    m_ssid_combo->Bind(wxEVT_TEXT, &WifiConfigDialog::on_combo, this);
    drive_button->Bind(wxEVT_BUTTON, &WifiConfigDialog::on_rescan_drives, this);
    ssid_button->Bind(wxEVT_BUTTON, &WifiConfigDialog::on_rescan_networks, this);

    wxGetApp().UpdateDlgDarkUI(this);
}

WifiConfigDialog::~WifiConfigDialog()
{
    if (m_wifi_scanner)
        delete m_wifi_scanner;
}

void WifiConfigDialog::on_combo(wxCommandEvent& e)
{
    fill_password();
}

void WifiConfigDialog::fill_password()
{
    assert(m_ssid_combo && m_pass_textctrl && m_wifi_scanner);
    if (auto it = m_wifi_scanner->get_map().find(m_ssid_combo->GetValue()); it != m_wifi_scanner->get_map().end())
        m_pass_textctrl->SetValue(boost::nowide::widen(it->second));
}

void WifiConfigDialog::on_retrieve_password(wxCommandEvent& e)
{
    if (m_ssid_combo->GetValue().empty()) {
        return;
    }
    
    std::string psk = m_wifi_scanner->get_psk(into_u8(m_ssid_combo->GetValue()));
    if (psk.empty()) {
        // TRN Alert message when retrieving password for wifi from keychain. Probably will display only on Apple so keychain is MacOS term.
        wxString msg = _L("No password in the keychain for given SSID.");
        show_info(nullptr, msg);
        return;
    }
    m_pass_textctrl->SetValue(boost::nowide::widen(psk));
}

void WifiConfigDialog::on_rescan_drives(wxCommandEvent& e)
{
    rescan_drives({});
}

void WifiConfigDialog::rescan_drives(const wxString& preffered_drive)
{
    assert(m_drive_combo && m_removable_manager);
    m_drive_combo->Clear();
    std::vector<DriveData> ddata = m_removable_manager->get_drive_list();
    for (const auto& data : ddata) {
        wxString item = boost::nowide::widen(data.path);
        m_drive_combo->Append(item);
        if (preffered_drive == item)
            m_ssid_combo->Select(m_ssid_combo->GetCount() - 1);
    }
    if (m_drive_combo->GetSelection() == -1 && m_drive_combo->GetCount() > 0) {
        m_drive_combo->Select(0);
    }
}

void WifiConfigDialog::on_rescan_networks(wxCommandEvent& e)
{
    rescan_networks(true);
}

void WifiConfigDialog::rescan_networks(bool select)
{
    assert(m_ssid_combo && m_wifi_scanner);
    // Do not do anything if scanner is in faulty state (which should has been propageted in constructor call)
    if (!m_wifi_scanner->is_init())
        return;
    m_wifi_scanner->scan();
    std::string current = m_wifi_scanner->get_current_ssid();
    const auto& map = m_wifi_scanner->get_map();
    m_ssid_combo->Clear();
    for (const auto& pair : map) {
        m_ssid_combo->Append(pair.first);
        // select ssid of current network (if connected)
        if (current == pair.first)
            m_ssid_combo->Select(m_ssid_combo->GetCount() - 1);
    }
    if (m_ssid_combo->GetSelection() == -1 && m_ssid_combo->GetCount() > 0)
        m_ssid_combo->Select(0);
    if (select && m_ssid_combo->GetSelection() != -1)
        fill_password();
}

void WifiConfigDialog::on_ok(wxCommandEvent& e)
{
    assert(m_ssid_combo && m_pass_textctrl);
    if (m_ssid_combo->GetValue().empty()) {
        // TRN Alert message when writing WiFi configuration file to usb drive.
        wxString msg = _L("SSID field is empty.");
        show_info(nullptr, msg);
        return;
    }
   
    std::string selected_path = into_u8(m_drive_combo->GetValue());

    if (selected_path.empty()) {
        // TRN Alert message when writing WiFi configuration file to usb drive.
        wxString msg = _L("Drive field is empty.");
        show_info(nullptr, msg);
        return;
    }

    boost::filesystem::path file_path = boost::filesystem::path(selected_path) / WIFI_CONFIGFILE_NAME;

    bool path_on_removable_media = m_removable_manager->set_and_verify_last_save_path(file_path.string());
    if (!path_on_removable_media) {
        // TRN Alert message when writing WiFi configuration file to usb drive.
        wxString msg = _L("Selected path is not on removable media.");
        show_info(nullptr, msg);
        return;
    }

    if (boost::filesystem::exists(file_path)) {
        // TRN placeholder 1 is path to file
        wxString msg_text = GUI::format_wxstr(_L("%1% already exists. Do you want to rewrite it?\n(Other items than Wi-Fi credentials will stay unchanged)"), file_path.string());
        WarningDialog dialog(m_parent, msg_text, _L("Warning"), wxYES | wxNO);
        if (dialog.ShowModal() == wxID_NO) {
            return;
        }
    }

    std::string data;
    namespace pt = boost::property_tree;
    pt::ptree tree;
    // File already exist and we only need to add data to it rather than rewrite it.
    if (boost::filesystem::exists(file_path)) {
        
        boost::nowide::ifstream ifs(file_path.string());
        try {
            pt::read_ini(ifs, tree);
        }
        catch (const boost::property_tree::ini_parser::ini_parser_error& err) {
            throw Slic3r::RuntimeError(format("Failed loading ini file \"%1%\"\nError: \"%2%\" at line %3%", file_path, err.message(), err.line()).c_str());
        }

        if (auto sub = tree.get_optional<std::string>("wifi"); sub) {
            tree.erase("wifi");
        } 
    }

    pt::ptree subtree;
    subtree.put("ssid", m_ssid_combo->GetValue().utf8_string());
    subtree.put("psk", m_pass_textctrl->GetValue().utf8_string());
    tree.add_child("wifi", subtree);

    data.clear();
    // manually write ini string (so there is extra line between sections)
    for (const auto& section : tree) {
        data += "[" + section.first + "]" + "\n";
        for (const auto& entry : section.second) {
            data += entry.first + " = " + entry.second.get_value<std::string>() + "\n";
        }
        data += "\n";
    }

    m_used_path = boost::nowide::widen(file_path.string());
    FILE* file;
    file = boost::nowide::fopen(file_path.string().c_str(), "w");
    if (file == NULL) {
        BOOST_LOG_TRIVIAL(error) << "Failed to write to file " << file_path;
        // TODO show error
        show_error(nullptr,  _L("Failed to open file for writing."));
        return;
    }
    fwrite(data.c_str(), 1, data.size(), file);
    fclose(file);
    out_file_path = file_path.string();
    this->EndModal(wxID_OK);
}

void WifiConfigDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    SetFont(wxGetApp().normal_font());

    const int em = em_unit();
    msw_buttons_rescale(this, em, { wxID_OK, wxID_CANCEL, m_ssid_button_id, m_pass_button_id, m_drive_button_id });

    Fit();
    Refresh();
}
}}// Slicer::GUI
