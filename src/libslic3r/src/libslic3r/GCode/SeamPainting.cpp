#include "libslic3r/GCode/SeamPainting.hpp"

#include "Slic3r/Biz/Algorithms/FacetsAnnotation.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Domain/TriangleSelector.hpp"

using namespace Slic3r::Biz;

namespace Slic3r::Seams::ModelInfo {

using Domain::its_merge;
namespace AABBTreeIndirect = Biz::Algorithms::AABBTreeIndirect;

Painting::Painting(const Transform3d &obj_transform, const Domain::ModelVolumePtrs &volumes) {
    for (const Domain::ModelVolume *mv : volumes) {
        if (mv->is_seam_painted()) {
            auto model_transformation = obj_transform * mv->get_matrix();

            indexed_triangle_set enforcers = Algorithms::FacetsAnnotation::get_facets(mv->seam_facets, *mv, Domain::TriangleSelector::TriangleStateType::ENFORCER);
            its_transform(enforcers, model_transformation);
            its_merge(this->enforcers, enforcers);

            indexed_triangle_set blockers = Algorithms::FacetsAnnotation::get_facets(mv->seam_facets, *mv, Domain::TriangleSelector::TriangleStateType::BLOCKER);
            its_transform(blockers, model_transformation);
            its_merge(this->blockers, blockers);
        }
    }

    this->enforcers_tree = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(
        this->enforcers.vertices, this->enforcers.indices
    );
    this->blockers_tree = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(
        this->blockers.vertices, this->blockers.indices
    );
}

bool Painting::is_enforced(const Vec3f &position, float radius) const {
    if (enforcers.empty()) {
        return false;
    }
    float radius_sqr = radius * radius;
    return AABBTreeIndirect::is_any_triangle_in_radius(
        enforcers.vertices, enforcers.indices, enforcers_tree, position, radius_sqr
    );
}

bool Painting::is_blocked(const Vec3f &position, float radius) const {
    if (blockers.empty()) {
        return false;
    }
    float radius_sqr = radius * radius;
    return AABBTreeIndirect::is_any_triangle_in_radius(
        blockers.vertices, blockers.indices, blockers_tree, position, radius_sqr
    );
}
} // namespace Slic3r::Seams::ModelInfo
