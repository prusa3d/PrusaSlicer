#include "Slic3r/Domain/MultiPoint.hpp"

#include "Slic3r/Domain/BoundingBox.hpp"
#include "Slic3r/Utils.hpp"

namespace Slic3r::Domain {

MultiPoint& MultiPoint::operator=(const MultiPoint& other)
{
    this->points = other.points;
    return *this;
}

MultiPoint& MultiPoint::operator=(MultiPoint&& other) noexcept
{
    this->points = std::move(other.points);
    return *this;
}

Point& MultiPoint::operator[](const Points::size_type idx) { return this->points[idx]; }

const Point& MultiPoint::operator[](const Points::size_type idx) const { return this->points[idx]; }

bool MultiPoint::operator==(const MultiPoint& rhs) const { return this->points == rhs.points; }

bool MultiPoint::operator!=(const MultiPoint& rhs) const { return this->points != rhs.points; }

void MultiPoint::reverse() { std::reverse(this->points.begin(), this->points.end()); }

bool MultiPoint::is_valid() const { return this->points.size() >= 2; }

void MultiPoint::append(const Point& point) { this->points.push_back(point); }

void MultiPoint::append(const Points& src_points)
{
    this->append(src_points.begin(), src_points.end());
}

void MultiPoint::append(const Points::const_iterator& begin, const Points::const_iterator& end)
{
    this->points.insert(this->points.end(), begin, end);
}

void MultiPoint::append(Points&& src_points)
{
    Slic3r::append(this->points, std::move(src_points));
}

void MultiPoint::rotate(double angle) { this->rotate(cos(angle), sin(angle)); }

void MultiPoint::rotate(double cos_angle, double sin_angle)
{
    for (Point& pt : this->points) {
        double cur_x = double(pt.x());
        double cur_y = double(pt.y());
        pt.x() = coord_t(std::round(cos_angle * cur_x - sin_angle * cur_y));
        pt.y() = coord_t(std::round(cos_angle * cur_y + sin_angle * cur_x));
    }
}

void MultiPoint::rotate(double angle, const Point& center)
{
    double s = sin(angle);
    double c = cos(angle);
    for (Point& pt : this->points) {
        Vec2crd v(pt - center);
        pt.x() = (coord_t) std::round(double(center.x()) + c * v.x() - s * v.y());
        pt.y() = (coord_t) std::round(double(center.y()) + c * v.y() + s * v.x());
    }
}

void MultiPoint::translate(double x, double y) { this->translate(Point(coord_t(x), coord_t(y))); }

void MultiPoint::translate(const Point& v)
{
    for (Point& pt : this->points) {
        pt += v;
    }
}

void MultiPoint::scale(double factor)
{
    for (Point& pt : this->points) {
        pt *= factor;
    }
}

void MultiPoint::scale(double factor_x, double factor_y)
{
    for (Point& pt : this->points) {
        pt.x() = coord_t(pt.x() * factor_x);
        pt.y() = coord_t(pt.y() * factor_y);
    }
}

int MultiPoint::find_point(const Point& query_pt) const
{
    auto query_it = std::find(this->points.cbegin(), this->points.cend(), query_pt);
    return (query_it != this->points.cend()) ? static_cast<int>(std::distance(this->points.cbegin(), query_it)) : -1;
}

} // namespace Slic3r::Domain
