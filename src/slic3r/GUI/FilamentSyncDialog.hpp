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

/// The preview: one row per tool the printer reports, with a checkbox for each change.
class FilamentSyncDialog : public wxDialog
{
public:
    FilamentSyncDialog(wxWindow* parent, const filament_sync::Plan& plan, const std::string& printer_name);

    /// One entry per plan row: whether to apply it.
    std::vector<bool> accepted() const;
    /// Whether to reset every extruder color to its filament color.
    bool reset_extruder_colours() const;

private:
    void update_apply_enabled();

    const filament_sync::Plan& m_plan;
    std::vector<wxCheckBox*>   m_checks; // null for rows with nothing to apply
    wxCheckBox*                m_reset_colours{nullptr};
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_FilamentSyncDialog_hpp_
