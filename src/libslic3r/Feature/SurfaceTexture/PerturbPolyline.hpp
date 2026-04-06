///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
// Shared polyline-perturbation core for Fuzzy Skin and Texture Skin.
// Walks a point sequence, inserts samples every `point_distance_scaled`
// (with optional per-point jitter), and calls the OffsetProvider to
// compute the signed perpendicular offset in scaled coordinates.
#ifndef libslic3r_SurfaceTexture_PerturbPolyline_hpp_
#define libslic3r_SurfaceTexture_PerturbPolyline_hpp_

#include <functional>

#include "libslic3r/Point.hpp"

namespace Slic3r::Arachne {
struct ExtrusionLine;
} // namespace Slic3r::Arachne

namespace Slic3r::Feature::SurfaceTexture {

struct PerturbSpacing {
    // Nominal distance between inserted points, scaled (coord_t units).
    double point_distance_scaled = 0.0;
    // 0.0 = constant spacing, 0.25 = step varies in 75%..125% of nominal.
    double jitter_fraction       = 0.0;
};

// Callback: given the point's 2D position (scaled coords) and the outward
// 2D perpendicular of the local segment direction, return a signed
// perpendicular offset in scaled coordinates.
using OffsetProvider = std::function<double(const Vec2d &pos_scaled, const Vec2d &perp_unit)>;

// Thread-safe uniform random in [0, 1]. Exposed for callers that need it
// for their own randomness (e.g. Fuzzy Skin's offset).
double random_unit();

// In-place perturbation of a polygon / polyline.
void perturb_polyline(Points &poly, bool closed,
                      const PerturbSpacing &spacing,
                      const OffsetProvider &offset_provider);

// In-place perturbation of an Arachne extrusion line.
void perturb_extrusion_line(Arachne::ExtrusionLine &line,
                            const PerturbSpacing &spacing,
                            const OffsetProvider &offset_provider);

} // namespace Slic3r::Feature::SurfaceTexture

#endif // libslic3r_SurfaceTexture_PerturbPolyline_hpp_
