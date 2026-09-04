#ifndef slic3r_CustomGCode_hpp_
#define slic3r_CustomGCode_hpp_

#include <stddef.h>
#include <string>
#include <vector>
#include <utility>
#include <cstddef>
#include "Slic3r/Domain/CustomGCode.hpp"

namespace Slic3r {

namespace CustomGCodeUtils {

/* For exporting GCode in GCodeWriter is used XYZF_NUM(val) = PRECISION(val, 3) for XYZ values. 
 * So, let use same value as a permissible error for layer height.
 */
constexpr double epsilon() { return 0.0011; }

// string anlogue of custom_code_per_height mode
static constexpr char SingleExtruderMode[] = "SingleExtruder";
static constexpr char MultiAsSingleMode [] = "MultiAsSingle";
static constexpr char MultiExtruderMode [] = "MultiExtruder";

// If information for custom Gcode per print Z was imported from older Slicer, mode will be undefined.
// So, we should set CustomGCode::Info.mode should be updated considering code values from items.
extern void check_mode_for_custom_gcode_per_print_z(Domain::CustomGCode::Info& info);

// Return pairs of <print_z, 1-based extruder ID> sorted by increasing print_z from custom_gcode_per_print_z.
// print_z corresponds to the first layer printed with the new extruder.
std::vector<std::pair<double, unsigned int>> custom_tool_changes(const Domain::CustomGCode::Info& custom_gcode_per_print_z, size_t num_extruders);

// Return pairs of <print_z, 1-based extruder ID> sorted by increasing print_z from custom_gcode_per_print_z.
// Where print_z corresponds to the layer on which we perform a color change for the specified extruder.
std::vector<std::pair<double, unsigned int>> custom_color_changes(const Domain::CustomGCode::Info& custom_gcode_per_print_z, size_t num_extruders);

} // namespace CustomGCode

} // namespace Slic3r



#endif /* slic3r_CustomGCode_hpp_ */
