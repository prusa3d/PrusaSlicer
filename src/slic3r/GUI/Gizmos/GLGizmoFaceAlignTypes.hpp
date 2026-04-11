///|/ Copyright (c) Prusa Research 2026
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_GLGizmoFaceAlignTypes_hpp_
#define slic3r_GLGizmoFaceAlignTypes_hpp_

#include "libslic3r/Point.hpp"
#include "slic3r/GUI/MeshUtils.hpp"

#include <vector>

namespace Slic3r {

class ModelObject;

namespace GUI {

// One coplanar face patch on the convex hull (same representation as GLGizmoFlatten).
struct FaceAlignPlaneData
{
    PickingModel vbo;
    Vec3d        normal_world{ Vec3d::UnitZ() };
    Vec3d        center_world{ Vec3d::Zero() };
    int          object_idx{ -1 };
    int          instance_idx{ -1 };
};

/// mesh_to_world must match picking/render (e.g. SLA Z + full instance matrix) so center_world matches the patch.
void face_align_build_planes_for_instance(
    const ModelObject*               mo,
    int                              instance_idx,
    int                              object_idx_tag,
    const Transform3d&               mesh_to_world,
    std::vector<FaceAlignPlaneData>& out_planes,
    int                              max_planes);

} // namespace GUI
} // namespace Slic3r

#endif
