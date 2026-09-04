#include "Slic3r/App/Scene/PickerFrustum.hpp"
#include "Slic3r/App/Scene/Camera.hpp"
#include "Slic3r/Biz/Algorithms/Geometry/ConvexHull.hpp"

#include <Slic3r/Assert.hpp>

namespace Slic3r::App::Scene {

bool PickerFrustum::intersects_precise(const Eigen::AlignedBox3d& box) const
{
    DEBUG_ASSERT(m_camera != nullptr);
    Domain::Points ss_box_corners;
    for (size_t j = 0; j < 8; ++j) {
        Domain::Vec2d ss = m_camera->project_to_screen_space(box.corner(Eigen::AlignedBox3d::CornerType(j)));
        ss_box_corners.push_back({ Domain::coord_t(ss.x()), Domain::coord_t(ss.y()) });
    }

    Domain::Polygon ss_box_convex_hull = Biz::Algorithms::Geometry::convex_hull(ss_box_corners);
    return Biz::Algorithms::Geometry::convex_polygons_intersect(ss_box_convex_hull, m_ss_rectangle);
}

void PickerFrustum::set_from(const Camera& camera, const Render::Rect& rect)
{
    Frustum::set_from(camera, rect);

    m_camera = &camera;
    int viewport_height = camera.viewport().height;
    int top = viewport_height - rect.y;
    int bottom = viewport_height - (rect.y + rect.height);
    int right = rect.x + rect.width;
    // vertices in CCW order
    m_ss_rectangle = {
        {rect.x, top},
        {rect.x, bottom},
        {right, bottom},
        {right, top}
    };
}

} // namespace Slic3r::App::Scene
