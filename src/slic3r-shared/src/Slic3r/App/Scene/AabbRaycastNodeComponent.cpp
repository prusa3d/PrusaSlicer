#include "Slic3r/App/Scene/AabbRaycastNodeComponent.hpp"
#include "Slic3r/App/Render/MathUtils.hpp"
#include "Slic3r/App/Scene/PickerFrustum.hpp"
#include "Slic3r/Domain/Types.hpp"

using Slic3r::Domain::SquareMatrix4d;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::Vec3f;
using Slic3r::Domain::Vec4d;

namespace Slic3r::App::Scene {

bool AabbRaycastNodeComponent::raycast(const SquareMatrix4d& world, const Ray& ray, RaycastResult& res) const
{
    ASSERT(m_aabb_mesh != nullptr);

    SquareMatrix4d inv_world = world.inverse();

    // Transform ray to local-space first
    Vec3d local_ray_origin = (inv_world * Vec4d{ray.origin.x(), ray.origin.y(), ray.origin.z(), 1}).head<3>();
    Vec3d local_ray_direction = (inv_world.block<3, 3>(0, 0) * ray.direction).normalized();

    // Do hit test in local-space
    auto hit = m_aabb_mesh->query_ray_hit(local_ray_origin, local_ray_direction);
    if (!hit.is_hit())
        return false;

    // Calculate the local-space hit point using the local t
    double t_local = hit.distance();
    Vec3d local_hit_point = local_ray_origin + t_local * local_ray_direction;

    // Transform the local hit point back to world space
    Vec3d world_hit_point = (world * Vec4d{local_hit_point.x(), local_hit_point.y(), local_hit_point.z(), 1}).head<3>();

    // Solve for the original ray's parameter 't'.
    // This is the key change for non-normalized rays.
    // The vector from ray origin to the hit point is (world_hit_point - ray.origin).
    // This vector must be equal to t * ray.direction.
    // We can solve for t using a dot product:
    // distance = ((world_hit_point - ray.origin).dot(ray.direction)) / ray.direction.squaredNorm();

    Vec3d hit_vector = world_hit_point - ray.origin;
    res.distance = hit_vector.dot(ray.direction) / ray.direction.squaredNorm();
    res.normal = world.block<3,3>(0,0) * hit.normal();
    res.triangle_index = hit.face();
    return true;
}

/*
Eigen::AlignedBox<float, 2> AabbRaycastNodeComponent::projected_bounding_box(
    const Matrix4f& mvp, const Render::Rect& viewport
) const
{
    ASSERT(m_aabb_mesh != nullptr);
    auto bbox3 = m_aabb_mesh->bounding_box();

    Eigen::AlignedBox<float, 2> ret;

    size_t count_z_plus1 = 0;
    size_t count_z_minus1 = 0;

    for (size_t i = 0; i < 8; i++) {
        Vec3f v = bbox3.corner(Eigen::AlignedBox<float, 3>::CornerType(i));
        Vec4f c4 = mvp * Vec4f{v.x(), v.y(), v.z(), 1};
        Vec3f c3 = Vec3f{
            c4.x() / c4.w(),
            c4.y() / c4.w(),
            c4.z() / c4.w()
        };
        if (c3.z() < -1) count_z_minus1++;
        else if (c3.z() > 1) count_z_plus1++;

        ret.extend(Render::viewport_transform(viewport, c3));
    }

    // All AABB corners are outside frustum z-range
    if (count_z_plus1 == 8 || count_z_minus1 == 8)
        return {};

    return ret;
}
*/

Eigen::AlignedBox3f AabbRaycastNodeComponent::world_bounding_box(const SquareMatrix4d& world) const
{
    ASSERT(m_aabb_mesh != nullptr);
    auto bbox3 = m_aabb_mesh->bounding_box();

    // TODO: use convex hull to get tight AABB in world space

    Eigen::AlignedBox<float, 3> ret;
    for (size_t i = 0; i < 8; i++) {
        Vec3f v = bbox3.corner(Eigen::AlignedBox<float, 3>::CornerType(i));
        Vec3f w = (world * Vec4d{v.x(), v.y(), v.z(), 1}).block<3, 1>(0, 0).cast<float>();
        ret.extend(w);
    }
    return ret;
}

AABBMesh::hit_result AabbRaycastNodeComponent::hit_result(
    const Domain::SquareMatrix4d& world,
    const Ray& ray
) const
{
    ASSERT(m_aabb_mesh != nullptr);

    SquareMatrix4d inv_world = world.inverse();

    Vec3d local_ray_origin = (inv_world * Vec4d{ray.origin.x(), ray.origin.y(), ray.origin.z(), 1.0})
                                 .head<3>()
                                 .cast<double>();
    Vec3d local_ray_direction = (inv_world.block<3, 3>(0, 0) * ray.direction).cast<double>().normalized();

    AABBMesh::hit_result ret = m_aabb_mesh->query_ray_hit(local_ray_origin, local_ray_direction);
    return ret;
}

bool AabbRaycastNodeComponent::intersects(const SquareMatrix4d& world, const Frustum& frustum) const
{
    ASSERT(m_aabb_mesh != nullptr);
    return frustum.intersects_fast(world_bounding_box(world).cast<double>());
}

bool AabbRaycastNodeComponent::intersects(const Domain::SquareMatrix4d& world, const PickerFrustum& frustum) const
{
    ASSERT(m_aabb_mesh != nullptr);
    return frustum.intersects_precise(world_bounding_box(world).cast<double>());
}

} // namespace Slic3r::App::Scene
