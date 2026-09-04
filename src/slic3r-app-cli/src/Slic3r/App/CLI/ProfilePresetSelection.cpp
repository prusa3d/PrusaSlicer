#include "Slic3r/App/CLI/ProfilePresetSelection.hpp"

#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/LegacyFormat.hpp"

#include <functional>
#include <spdlog/spdlog.h>
#include <tl/expected.hpp>
#include <utility>

using Slic3r::Biz::Preset::PresetInteractor;
using Slic3r::Domain::PrinterTechnology;
using Slic3r::Domain::Preset::EvaluatedMaterialPreset;
using Slic3r::Domain::Preset::EvaluatedPrinterPreset;
using Slic3r::Domain::Preset::EvaluatedPrintPreset;
using Slic3r::Domain::Preset::EvaluatedToolPrintPreset;
using Slic3r::Domain::Preset::HwPrinterConfig;

namespace Slic3r::App {

namespace {

/**
 * @brief Profile names resolved into bundle preset IDs.
 */
struct ResolvedProfilePresets
{
    std::string hw_config_id;
    PrinterTechnology technology{PrinterTechnology::FFF};
    std::string printer_preset_id;
    std::string print_preset_id;
    std::vector<std::string> tool_preset_ids;
    std::vector<std::string> material_preset_ids;
};

tl::expected<ResolvedProfilePresets, std::string> resolve_profile_presets(
    PresetInteractor& preset_interactor,
    const ProfilePresetSelectionRequest& request
)
{
    using HwConfigViewItem = std::pair<std::reference_wrapper<const HwPrinterConfig>, bool>;
    using PrinterPresetViewItem =
        std::pair<std::reference_wrapper<const EvaluatedPrinterPreset::Preset>, bool>;
    using PrintPresetViewItem =
        std::pair<std::reference_wrapper<const EvaluatedPrintPreset::Preset>, bool>;
    using ToolPresetViewItem =
        std::pair<std::reference_wrapper<const EvaluatedToolPrintPreset::Preset>, bool>;
    using MaterialPresetViewItem =
        std::pair<std::reference_wrapper<const EvaluatedMaterialPreset::Preset>, bool>;

    std::string errors;

    if (request.print_profile_name.empty()
        || request.material_profile_names.empty()
        || request.printer_profile_name.empty())
    {
        errors +=
            "\nRequest is not completed. All of Print/Material/Printer profiles have to be entered";
        return tl::make_unexpected(errors);
    }

    // The printer profile is matched by the HwPrinterConfig name.
    const HwPrinterConfig* found_hw_config = nullptr;
    for (const HwConfigViewItem hw_config_item : preset_interactor.get_printer_configs()) {
        if (hw_config_item.first.get().name == request.printer_profile_name) {
            found_hw_config = &hw_config_item.first.get();
            break;
        }
    }

    if (found_hw_config == nullptr) {
        errors += "\n"
            + Slic3r::format("Printer profile '%1%' wasn't found.", request.printer_profile_name);
        return tl::make_unexpected(errors);
    }

    ResolvedProfilePresets resolved_presets;
    resolved_presets.hw_config_id = found_hw_config->id;
    resolved_presets.technology   = found_hw_config->technology;

    // The printer preset that belongs to the HwPrinterConfig.
    for (const PrinterPresetViewItem printer_preset_item :
         preset_interactor.get_printer_presets(resolved_presets.hw_config_id))
    {
        resolved_presets.printer_preset_id = printer_preset_item.first.get().id;
        break;
    }

    if (resolved_presets.printer_preset_id.empty()) {
        errors += "\n"
            + Slic3r::format("Printer profile '%1%' wasn't found.", request.printer_profile_name);
    }

    // The print profile is matched by the preset name.
    if (!resolved_presets.printer_preset_id.empty()) {
        for (const PrintPresetViewItem print_preset_item : preset_interactor.get_print_presets(
                 resolved_presets.hw_config_id,
                 resolved_presets.printer_preset_id
             ))
        {
            if (print_preset_item.first.get().name == request.print_profile_name) {
                resolved_presets.print_preset_id = print_preset_item.first.get().id;
                break;
            }
        }

        if (resolved_presets.print_preset_id.empty()) {
            errors += "\n"
                + Slic3r::format("Print profile '%1%' wasn't found.", request.print_profile_name);
        }
    }

    if (!errors.empty()) {
        return tl::make_unexpected(errors);
    }

    // Adjust the material preset list and the tool print preset list based on printer technology.
    std::vector<std::string> material_profile_names = request.material_profile_names;
    std::vector<std::string> tool_profile_names     = request.tool_profile_names;
    if (resolved_presets.technology == PrinterTechnology::SLA) {
        if (material_profile_names.size() > 1) {
            SPDLOG_WARN(
                "Note: More than one SLA material profiles were entered. Extras material profiles will be ignored."
            );
            material_profile_names.resize(1);
        }

        // SLA printers have no tool print presets.
        tool_profile_names.clear();
    } else {
        const size_t extruders_count = found_hw_config->tool_count;

        // For FDM, adjust the material count to match the extruder count.
        if (!material_profile_names.empty()) {
            if (extruders_count > material_profile_names.size()) {
                SPDLOG_WARN(
                    "Note: Less than needed filament profiles were entered. Missed filament profiles will be filled with first material."
                );
                material_profile_names.resize(extruders_count, material_profile_names.front());
            } else if (extruders_count < material_profile_names.size()) {
                SPDLOG_WARN(
                    "Note: More than needed filament profiles were entered. Extra filament profiles will be ignored."
                );
                material_profile_names.resize(extruders_count);
            }
        }

        // For FDM, adjust the tool print count to match the extruder count.
        if (!tool_profile_names.empty()) {
            if (extruders_count > tool_profile_names.size()) {
                SPDLOG_WARN(
                    "Note: Less than needed tool print profiles were entered. Missed tool print profiles will be filled with first tool print."
                );
                tool_profile_names.resize(extruders_count, tool_profile_names.front());
            } else if (extruders_count < tool_profile_names.size()) {
                SPDLOG_WARN(
                    "Note: More than needed tool print profiles were entered. Extra tool print profiles will be ignored."
                );
                tool_profile_names.resize(extruders_count);
            }
        }
    }

    for (size_t tool_idx = 0; tool_idx < tool_profile_names.size(); ++tool_idx) {
        const std::string& tool_profile_name = tool_profile_names[tool_idx];
        std::string found_tool_preset_id;
        for (const ToolPresetViewItem tool_preset_item : preset_interactor.get_tool_print_presets(
                 resolved_presets.hw_config_id,
                 resolved_presets.printer_preset_id,
                 resolved_presets.print_preset_id,
                 tool_idx
             ))
        {
            if (tool_preset_item.first.get().name == tool_profile_name) {
                found_tool_preset_id = tool_preset_item.first.get().id;
                break;
            }
        }

        if (found_tool_preset_id.empty()) {
            errors +=
                "\n" + Slic3r::format("Tool print profile '%1%' wasn't found.", tool_profile_name);
        } else {
            resolved_presets.tool_preset_ids.push_back(found_tool_preset_id);
        }
    }

    for (size_t material_idx = 0; material_idx < material_profile_names.size(); ++material_idx) {
        const std::string& material_profile_name = material_profile_names[material_idx];
        std::string found_material_preset_id;
        for (const MaterialPresetViewItem material_preset_item :
             preset_interactor.get_material_presets(
                 resolved_presets.hw_config_id,
                 resolved_presets.printer_preset_id,
                 resolved_presets.print_preset_id,
                 material_idx
             ))
        {
            if (material_preset_item.first.get().name == material_profile_name) {
                found_material_preset_id = material_preset_item.first.get().id;
                break;
            }
        }

        if (found_material_preset_id.empty()) {
            errors += "\n"
                + Slic3r::format("Material profile '%1%' wasn't found.", material_profile_name);
        } else {
            resolved_presets.material_preset_ids.push_back(found_material_preset_id);
        }
    }

    if (!errors.empty()) {
        return tl::make_unexpected(errors);
    }

    return resolved_presets;
}

} // namespace

std::optional<std::string> select_profile_presets_by_name(
    PresetInteractor& preset_interactor,
    const ProfilePresetSelectionRequest& request
)
{
    const tl::expected<ResolvedProfilePresets, std::string> resolved_presets =
        resolve_profile_presets(preset_interactor, request);
    if (!resolved_presets.has_value()) {
        return resolved_presets.error();
    }

    preset_interactor.select_printer_preset(
        resolved_presets->hw_config_id,
        resolved_presets->printer_preset_id
    );
    preset_interactor.select_print_preset(resolved_presets->print_preset_id);

    for (size_t tool_idx = 0; tool_idx < resolved_presets->tool_preset_ids.size(); ++tool_idx) {
        preset_interactor.select_tool_print_preset(
            tool_idx,
            resolved_presets->tool_preset_ids[tool_idx]
        );
    }

    for (size_t material_idx = 0; material_idx < resolved_presets->material_preset_ids.size();
         ++material_idx)
    {
        preset_interactor.select_material_preset(
            material_idx,
            resolved_presets->material_preset_ids[material_idx]
        );
    }

    return std::nullopt;
}

} // namespace Slic3r::App
