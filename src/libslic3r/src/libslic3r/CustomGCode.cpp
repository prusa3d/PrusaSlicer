#include "libslic3r/CustomGCode.hpp"

#include <cassert>

#include "libslic3r/GCode.hpp"

namespace Slic3r {

namespace CustomGCodeUtils {

using Domain::CustomGCode::Info;
using Domain::CustomGCode::Mode;
using Domain::CustomGCode::Type;
using Domain::CustomGCode::Item;

// If information for custom Gcode per print Z was imported from older Slicer, mode will be undefined.
// So, we should set CustomGCode::Info.mode should be updated considering code values from items.
extern void check_mode_for_custom_gcode_per_print_z(Info& info)
{
    if (info.mode != Mode::Undef)
        return;

    bool is_single_extruder = true;
    for (const Item& item : info.gcodes)
    {
        if (item.type == Type::ToolChange) {
            info.mode = Mode::MultiAsSingle;
            return;
        }
        if (item.type == Type::ColorChange && item.extruder > 1)
            is_single_extruder = false;
    }

    info.mode = is_single_extruder ? Mode::SingleExtruder : Mode::MultiExtruder;
}

// Return pairs of <print_z, 1-based extruder ID> sorted by increasing print_z from custom_gcode_per_print_z.
// print_z corresponds to the first layer printed with the new extruder.
std::vector<std::pair<double, unsigned int>> custom_tool_changes(const Info& custom_gcode_per_print_z, size_t num_extruders)
{
    std::vector<std::pair<double, unsigned int>> custom_tool_changes;
    for (const Item& custom_gcode : custom_gcode_per_print_z.gcodes)
        if (custom_gcode.type == Type::ToolChange) {
            // If extruder count in PrinterSettings was changed, use default (0) extruder for extruders, more than num_extruders
            assert(custom_gcode.extruder >= 0);
            custom_tool_changes.emplace_back(custom_gcode.print_z, static_cast<unsigned int>(size_t(custom_gcode.extruder) > num_extruders ? 1 : custom_gcode.extruder));
        }
    return custom_tool_changes;
}

// Return pairs of <print_z, 1-based extruder ID> sorted by increasing print_z from custom_gcode_per_print_z.
// Where print_z corresponds to the layer on which we perform a color change for the specified extruder.
std::vector<std::pair<double, unsigned int>> custom_color_changes(const Info& custom_gcode_per_print_z, size_t num_extruders)
{
    std::vector<std::pair<double, unsigned int>> custom_color_changes;
    for (const Item& custom_gcode : custom_gcode_per_print_z.gcodes)
        if (custom_gcode.type == Type::ColorChange) {
            // If extruder count in PrinterSettings was changed, ignore custom g-codes for extruder ids bigger than num_extruders.
            assert(custom_gcode.extruder >= 0);
            if (size_t(custom_gcode.extruder) <= num_extruders) {
                custom_color_changes.emplace_back(custom_gcode.print_z, static_cast<unsigned int>(custom_gcode.extruder));
            }
        }
    return custom_color_changes;
}

} // namespace CustomGCode

} // namespace Slic3r
