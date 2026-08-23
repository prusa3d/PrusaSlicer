///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_CalibrationExtrusionDialog_hpp_
#define slic3r_CalibrationExtrusionDialog_hpp_

#include <wx/dialog.h>
#include <wx/checkbox.h>

namespace Slic3r { namespace GUI {

/// Simple dialog for the extrusion multiplier calibration test.
/// Generates a 40×40×40 mm cube in vase mode with classic perimeters,
/// no bottom layers.  The user prints the cube and measures wall
/// thickness to tune extrusion multiplier.
class CalibrationExtrusionDialog : public wxDialog
{
public:
    CalibrationExtrusionDialog(wxWindow* parent);

    bool generate_and_load();

private:
    wxCheckBox* m_brim{nullptr};
};

}} // namespace Slic3r::GUI

#endif // slic3r_CalibrationExtrusionDialog_hpp_
