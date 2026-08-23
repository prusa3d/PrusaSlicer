///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_CalibrationFanDialog_hpp_
#define slic3r_CalibrationFanDialog_hpp_

#include <wx/dialog.h>
#include <wx/checkbox.h>
#include <wx/spinctrl.h>

namespace Slic3r { namespace GUI {

/// Fan speed calibration: generates a tower with overhang shelves and
/// bridge windows.  Fan speed varies per level via M106 custom G-code.
class CalibrationFanDialog : public wxDialog
{
public:
    explicit CalibrationFanDialog(wxWindow* parent);

private:
    wxSpinCtrl* m_start_fan{nullptr};    // start fan % (default 0)
    wxSpinCtrl* m_end_fan{nullptr};      // end fan % (default 100)
    wxSpinCtrl* m_fan_step{nullptr};     // step size % (default 10)
    wxCheckBox* m_brim{nullptr};

    bool generate_and_load();
};

}} // namespace Slic3r::GUI

#endif // slic3r_CalibrationFanDialog_hpp_
