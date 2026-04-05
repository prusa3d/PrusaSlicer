///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "TextureSampler.hpp"

#include <cmath>

namespace Slic3r::Feature::TextureSkin {

static inline double fract(double x) { return x - std::floor(x); }

double sample_bilinear(const GrayImage &img, double u, double v)
{
    if (img.empty()) return 0.5;

    const double uu = fract(u);
    const double vv = fract(v);
    const double fx = uu * static_cast<double>(img.width);
    const double fy = vv * static_cast<double>(img.height);
    const size_t x0 = static_cast<size_t>(std::floor(fx)) % img.width;
    const size_t y0 = static_cast<size_t>(std::floor(fy)) % img.height;
    const size_t x1 = (x0 + 1) % img.width;
    const size_t y1 = (y0 + 1) % img.height;
    const double tx = fx - std::floor(fx);
    const double ty = fy - std::floor(fy);

    const double p00 = static_cast<double>(img.pixels[y0 * img.width + x0]) / 255.0;
    const double p10 = static_cast<double>(img.pixels[y0 * img.width + x1]) / 255.0;
    const double p01 = static_cast<double>(img.pixels[y1 * img.width + x0]) / 255.0;
    const double p11 = static_cast<double>(img.pixels[y1 * img.width + x1]) / 255.0;

    const double a = p00 * (1.0 - tx) + p10 * tx;
    const double b = p01 * (1.0 - tx) + p11 * tx;
    return a * (1.0 - ty) + b * ty;
}

} // namespace Slic3r::Feature::TextureSkin
