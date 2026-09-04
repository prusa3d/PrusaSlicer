#ifndef slic3r_WifiConfigDialog_hpp_
#define slic3r_WifiConfigDialog_hpp_

#include "GUI_Utils.hpp"

#include "../Utils/WifiScanner.hpp"

#include <wx/event.h>
#include <wx/dialog.h>
#include <wx/combobox.h>
#include <wx/textctrl.h>

#include "Widgets/ComboBox.hpp"
#include "Widgets/TextInput.hpp"

namespace Slic3r {
namespace GUI {

class RemovableDriveManager;
class WifiConfigDialog : public DPIDialog
{
public:
    WifiConfigDialog(wxWindow* parent, std::string& file_path, RemovableDriveManager* removable_manager, const wxString& preffered_drive );
    ~WifiConfigDialog();    
    wxString    get_used_path() const { return m_used_path; }

private:
    ::ComboBox*             m_ssid_combo {nullptr};
    ::TextInput*            m_pass_textctrl {nullptr};
    ::ComboBox*             m_drive_combo {nullptr};
    // reference to string that is filled after ShowModal is called from owner
    std::string&            out_file_path;
    WifiScanner*            m_wifi_scanner;
    RemovableDriveManager*  m_removable_manager;
    wxString                m_used_path;
    int                     m_ssid_button_id {wxID_ANY};
    int                     m_pass_button_id {wxID_ANY};
    int                     m_drive_button_id {wxID_ANY};

    void        on_ok(wxCommandEvent& e);
    void        on_combo(wxCommandEvent& e);
    void        on_rescan_drives(wxCommandEvent& e);
    void        on_rescan_networks(wxCommandEvent& e);
    void        on_retrieve_password(wxCommandEvent& e);
    void        rescan_drives(const wxString& preffered_drive);
    void        rescan_networks(bool select);
    void        fill_password();

protected:
    void        on_dpi_changed(const wxRect& suggested_rect) override;
    void        on_sys_color_changed() override {}
};

}} // Slicer::GUI
#endif