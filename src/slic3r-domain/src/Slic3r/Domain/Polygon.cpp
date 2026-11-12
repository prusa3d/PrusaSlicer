#include "Slic3r/Domain/Polygon.hpp"
#include "Slic3r/Math.hpp"

namespace Slic3r::Domain {

Polygon& Polygon::operator=(const Polygon& other)
{
    this->points = other.points;
    return *this;
}

Polygon& Polygon::operator=(Polygon&& other) noexcept
{
    this->points = std::move(other.points);
    return *this;
}

bool Polygon::operator==(const Polygon& rhs) const { return this->points == rhs.points; }

bool Polygon::operator!=(const Polygon& rhs) const { return this->points != rhs.points; }

bool Polygon::is_valid() const { return this->points.size() >= 3; }

double Polygon::length() const
{
    if (this->points.size() < 2)
        return 0.;

    double total_length = (this->points.back() - this->points.front()).cast<double>().norm();
    for (size_t idx = 1; idx < this->points.size(); ++idx) {
        total_length += (this->points[idx] - this->points[idx - 1]).cast<double>().norm();
    }

    return total_length;
}

double Polygon::area() const
{
    if (points.size() < 3)
        return 0.;

    double total_area = 0.;
    Vec2d p1 = points.back().cast<double>();
    for (const Point& p : points) {
        Vec2d p2 = p.cast<double>();
        total_area += Slic3r::cross2(p1, p2);
        p1 = p2;
    }

    return 0.5 * total_area;
}

Point Polygon::centroid() const
{
    double area_sum = 0.;
    Vec2d c(0., 0.);
    if (points.size() >= 3) {
        Vec2d p1 = points.back().cast<double>();
        for (const Point& p : points) {
            const Vec2d  p2 = p.cast<double>();
            const double a  = cross2(p1, p2);
            area_sum += a;
            c        += (p1 + p2) * a;
            p1        = p2;
        }
    }

    Vec2d result{c / (3. * area_sum)};
    return Point(Vec2d{std::round(result.x()), std::round(result.y())}.cast<coord_t>());
}

} // namespace Slic3r::Domain
