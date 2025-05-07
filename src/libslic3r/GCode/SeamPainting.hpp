#ifndef libslic3r_GlobalModelInfo_hpp_
#define libslic3r_GlobalModelInfo_hpp_

#include "libslic3r/AABBTreeIndirect.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Model.hpp"
#include "admesh/stl.h"

namespace Slic3r::Seams::ModelInfo {
class Painting
{
public:
    Painting(const Transform3d &obj_transform, const ModelVolumePtrs &volumes);

    bool is_enforced(const Vec3f &position, float radius) const;
    bool is_blocked(const Vec3f &position, float radius) const;

private:
    indexed_triangle_set enforcers;
    indexed_triangle_set blockers;
    AABBTreeIndirect::Tree<3, float> enforcers_tree;
    AABBTreeIndirect::Tree<3, float> blockers_tree;
};
} // namespace Slic3r::Seams::ModelInfo
#endif // libslic3r_GlobalModelInfo_hpp_
