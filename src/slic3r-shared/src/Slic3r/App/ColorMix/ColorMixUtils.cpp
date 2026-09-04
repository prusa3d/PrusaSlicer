#include "Slic3r/App/ColorMix/ColorMixUtils.hpp"

#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/Biz/Algorithms/Color.hpp"
#include "Slic3r/Biz/Algorithms/VirtualExtruder.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include <fmt/format.h>

using Slic3r::Domain::ColorRGBA;
using Slic3r::Domain::VirtualExtruder;

using namespace Slic3r::Biz;

namespace Slic3r::App::ColorMix {

ImColor parse_hex_color(const std::string& hex_color)
{
    ColorRGBA color;
    if (!Algorithms::Color::decode_color(hex_color, color)) {
        return ImColor(0x80, 0x80, 0x80);
    }

    return ImColor(color.r(), color.g(), color.b(), color.a());
}

ImColor physical_slot_color(
    const std::vector<std::string>& physical_colors,
    unsigned int extruder_id_1based
)
{
    if (extruder_id_1based >= 1 && extruder_id_1based <= physical_colors.size()) {
        return parse_hex_color(physical_colors[extruder_id_1based - 1]);
    }

    return ImColor(0x80, 0x80, 0x80);
}

std::string effective_color_hex(
    const VirtualExtruder& virtual_extruder,
    const std::vector<std::string>& physical_colors
)
{
    const std::string blended =
        Algorithms::VirtualExtruder::effective_color(virtual_extruder, physical_colors);
    return blended.empty() ? std::string("#808080") : blended;
}

std::string recipe_title(const VirtualExtruder& virtual_extruder)
{
    return fmt::format(fmt::runtime(_u8L("Virtual extruder #{}")), virtual_extruder.id);
}

void clear_children_later(Yoga::Item* container)
{
    for (Yoga::Item* child : container->items()) {
        child->set_visible(false);
        container->remove_later(child);
    }
}

} // namespace Slic3r::App::ColorMix
