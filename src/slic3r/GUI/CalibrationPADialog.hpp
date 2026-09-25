///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_CalibrationPADialog_hpp_
#define slic3r_CalibrationPADialog_hpp_

#include <wx/dialog.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/spinctrl.h>

namespace Slic3r { namespace GUI {

/// Dialog for pressure advance (PA) calibration. Two test styles:
///  - Tower: nested V-shaped chevrons, one PA value per layer group (sliced
///    STL + per-Z PA command); pick the height with the sharpest tips.
///  - Line (garethky / K-factor): a flat single-layer test the dialog generates
///    as a toolpath — one slow->fast->slow line per PA value between two anchor
///    bars, plus reference ticks and (optionally) PA value labels. The body is
///    spliced over a sliced placeholder on export by
///    CalibrationPALinePostProcessor.
class CalibrationPADialog : public wxDialog
{
public:
    CalibrationPADialog(wxWindow* parent);

private:
    bool generate_and_load();
    bool generate_tower();        // mode 0: chevron tower, per-Z PA
    bool generate_line_pattern(); // mode 1: line (garethky toolpath, injected)

    wxChoice*         m_mode{nullptr};       // 0 = Tower, 1 = Line
    wxSpinCtrlDouble* m_start_pa{nullptr};
    wxSpinCtrlDouble* m_end_pa{nullptr};
    wxSpinCtrlDouble* m_pa_step{nullptr};
    wxSpinCtrlDouble* m_test_speed{nullptr}; // mm/s — see note in .cpp
    wxCheckBox*       m_brim{nullptr};
    wxCheckBox*       m_labels{nullptr};     // Line only: print the PA value labels
};

}} // namespace Slic3r::GUI

#endif // slic3r_CalibrationPADialog_hpp_
