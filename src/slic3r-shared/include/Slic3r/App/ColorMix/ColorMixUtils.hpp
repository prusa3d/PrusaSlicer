#pragma once

#include "Slic3r/Domain/VirtualExtruder.hpp"

#include <imgui.h>

#include <string>
#include <vector>

namespace Slic3r::App::Yoga {
class Item;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App::ColorMix {

/**
 * @brief Parse a config hex color like "#AA5500".
 */
ImColor parse_hex_color(const std::string& hex_color);

/**
 * @brief Color of the given 1-based physical slot.
 */
ImColor physical_slot_color(
    const std::vector<std::string>& physical_colors,
    unsigned int extruder_id_1based
);

/**
 * @brief The color the user assigned to the recipe, or the one predicted for the mix.
 */
std::string effective_color_hex(
    const Domain::VirtualExtruder& virtual_extruder,
    const std::vector<std::string>& physical_colors
);

std::string recipe_title(const Domain::VirtualExtruder& virtual_extruder);

void clear_children_later(Yoga::Item* container);

} // namespace Slic3r::App::ColorMix
