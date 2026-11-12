#pragma once

#include "Slic3r/App/Scene/Camera.hpp"
#include "Slic3r/App/Scene/Ray.hpp"
#include "Slic3r/Biz/Utils/MeshClipper.hpp"
#include "Slic3r/Domain/Transformation.hpp"
#include "Slic3r/Domain/Types.hpp"

#include <optional>

namespace Slic3r {
class AABBMesh;
} // namespace Slic3r

namespace Slic3r::Biz::Utils::MeshRaycaster {

struct UnprojectResult
{
    Domain::Vec3d position = Domain::Vec3d::Zero();
    Domain::Vec3d normal   = Domain::Vec3d::Zero();
    size_t facet_idx       = 0;
};

/**
 * Given a ray from screen, this returns hit information if the ray intersects the mesh.
 * @param aabb_mesh The mesh to raycast against
 * @param ray_from_screen Ray in world space (from camera/screen position)
 * @param trafo_world Transformation matrix from local to world space
 * @param clipping_plane Optional clipping plane (if is provided, hits behind it are ignored)
 * @param require_even_number_of_hits When true, an odd number of unclipped hits are ignored (hit from inside the mesh)
 * @return UnprojectResult containing hit position, normal, and facet index in mesh coords, or std::nullopt if no valid intersection found
 */
std::optional<UnprojectResult> unproject_on_mesh(
    const AABBMesh& aabb_mesh,
    const App::Scene::Ray& ray_from_screen,
    const Domain::Transform3d& trafo_world,
    const std::optional<Biz::ClippingPlane>& clipping_plane = std::nullopt,
    bool require_even_number_of_hits                        = true
);

/**
 * Given a point in mesh coords, returns the index of the closest facet from the mesh.
 * @param aabb_mesh The mesh to search in
 * @param point Point in mesh coordinates
 * @return Index of the closest facet
 */
int get_closest_facet(const AABBMesh& aabb_mesh, const Domain::Vec3f& point);

} // namespace Slic3r::Biz::Utils::MeshRaycaster
