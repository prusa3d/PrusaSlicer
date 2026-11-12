#pragma once

#include "Slic3r/Domain/Preset/HwConfig.hpp"

namespace Slic3r::Test {

Domain::Preset::HwPrinterConfig build_fff_printer_config(uint8_t tool_count,
                                                         Domain::Preset::HwFeederConfigs feeders);

} // namespace Slic3r::Test
