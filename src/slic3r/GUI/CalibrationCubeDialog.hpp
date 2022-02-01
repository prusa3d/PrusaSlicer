#ifndef slic3r_GUI_CalibrationCubeDialog_hpp_
#define slic3r_GUI_CalibrationCubeDialog_hpp_

#include "CalibrationAbstractDialog.hpp"

namespace Slic3r { 
namespace GUI {

class CalibrationCubeDialog : public CalibrationAbstractDialog
{

public:
    CalibrationCubeDialog(GUI_App* app, MainFrame* mainframe) : CalibrationAbstractDialog(app, mainframe, "Calibration cube") { create(boost::filesystem::path("calibration") / "cube", "cube.html"); }
    virtual ~CalibrationCubeDialog(){ }
    
protected:
    void create_buttons(wxStdDialogButtonSizer* sizer) override;
    void create_geometry(std::string cube_path);
    void create_geometry_voron(wxCommandEvent& event_args) { create_geometry("voron_design_cube_v7.amf"); }
    void create_geometry_standard(wxCommandEvent& event_args) { create_geometry("xyzCalibration_cube.amf"); }

    wxComboBox* scale;
    wxComboBox* calibrate;

};

} // namespace GUI
} // namespace Slic3r

#endif
