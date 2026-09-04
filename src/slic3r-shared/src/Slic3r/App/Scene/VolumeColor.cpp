#include "Slic3r/App/Scene/VolumeColor.hpp"

#include "Slic3r/Biz/Algorithms/VirtualExtruder.hpp"
#include "Slic3r/Domain/ConfigContainer.hpp"

#include "libslic3r/ExtruderCandidates.hpp"

#include <algorithm>
#include <set>
#include <variant>

using Slic3r::Domain::ColorRGB;
using Slic3r::Domain::ColorRGBA;
using Slic3r::Domain::ConfigContainer;
using Slic3r::Domain::ModelObject;
using Slic3r::Domain::ModelVolume;
using Slic3r::Domain::PrintSettings;
using Slic3r::Domain::VirtualExtruder;
using Slic3r::Domain::VirtualExtruders;

using Slic3r::Biz::Algorithms::VirtualExtruder::effective_color;
using Slic3r::Biz::Slicing::get_volume_extruder_candidates;

namespace Slic3r::App::Scene {

namespace {

unsigned int volume_extruder_color_idx(const ModelVolume& volume)
{
    if (volume.get_object() == nullptr) {
        return 0;
    }

    const int extruder_id = volume.extruder_id();
    return extruder_id > 0 ? static_cast<unsigned int>(extruder_id - 1) : 0;
}

unsigned int
volume_extruder_color_idx(const ModelVolume& volume, const ConfigContainer& config_container)
{
    const PrintSettings* print_settings =
        std::get_if<PrintSettings>(&config_container.selected_preset().print.values);

    const ModelObject* object = volume.get_object();
    if (print_settings != nullptr && object != nullptr) {
        const std::set<unsigned> extruder_candidates = get_volume_extruder_candidates(
            volume.volume_settings,
            object->object_settings,
            *print_settings,
            config_container.virtual_extruders()
        );

        if (extruder_candidates.size() == 1) {
            return *extruder_candidates.begin();
        }
    }

    return volume_extruder_color_idx(volume);
}

std::optional<ColorRGBA>
extruder_color(const std::vector<ColorRGB>& slot_colors, const unsigned int extruder_idx)
{
    if (slot_colors.empty()) {
        return std::nullopt;
    }

    const ColorRGB& color = slot_colors[extruder_idx < slot_colors.size() ? extruder_idx : 0];
    return ColorRGBA{color.r(), color.g(), color.b(), 1.f};
}

std::optional<ColorRGBA> virtual_extruder_color(
    const std::vector<ColorRGB>& slot_colors,
    const int extruder_id,
    const VirtualExtruders& virtual_extruders
)
{
    const auto virtual_extruder_it = std::ranges::find_if(
        virtual_extruders,
        [extruder_id](const VirtualExtruder& virtual_extruder)
        { return extruder_id > 0 && virtual_extruder.id == static_cast<unsigned int>(extruder_id); }
    );
    if (virtual_extruder_it == virtual_extruders.end() || slot_colors.empty()) {
        return std::nullopt;
    }

    const ColorRGB color =
        effective_color(*virtual_extruder_it, slot_colors).value_or(ColorRGB::GRAY());
    return ColorRGBA{color.r(), color.g(), color.b(), 1.f};
}

} // namespace

std::optional<ColorRGBA> color_from_extruder_slot(
    const std::vector<ColorRGB>& slot_colors,
    const ModelVolume& volume,
    const ConfigContainer& config_container
)
{
    const std::optional<ColorRGBA> assigned_virtual_extruder_color = virtual_extruder_color(
        slot_colors,
        volume.extruder_id(),
        config_container.virtual_extruders()
    );

    if (assigned_virtual_extruder_color.has_value()) {
        return assigned_virtual_extruder_color;
    }

    return extruder_color(slot_colors, volume_extruder_color_idx(volume, config_container));
}

} // namespace Slic3r::App::Scene
