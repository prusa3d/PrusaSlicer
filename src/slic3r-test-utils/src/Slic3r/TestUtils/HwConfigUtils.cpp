#include "Slic3r/TestUtils/HwConfigUtils.hpp"
#include "Slic3r/Uuid.hpp"

namespace Slic3r::Test {
using Domain::Preset::HwPrinterConfig;

HwPrinterConfig
create_dummy_hw_config(uint8_t tool_count, double nozzle_diameter, Domain::PrinterTechnology tech)
{
    using Domain::Preset::Address;
    using Domain::Preset::FeederType;
    using Domain::Preset::HwFeederConfig;
    using Domain::Preset::HwToolConfig;
    using Domain::Preset::HwToolConfigs;
    HwPrinterConfig result{
        .id         = generate_uuid(),
        .technology = tech,
        .tool_count = tool_count,
        .tools      = HwToolConfigs(tool_count, HwToolConfig{})
    };
    if (tech == Domain::PrinterTechnology::FFF) {
        for (std::size_t tool_index{}; tool_index < result.tools.size(); ++tool_index) {
            HwToolConfig& tool{result.tools[tool_index]};
            tool.features.insert({"nozzle_diameter", nozzle_diameter});
        }
        if (result.tools.size() > 1) {
            result.features.insert({"multi_extruder", true});
        }
    }
    return result;
}
} // namespace Slic3r::Test
