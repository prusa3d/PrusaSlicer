#pragma once

#include "Slic3r/Biz/libpgcode/ProcessorConfig.hpp"
#include "libslic3r/ConfigViews.hpp"

namespace Slic3r {

// Use the exporter's configuration path when replaying already-generated G-code.
Biz::libpgcode::ProcessorConfig make_gcode_processor_config(const PrintConfigView& config);

}
