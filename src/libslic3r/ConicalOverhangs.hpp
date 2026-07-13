#ifndef slic3r_ConicalOverhangs_hpp_
#define slic3r_ConicalOverhangs_hpp_

#include <vector>

#include "libslic3r/Point.hpp"
#include "libslic3r/ExPolygon.hpp"

namespace Slic3r {

class Layer;

// Conical overhangs (Cura-style), in slice space.
//
// Operates on the already-sliced object layers (bottom..top). Going top-down,
// every layer's outline is unioned with the outline of the layer above dilated
// outward by layer_height * tan(max_overhang_angle_deg). This grows a printable
// cone (max slope = the given angle from vertical) beneath every overhang, all
// the way down to the build plate. Additive: material is only ever added.
//
// The added material is inserted into the layer's first region as internal
// surfaces, and the merged layer outline (lslices) is updated to match.
//
// Holes (internal cavities) whose per-layer area is <= max_hole_area_mm2 are
// filled in to support their ceiling; larger holes are kept open. Pass 0 to
// keep every hole open.
//
// extra_melt_angle_deg > 0 melts the sharp convex ridges the cone forms where its
// support surfaces collide. Each layer, the eroded cone outline (before the model
// is unioned back in) is subdivided so no edge exceeds subdivision_length_mm
// (giving each corner's turn somewhere to spread), then one step of discrete
// curve-shortening (Laplacian) flow is applied, with each vertex's move capped to
// dz * (tan(overhang_angle + extra_melt_angle) - tan(overhang_angle)). Compounded
// down the stack, sharp ridges melt away completely (a square cross-section
// slowly rounds into a circle) while the cap guarantees no melted wall is steeper
// than overhang_angle + extra_melt_angle. This is subtractive at convex corners.
// Because the model is unioned in afterwards its own edges are never touched.
// Pass 0 for the original sharp behaviour. See doc/conical_overhangs_ridge_relaxation.md.
void apply_conical_overhangs(const std::vector<Layer*> &layers,
                             double                     max_overhang_angle_deg,
                             double                     max_hole_area_mm2,
                             double                     extra_melt_angle_deg,
                             double                     subdivision_length_mm);

// --- Exposed for unit tests (tests/libslic3r/test_conical_overhangs.cpp) ---

// Signed turning (exterior) angle at b for the path a -> b -> c, in radians.
// Positive = left turn (convex on a CCW contour), negative = right turn.
double conical_signed_turning_angle(const Vec2d &a, const Vec2d &b, const Vec2d &c);

// Melt the convex ridges of an eroded cone outline by one step of discrete
// curve-shortening (Laplacian) flow: move each vertex towards the midpoint of its
// neighbours, capped to `max_travel` (scaled). Subdivide the input first
// (Polygon::densify) so corners can round rather than just shrink.
ExPolygons relax_cone_ridges(const ExPolygons &eroded, double max_travel);

} // namespace Slic3r

#endif // slic3r_ConicalOverhangs_hpp_
