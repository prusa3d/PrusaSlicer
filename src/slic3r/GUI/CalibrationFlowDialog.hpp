#ifndef slic3r_GUI_CalibrationFlowDialog_hpp_
#define slic3r_GUI_CalibrationFlowDialog_hpp_

#include "CalibrationAbstractDialog.hpp"

namespace Slic3r { 
namespace GUI {

class CalibrationFlowDialog : public CalibrationAbstractDialog
{

public:
    CalibrationFlowDialog(GUI_App* app, MainFrame* mainframe) : CalibrationAbstractDialog(app, mainframe, "Flow calibration") { create(boost::filesystem::path("calibration") / "filament_flow","filament_flow.html", wxSize(900, 500));  }
    virtual ~CalibrationFlowDialog() {}
    
protected:
    void create_buttons(wxStdDialogButtonSizer* sizer) override;
    void create_geometry(float start, float delta);
    void create_geometry_10(wxCommandEvent& event_args) { create_geometry(80.f, 10.f); }
    void create_geometry_2_5(wxCommandEvent& event_args) { create_geometry(92.f, 2.F); }

};

} // namespace GUI
} // namespace Slic3r

#endif
