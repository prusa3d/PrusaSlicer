#pragma once

#include <string>
#include "Slic3r/Domain/ConfigValue.hpp"
#include "Slic3r/Domain/PrinterTechnology.hpp"

namespace Slic3r::Domain {

class ConfigDefinitions;

// Defined in PrinterTechnology.hpp:
//enum class PrinterTechnology : uint8_t

enum class GCodeThumbnailsFormat {
    PNG, JPG, QOI
};
enum class SlicingMode
{    
    Regular, // Regular, applying ClipperLib::pftNonZero rule when creating ExPolygons.  
    EvenOdd, // Compatible with 3DLabPrint models, applying ClipperLib::pftEvenOdd rule when creating ExPolygons.
    CloseHoles, // Orienting all contours CCW, thus closing all holes.
};

void init_common_fdm_sla_config_items(ConfigDefinitions& defs, const PrinterTechnology technology);
	
}
