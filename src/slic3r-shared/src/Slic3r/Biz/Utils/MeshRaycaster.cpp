#include "Slic3r/Biz/Utils/MeshRaycaster.hpp"

#include "Slic3r/App/Scene/Ray.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/Transformation.hpp"
#include "Slic3r/Domain/Types.hpp"

#include "Slic3r/Biz/Algorithms/AABBMesh.hpp"

using Slic3r::Domain::Transform3d;
using Slic3r::Domain::Vec2d;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::Vec3f;

using namespace Slic3r::App;

namespace Slic3r::Biz::Utils::MeshRaycaster {

/**
 * Transform ray from world space to local space.
 * @param ray_to_transform Ray in world space
 * @param world_trafo Transformation matrix from local to world space
 * @return Ray in local space
 */
static Scene::Ray transformed_to_local_space(
    const Scene::Ray& ray_to_transform,
    const Domain::Transform3d& world_trafo
)
{
    Transform3d inv = world_trafo.inverse();
    return {inv * ray_to_transform.origin, inv.linear() * ray_to_transform.direction};
}

std::optional<UnprojectResult> unproject_on_mesh(
    const AABBMesh& aabb_mesh,
    const Scene::Ray& ray_from_screen,
    const Transform3d& trafo_world,
    const std::optional<ClippingPlane>& clipping_plane,
    const bool require_even_number_of_hits
)
{
    const Scene::Ray ray_from_screen_local =
        transformed_to_local_space(ray_from_screen, trafo_world);

    std::vector<AABBMesh::hit_result> hits =
        aabb_mesh.query_ray_hits(ray_from_screen_local.origin, ray_from_screen_local.direction);
    if (hits.empty()) {
        return std::nullopt; // No intersection found.
    }

    unsigned hit_idx = 0;

    // Remove points that are obscured or cut by the clipping plane.
    // Also, remove anything below the bed (sinking objects).
    for (hit_idx = 0; hit_idx < hits.size(); ++hit_idx) {
        const Vec3d transformed_hit = trafo_world * hits[hit_idx].position();
        if (transformed_hit.z() >= Domain::SINKING_Z_THRESHOLD
            && (!clipping_plane.has_value() || !clipping_plane->is_point_clipped(transformed_hit)))
        {
            break;
        }
    }

    if (hit_idx == hits.size() || (require_even_number_of_hits && (hits.size() - hit_idx) % 2 != 0))
    {
        // All hits are either clipped, or there is an odd number of unclipped
        // hits - meaning the nearest must be from inside the mesh.
        return std::nullopt;
    }

    return UnprojectResult{
        hits[hit_idx].position(),
        hits[hit_idx].normal(),
        static_cast<size_t>(hits[hit_idx].face())
    };
}

int get_closest_facet(const AABBMesh& aabb_mesh, const Vec3f& point)
{
    int facet_idx = 0;
    Vec3d closest_point;
    aabb_mesh.squared_distance(point.cast<double>(), facet_idx, closest_point);
    return facet_idx;
}

} // namespace Slic3r::Biz::Utils::MeshRaycaster
