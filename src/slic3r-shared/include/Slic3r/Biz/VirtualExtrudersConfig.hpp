#pragma once

#include "Slic3r/Domain/VirtualExtruder.hpp"

#include <string>
#include <vector>

namespace Slic3r::Biz {

/**
 * @brief All data parsed from the Full Spectrum JSON in a 3MF.
 */
struct VirtualExtrudersConfig
{
    Domain::VirtualExtruders virtual_extruders;
    // Number of physical extruders the 3MF was saved with (0 = absent/old format).
    unsigned int source_physical_count = 0;
};

} // namespace Slic3r::Biz
