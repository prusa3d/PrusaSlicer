#include "Slic3r/Domain/ExPolygon.hpp"

namespace Slic3r::Domain {

bool ExPolygon::operator==(const ExPolygon& rhs) const { return this->contour == rhs.contour && this->holes == rhs.holes; }

bool ExPolygon::operator!=(const ExPolygon& rhs) const { return this->contour != rhs.contour || this->holes != rhs.holes; }

bool ExPolygon::empty() const { return this->contour.points.empty(); }

void ExPolygon::clear()
{
    this->contour.points.clear();
    this->holes.clear();
}

//bool is_valid() const;

void ExPolygon::scale(const double factor)
{
    this->contour.scale(factor);
    for (Polygon& hole : this->holes) {
        hole.scale(factor);
    }
}

void ExPolygon::scale(const double factor_x, const double factor_y)
{
    this->contour.scale(factor_x, factor_y);
    for (Polygon& hole : this->holes) {
        hole.scale(factor_x, factor_y);
    }
}

void ExPolygon::translate(const Point& p)
{
    this->contour.translate(p);
    for (Polygon& hole : this->holes) {
        hole.translate(p);
    }
}

void ExPolygon::translate(const double x, const double y)
{
    this->translate(Point(coord_t(x), coord_t(y)));
}

void ExPolygon::rotate(const double angle)
{
    this->contour.rotate(angle);
    for (Polygon& hole : this->holes) {
        hole.rotate(angle);
    }
}

void ExPolygon::rotate(const double angle, const Point& center)
{
    this->contour.rotate(angle, center);
    for (Polygon& hole : this->holes) {
        hole.rotate(angle, center);
    }
}

double ExPolygon::area() const
{
    double a = this->contour.area();
    for (const Polygon& hole : this->holes) {
        a -= -hole.area(); // Holes have negative area.
    }

    return a;
}

size_t ExPolygon::num_contours() const { return this->holes.size() + 1; }

Polygon& ExPolygon::contour_or_hole(const size_t idx)
{
    return (idx == 0) ? this->contour : this->holes[idx - 1];
}

const Polygon& ExPolygon::contour_or_hole(const size_t idx) const
{
    return (idx == 0) ? this->contour : this->holes[idx - 1];
}

} // namespace Slic3r::Domain
