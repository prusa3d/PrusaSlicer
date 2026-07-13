#ifndef slic3r_ConicalOverhangs_hpp_
#define slic3r_ConicalOverhangs_hpp_

#include <vector>

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
void apply_conical_overhangs(const std::vector<Layer*> &layers,
                             double                     max_overhang_angle_deg,
                             double                     max_hole_area_mm2);

} // namespace Slic3r

#endif // slic3r_ConicalOverhangs_hpp_
