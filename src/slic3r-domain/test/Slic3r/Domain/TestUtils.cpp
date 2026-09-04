#include "Slic3r/Domain/TestUtils.hpp"

namespace Slic3r::Test {

using namespace Slic3r::Domain::Preset;

HwPrinterConfig build_fff_printer_config(uint8_t tool_count, HwFeederConfigs feeders)
{
    FeatureValueMap tool_features;
    tool_features["nozzle_diameter"] = 0.4;

    HwToolConfigs tools{tool_count, HwToolConfig{.features = tool_features}};
    HwPrinterConfig printer_config = {.printer_id = "printer",
                                      .technology = Domain::PrinterTechnology::FFF,
                                      .tool_count = tool_count,
                                      .tools      = std::move(tools),
                                      .feeders    = std::move(feeders)};
    return printer_config;
}

} // namespace Slic3r::Test
