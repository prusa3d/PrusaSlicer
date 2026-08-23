///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_CalibrationTempDialog_hpp_
#define slic3r_CalibrationTempDialog_hpp_

#include <wx/dialog.h>
#include <wx/checkbox.h>
#include <wx/spinctrl.h>

namespace Slic3r { namespace GUI {

class CalibrationTempDialog : public wxDialog
{
public:
    CalibrationTempDialog(wxWindow* parent);

    int get_start_temp() const;
    int get_end_temp()   const;
    int get_temp_step()  const;

    // Generate the tower STL, load it onto the bed, and set per-layer temps.
    // Called when the user clicks OK.
    bool generate_and_load();

private:
    wxSpinCtrl*  m_start_temp{nullptr};
    wxSpinCtrl*  m_end_temp{nullptr};
    wxSpinCtrl*  m_temp_step{nullptr};
    wxCheckBox*  m_brim{nullptr};
};

}} // namespace Slic3r::GUI

#endif // slic3r_CalibrationTempDialog_hpp_
