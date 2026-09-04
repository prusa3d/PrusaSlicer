#include "libslic3r/HwConfigUtils.hpp"

namespace Slic3r::Biz::Slicing {

std::vector<double> get_nozzle_diameters(const Domain::Preset::HwPrinterConfig& hw_config)
{
    std::vector<double> result;

    for (const auto& tool : hw_config.tools) {
        const std::optional<double> nozzle_diameter{
            Domain::Preset::get_feature<double>(tool.features, "nozzle_diameter")
        };
        ASSERT(nozzle_diameter);
        result.push_back(*nozzle_diameter);
    }

    return result;
}

double get_nozzle_diameter(const Domain::Preset::HwPrinterConfig& hw_config, std::size_t slot_index)
{
    const std::size_t tool_index{
        Domain::Preset::MaterialIterator::from_slot_index(hw_config, slot_index).tool_index()
    };
    const std::optional<double> nozzle_diameter{Domain::Preset::get_feature<double>(
        hw_config.tools.at(tool_index).features,
        "nozzle_diameter"
    )};
    ASSERT(nozzle_diameter);
    return *nozzle_diameter;
}

} // namespace Slic3r::Biz::Slicing
