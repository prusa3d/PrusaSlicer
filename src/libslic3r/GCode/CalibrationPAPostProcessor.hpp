///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_CalibrationPAPostProcessor_hpp_
#define slic3r_CalibrationPAPostProcessor_hpp_

#include <string>

namespace Slic3r {

// Shared helpers for the Pressure Advance calibration tools (the chevron tower and
// the garethky Line test): the firmware PA-command selector and the job-scoped
// calibration marker the Line post-processor gates on.

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

// Job-scoped marker the calibration dialogs emit as first-layer custom G-code so the
// in-process Line post-processor only rewrites a genuine PA-calibration export.
inline constexpr const char *calibration_pa_marker()
{
    return "PRUSASLICER_PA_CALIBRATION";
}

} // namespace Slic3r

#endif // slic3r_CalibrationPAPostProcessor_hpp_
