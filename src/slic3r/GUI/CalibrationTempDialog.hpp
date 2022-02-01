#ifndef slic3r_GUI_CalibrationTempDialog_hpp_
#define slic3r_GUI_CalibrationTempDialog_hpp_

#include "CalibrationAbstractDialog.hpp"

namespace Slic3r { 
namespace GUI {

class CalibrationTempDialog : public CalibrationAbstractDialog
{

public:
    CalibrationTempDialog(GUI_App* app, MainFrame* mainframe) : CalibrationAbstractDialog(app, mainframe, "Temperature calibration") { create(boost::filesystem::path("calibration") / "filament_temp", "filament_temp.html"); }
    virtual ~CalibrationTempDialog(){ }
    
protected:
    void create_buttons(wxStdDialogButtonSizer* sizer) override;
    void create_geometry(wxCommandEvent& event_args);

    wxComboBox* steps;
    wxComboBox* nb_down;
    wxComboBox* nb_up;

};

} // namespace GUI
} // namespace Slic3r

#endif
