#pragma once


#include <vector>
#include <set>
#include <string>
#include "Slic3r/Domain/Preset/HwConfig.hpp"
#include "Slic3r/Domain/Preset/PresetTree.hpp"
#include "Slic3r/Domain/ConfigBoxesFDM.hpp"
#include "Slic3r/Domain/ConfigBoxesSLA.hpp"
#include "Slic3r/Domain/FullConfigFDM.hpp"
#include "Slic3r/Log.hpp"

namespace Slic3r::Domain::Preset {

template <typename T>
concept ConfigBoxLike = std::is_base_of_v<ConfigBox, T>;

template <typename T>
concept ConfigBoxLikeOrMonostate = std::is_base_of_v<ConfigBox, T>
    || std::is_same_v<std::monostate, T>;

struct EvaluatedPresetMetadata
{
    std::string root_id;
    std::string id;
    std::string name;
    FeatureValueMap features;
    std::vector<std::string> conditions;
};

template <ConfigBoxLike ConfigFdmType, ConfigBoxLikeOrMonostate ConfigSlaType>
struct EvaluatedPreset
{
    using PresetValues = std::variant<ConfigFdmType, ConfigSlaType>;

    PresetKind kind{PresetKind::FdmPrinter};
    PresetOrigin origin{PresetOrigin::System};
    // only for user presets
    std::optional<std::string> user_file;

    std::string root_id;
    std::string id;
    std::string name;
    PresetValues values;
    FeatureValueMap features;
    std::vector<std::string> conditions;
    SourceLocation last_node_location;

    bool has_same_values(const EvaluatedPreset& rhs) const
    {
#ifndef _NDEBUG
        if (rhs.name.starts_with(name.substr(0, 20))
            && !(
                kind == rhs.kind
                && root_id == rhs.root_id
                && id == rhs.id
                && name == rhs.name
                && values == rhs.values
                && features == rhs.features
            ))
        {
            SPDLOG_INFO("No match between {} ({}) and {} ({}):", name, id, rhs.name, rhs.id);
            if (kind != rhs.kind)
            {
                SPDLOG_INFO("- kind mismatched: {} != {}", int(kind), int(rhs.kind));
            }
            if (root_id != rhs.root_id)
            {
                SPDLOG_INFO("- root_id mismatched: {} != {}", root_id, rhs.root_id);
            }
            if (id != rhs.id)
            {
                SPDLOG_INFO("- id mismatched: {} != {}", id, rhs.id);
            }
            if (name != rhs.name)
            {
                SPDLOG_INFO("- name mismatched: {} != {}", name, rhs.name);
            }
            if (values != rhs.values)
            {
                SPDLOG_INFO("- values mismatched: {} != {}", config_box().items.all_items().size(), rhs.config_box().items.all_items().size());
            }
            if (features != rhs.features)
            {
                SPDLOG_INFO("- features mismatched: {} != {}", features.size(), rhs.features.size());
            }

        }
#endif
        // last_node_location and conditions intentionally left
        return kind == rhs.kind
            && root_id == rhs.root_id
            && id == rhs.id
            && name == rhs.name
            && values == rhs.values
            && features == rhs.features /*&& lhs.conditions == rhs.conditions*/;
    }

    static EvaluatedPreset make(PresetKind kind, const EvaluatedPresetMetadata& metadata, PresetValues values)
    {
        return {
            .kind = kind,
            .root_id = metadata.root_id,
            .id = metadata.id,
            .name = metadata.name,
            .values = values,
            .features = metadata.features,
            .conditions = metadata.conditions,
        };
    }

    [[nodiscard]] EvaluatedPresetMetadata metadata() const
    {
        return {
            .root_id = root_id,
            .id = id,
            .name = name,
            .features = features,
            .conditions = conditions,
        };
    }

    [[nodiscard]] std::string_view short_name() const
    {
        return Domain::Preset::short_name(name);
    }

    [[nodiscard]] const ConfigBox& config_box() const
    {
        return std::visit(
            overloaded{
                [](const auto& c) -> const ConfigBox& { return c; },
                [](const std::monostate& c) -> const ConfigBox& { PANIC("Unsupported"); },
            },
            values
        );
    }

