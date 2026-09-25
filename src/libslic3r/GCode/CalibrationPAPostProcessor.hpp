///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_CalibrationPAPostProcessor_hpp_
#define slic3r_CalibrationPAPostProcessor_hpp_

#include <string>

namespace Slic3r {

// Shared helpers for the Pressure Advance calibration tools (the chevron tower and
// the garethky Line test): the firmware PA-command selector and formatter, the
// start_filament_gcode rewrite FilamentDB uses to apply a stored PA value, and the
// job-scoped calibration marker the Line post-processor gates on.

enum GCodeFlavor : unsigned char;   // defined in libslic3r/PrintConfig.hpp

enum class PACalibrationCommand { M572, M900, Klipper };

// Select the firmware pressure-advance / linear-advance command for a printer, given
// its g-code flavor and printer_notes. Klipper -> SET_PRESSURE_ADVANCE; RepRapFirmware
// /Duet -> M572. For Marlin-flavored printers (which includes ALL Prusa printers) the
// choice depends on the firmware GENERATION, not the flavor: Prusa's Buddy input-shaper
// line uses M572 S (pressure advance), while older Prusa and generic Marlin use M900 K
// (linear advance). Detected from the same printer_notes markers Prusa's own
// start_filament_gcode keys on -- the printer MODEL name is unreliable (the MK3.9 is
// flagged PRINTER_MODEL_MK4IS in its notes, not "MK3.9").
PACalibrationCommand select_pa_command(GCodeFlavor flavor, const std::string &printer_notes);

// The command text up to its value: "M572 S", "M900 K" or "SET_PRESSURE_ADVANCE ADVANCE=".
const char *pa_command_prefix(PACalibrationCommand cmd);

// One PA command without a line ending, e.g. "M572 S0.0360". Every PA tool formats the value
// this way: 4 decimals, '.' as the decimal separator whatever the locale.
std::string format_pa_command(PACalibrationCommand cmd, double pa);

// Set the pressure advance in a start_filament_gcode value (the in-memory form, with real
// newlines) to `pa`, using the firmware command `cmd` (see select_pa_command). Every occurrence
// of that command gets the new value: only one {if printer_notes...} branch runs on a given
// printer, and the value is meant for the current printer. The value may be a number or a
// PlaceholderParser template such as an {if nozzle_diameter...}...{endif} chain. Only the value
// is replaced, so trailing parameters, the ';' comment and the {elsif}/{else}/{endif} tags of an
// enclosing block on the same line are kept. Lines with the other PA commands are left alone,
// and so are occurrences inside a ';' comment. An M900 value in the legacy Linear Advance 1.0
// scale (every number >= 10, the MK3-era "LA 1.0" fallback line) is kept too. If no occurrence
// was replaced, the command is appended on a line of its own - or, when only LA 1.0 lines exist,
// inserted before them so that LA 1.5 firmware picks its mode from it. Such an added line ends in
// "; FilamentDB pressure advance"; the next call removes it first, so changing to a printer with
// another PA command replaces it rather than keeping both. The text older builds
// appended instead (a literal backslash and 'n', then "M572 S<value>"), which never ran, is
// removed.
std::string apply_pressure_advance_to_start_gcode(const std::string &gcode,
                                                  PACalibrationCommand cmd,
                                                  double pa);

// Job-scoped marker the calibration dialogs emit as first-layer custom G-code so the
// in-process Line post-processor only rewrites a genuine PA-calibration export.
inline constexpr const char *calibration_pa_marker()
{
    return "PRUSASLICER_PA_CALIBRATION";
}

} // namespace Slic3r

#endif // slic3r_CalibrationPAPostProcessor_hpp_
