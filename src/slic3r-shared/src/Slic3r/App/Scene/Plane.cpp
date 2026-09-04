#include "Slic3r/App/Scene/Plane.hpp"
#include "Slic3r/App/Scene/Ray.hpp"

using Slic3r::Domain::Vec3d;

namespace Slic3r::App::Scene {

void Plane::normalize()
{
    double l = normal.norm();
    if (l != 0.0) {
        normal.normalize();
        d /= l;
    }
}

Plane Plane::normalized() const
{
    Plane ret = *this;
    ret.normalize();
    return ret;
}

Plane Plane::from_point_and_vectors(const Vec3d& point, const Vec3d& v0, const Vec3d& v1)
{
    const Vec3d normal = v0.cross(v1);
    const double d = -normal.dot(point);
    return {normal, d};
}

bool Plane::intersects(const Ray& ray, double& t) const
{
    const double denom = ray.direction.dot(normal);
    if (std::fabs(denom) <= std::numeric_limits<double>::epsilon())
        // the ray and plane are parallel => the only case there is no intersection
        return false;

    t = - (ray.origin.dot(normal) + d) / denom;
    return true;
}

bool Plane::intersects(const Eigen::AlignedBox3d& box) const
{
    const Vec3d& b_min = box.min();
    const Vec3d& b_max = box.max();
    Vec3d p_min = {
        (normal.x() >= 0.0) ? b_min.x() : b_max.x(),
        (normal.y() >= 0.0) ? b_min.y() : b_max.y(),
        (normal.z() >= 0.0) ? b_min.z() : b_max.z()
    };
    Vec3d p_max = {
        (normal.x() >= 0.0) ? b_max.x() : b_min.x(),
        (normal.y() >= 0.0) ? b_max.y() : b_min.y(),
        (normal.z() >= 0.0) ? b_max.z() : b_min.z()
    };
    return signed_distance(p_max) > 0.0 && signed_distance(p_min) < 0.0;
}

} // namespace Slic3r::App::Scene
