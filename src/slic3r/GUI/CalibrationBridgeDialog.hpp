#ifndef slic3r_GUI_CalibrationBridgeDialog_hpp_
#define slic3r_GUI_CalibrationBridgeDialog_hpp_

#include "CalibrationAbstractDialog.hpp"

namespace Slic3r { 
namespace GUI {

class CalibrationBridgeDialog : public CalibrationAbstractDialog
{

public:
    CalibrationBridgeDialog(GUI_App* app, MainFrame* mainframe) : CalibrationAbstractDialog(app, mainframe, "Bridge calibration") { create(boost::filesystem::path("calibration") / "bridge_flow", "bridge_flow.html", wxSize(850, 400)); }
    virtual ~CalibrationBridgeDialog() { }
    
protected:
    void create_buttons(wxStdDialogButtonSizer* buttons) override;
    void create_geometry(std::string setting_key, bool add);
    void create_geometry_flow_ratio(wxCommandEvent& event_args) { create_geometry("bridge_flow_ratio", false);  }
    void create_geometry_overlap(wxCommandEvent& event_args) { create_geometry("bridge_overlap", true); }

    wxComboBox* steps;
    wxComboBox* nb_tests;
};

} // namespace GUI
} // namespace Slic3r

#endif
