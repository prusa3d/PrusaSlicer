#include "Slic3r/Domain/Line.hpp"
#include "Slic3r/Domain/Constants.hpp"
#include "Slic3r/Math.hpp"

#include <numbers>

namespace Slic3r::Domain::Impl {
inline double atan2(const Domain::Line& line)
{
    return std::atan2(line.b.y() - line.a.y(), line.b.x() - line.a.x());
}
} // namespace Slic3r::Domain::Impl

namespace Slic3r::Domain {

bool Line::operator==(const Line& rhs) const { return this->a == rhs.a && this->b == rhs.b; }

bool Line::operator!=(const Line& rhs) const { return this->a != rhs.a || this->b != rhs.b; }

double Line::length() const { return (b.cast<double>() - a.cast<double>()).norm(); }

double Line::orientation() const
{
    double angle = Impl::atan2(*this);
    if (angle < 0) {
        angle = 2. * std::numbers::pi + angle;
    }

    return angle;
}

double Line::direction() const
{
    const double atan2 = Impl::atan2(*this);
    return (fabs(atan2 - std::numbers::pi) < EPSILON) ? 0. :
           (atan2 < 0.)                               ? (atan2 + std::numbers::pi) :
                                                        atan2;
}

Point Line::midpoint() const { return (a + b) / 2; }

Vec2crd Line::vector() const { return this->b - this->a; }

Vec2crd Line::normal() const { return {(this->b.y() - this->a.y()), -(this->b.x() - this->a.x())}; }

void Line::scale(const double factor)
{
    this->a *= factor;
    this->b *= factor;
}

void Line::translate(const Point& vector)
{
    this->a += vector;
    this->b += vector;
}

void Line::translate(const coord_t x, const coord_t y) { this->translate(Point(x, y)); }

void Line::rotate(const double angle, const Point& center)
{
    this->a = Domain::rotated(this->a, angle, center);
    this->b = Domain::rotated(this->b, angle, center);
}

void Line::reverse() { std::swap(this->a, this->b); }

void Line::extend(const double offset)
{
    const Vec2crd offset_vector = (offset * this->vector().cast<double>().normalized()).cast<coord_t>();
    this->a -= offset_vector;
    this->b += offset_vector;
}

double Line::perp_signed_distance_to(const Point& point) const
{
    // Sign is dependent on the line orientation.
    // For CCW oriented polygon is possitive distace into shape and negative outside.
    // For Line({0,0},{0,2}) and point {1,1} the distance is negative one(-1).
    const Line& line = *this;
    const Vec2d v = (line.b - line.a).cast<double>();
    const Vec2d va = (point - line.a).cast<double>();
    if (line.a == line.b)
        return va.norm();

    return Slic3r::cross2(v, va) / v.norm();
}

double Line::perp_distance_to(const Point& point) const
{
    return std::abs(this->perp_signed_distance_to(point));
}

bool Line::is_parallel_to(const Line& line) const
{
    const Vec2d v1 = (this->b - this->a).cast<double>();
    const Vec2d v2 = (line.b - line.a).cast<double>();
    return Slic3r::sqr(Slic3r::cross2(v1, v2)) < Slic3r::sqr(EPSILON) * v1.squaredNorm() * v2.squaredNorm();
}

bool Line::is_parallel_to(const double angle) const
{
    const double diff = std::fabs(this->direction() - angle);
    return diff < EPSILON || std::fabs(diff - std::numbers::pi) < EPSILON;
}

bool Line::is_perpendicular_to(const Line& line) const
{
    const Vec2d v1 = (this->b - this->a).cast<double>();
    const Vec2d v2 = (line.b - line.a).cast<double>();
    return Slic3r::sqr(v1.dot(v2)) < Slic3r::sqr(EPSILON) * v1.squaredNorm() * v2.squaredNorm();
}

Vec3d Line3d::vector() const
{
    return this->b - this->a;
}

double Line3d::length() const
{
    return vector().norm();
}

void Line3d::scale(const double factor)
{
    this->a *= factor;
    this->b *= factor;
}

} // namespace Slic3r::Domain
