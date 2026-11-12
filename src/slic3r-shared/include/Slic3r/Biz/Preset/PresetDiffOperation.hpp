#pragma once

#include <map>
#include <optional>

namespace Slic3r::Domain {
struct ConfigValue;
} // namespace Slic3r::Domain

namespace Slic3r::Biz::Preset {

enum class PresetDiffOperation
{
    Undef, // used for Cross or Cancel buttons
    Transfer,
    Discard,
    Save,
};

/**
 * @struct PresetSwitchState
 * @brief Describes a preset switching operation and its related data.
 *
 * Used to store information about saving or transferring presets.
 */
struct PresetSwitchState
{
    /**
     * @brief Operation type (e.g., Discard, Save or Transfer).
     */
    PresetDiffOperation operation;

    /**
     * @brief Lists of affected options with their values.
     *
     * - For **Save**, contains unsaved options.
     * - For **Transfer**, contains options to transfer to the new preset.
     */
    std::map<std::string, Domain::ConfigValue> items;
    /**
     * @brief Override values for affected options.
     *
     * Note: Can contain std::nullopt value if the override was disabled.
     */
    std::map<std::string, std::optional<Domain::ConfigValue>> overrides;

    /**
     * @brief Name of the new preset, valid only for the **Save** operation.
     */
    std::optional<std::string> new_preset_name{std::nullopt};
};

} // namespace Slic3r::Biz::Preset
