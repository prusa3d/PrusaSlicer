///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_CalibrationShrinkageDialog_hpp_
#define slic3r_CalibrationShrinkageDialog_hpp_

#include <wx/dialog.h>
#include <wx/checkbox.h>
#include <wx/spinctrl.h>

namespace Slic3r { namespace GUI {

/// Dimensional accuracy / shrinkage calibration dialog.
/// Generates an XYZ cross gauge for caliper measurement.
class CalibrationShrinkageDialog : public wxDialog
{
public:
    explicit CalibrationShrinkageDialog(wxWindow* parent);

private:
    wxSpinCtrl*  m_length{nullptr};
    wxCheckBox*  m_brim{nullptr};

    bool generate_and_load();
};

}} // namespace Slic3r::GUI

#endif // slic3r_CalibrationShrinkageDialog_hpp_
