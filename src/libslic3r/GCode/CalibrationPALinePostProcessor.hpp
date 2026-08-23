///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_CalibrationPALinePostProcessor_hpp_
#define slic3r_CalibrationPALinePostProcessor_hpp_

#include <string>

namespace Slic3r {

// In-process post-processor for the *Line* (garethky / K-factor) Pressure
// Advance test. Unlike the Pattern test (a sliced zigzag), the line method is a
// DELIBERATE TOOLPATH — one anchor frame plus one constant-Y pass per PA value,
// each printed slow→fast→slow with the firmware PA command set just before it,
// all welded into a single peelable piece. PrusaSlicer's slicer cannot produce
// that, so the dialog generates the toolpath itself and this post-processor
// splices it in: it keeps the sliced placeholder's header/start G-code and end
// G-code, and REPLACES the placeholder's body (between the first post-marker
// "; printing object" / "; stop printing object" pair) with the generated body.
//
// Runs only on a G-code carrying the job-scoped calibration marker
// (calibration_pa_marker); a pass-through no-op otherwise. The dialog forces
// OctoPrint object labeling so the body boundary comments are present on every
// G-code flavor.
//
// URL format:
//     ::builtin::pa_line_pattern?body=<path-to-toolpath-file>

bool is_pa_line_url(const std::string &script);

// Run the splicer against `gcode_path`. Returns true if the file was rewritten,
// false if left untouched (no calibration marker). Throws on a malformed URL, an
// unreadable file, a missing body file, a marker-carrying binary G-code, or a
// G-code with no post-marker object body to replace.
bool run_pa_line_post_processor(const std::string &url, const std::string &gcode_path);

// Build a `::builtin::pa_line?body=<path>` URL.
std::string make_pa_line_url(const std::string &body_file);

} // namespace Slic3r

#endif // slic3r_CalibrationPALinePostProcessor_hpp_
