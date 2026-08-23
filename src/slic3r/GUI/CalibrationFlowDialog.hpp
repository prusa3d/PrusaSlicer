///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_CalibrationFlowDialog_hpp_
#define slic3r_CalibrationFlowDialog_hpp_

#include <wx/dialog.h>
#include <wx/checkbox.h>
#include <wx/spinctrl.h>

namespace Slic3r { namespace GUI {

class CalibrationFlowDialog : public wxDialog
{
public:
    CalibrationFlowDialog(wxWindow* parent);

    double get_start_flow() const;
    double get_end_flow()   const;
    double get_flow_step()  const;

    // Generate the serpentine STL, load it onto the bed, and set per-layer
    // speed overrides to achieve target volumetric flow rates.
    bool generate_and_load();

private:
    wxSpinCtrlDouble* m_start_flow{nullptr};
    wxSpinCtrlDouble* m_end_flow{nullptr};
    wxSpinCtrlDouble* m_flow_step{nullptr};
    wxCheckBox*       m_brim{nullptr};
};

}} // namespace Slic3r::GUI

#endif // slic3r_CalibrationFlowDialog_hpp_
