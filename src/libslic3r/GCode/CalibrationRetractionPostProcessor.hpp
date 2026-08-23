///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_CalibrationRetractionPostProcessor_hpp_
#define slic3r_CalibrationRetractionPostProcessor_hpp_

#include <string>
#include <utility>
#include <vector>

namespace Slic3r {

// In-process post-processor for the Filament-Edition Retraction Calibration.
//
// Triggered via the standard `post_process` config when one of its entries
// starts with the `::builtin::retraction_calibration?...` prefix (see
// is_calibration_retraction_url). Rewrites slicer-side retract/recovery
// moves so that each Z band uses a per-band retraction distance, instead
// of the global retract_length the slicer baked in. Targets Buddy firmware,
// which does not implement M207/G10/G11 (so the firmware-retraction path
// the dialog used previously is a no-op there).
//
// URL format:
//     ::builtin::retraction_calibration?base=<mm>&levels=<z>:<mm>,<z>:<mm>,...
// where each `<z>:<mm>` pair is `z_top_exclusive:retract_mm` ascending by z_top.

bool is_calibration_retraction_url(const std::string &script);

// Comment the calibration dialog injects into the sliced G-code (job-scoped,
// via the model's custom per-Z G-code). The `post_process` hook lives in the
// print *preset* and therefore runs for every subsequent slice; the
// post-processor rewrites ONLY a G-code that carries this marker and is a
// pass-through no-op for any normal print. The marker is wiped automatically
// when a different model is loaded, so it cannot leak across jobs.
inline constexpr const char *calibration_retraction_marker()
{
    return "PRUSASLICER_RETRACTION_CALIBRATION_TOWER";
}

// Run the in-process post-processor against `gcode_path` using the parameters
// embedded in `url`. Returns true if the G-code was rewritten, false if it was
// left untouched (no calibration marker present — i.e. not a calibration
// print). Throws Slic3r::RuntimeError on a malformed URL, an unreadable file,
// or a marker-carrying G-code that is binary or absolute-E.
bool run_calibration_retraction_post_processor(const std::string &url, const std::string &gcode_path);

// Build a `::builtin::retraction_calibration?...` URL from a level table.
std::string make_calibration_retraction_url(double base_retract,
                                            const std::vector<std::pair<double, double>> &levels);

} // namespace Slic3r

#endif // slic3r_CalibrationRetractionPostProcessor_hpp_
