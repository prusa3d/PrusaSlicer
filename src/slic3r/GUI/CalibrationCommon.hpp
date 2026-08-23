///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_CalibrationCommon_hpp_
#define slic3r_CalibrationCommon_hpp_

#include <string>

namespace Slic3r { namespace GUI {

// Give a generated calibration model a meaningful export filename.
//
// output_filename_format defaults to a "{input_filename_base}_..." template,
// and input_filename_base is derived from the first object's name (or its
// source file). For generated calibration models that name is either a temp
// STL stem (e.g. "temp_tower") or, worse, an object label containing a '.'
// (the Flow Rate pads are named "-.05" etc.), which PrintBase truncates at the
// dot down to a bare "-", producing files like "-_0.4n_...". Substitute a
// fixed, human-readable test name for that token so every calibration export
// gets a clean, self-describing prefix (e.g. "FlowRate_0.4n_0.2mm_PLA_...").
//
// Edits the active (edited) print preset and reloads the Print tab, so call it
// after any prints.discard_current_changes() the dialog performs.
void apply_calibration_filename_prefix(const std::string& test_name);

}} // namespace Slic3r::GUI

#endif // slic3r_CalibrationCommon_hpp_
