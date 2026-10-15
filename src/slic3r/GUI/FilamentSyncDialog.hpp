///|/ Copyright (c) 2026
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_FilamentSyncDialog_hpp_
#define slic3r_FilamentSyncDialog_hpp_

#include <vector>

#include <wx/dialog.h>

#include "slic3r/Utils/FilamentSync.hpp"

class wxCheckBox;
class wxChoice;
class wxStaticBitmap;
class wxStaticText;
class wxWindow;

namespace Slic3r {
namespace GUI {

/// A physical printer with PrusaLink and an address is selected for an FFF printer profile, so
/// its filaments can be read.
bool can_sync_filaments_from_printer();

/// Reads the filaments loaded in the selected physical printer, shows what would change, and
/// applies what the user accepts: filament presets selected (created or installed if needed) and
/// extruder colors. Explains instead when the printer can't be read.
void sync_filaments_from_printer(wxWindow* parent);

/// Shows a warning while the print uses an extruder the selected physical printer reported empty,
/// or without a calibrated dock, at its last sync in this session; closes it otherwise.
void update_unavailable_extruders_warning();

/// Hides the sidebar's filament lists of the extruders the selected physical printer reported
/// empty, or without a calibrated dock, at its last sync in this session, unless the user turned
/// that off; shows them all otherwise.
void update_filament_list_visibility();

/// The preview: one row per tool the printer reports, where each loaded tool's row offers the
/// plan's own action and its alternatives.
class FilamentSyncDialog : public wxDialog
{
public:
    FilamentSyncDialog(wxWindow* parent, const filament_sync::Plan& plan, const std::string& printer_name);

    /// The plan with each row's choice in place of its own action.
    filament_sync::Plan chosen_plan() const;
    /// One entry per row of chosen_plan(): whether it changes anything to apply.
    std::vector<bool> accepted() const;
    /// Whether to reset every extruder color to its filament color.
    bool reset_extruder_colours() const;

private:
    void update_apply_enabled();
    // The color and notes of the row's choice
    void update_row(size_t row);

    const filament_sync::Plan&   m_plan;
    // Per row, null where there's nothing to choose, no filament to show the color of, or no
    // notes for any choice
    std::vector<wxChoice*>       m_choices;
    std::vector<wxStaticBitmap*> m_swatches;
    std::vector<wxStaticText*>   m_notes;
    wxCheckBox*                  m_reset_colours{nullptr};
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_FilamentSyncDialog_hpp_
