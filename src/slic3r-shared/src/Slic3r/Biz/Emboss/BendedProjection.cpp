#include "Slic3r/Biz/Emboss/BendedProjection.hpp"

namespace Slic3r::Biz::Emboss {

BendedProjection::BendedProjection(
    double depth,
    double width,
    double height,
    const Domain::Vec3d& center,
    float horizontal_bend,
    float vertical_curl
) :
    m_depth(depth),
    m_width(width),
    m_height(height),
    m_center(center),
    m_params{horizontal_bend, vertical_curl}
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
    Domain::BoundingBox3f bbox(
        (m_center - 0.5 * Domain::Vec3d(m_width, m_height, m_depth)).cast<float>(),
        (m_center + 0.5 * Domain::Vec3d(m_width, m_height, m_depth)).cast<float>()
    );
    Domain::Vec3d unbent = TextBender::unbend_point(point, m_params, bbox);
    unbent.z() += m_depth;
    return TextBender::bend_point(unbent, m_params, bbox);
}

std::optional<Domain::Vec2d> BendedProjection::unproject(const Domain::Vec3d& p, double* depth) const
{
    Domain::BoundingBox3f bbox(
        (m_center - 0.5 * Domain::Vec3d(m_width, m_height, m_depth)).cast<float>(),
        (m_center + 0.5 * Domain::Vec3d(m_width, m_height, m_depth)).cast<float>()
    );
    Domain::Vec3d unbent = TextBender::unbend_point(p, m_params, bbox);
    if (depth != nullptr) {
        *depth = unbent.z();
    }
    return Domain::Vec2d(unbent.x(), unbent.y());
}

} // namespace Slic3r::Biz::Emboss
