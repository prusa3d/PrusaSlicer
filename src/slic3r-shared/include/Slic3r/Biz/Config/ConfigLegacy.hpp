#pragma once
#include <string>
#include <optional>
#include <vector>

#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Domain/ConfigPack.hpp"
#include "Slic3r/Domain/Preset/HwConfig.hpp"

namespace Slic3rLegacy { class DynamicPrintConfig; }

namespace Slic3r::Biz {

struct LegacyHwToolConfig
{
    std::optional<bool> nozzle_high_flow;
    std::optional<double> nozzle_diameter;
};

using LegacyHwToolConfigs = std::vector<LegacyHwToolConfig>;

/**
 * @brief A set of metadata extracted from Slic3rLegacy::DynamicPrintConfig used to create related
 * HwPrintConfig.
 */
struct LegacyPresetMetadata
{
    Domain::PrinterTechnology technology;
    std::string printer_model;
    std::string printer_notes;
    LegacyHwToolConfigs tools;
    std::string printer_settings_id;
    std::string print_settings_id;
    std::vector<std::string> material_settings_id;
};

// Loads config from INI / GCODE / BGCODE produced by PrusaSlicer < 3.0.0 and converts
// all matching keys into the provided ConfigBox.
// May throw!
Domain::ConfigPack load_config_from_legacy_file(const std::string& filename);

Domain::ConfigPack convert_dynamic_print_config_to_new(Slic3rLegacy::DynamicPrintConfig& cfg);
LegacyPresetMetadata extract_legacy_preset_metadata(Slic3rLegacy::DynamicPrintConfig& cfg);
void fill_config_box_from_legacy(const Slic3rLegacy::DynamicPrintConfig& cfg, Domain::ConfigBox& box);

// The following only works for volume and object settings.
Slic3rLegacy::DynamicPrintConfig convert_box_to_dynamic_print_config(const Domain::ConfigBox& box);

// Export the config in the old format.
std::string serialize_as_legacy_config(
    const Domain::ConfigPack& config_pack,
    const Domain::Preset::HwPrinterConfig& hw_config
);

} // namespace Slic3r::Biz
