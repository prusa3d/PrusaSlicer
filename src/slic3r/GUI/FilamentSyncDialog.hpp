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

/// Where a sync reads the filaments from, for the edited FFF printer profile.
enum class FilamentSyncSource {
    None,
    /// The selected physical printer: PrusaLink, with an address.
    PrusaLink,
    /// The user's Prusa Connect account (signed in to Prusa Account), when no PrusaLink physical
    /// printer is selected and the profile is a Prusa printer's (or saved from one) with a
    /// printer_model to find its printer by.
    Connect,
};
FilamentSyncSource filament_sync_source();

/// The filaments can be read: filament_sync_source() isn't None.
bool can_sync_filaments_from_printer();

/// Drops what the last sync found when it read Prusa Connect: for after signing out of Prusa
/// Account, so the empty extruders it found don't come back at the next sign-in.
void forget_filament_sync_from_connect();

/// Reads the filaments loaded in the selected physical printer, or those Prusa Connect has for the
/// account's printer of this profile's model (asking which one when there are several), shows
/// what would change, and applies what the user accepts: filament presets selected (created or
/// installed if needed) and extruder colors. Explains instead when the filaments can't be read.
void sync_filaments_from_printer(wxWindow* parent);

/// Shows a warning while the print uses an extruder the last sync in this session reported empty,
/// or without a calibrated dock, while that sync still applies (the same PrusaLink physical
/// printer is selected, or, from Prusa Connect, the same account is signed in and the profile has
/// the same printer_model); closes it otherwise.
void update_unavailable_extruders_warning();

/// Hides the sidebar's filament lists of the extruders the last sync in this session reported
/// empty, or without a calibrated dock, while that sync still applies (as above), unless the user
/// turned that off; shows them all otherwise.
void update_filament_list_visibility();

/// The preview: one row per tool the printer reports, where each loaded tool's row offers the
/// plan's own action and its alternatives.
class FilamentSyncDialog : public wxDialog
{
public:
    /// from_connect: the filaments are those Prusa Connect has for the printer, not the printer's own.
    FilamentSyncDialog(wxWindow* parent, const filament_sync::Plan& plan, const std::string& printer_name, bool from_connect = false);

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
