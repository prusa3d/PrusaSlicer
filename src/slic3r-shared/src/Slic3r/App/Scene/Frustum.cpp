#include "Slic3r/App/Scene/Frustum.hpp"
#include "Slic3r/App/Scene/Camera.hpp"
#include "Slic3r/App/Render/ScreenInfo.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Biz/Algorithms/Geometry/ConvexHull.hpp"

#include <Slic3r/Assert.hpp>

using Slic3r::Domain::Vec3d;

namespace Slic3r::App::Scene {

bool Frustum::intersects_fast(const Eigen::AlignedBox3d& box) const
{
    size_t tested_planes = 0;
    // degenerated frustum
    if (m_planes.size() < 6) {
        if (!m_planes.front().intersects(box))
            return false;
        tested_planes = 1;
    }

    if (!intersects(box.center(), 0.5 * box.diagonal().norm()))
        return false;

    size_t inside = 0;
    for (size_t i = tested_planes; i < m_planes.size(); ++i) {
        inside = 0;
        for (size_t j = 0; j < 8 && inside == 0; ++j) {
            if (m_planes[i].signed_distance(box.corner(Eigen::AlignedBox3d::CornerType(j))) >= 0.0)
            ++inside;
        }
        if (inside == 0)
            return false;
    }
    return true;
}

bool Frustum::intersects(const Vec3d& center, double radius) const
{
    size_t tested_planes = 0;
    // degenerated frustum
    if (m_planes.size() < 6) {
        if (!m_planes.front().intersects(center, radius))
            return false;
        tested_planes = 1;
    }

    for (size_t i = tested_planes; i < m_planes.size(); ++i) {
        if (m_planes[i].signed_distance(center) < -radius)
            return false;
    }
    return true;
}

struct FrustumCorners
{
    Vec3d nlb; // near left bottom
    Vec3d nrb; // near right bottom
    Vec3d nrt; // near right top
    Vec3d nlt; // near left top
    Vec3d flb; // far left bottom
    Vec3d frb; // far right bottom
    Vec3d frt; // far right top
    Vec3d flt; // far left top
};

static FrustumCorners detect_corners(const Camera& camera, const Render::Rect& rect)
{
    Scene::Ray ray_lt = camera.ray_at(rect.x, rect.y);
    Scene::Ray ray_lb = camera.ray_at(rect.x, rect.y + rect.height);
    Scene::Ray ray_rt = camera.ray_at(rect.x + rect.width, rect.y);
    Scene::Ray ray_rb = camera.ray_at(rect.x + rect.width, rect.y + rect.height);

    const Scene::AbstractCameraProjection& cam_proj = camera.cam_projection();
    double near_z = cam_proj.z_near();
    double far_z  = cam_proj.z_far();

    Vec3d camera_position = camera.position();
    Vec3d camera_forward  = camera.forward();
    Scene::Plane camera_near = Scene::Plane::from_point_and_normal(camera_position + near_z * camera_forward, camera_forward);
    Scene::Plane camera_far  = Scene::Plane::from_point_and_normal(camera_position + far_z * camera_forward, -camera_forward);

    double t = 0.0;
    FrustumCorners ret;
    if (camera_near.intersects(ray_lb, t)) ret.nlb = ray_lb.point_at(t); else { DEBUG_ASSERT(false); }
    if (camera_near.intersects(ray_rb, t)) ret.nrb = ray_rb.point_at(t); else { DEBUG_ASSERT(false); }
    if (camera_near.intersects(ray_rt, t)) ret.nrt = ray_rt.point_at(t); else { DEBUG_ASSERT(false); }
    if (camera_near.intersects(ray_lt, t)) ret.nlt = ray_lt.point_at(t); else { DEBUG_ASSERT(false); }
    if (camera_far.intersects(ray_lb, t))  ret.flb = ray_lb.point_at(t); else { DEBUG_ASSERT(false); }
    if (camera_far.intersects(ray_rb, t))  ret.frb = ray_rb.point_at(t); else { DEBUG_ASSERT(false); }
    if (camera_far.intersects(ray_rt, t))  ret.frt = ray_rt.point_at(t); else { DEBUG_ASSERT(false); }
    if (camera_far.intersects(ray_lt, t))  ret.flt = ray_lt.point_at(t); else { DEBUG_ASSERT(false); }
    return ret;
}

void Frustum::set_from(const Camera& camera, const Render::Rect& rect)
{
    FrustumCorners vs = detect_corners(camera, rect);
    // rectangle degenerated into a vertical segment
    if (vs.nlb.isApprox(vs.nrb)) {
        m_vertices = { vs.nlb, vs.nlt, vs.flb, vs.flt };
        m_planes = {
            // vertical
            Scene::Plane::from_three_points(vs.nlb, vs.flb, vs.nlt).normalized(),
            // horizontal bottom
            Scene::Plane::from_three_points(vs.nlb, vs.nlb + camera.right(), vs.flb).normalized(),
            // horizontal top
            Scene::Plane::from_three_points(vs.nlt + camera.right(), vs.nlt, vs.frt).normalized()
        };
    }
    // rectangle degenerated into an horizontal segment
    else if (vs.nlb.isApprox(vs.nlt)) {
        m_vertices = { vs.nlb, vs.nrb, vs.flb, vs.frb };
        m_planes = {
            // horizontal
            Scene::Plane::from_three_points(vs.nlb, vs.nrb, vs.flb).normalized(),
            // vertical left
            Scene::Plane::from_three_points(vs.nlb, vs.flb, vs.nlb + camera.up()).normalized(),
            // vertical right
            Scene::Plane::from_three_points(vs.frb, vs.nrb, vs.frb + camera.up()).normalized(),
        };
    }
    // regular rectangle
    else {
        m_vertices = { vs.nlb, vs.nrb, vs.nrt, vs.nlt, vs.flb, vs.frb, vs.frt, vs.flt };
        m_planes = {
            // near
            Scene::Plane::from_three_points(vs.nrb, vs.nlb, vs.nlt).normalized(),
            // far
            Scene::Plane::from_three_points(vs.flb, vs.frb, vs.flt).normalized(),
            // left
            Scene::Plane::from_three_points(vs.nlb, vs.flb, vs.nlt).normalized(),
            // right
            Scene::Plane::from_three_points(vs.frb, vs.nrb, vs.frt).normalized(),
            // bottom
            Scene::Plane::from_three_points(vs.nlb, vs.nrb, vs.flb).normalized(),
            // top
            Scene::Plane::from_three_points(vs.nrt, vs.nlt, vs.frt).normalized()
        };
    }
}

} // namespace Slic3r::App::Scene
