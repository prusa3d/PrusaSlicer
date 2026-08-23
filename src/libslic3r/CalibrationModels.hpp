///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_CalibrationModels_hpp_
#define slic3r_CalibrationModels_hpp_

#include "admesh/stl.h"

namespace Slic3r {

// Shared geometry constants for the temperature tower.
// Exposed so that CalibrationTempDialog can compute per-layer Z heights
// without duplicating magic numbers.
inline constexpr double TEMP_TOWER_BASE_HEIGHT = 1.0;   // mm — base plate height
inline constexpr double TEMP_TOWER_TIER_HEIGHT = 10.0;  // mm — height per tier

// Generate a temperature calibration tower mesh.
// The tower has a 1mm base plate and num_tiers stacked 10mm tiers,
// each with overhang features, holes, cones, cutout, protrusion,
// and raised temperature labels on the back face.
// start_temp: temperature of the first (bottom) tier
// temp_step: temperature decrease per tier (positive value)
indexed_triangle_set make_temp_tower(int num_tiers, int start_temp, int temp_step);

// Generate a serpentine (E-shaped) flow-rate calibration specimen.
// When printed in spiral vase mode, produces a long continuous perimeter
// path ideal for sustained extrusion rate testing.
indexed_triangle_set make_flow_specimen(
    int    num_levels,
    double level_height  = 1.0,
    double width         = 170.0,
    double arm_thickness = 20.0,
    double gap_width     = 20.0,
    int    num_arms      = 3
);

/// Generate a single chevron (V-shape) for pressure advance calibration.
/// The model is a single thick chevron inside a 1-layer-tall rectangular
/// frame, extruded to num_layers × layer_height.  Each group of
/// layers_per_level layers is assigned a different PA value via per-layer
/// custom G-code.
/// Returns the mesh centred at XY origin.
indexed_triangle_set make_pa_pattern(
    int    num_layers     = 4,
    double layer_height   = 0.2,
    double corner_angle   = 90.0,    // degrees — angle at chevron tip
    double arm_length     = 40.0,    // mm — length of each arm
    double wall_thickness = 1.6      // mm — arm width
);

/// Generate two cylindrical towers for retraction calibration.
/// The towers are placed symmetrically about the Y axis, separated
/// by `spacing` mm (center-to-center).  Retraction distance is varied
/// per layer group via custom G-code inserted by the dialog.
indexed_triangle_set make_retraction_towers(
    double height   = 100.0,   // mm — tower height
    double diameter  = 10.0,   // mm — tower diameter
    double spacing   = 50.0    // mm — center-to-center distance
);

/// Generate raised block-letter text mesh for labelling calibration specimens.
/// Text is centred at XY origin, sitting on Z=0, protruding upward to depth_mm.
/// Supports digits 0-9, minus sign, plus sign, percent sign, and period.
indexed_triangle_set make_block_text(const std::string& text, double height_mm, double depth_mm, bool mirror_x = true);

/// Fan speed test tower constants (exposed for dialog Z-height calculation).
inline constexpr double FAN_TOWER_LEVEL_HEIGHT = 10.0;  // mm per fan speed level

/// Generate a fan speed test tower with overhang shelves and bridge windows.
/// Each 10mm level tests a different fan speed (0% at bottom → 100% at top).
/// The tower body is 20×20mm, with 5mm overhang shelves on the front and
/// 8×4mm bridge windows on the right side at each level.
/// @param num_levels  Number of fan speed levels (default 11 for 0-100% in 10% steps)
indexed_triangle_set make_fan_tower(int num_levels = 11);

/// Generate an XYZ dimensional accuracy / shrinkage gauge.
/// Three 10×10mm cross-section bars extend from a common corner along the
/// X, Y, and Z axes.  5mm square through-holes are cut at 25mm intervals
/// on two visible faces of each arm for caliper jaw reference.
/// @param length  Total length of each arm (default 100mm).
/// @param skipped_holes  If non-null, receives the number of measurement holes
///        that could not be cut (0 in the normal case). #40: lets a caller warn
///        the user instead of silently shipping a gauge with missing marks.
indexed_triangle_set make_shrinkage_gauge(double length = 100.0, int *skipped_holes = nullptr);

/// Generate a rounded-rectangle prism (box with rounded corners).
/// Origin at (0,0,0), extends to (width, depth, height).
/// @param corner_r  Radius of the rounded corners (clamped to half the smallest dimension).
/// @param segments  Number of arc segments per corner (default 8).
indexed_triangle_set make_rounded_rect_pad(double width, double depth, double height,
                                           double corner_r = 1.5, int segments = 8);

} // namespace Slic3r

#endif // slic3r_CalibrationModels_hpp_
