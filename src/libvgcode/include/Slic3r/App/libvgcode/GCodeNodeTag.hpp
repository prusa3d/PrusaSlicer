#pragma once

#include <cstdint>

namespace Slic3r::App::libvgcode {

/**
  * @brief Type of render elements for gcode
  */
enum class GCodeElementType : int8_t
{
    Undefined = 0,
    Toolpaths,
    Options,
    CogMarker,
    ToolMarker,
    Bed,
};

/**
  * @brief Node tag for GCode elements
  */
struct GCodeNodeTag
{
    const GCodeElementType type{ GCodeElementType::Undefined };
};

} // namespace Slic3r::App::libvgcode