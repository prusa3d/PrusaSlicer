#pragma once

#include <map>
#include <tuple>
#include "Slic3r/Domain/SelectionId.hpp"
#include "Slic3r/Domain/Preset/EvaluatedPreset.hpp"
#include "Slic3r/Biz/Preset/PresetInteractorConfigContainerContext.hpp"
#include "Slic3r/Domain/Preset/Bundle.hpp"

namespace Slic3r::Domain { class Project; }

namespace Slic3r::Biz::Preset {


/**
 * @brief Container for presets loaded at runtime (i.e. coming from 3MF file).
 */
struct RuntimePresets
{
    using PrinterPreset = Domain::Preset::EvaluatedPrinterPreset::Preset;
    using PrintPreset = Domain::Preset::EvaluatedPrintPreset::Preset;
    using ToolPrintPreset = Domain::Preset::EvaluatedToolPrintPreset::Preset;
    using MaterialPreset = Domain::Preset::EvaluatedMaterialPreset::Preset;

    using HwConfigPrinterKey = std::tuple<std::string, std::string>;
    using HwConfingPrinterPrintKey = std::tuple<std::string, std::string, std::string>;

    Domain::Preset::PrinterConfigs printer_configs;

    /**
     * @brief Map where key is parent HW printer config ID and value is vector of printer presets to
     * belong to that HW printer configuration.
     */
    std::map<std::string, std::vector<PrinterPreset>> printer{};

    /**
     * @brief Map where key is parent printer preset ID and value is vector of print presets to
     * belong to that printer preset.
     */
    std::map<HwConfigPrinterKey, std::vector<PrintPreset>> print{};

    using SingleToolPrints = std::vector<ToolPrintPreset>;
    using AllToolPrints = std::vector<SingleToolPrints>;

    /**
     * @brief Map where key is parent print preset ID and value is vector of tool print presets to
     * belong to that print preset.
     */
    std::map<HwConfingPrinterPrintKey, AllToolPrints> tool_print{};

    using SingleSlotMaterials = std::vector<MaterialPreset>;
    using AllSlotMaterials = std::vector<SingleSlotMaterials>;
    /**
     * @brief Map where key is parent print preset ID and value is vector of material presets to
     * belong to that print preset.
     */
    std::map<HwConfingPrinterPrintKey, AllSlotMaterials> material{};

    const Domain::Preset::HwPrinterConfig* find_printer_config_by_id(const std::string& hw_config_id) const;
    Domain::Preset::HwPrinterConfig* find_printer_config_by_id(const std::string& hw_config_id);
    const PrinterPreset* find_printer_preset_by_id(const std::string& hw_config_id, const std::string& printer_preset_id) const;
    const PrintPreset* find_print_preset_by_id(const HwConfigPrinterKey& parent, const std::string& print_id) const;
    const ToolPrintPreset* find_tool_print_preset_by_id(const HwConfingPrinterPrintKey& parent, size_t tool_index, const std::string& tool_print_id) const;
    const MaterialPreset* find_material_preset_by_id(const HwConfingPrinterPrintKey& parent, size_t material_index, const std::string& material_id) const;
    void add_tool_print(const HwConfingPrinterPrintKey& parent, const Domain::Preset::HwPrinterConfig& hw_config, size_t tool_index, const ToolPrintPreset& tool_print);
    void add_material(const HwConfingPrinterPrintKey& parent, const Domain::Preset::HwPrinterConfig& hw_config, size_t slot_index, const MaterialPreset& material);


};

struct PresetInteractorProjectContext
{
    using ConfigContainerContexts = std::map<Domain::SelectionId, PresetInteractorConfigContainerContext>;

    Domain::SelectionId project_id;
    Domain::SelectionId selected_config_container_id{Domain::INVALID_ID};
    // TODO: Selected Object / Volume with ModelConfigObject
    RuntimePresets runtime_presets;

    ConfigContainerContexts  config_containers;
    /**
     * @brief Config being editted but not being valid (so it can be set to cc, but we still want
     * to keep for editting, so we have some chance to make it valid
     */
    std::optional<Domain::Preset::HwPrinterConfig> invalid_hw_config;

};

}
