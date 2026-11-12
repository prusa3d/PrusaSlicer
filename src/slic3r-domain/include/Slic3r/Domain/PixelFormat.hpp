#pragma once

#include <cstddef>

namespace Slic3r::Domain {

enum class PixelFormat
{
    RGB8 = 0,
    RGBA8,
    R16F,
    R32F,
    R32UI,
    RG16F,
    RGBA32F,
    RGBA16F,
    RGB32F,
    DepthComponent,
    RGB_DXT1,
    RGBA_DXT5,
};

std::size_t pixel_format_bytes_per_pixel(PixelFormat pf);

} // namespace Slic3r::Domain
