#ifndef slic3r_GUI_CalibrationRetractionDialog_hpp_
#define slic3r_GUI_CalibrationRetractionDialog_hpp_

#include "CalibrationAbstractDialog.hpp"

namespace Slic3r { 
namespace GUI {

class CalibrationRetractionDialog : public CalibrationAbstractDialog
{

public:
    CalibrationRetractionDialog(GUI_App* app, MainFrame* mainframe) : CalibrationAbstractDialog(app, mainframe, "Flow calibration") { create(boost::filesystem::path("calibration") / "retraction", "retraction.html", wxSize(900, 500));  }
    virtual ~CalibrationRetractionDialog() {}
    
protected:
    void create_buttons(wxStdDialogButtonSizer* sizer) override;
    void remove_slowdown(wxCommandEvent& event_args);
    void create_geometry(wxCommandEvent& event_args);

    wxComboBox* steps;
    wxComboBox* nb_steps;
    //wxComboBox* start_step;
    wxTextCtrl* temp_start;
    wxComboBox* decr_temp;
};

} // namespace GUI
} // namespace Slic3r

#endif
