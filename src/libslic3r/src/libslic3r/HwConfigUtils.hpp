#pragma once

#include "Slic3r/Domain/Preset/HwConfig.hpp"

namespace Slic3r::Biz::Slicing {

std::vector<double> get_nozzle_diameters(const Domain::Preset::HwPrinterConfig& hw_config);

double get_nozzle_diameter(const Domain::Preset::HwPrinterConfig& hw_config, std::size_t slot_index);

} // namespace Slic3r
