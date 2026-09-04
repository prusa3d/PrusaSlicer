#include "Slic3r/App/Scene/Ray.hpp"
#include "Slic3r/Assert.hpp"

#include <limits>

namespace Slic3r::App::Scene {

bool Ray::closest_point_from_ray(const Ray& ray, double& out_t) const
{
    auto a = ray.direction.dot(ray.direction);
    auto b = direction.dot(ray.direction);
    auto c = direction.dot(direction);
    auto denom = a * c - b * b;
    if (std::abs(denom) < std::numeric_limits<double>::epsilon())
        return false;
    auto w0 = ray.origin - origin;
    auto d = ray.direction.dot(w0);
    auto e = direction.dot(w0);
    out_t = (a * e - b * d) / denom;
    return true;
}

}
