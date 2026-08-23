///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_CalibrationFlowPostProcessor_hpp_
#define slic3r_CalibrationFlowPostProcessor_hpp_

#include <string>
#include <utility>
#include <vector>

namespace Slic3r {

// In-process post-processor for the Filament-Edition YOLO Flow Rate Calibration.
//
// Why this exists: the flow rate test prints one pad per flow value and the
// user picks the best-looking top surface. The natural way to vary flow is a
// per-object `extrusion_multiplier`, but that option lives in GCodeConfig (it
// is resolved per-extruder, baked once into Extruder::m_e_per_mm3) — it is NOT
// a PrintObjectConfig/PrintRegionConfig key, so a per-object override is
// silently dropped during slicing and every pad ends up at the *same* flow.
// (That is GitHub issue #20: the pads are indistinguishable.)
//
// This post-processor restores the per-pad variation by scaling the extruded
// E of each pad's deposition moves. Each pad is a separate object, so the
// object boundaries the slicer already emits (M486 / EXCLUDE_OBJECT) delimit
// exactly the moves to scale. The scale factor is applied *on top of* whatever
// flow the slicer baked in, so the center pad (factor 1.0) prints at the
// filament's current flow and the others bracket it.
//
// Triggered via the standard `post_process` config when one of its entries
// starts with the `::builtin::flow_calibration?...` prefix (see
// is_calibration_flow_url). It only rewrites a G-code carrying the
// calibration marker (see below) and is a pass-through no-op otherwise.
//
// URL format:
//     ::builtin::flow_calibration?objects=<name>:<factor>,<name>:<factor>,...
// where <name> is the object name as it appears in the object-label markers
// (the pad's flow-offset label, e.g. "-.05", "0", ".05") and <factor> is the
// E scale factor for that object (e.g. 0.95 .. 1.05). The name is matched
// verbatim, so it must not contain ',' (the pair separator). Object identity
// is keyed by name, never by the M486 numeric id — those ids are assigned in
// pointer order and do not match the dialog's pad order.

bool is_calibration_flow_url(const std::string &script);

// Comment the calibration dialog injects into the sliced G-code (job-scoped,
// via the model's custom per-Z G-code). The `post_process` hook lives in the
// print *preset* and therefore runs for every subsequent slice; the
// post-processor rewrites ONLY a G-code that carries this marker and is a
// pass-through no-op for any normal print. The marker is wiped automatically
// when a different model is loaded, so it cannot leak across jobs.
inline constexpr const char *calibration_flow_marker()
{
    return "PRUSASLICER_FLOWRATE_CALIBRATION";
}

// Run the in-process post-processor against `gcode_path` using the parameters
// embedded in `url`. Returns true if the G-code was rewritten, false if it was
// left untouched (no calibration marker present — i.e. not a calibration
// print). Throws Slic3r::RuntimeError on a malformed URL, an unreadable file,
// or a marker-carrying G-code that is binary or absolute-E.
bool run_calibration_flow_post_processor(const std::string &url, const std::string &gcode_path);

// Build a `::builtin::flow_calibration?...` URL from an (object-name, factor)
// table. Factors are formatted locale-invariantly ('.' decimals).
std::string make_calibration_flow_url(const std::vector<std::pair<std::string, double>> &objects);

} // namespace Slic3r

#endif // slic3r_CalibrationFlowPostProcessor_hpp_
