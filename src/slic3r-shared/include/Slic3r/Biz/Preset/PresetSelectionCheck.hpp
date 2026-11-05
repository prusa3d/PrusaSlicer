#pragma once
#include "Slic3r/Domain/ConfigDef.hpp"
#include "Slic3r/Biz/Preset/PreferredMaterialTypes.hpp"

#include <string>
#include <vector>

namespace Slic3r::Domain {
struct ConfigBox;
} // namespace Slic3r::Domain

namespace Slic3r::Domain::Preset {
struct HwPrinterConfig;
} // namespace Slic3r::Domain::Preset

namespace Slic3r::Biz::Preset {

class PresetInteractor;

namespace PresetSelectionCheck {

void filter_diff_keys(const Domain::ConfigBox& cbox, std::vector<std::string>& diff_keys);

bool can_select_printer_preset(
    PresetInteractor& preset_interactor,
    const std::string& printer_hw_config_id,
    const std::string& printer_preset_id
);

/**
 * @brief Checks unsaved changes before PresetInteractor::select_printer_config().
 * @param material_types  The same as passed to PresetInteractor::select_printer_config().
 * @return False if the user canceled the switch.
 */
bool can_select_printer_config(
    PresetInteractor& preset_interactor,
    const std::string& hw_config_id,
    const PreferredMaterialTypes& material_types = {}
);

/**
 * @brief Checks unsaved changes before PresetInteractor::select_external_printer_config().
 * If a new hw config is going to be created, new configuration is evaluated without storing it.
 * @param material_types  The same as passed to PresetInteractor::select_external_printer_config().
 * @return False if the user canceled the switch.
 */
bool can_select_external_printer_config(
    PresetInteractor& preset_interactor,
    const Domain::Preset::HwPrinterConfig& printer,
    const PreferredMaterialTypes& material_types = {}
);

bool can_select_print_preset(PresetInteractor& preset_interactor, const std::string& id);

bool can_select_tool_print_preset(
    PresetInteractor& preset_interactor,
    size_t tool_index,
    const std::string& id
);

bool can_select_material_preset(
    PresetInteractor& preset_interactor,
    size_t material_index,
    const std::string& id
);

} // namespace PresetSelectionCheck

} // namespace Slic3r::Biz::Preset
