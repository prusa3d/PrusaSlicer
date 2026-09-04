#include "Slic3r/Biz/Emboss/BendedProjection.hpp"
#include <algorithm>

namespace Slic3r::Biz::Emboss {

BendedProjection::BendedProjection(
    double depth,
    double width,
    double height,
    const Domain::Vec3d& center,
    float horizontal_bend,
    float vertical_curl,
    float vertical_arc
) :
    m_depth(depth),
    m_width(width),
    m_height(height),
    m_center(center),
    m_params{horizontal_bend, vertical_curl, vertical_arc}
{}

std::pair<Domain::Vec3d, Domain::Vec3d> BendedProjection::create_front_back(const Domain::Vec2crd& p) const
{
    Domain::Vec3d front(static_cast<double>(p.x()), static_cast<double>(p.y()), 0.0);
    Domain::Vec3d back(static_cast<double>(p.x()), static_cast<double>(p.y()), m_depth);

    Domain::BoundingBox3f bbox(
        (m_center - 0.5 * Domain::Vec3d(m_width, m_height, m_depth)).cast<float>(),
        (m_center + 0.5 * Domain::Vec3d(m_width, m_height, m_depth)).cast<float>()
    );

    Domain::Vec3d bent_front = TextBender::bend_point(front, m_params, bbox);
    Domain::Vec3d bent_back  = TextBender::bend_point(back, m_params, bbox);

    return std::make_pair(bent_front, bent_back);
}

Domain::Vec3d BendedProjection::project(const Domain::Vec3d& point) const
{
    // Bend displacement is independent of Z, so projection follows the same
    // extrusion direction without applying the deformation a second time.
    Domain::Vec3d result = point;
    result.z() += m_depth;
    return result;
}

std::optional<Domain::Vec2d> BendedProjection::unproject(const Domain::Vec3d& p, double* depth) const
{
    if (!p.allFinite())
        return std::nullopt;
    Domain::BoundingBox3f bbox(
        (m_center - 0.5 * Domain::Vec3d(m_width, m_height, m_depth)).cast<float>(),
        (m_center + 0.5 * Domain::Vec3d(m_width, m_height, m_depth)).cast<float>()
    );
    Domain::Vec3d unbent = TextBender::unbend_point(p, m_params, bbox);
    if ((TextBender::bend_point(unbent, m_params, bbox) - p).norm() > 1e-7 * std::max(1.0, p.norm()))
        return std::nullopt;
    if (depth != nullptr) {
        *depth = unbent.z();
    }
    return Domain::Vec2d(unbent.x(), unbent.y());
}

} // namespace Slic3r::Biz::Emboss
