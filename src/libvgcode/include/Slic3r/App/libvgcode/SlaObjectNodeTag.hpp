#pragma once
//#include "Slic3r/Domain/ObjectID.hpp"
#include <cstdint>

namespace Slic3r::App::libvgcode {

/**
  * @brief Type of render elements for gcode
  */
enum class SlaMeshType : int8_t
{
    Undefined = 0,
    Object,
    Supports,
    Pad,
    TopClip,
    BottomClip
};

/**
  * @brief Node tag for GCode elements
  */
struct SlaObjectNodeTag
{
    size_t object_id{ 0 };
    size_t instance_id{ 0 };
    const SlaMeshType type{ SlaMeshType::Undefined };
    bool is_clip() const { return type == SlaMeshType::TopClip || type == SlaMeshType::BottomClip; }
};

} // namespace Slic3r::App::libvgcode
