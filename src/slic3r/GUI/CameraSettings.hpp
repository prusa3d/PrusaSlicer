// PrusaSlicer is released under the terms of the AGPLv3 or higher.
#ifndef slic3r_CameraSettings_hpp_
#define slic3r_CameraSettings_hpp_
#include <wx/panel.h>
#include "libslic3r/PrintConfig.hpp"
#include "slic3r/Utils/CameraDevices.hpp"
class ComboBox;
class TextInput;
class wxStaticText;
class wxButton;
namespace Slic3r::GUI {
// Shared by the physical-printer editor and the Camera tab's focused dialog.
class CameraSettings : public wxPanel
{
public:
    CameraSettings(wxWindow* parent, const DynamicPrintConfig& config);
    bool apply(DynamicPrintConfig& config);
    void sys_color_changed();
private:
    void update_controls();
    void refresh_devices(const std::string& selected);
    ComboBox* m_type;
    TextInput* m_url;
    ComboBox* m_device;
    wxButton* m_refresh;
    wxButton* m_remove;
    wxStaticText* m_help;
    wxStaticText* m_error;
    wxBoxSizer* m_url_row;
    wxBoxSizer* m_device_row;
    std::vector<CameraDevice> m_devices;
    std::string m_saved_device;
};
}
#endif
