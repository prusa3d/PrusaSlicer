#pragma once

#include <vector>
#include <cstddef>
#include <cstdint>

#include "Slic3r/Assert.hpp"
#include "Slic3r/Domain/PixelFormat.hpp"

namespace Slic3r::Domain {

/**
 * Simple bitmap container storing image dimensions, pixel format and pixels themselves.
 * Use ImageCodecManager to get image codec to load image.
 */
class Image
{
public:
    using Data = std::vector<uint8_t>;

    Image(PixelFormat format, int w, int h, Data&& data = Data()) noexcept :
        pixels(std::move(data)),
        m_width(w),
        m_height(h),
        m_pixel_format(format)
    {
        if (format == PixelFormat::RGB_DXT1 || format == PixelFormat::RGBA_DXT5)
            ASSERT(!pixels.empty());
        else {
            const size_t bytes_per_pixel = pixel_format_bytes_per_pixel(format);
            if (pixels.empty())
                pixels.resize(w * h * bytes_per_pixel, 0);
            ASSERT(pixels.size() == w * h * bytes_per_pixel);
        }
    }

    PixelFormat format() const
    {
        return m_pixel_format;
    }

    int width() const
    {
        return m_width;
    }

    int height() const
    {
        return m_height;
    }

    Data pixels;

private:
    int m_width{0};
    int m_height{0};
    PixelFormat m_pixel_format{PixelFormat::RGBA8};
};

using Images = std::vector<Image>;

} // namespace Slic3r::Domain