    [[nodiscard]] ConfigBox& config_box()
    {
        return std::visit(
            overloaded{
                [](auto& c) -> ConfigBox& { return c; },
                [](std::monostate& c) -> ConfigBox& { PANIC("Unsupported"); },
            },
            values
        );
    }
};

// using EvaluatedPresets = std::vector<EvaluatedPreset>;

struct EvaluatedToolPrintPreset
{
    using Preset = EvaluatedPreset<ToolPrintSettings, std::monostate>;
    Preset preset;

    EvaluatedToolPrintPreset()                                    = default;
    EvaluatedToolPrintPreset(const EvaluatedToolPrintPreset&)     = default;
    EvaluatedToolPrintPreset(EvaluatedToolPrintPreset&&) noexcept = default;

    EvaluatedToolPrintPreset& operator=(const EvaluatedToolPrintPreset&) = default;
    EvaluatedToolPrintPreset& operator=(EvaluatedToolPrintPreset&&) noexcept = default;

    explicit EvaluatedToolPrintPreset(Preset&& preset) : preset(std::move(preset)) {}

};

/**
 * @brief Single tool `tool_print` presets variants (e.g. quality vs. speed variants).
 */
using SingleToolEvaluatedToolPrintPresets = std::vector<EvaluatedToolPrintPreset>;
/**
 * @brief `tool_print` variants for all tools.
 */
using AllToolsEvaluatedToolPrintPresets = std::vector<SingleToolEvaluatedToolPrintPresets>;

struct EvaluatedMaterialPreset
{
    using Preset = EvaluatedPreset<FilamentSettings, SLAMaterialSettings>;
    Preset preset;

    EvaluatedMaterialPreset()                                   = default;
    EvaluatedMaterialPreset(const EvaluatedMaterialPreset&)     = default;
    EvaluatedMaterialPreset(EvaluatedMaterialPreset&&) noexcept = default;

    EvaluatedMaterialPreset& operator=(const EvaluatedMaterialPreset&) = default;
    EvaluatedMaterialPreset& operator=(EvaluatedMaterialPreset&&) noexcept = default;

    explicit EvaluatedMaterialPreset(Preset&& preset) : preset(std::move(preset)) {}

};

using SingleToolEvaluatedMaterialPresets = std::vector<EvaluatedMaterialPreset>;
using AllToolsEvaluatedMaterialPresets  = std::vector<SingleToolEvaluatedMaterialPresets>;


struct EvaluatedPrintPreset
{
    using Preset = EvaluatedPreset<PrintSettings, SLAPrintSettings>;
    Preset preset;
    AllToolsEvaluatedToolPrintPresets tools;
    AllToolsEvaluatedMaterialPresets materials;

    EvaluatedPrintPreset()                                = default;
    EvaluatedPrintPreset(const EvaluatedPrintPreset&)     = default;
    EvaluatedPrintPreset(EvaluatedPrintPreset&&) noexcept = default;

    EvaluatedPrintPreset& operator=(const EvaluatedPrintPreset&) = default;
    EvaluatedPrintPreset& operator=(EvaluatedPrintPreset&&) noexcept = default;

    EvaluatedPrintPreset(
        Preset&& preset,
        AllToolsEvaluatedToolPrintPresets&& tools,
        AllToolsEvaluatedMaterialPresets&& materials
    ) :
        preset(std::move(preset)),
        tools(std::move(tools)),
        materials(std::move(materials))
    {}

    const EvaluatedToolPrintPreset* find_tool_preset_by_id(size_t tool_idx, const std::string& id) const;
    const EvaluatedMaterialPreset* find_material_preset_by_id(size_t tool_idx, const std::string& id) const;

};

using EvaluatedPrintPresets = std::vector<EvaluatedPrintPreset>;


struct EvaluatedPrinterPreset
{
    using Preset = EvaluatedPreset<PrinterSettings, SLAPrinterSettings>;
    HwPrinterConfig hw_config;
    Preset preset;
    EvaluatedPrintPresets prints;

    const EvaluatedPrintPreset* find_print_preset_by_id(const std::string& id) const;

    PrinterTechnology technology() const
    {
        return preset.kind == PresetKind::FdmPrinter ? PrinterTechnology::FFF :
                                                       PrinterTechnology::SLA;
    }

    bool is_valid() const;

};

template <typename EP>
const EP* find_preset_with_same_value(const typename EP::Preset& preset, const std::vector<EP>& presets)
{
    auto it = std::ranges::find_if(presets, [&preset](const auto& p) { return p.preset.has_same_values(preset); });
    return it == presets.end() ? nullptr : &(*it);
}


} // namespace Slic3r::Domain::Preset
