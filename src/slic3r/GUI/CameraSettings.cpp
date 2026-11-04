// PrusaSlicer is released under the terms of the AGPLv3 or higher.
#include "CameraSettings.hpp"
#include "GUI_App.hpp"
#include "GUI.hpp"
#include "I18N.hpp"
#include <wx/button.h>
#include "Widgets/ComboBox.hpp"
#include "Widgets/TextInput.hpp"
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/uri.h>

namespace Slic3r::GUI {
namespace {
const char* const camera_types[] = {"none", "rtsp", "http", "usb"};
}
CameraSettings::CameraSettings(wxWindow* parent, const DynamicPrintConfig& config)
    : wxPanel(parent), m_saved_device(config.opt_string("camera_device"))
{
    const int gap = FromDIP(8);
    const wxSize field_size(24 * wxGetApp().em_unit(), -1);
    auto* root = new wxBoxSizer(wxVERTICAL);
    // Match the label column used by ConfigOptionsGroup in the printer editor.
    const int label_width = 20 * wxGetApp().em_unit();
    auto add_label = [this, label_width](wxBoxSizer* row, const wxString& text) {
        auto* label = new wxStaticText(this, wxID_ANY, text + ":",
            wxDefaultPosition, wxSize(label_width, -1));
        label->SetFont(wxGetApp().normal_font());
        label->Wrap(label_width);
        row->Add(label, 0, wxALIGN_CENTER_VERTICAL);
    };
    auto* type_row = new wxBoxSizer(wxHORIZONTAL);
    add_label(type_row, _L("Camera type"));
    m_type = new ComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
        field_size, 0, nullptr, wxCB_READONLY | DD_NO_CHECK_ICON);
    m_type->Append(_L("No camera"));
    m_type->Append(_L("RTSP / IP camera"));
    m_type->Append(_L("HTTP / MJPEG stream"));
    m_type->Append(_L("USB / local camera"));
    int selection = 0;
    for (int index = 0; index < 4; ++index) {
        if (config.opt_string("camera_type") == camera_types[index]) selection = index;
    }
    m_type->SetSelection(selection);
    type_row->Add(m_type, 0, wxALIGN_CENTER_VERTICAL);
    root->Add(type_row, 0, wxEXPAND | wxBOTTOM, gap);
    m_url_row = new wxBoxSizer(wxHORIZONTAL);
    add_label(m_url_row, _L("Camera address"));
    m_url = new TextInput(this, from_u8(config.opt_string("camera_url")), "", "",
        wxDefaultPosition, field_size, wxTE_PROCESS_ENTER);
    m_url->SetFont(wxGetApp().normal_font());
    m_url_row->Add(m_url, 0, wxALIGN_CENTER_VERTICAL);
    root->Add(m_url_row, 0, wxEXPAND | wxBOTTOM, gap);
    m_device_row = new wxBoxSizer(wxHORIZONTAL);
    add_label(m_device_row, _L("Local camera"));
    m_device = new ComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
        field_size, 0, nullptr, wxCB_READONLY | DD_NO_CHECK_ICON);
    m_device_row->Add(m_device, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);
    m_refresh = new wxButton(this, wxID_ANY, _L("Refresh"));
    m_device_row->Add(m_refresh, 0, wxALIGN_CENTER_VERTICAL);
    root->Add(m_device_row, 0, wxEXPAND | wxBOTTOM, gap);
    m_help = new wxStaticText(this, wxID_ANY, wxEmptyString);
    root->Add(m_help, 0, wxEXPAND | wxTOP, gap);
    m_error = new wxStaticText(this, wxID_ANY, wxEmptyString);
    root->Add(m_error, 0, wxEXPAND | wxTOP, gap);
    m_error->Hide();
    m_remove = new wxButton(this, wxID_ANY, _L("Remove camera"));
    root->Add(m_remove, 0, wxALIGN_RIGHT | wxTOP, FromDIP(12));
    SetSizer(root);
    m_type->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) { update_controls(); });
    m_refresh->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        const int index = m_device->GetSelection();
        refresh_devices(index >= 0 ? m_devices[index].id : m_saved_device);
    });
    m_remove->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        m_type->SetSelection(0);
        m_url->SetValue(wxEmptyString);
        m_saved_device.clear();
        m_devices.clear();
        m_device->Clear();
        update_controls();
    });
    if (selection == 3) refresh_devices(m_saved_device);
    update_controls();
    sys_color_changed();
}
void CameraSettings::refresh_devices(const std::string& selected)
{
    m_devices = camera_devices();
    m_device->Clear();
    int selection = wxNOT_FOUND;
    for (size_t index = 0; index < m_devices.size(); ++index) {
        m_device->Append(from_u8(m_devices[index].name));
        if (m_devices[index].id == selected) selection = int(index);
    }
    // Preserve a disconnected camera; never silently substitute a different one.
    if (!selected.empty() && selection == wxNOT_FOUND) {
        selection = int(m_devices.size());
        m_devices.push_back({selected, selected});
        m_device->Append(from_u8(selected) + " (" + _L("unavailable") + ")");
    }
    m_device->SetSelection(selection);
    if (m_devices.empty()) m_device->SetToolTip(_L("No local cameras found. Connect a camera and choose Refresh."));
    else m_device->UnsetToolTip();
    Layout();
}
void CameraSettings::update_controls()
{
    const int type = m_type->GetSelection();
    const bool network = type == 1 || type == 2;
    GetSizer()->Show(m_url_row, network, true);
    GetSizer()->Show(m_device_row, type == 3, true);
    if (type == 3 && m_device->IsEmpty()) refresh_devices(m_saved_device);
    m_url->GetTextCtrl()->SetHint(type == 2 ? "http://camera-host/stream" : "rtsp://camera-host/live");
    const wxString help = type == 0 ? _L("No camera is assigned to this physical printer.") :
        type == 1 ? _L("Enter the camera's complete RTSP address, including its stream path.") :
        type == 2 ? _L("Enter the direct HTTP MJPEG stream address, not the camera's web page.") :
        _L("Choose a camera connected to this computer. Only video is captured.");
    m_help->SetLabel(help);
    m_help->Wrap(FromDIP(440));
    m_error->Hide();
    m_remove->Enable(type != 0);
    Layout();
    if (wxWindow* dialog = wxGetTopLevelParent(this)) {
        dialog->Layout();
        dialog->Fit();
    }
}
bool CameraSettings::apply(DynamicPrintConfig& config)
{
    const int type = m_type->GetSelection();
    const wxString address = m_url->GetValue().Strip(wxString::both);
    wxString error;
    if (type == 1 || type == 2) {
        const wxURI uri(address);
        const wxString scheme = type == 1 ? "rtsp" : "http";
        if (uri.GetScheme().CmpNoCase(scheme) != 0 || uri.GetServer().empty())
            error = type == 1 ? _L("Enter a complete rtsp:// camera address.") :
                _L("Enter a direct http:// MJPEG stream address.");
        else if (!uri.GetUserInfo().empty())
            error = _L("Use a camera address without an embedded username or password.");
    } else if (type == 3 && m_device->GetSelection() == wxNOT_FOUND)
        error = _L("Select a local camera, or choose No camera.");
    if (!error.empty()) {
        m_error->SetLabel(error);
        m_error->Wrap(FromDIP(440));
        m_error->Show();
        wxGetTopLevelParent(this)->Fit();
        return false;
    }
    config.opt_string("camera_type") = camera_types[type];
    config.opt_string("camera_url") = type == 1 || type == 2 ? into_u8(address) : "";
    config.opt_string("camera_device") = type == 3 ? m_devices[m_device->GetSelection()].id : "";
    return true;
}
void CameraSettings::sys_color_changed()
{
    wxGetApp().UpdateDarkUI(this);
    for (wxWindow* control : std::vector<wxWindow*>{m_type, m_url, m_device, m_refresh, m_remove})
        wxGetApp().UpdateDarkUI(control);
    m_error->SetForegroundColour(wxColour(237, 107, 33));
}
}
