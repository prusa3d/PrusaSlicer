#include "libslic3r/SlicingInput.hpp"

namespace Slic3r {
using Domain::ConfigPackFDM;
using Domain::PartialConfig;
using Domain::FullConfigFDM;
using Domain::FullConfigFDMPtr;
using Domain::PartialConfig;
using Biz::Slicing::Error;
using Biz::Slicing::ErrorCode;


namespace {

void set_extruders(PartialConfig& partial_config, const auto& settings, std::size_t material_slot_count)
{
    if (material_slot_count == 1) {
        for (const char* key :
             {
                 "perimeter_extruder",
                 "infill_extruder",
                 "solid_infill_extruder",
                 "support_material_extruder",
                 "support_material_interface_extruder",
             })
        {
            if (partial_config.get<int>(key)) {
                partial_config.set(key, 1);
            }
        }
    } else if (const auto extruder{partial_config.template get<int>("extruder")}; extruder > 0) {
        for (const char* key : {"perimeter_extruder", "infill_extruder", "solid_infill_extruder"}) {
            if (!settings.overrides.get(key)) {
                partial_config.set(key, *extruder);
            }
        }
    }
}
} // namespace

tl::expected<FullConfigFDMPtr, std::vector<Error>> prepare_slicing_input(
    const ConfigPackFDM& config_pack,
    const std::vector<unsigned>& extruder_candidates,
    const Domain::Preset::HwPrinterConfig& hw_config
)
{
    std::vector<Error> errors;
    if (hw_config.tools.size() == 0) {
        errors.push_back(Error{ErrorCode::NoHwConfigTools});
    }

    if (hw_config.material_slot_count() < hw_config.tools.size()) {
        errors.push_back(Error{ErrorCode::HwConfigLessMaterialsThanTools});
    }

    for (const Domain::Preset::HwToolConfig& tool : hw_config.tools) {
        if (!tool.features.contains("nozzle_diameter")) {
            errors.push_back(Error{ErrorCode::MissingHwConfigNozzleDiameter});
            break;
        }
    }

    if (!errors.empty()) {
        return tl::unexpected{errors};
    }

    FullConfigFDM result{config_pack, extruder_candidates, hw_config};
    if (hw_config.material_slot_count() == 1) {
        for (const char* key :
             {
                 "perimeter_extruder",
                 "infill_extruder",
                 "solid_infill_extruder",
                 "support_material_extruder",
                 "support_material_interface_extruder",
             })
        {
            result.set(key, 1);
        }
    }

    return std::make_shared<const FullConfigFDM>(std::move(result));
}
} // namespace Slic3r
