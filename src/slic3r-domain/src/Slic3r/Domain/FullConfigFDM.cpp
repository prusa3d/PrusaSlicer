#include "Slic3r/Domain/FullConfigFDM.hpp"

namespace Slic3r::Domain {

namespace {
template<typename T>
BoxRefs convert_to_box_refs(
    const std::vector<T>& settings
)
{
    BoxRefs result;
    result.insert(result.end(), settings.cbegin(), settings.cend());
    return result;
}

template<typename T>
MutBoxRefs convert_to_mut_box_refs(
    std::vector<T>& settings
)
{
    MutBoxRefs result;
    result.insert(result.end(), settings.begin(), settings.end());
    return result;
}
}

BoxOrBoxesVector as_boxes(const ConfigPackFDM& config_pack) {
    BoxOrBoxesVector result;
    result.push_back(config_pack.print);
    result.push_back(convert_to_box_refs(config_pack.tool));
    result.push_back(config_pack.printer);
    result.push_back(convert_to_box_refs(config_pack.filament));
    result.push_back(config_pack.project);
    return result;
}

MutBoxOrBoxesVector as_mut_boxes(ConfigPackFDM& config_pack) {
    MutBoxOrBoxesVector result;
    result.push_back(config_pack.print);
    result.push_back(convert_to_mut_box_refs(config_pack.tool));
    result.push_back(config_pack.printer);
    result.push_back(convert_to_mut_box_refs(config_pack.filament));
    result.push_back(config_pack.project);
    return result;
}

FullConfigFDM::FullConfigFDM(
    const ConfigPackFDM& config_pack,
    const std::vector<unsigned>& extruder_candidates,
    const Preset::HwPrinterConfig& hw_config
) :
    FullConfig{as_boxes(config_pack), extruder_candidates, hw_config},
    m_virtual_extruders{config_pack.virtual_extruders}
{}

FullConfigFDM FullConfigFDM::defaults()
{
    Preset::HwToolConfig tool_config;

    Preset::HwPrinterConfig hw_config{
        .id                   = generate_uuid(),
        .printer_id           = {},
        .legacy_printer_model = {},
        .vendor_id            = {},
        .repo_id              = {},
        .repo_version         = {},
        .name                 = {},
        .short_name           = {},
        .technology           = PrinterTechnology::FFF,
        .model                = {},
        .tool_count           = 1,
        .features             = {},
        .visual               = {},
        .tools                = {1, Preset::HwToolConfig{.features = {{"nozzle_diameter", 0.4}}}},
        .feeders              = {},
        .materials            = {},
        .sheet                = {}};

    return {ConfigPackFDM{}, {0}, hw_config};
}

PartialObjectConfigFDM::PartialObjectConfigFDM(
    const ObjectSettings& object_settings,
    const Preset::HwPrinterConfig& hw_config
) :
    PartialConfig{
        {object_settings},
        hw_config
    }
{}

PartialVolumeConfigFDM::PartialVolumeConfigFDM(
    const VolumeSettings& volume_settings,
    const Preset::HwPrinterConfig& hw_config
) :
    PartialConfig{
        {volume_settings},
        hw_config
    }
{}

} // namespace Slic3r::Domain
