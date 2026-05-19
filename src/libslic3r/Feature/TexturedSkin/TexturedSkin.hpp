///|/ Copyright (c) Prusa Research 2026
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef libslic3r_TexturedSkin_hpp_
#define libslic3r_TexturedSkin_hpp_

#include "libslic3r/Polygon.hpp"
#include "libslic3r/ExPolygon.hpp"

#include <string>
#include <memory>

namespace Slic3r {
struct PrintRegionConfig;
} // namespace Slic3r

namespace Slic3r::Feature::TexturedSkin {

/// How the 2D pattern maps onto the 3D perimeter.
enum class MappingMode : int {
    PaintedOn    = 0,  // u=perimeter arc-length, v=layer_z. Consistent tile size.
    Mercator     = 1,  // u=angle×R, v=z×(tile_w/circ). Conformal for spheres.
    StretchFit   = 2,  // Integer tile count per revolution. Seamless.
    StampFront   = 3,  // Project from front:  u=x, v=z.
    StampBack    = 4,  // Project from back:   u=-x, v=z.
    StampLeft    = 5,  // Project from left:   u=y, v=z.
    StampRight   = 6,  // Project from right:  u=-y, v=z.
    StampTop     = 7,  // Project from top:    u=x, v=y.
    StampBottom  = 8,  // Project from bottom: u=x, v=-y.
    Adaptive     = 9,  // Blend PaintedOn and Mercator.
    Cylindrical  = 10, // u=angle×R, v=layer_z. Seamless wrap, no v-scaling.
    Triplanar    = 11, // Auto-selects best projection axis per surface normal.
};

/// Cached SVG/PNG pattern data for fast sampling during perimeter generation.
class PatternSampler {
public:
    /// Load SVG or PNG pattern file. PNG is loaded directly (fast).
    /// tile_height_mm: if > 0, overrides the height derived from image aspect ratio.
    static std::shared_ptr<PatternSampler> create(const std::string &path, double tile_size_mm, double tile_height_mm = 0, double resolution = 10.0);
    double sample(double u, double v) const;
    double tile_width() const { return m_tile_w; }
    double tile_height() const { return m_tile_h; }

private:
    PatternSampler() = default;
    std::vector<float> m_grid;
    int    m_grid_w = 0;
    int    m_grid_h = 0;
    double m_tile_w = 0;
    double m_tile_h = 0;
    double m_resolution = 10.0;
};

/// Apply textured skin displacement to a perimeter polygon.
void textured_polygon(
    Polygon              &polygon,
    const PatternSampler &sampler,
    double                layer_z,
    double                thickness,
    double                point_distance,
    MappingMode           mapping = MappingMode::PaintedOn,
    bool                  invert = false);

} // namespace Slic3r::Feature::TexturedSkin

#endif // libslic3r_TexturedSkin_hpp_
