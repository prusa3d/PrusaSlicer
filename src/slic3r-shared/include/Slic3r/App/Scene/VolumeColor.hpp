#pragma once

#include "Slic3r/Domain/Color.hpp"
#include "Slic3r/Domain/ModelVolume.hpp"

#include <optional>
#include <vector>

namespace Slic3r::Domain {
class ConfigContainer;
} // namespace Slic3r::Domain

namespace Slic3r::App::Scene {

static const std::unordered_map<Domain::ModelVolumeType, Domain::ColorRGBA> VOLUME_COLORS = {
    {Domain::ModelVolumeType::MODEL_PART, {1.0f, 0.5f, 0.0f, 1.0f}},
    {Domain::ModelVolumeType::NEGATIVE_VOLUME, {0.5f, 0.5f, 0.5f, 0.5f}},
    {Domain::ModelVolumeType::SUPPORT_BLOCKER, {1.0f, 0.2f, 0.2f, 0.5f}},
    {Domain::ModelVolumeType::SUPPORT_ENFORCER, {0.2f, 0.2f, 1.0f, 0.5f}},
    {Domain::ModelVolumeType::PARAMETER_MODIFIER, {1.0f, 1.0f, 0.2f, 0.5f}},
    {Domain::ModelVolumeType::INVALID, {1.0f, 0.2f, 0.2f, 0.5f}},
};

/**
 * @brief Returns the color used to draw a volume in the scene.
 * @param slot_colors Colors of all extruder slots of a config container.
 * @param volume Volume to get the color for.
 * @param config_container Config container the volume belongs to.
 * @return Color of the volume's extruder, or no value when there are no slot colors.
 */
std::optional<Domain::ColorRGBA> color_from_extruder_slot(
    const std::vector<Domain::ColorRGB>& slot_colors,
    const Domain::ModelVolume& volume,
    const Domain::ConfigContainer& config_container
);

} // namespace Slic3r::App::Scene
