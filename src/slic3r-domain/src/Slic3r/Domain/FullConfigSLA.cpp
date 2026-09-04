#include "Slic3r/Domain/FullConfigSLA.hpp"

namespace Slic3r::Domain {

BoxOrBoxesVector as_boxes(const ConfigPackSLA& config_pack) {
    BoxOrBoxesVector result;
    result.push_back(config_pack.sla_printer_settings);
    result.push_back(config_pack.sla_print_settings);
    result.push_back(config_pack.sla_material_settings);
    return result;
}

FullConfigSLA::FullConfigSLA(const ConfigPackSLA& config_pack, const Preset::HwPrinterConfig& hw_config)
    : FullConfig{as_boxes(config_pack), {}, hw_config}
{}

PartialObjectConfigSLA::PartialObjectConfigSLA(
    const SLAObjectSettings& object_settings,
    const Preset::HwPrinterConfig& hw_config
)
    : PartialConfig{{object_settings}, hw_config}
{}
} // namespace Slic3r::Domain
