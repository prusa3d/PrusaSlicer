#pragma once

#include "Slic3r/App/Scene/Ray.hpp"
#include "Slic3r/App/Render/Types.hpp"
#include "Slic3r/App/Render/MathUtils.hpp"

namespace Slic3r::App::Scene {

class Frustum;
class PickerFrustum;

struct RaycastResult
{
    double distance;
    Domain::Vec3d normal; // world normal of the hit
    int triangle_index; // index of triangle in the mesh
};

/**
 * @brief Generic interface for raycast collision detection component.
 * @see AabbRaycastNodeComponent
 */
class IRaycastNodeComponent {
public:
    virtual ~IRaycastNodeComponent() = default;

    /**
     * @brief raycast test given ray against collision object.
     * @param world World transform of associated node
     * @param ray Ray in world space
     * @param[out] res Output of the first hit (set only if `true` returned)
     * @return True if any collision hit was detected
     */
    virtual bool raycast(const Domain::SquareMatrix4d& world, const Ray& ray, RaycastResult& res) const = 0;

    virtual Eigen::AlignedBox3f world_bounding_box(const Domain::SquareMatrix4d& world) const = 0;

    /**
     * @brief intersection test against given frustum.
     * @param world World transform of associated node
     * @param frustum Frustum in world space
     * @return True if associated node and frustum intersect
     */
    virtual bool intersects(const Domain::SquareMatrix4d& world, const Frustum& frustum) const = 0;
    virtual bool intersects(const Domain::SquareMatrix4d& world, const PickerFrustum& frustum) const = 0;
};

/**
 * @brief Gets screen space projection of collision object as AABB
 * @param m Model matrix
 * @param vp View-projection matrix
 * @param viewport Viewport
 * @return Projected bounding box in OpenGL screen space (i.e. coordinate  (0,0)
 * located in bottom left corner). Empty if object is outside z-range (i.e. behind camera
 * or too far from camera)
 */
inline Eigen::AlignedBox<float, 2> projected_bounding_box(
    const IRaycastNodeComponent& raycast, const Domain::SquareMatrix4d& m, const Domain::SquareMatrix4f& vp, const Render::Rect& viewport
)
{
    auto world_box = raycast.world_bounding_box(m);
    Eigen::AlignedBox<float, 2> ret;

    size_t count_z_plus1 = 0;
    size_t count_z_minus1 = 0;

    for (size_t i = 0; i < 8; i++) {
        Domain::Vec3f v = world_box.corner(Eigen::AlignedBox<float, 3>::CornerType(i));
        Domain::Vec4f c4 = vp * Domain::Vec4f{v.x(), v.y(), v.z(), 1};
        Domain::Vec3f c3 = Domain::Vec3f{
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


}
