///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef libslic3r_TextureSkin_TextureSampler_hpp_
#define libslic3r_TextureSkin_TextureSampler_hpp_

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Slic3r::Feature::TextureSkin {

// Tiled bilinear grayscale sampler. The image is stored row-major, 8-bit
// grayscale, and tiled via modulo-1 on (u, v).
struct GrayImage {
    std::vector<uint8_t> pixels; // size = width * height
    size_t width  = 0;
    size_t height = 0;

    bool empty() const { return pixels.empty(); }
};

// Sample grayscale image at tiled UV coordinates, returning a value in [0, 1].
// Bilinear interpolation, coordinate wraps modulo 1.
double sample_bilinear(const GrayImage &img, double u, double v);

} // namespace Slic3r::Feature::TextureSkin

#endif // libslic3r_TextureSkin_TextureSampler_hpp_
