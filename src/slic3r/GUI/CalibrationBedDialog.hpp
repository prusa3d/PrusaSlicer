#ifndef slic3r_GUI_CalibrationBedDialog_hpp_
#define slic3r_GUI_CalibrationBedDialog_hpp_

#include "CalibrationAbstractDialog.hpp"

namespace Slic3r { 
namespace GUI {

class CalibrationBedDialog : public CalibrationAbstractDialog
{

public:
    CalibrationBedDialog(GUI_App* app, MainFrame* mainframe) : CalibrationAbstractDialog(app, mainframe, "Bed leveling calibration") { create(boost::filesystem::path("calibration") / "bed_leveling", "bed_leveling.html");  }
    virtual ~CalibrationBedDialog() {}
protected:
    void create_buttons(wxStdDialogButtonSizer* sizer) override;
private:
    void create_geometry(wxCommandEvent& event_args);

};

} // namespace GUI
} // namespace Slic3r

#endif
