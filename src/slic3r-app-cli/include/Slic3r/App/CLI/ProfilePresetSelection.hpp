#pragma once

#include <optional>
#include <string>
#include <vector>

namespace Slic3r::Biz::Preset {
class PresetInteractor;
} // namespace Slic3r::Biz::Preset

namespace Slic3r::App {

struct ProfilePresetSelectionRequest
{
    std::string printer_profile_name;
    std::string print_profile_name;
    std::vector<std::string> material_profile_names;
    std::vector<std::string> tool_profile_names;
};

/**
 * @brief Resolves the profile names to bundle preset IDs and selects them through the
 * given PresetInteractor.
 *
 * @return The accumulated error message on failure, std::nullopt on success.
 */
std::optional<std::string> select_profile_presets_by_name(
    Biz::Preset::PresetInteractor& preset_interactor,
    const ProfilePresetSelectionRequest& request
);

} // namespace Slic3r::App
