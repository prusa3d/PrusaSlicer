///|/ Copyright (c) 2026
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_Utils_FilamentSync_hpp_
#define slic3r_Utils_FilamentSync_hpp_

#include <set>
#include <string>
#include <vector>

#include "PrinterTools.hpp"

namespace Slic3r {

class AppConfig;
class DynamicPrintConfig;
class PresetBundle;
class PresetCollection;

// Sync filament presets from the printer: tool N of a printer with Filament Slots (as
// PrinterTools reports it) is extruder N. For each loaded tool the plan picks a filament preset
// that matches its vendor and material, or a new user preset to create, and the color for the
// extruder. One way only: nothing is sent to the printer or to Filament DB.
//
// Plain data in and out and no wx, so the matching is unit-tested on its own; apply_presets()
// then works on a PresetBundle, also without the GUI.
namespace filament_sync {

/// A filament preset as one extruder sees it.
struct Candidate
{
    std::string name;
    /// Display name of a system preset (its name before "@"); empty for user presets.
    std::string alias;
    /// For a user preset saved from a system preset (its parent), the parent's display name;
    /// empty otherwise.
    std::string parent;
    /// filament_vendor
    std::string vendor;
    /// filament_type
    std::string type;
    /// filament_colour, "#rrggbb" in either case, or empty.
    std::string colour;
    /// For a user preset saved from a system preset, the parent's filament_colour; empty
    /// otherwise.
    std::string parent_colour;
    bool        is_system     = false;
    bool        is_visible    = true;
    /// Compatible with this extruder of the printer profile.
    bool        is_compatible = true;
    /// filament_abrasive, or a carbon or glass fiber filament by its name.
    bool        is_abrasive   = false;
};

/// One extruder of the printer profile. Default, external and template presets are left out
/// of the candidates.
struct ExtruderState
{
    int                    idx = 0;
    std::string            selected;
    double                 nozzle_diameter = 0.;
    bool                   high_flow       = false;
    std::vector<Candidate> candidates;
};

enum class Action {
    /// The tool is empty: the extruder keeps its preset and color.
    NotLoaded,
    /// The selected preset already matches.
    Keep,
    /// Select an installed preset.
    Select,
    /// Select a system preset that isn't installed, and install it.
    Install,
    /// Create a user preset from a template and select it: the tool names a vendor, and none of
    /// its presets of that material fits, or only ones of another color do.
    Create,
    /// Nothing to select or start from; the notes say why.
    NoMatch,
};

/// Something the preview offers for a loaded tool instead of its plan's own action. The fields
/// are those of ExtruderPlan.
struct Alternative
{
    /// Keep (leave the extruder's preset), Select, Install or Create
    Action      action = Action::Keep;
    std::string preset;
    std::string template_preset;
    std::string vendor;
    std::string preset_colour;
    /// The row's notes with this choice: the tool's, and warnings about this preset: abrasive
    /// filament, or another color than the tool's and why no preset of its color can be made.
    std::vector<std::string> notes;
};

struct ExtruderPlan
{
    int                      idx = 0;
    PrinterTool              tool;
    Action                   action = Action::NotLoaded;
    /// Keep, Select, Install: an existing preset. Create: the name of the new preset, which
    /// several extruders can share.
    std::string              preset;
    /// Create: the preset the new one is cloned from.
    std::string              template_preset;
    /// Create: filament_vendor of the new preset, as the printer spells it.
    std::string              vendor;
    /// Filament color of the preset the extruder ends up with ("#RRGGBB"; a new preset gets the
    /// tool's color), which it shows once its extruder color is reset; empty when unknown.
    std::string              preset_colour;
    std::vector<std::string> notes;
    /// For a loaded tool, what the preview offers instead, in order: a new preset of the tool's
    /// vendor, material and color, unless the plan creates one; the extruder's other presets of
    /// that material, the tool's vendor's first, then that vendor's system presets that aren't
    /// installed, then other vendors', each best first; and keeping the extruder's preset when
    /// the plan changes it and it isn't offered yet.
    std::vector<Alternative> alternatives;
};

struct Plan
{
    /// One per reported tool that the printer profile has, in extruder order.
    std::vector<ExtruderPlan> extruders;
    /// Tools the profile doesn't have, and tool entries the printer sent that couldn't be read.
    std::vector<std::string>  notes;
};

/// Folds vendor spellings together: case and punctuation ("3D-Fuel", "3D Fuel"), and the
/// printer's "Prusament" with PrusaSlicer's "Prusa Polymers".
std::string vendor_key(const std::string &vendor);

/// No vendor, or one of the printer's unnamed user vendors VENDOR1..VENDOR8.
bool is_placeholder_vendor(const std::string &vendor);

/// Whether a preset's filament_type fits the material a tool reports: case-insensitive, the
/// flexible types (FLEX, TPU, TPE, PEBA) as one, and a type the printer cut to its 7 characters.
/// PETG and PET stay apart.
bool same_material(const std::string &tool_material, const std::string &preset_type);

/// The plan for the reported tools. taken_names holds every name a new preset must not get,
/// lowercased (see taken_filament_names()); name_tag scopes a new preset's name when the plain
/// one is taken ("Overture PETG @INDX").
Plan make_plan(const PrinterTools                &tools,
               const std::vector<ExtruderState>  &extruders,
               const std::set<std::string>       &taken_names,
               const std::string                 &name_tag);

/// The row with alternatives[i] in place of its own action, preset, color and notes (the row as it
/// is when i is out of range).
ExtruderPlan with_alternative(const ExtruderPlan &row, size_t i);

/// The extruders of the edited printer preset and the filaments each of them can use.
std::vector<ExtruderState> collect_extruder_states(const PresetBundle &bundle);

/// Lowercased names a new filament preset can't take: every preset name and alias, and the
/// .ini files in the user filament folder.
std::set<std::string> taken_filament_names(PresetCollection &filaments);

/// Short tag for the edited printer, used in new preset names: "INDX" for the CORE One INDX
/// models, else the printer model.
std::string printer_name_tag(const PresetBundle &bundle);

/// Checks a name for a new filament preset: the characters and length Save Preset accepts, and
/// that it sorts after "- default -". Returns an empty string if fine, else why not.
std::string check_new_preset_name(const std::string &name);

struct ApplyResult
{
    std::vector<std::string> created;
    std::vector<std::string> installed;
    /// Extruders whose preset changed.
    std::vector<int>         selected;
    std::vector<std::string> errors;
};

/// Creates, installs and selects the presets of the accepted rows (accepted[i] is
/// plan.extruders[i]). New presets are saved to the user filament folder, never pushed to
/// Filament DB; installed ones are added to app_config's installed filaments when app_config
/// isn't null. Unsaved edits of the edited filament preset are kept.
///
/// Changes only the extruder selections: the caller then brings the Filament tab to its
/// extruder's preset before anything runs update_compatible().
///
/// A new preset is saved and read back; one whose file doesn't hold all of it (a full disk) is
/// taken out again and reported.
ApplyResult apply_presets(PresetBundle &bundle, AppConfig *app_config, const Plan &plan, const std::vector<bool> &accepted);

/// Extruders (0-based, below the profile's extruder count) the printer can't print with: tools it
/// reports empty, and docks it leaves out because they aren't calibrated.
struct UnavailableExtruders
{
    std::vector<int> empty;
    std::vector<int> uncalibrated;
};
UnavailableExtruders unavailable_extruders(const PrinterTools &tools, size_t extruder_count);

/// A warning for a print that uses some of them (used: 0-based, as Print::extruders() gives
/// them), or an empty string when it uses none. It names each extruder's filament from
/// filament_names (0-based, as the sidebar shows them); an extruder without one there goes by its
/// number alone.
std::string unavailable_extruders_warning(const UnavailableExtruders &unavailable, const std::vector<unsigned int> &used,
                                          const std::vector<std::string> &filament_names, const std::string &printer_name,
                                          const std::string &sync_time);

/// Whether the file holds every option of config, as config serializes it: ConfigBase::save()
/// doesn't report a write that stopped short.
bool saved_config_matches(const std::string &file, const DynamicPrintConfig &config);

} // namespace filament_sync
} // namespace Slic3r

#endif // slic3r_Utils_FilamentSync_hpp_
