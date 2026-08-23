///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "CalibrationPAPostProcessor.hpp"

#include "libslic3r/PrintConfig.hpp"   // GCodeFlavor

#include <regex>
#include <string>

namespace Slic3r {

PACalibrationCommand select_pa_command(GCodeFlavor flavor, const std::string &printer_notes)
{
    if (flavor == gcfKlipper)
        return PACalibrationCommand::Klipper;
    // Non-Marlin flavors that take a pressure-advance command (RepRapFirmware / Duet).
    if (flavor != gcfMarlinFirmware && flavor != gcfMarlinLegacy)
        return PACalibrationCommand::M572;
    // Marlin-flavored, which includes ALL Prusa printers. Prusa's Buddy input-shaper
    // firmware uses M572 (pressure advance); older firmware and generic Marlin use
    // M900 K (linear advance). Key off the SAME printer_notes markers Prusa's own
    // start_filament_gcode switches on -- the printer MODEL name is not reliable (e.g.
    // the MK3.9 carries PRINTER_MODEL_MK4IS in its notes, not "MK3.9"). Generic Marlin
    // printers carry none of these markers and correctly fall through to M900.
    static const std::regex m572_re(R"(MK4IS|XLIS|MK4S|MK3\.9S|MK3\.5|MINIIS|COREONE)",
                                    std::regex::icase);
    return std::regex_search(printer_notes, m572_re) ? PACalibrationCommand::M572
                                                     : PACalibrationCommand::M900;
}

} // namespace Slic3r
