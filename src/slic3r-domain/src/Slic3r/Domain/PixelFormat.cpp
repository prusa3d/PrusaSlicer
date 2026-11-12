#include "Slic3r/Domain/PixelFormat.hpp"
#include "Slic3r/Assert.hpp"

namespace Slic3r::Domain {
std::size_t pixel_format_bytes_per_pixel(PixelFormat pf)
{
    switch (pf) {
    case PixelFormat::RGB8:
        return 3;
    case PixelFormat::RGBA8:
        return 4;
    case PixelFormat::R16F:
        return 2;
    case PixelFormat::R32F:
        return 4;
    case PixelFormat::R32UI:
        return 4;
    case PixelFormat::RG16F:
        return 4;
    case PixelFormat::RGBA32F:
        return 16;
    case PixelFormat::RGBA16F:
        return 8;
    case PixelFormat::RGB32F:
        return 12;
    case PixelFormat::DepthComponent:
        return 4;
    default:
        // unsupported format
        PANIC("Unsupported pixel format");
        return 0;
    }
}
} // namespace Slic3r::Domain
