#pragma once
#include "Slic3r/Domain/Preset/HwConfig.hpp"

namespace Slic3r::Test {

Domain::Preset::HwPrinterConfig create_dummy_hw_config(
    uint8_t tool_count = 1,
    double nozzle_diameter         = 0.4,
    Domain::PrinterTechnology tech = Domain::PrinterTechnology::FFF
);
} // namespace Slic3r::Test
