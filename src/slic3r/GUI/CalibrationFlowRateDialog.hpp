///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_CalibrationFlowRateDialog_hpp_
#define slic3r_CalibrationFlowRateDialog_hpp_

#include <wx/dialog.h>
#include <wx/checkbox.h>
#include <wx/spinctrl.h>

namespace Slic3r { namespace GUI {

/// YOLO Flow Rate calibration: generates flat pads with Archimedean Chords
/// top pattern, each printed at a different extrusion multiplier.
/// Compare the top surface quality to find the optimal flow.
/// Inspired by OrcaSlicer's YOLO Flow Calibration.
class CalibrationFlowRateDialog : public wxDialog
{
public:
    explicit CalibrationFlowRateDialog(wxWindow* parent);

private:
    wxSpinCtrl*       m_num_steps{nullptr};    // number of steps each side of center
    wxSpinCtrl*       m_step_pct{nullptr};     // step size in percent (e.g. 1 = 1%)
    wxSpinCtrl*       m_pad_width{nullptr};    // width of each pad (mm)
    wxSpinCtrl*       m_pad_depth{nullptr};    // depth of each pad (mm)
    wxCheckBox*       m_brim{nullptr};

    bool generate_and_load();
};

}} // namespace Slic3r::GUI

#endif // slic3r_CalibrationFlowRateDialog_hpp_
