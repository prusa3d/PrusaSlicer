#pragma once

#include "Slic3r/Domain/Polygon.hpp"

namespace Slic3r::Domain {

class ExPolygon
{
public:
    Polygon  contour; //CCW
    Polygons holes; //CW

    ExPolygon() = default;
    ExPolygon(const ExPolygon& other) = default;
    ExPolygon(ExPolygon&& other) noexcept = default;
    explicit ExPolygon(const Polygon& contour) : contour(contour) {}
    explicit ExPolygon(Polygon&& contour) noexcept : contour(std::move(contour)) {}
    explicit ExPolygon(const Points& contour) : contour(contour) {}
    explicit ExPolygon(Points&& contour) noexcept : contour(std::move(contour)) {}
    explicit ExPolygon(const Polygon& contour, const Polygon& hole) : contour(contour) { holes.emplace_back(hole); }
    explicit ExPolygon(Polygon&& contour, Polygon&& hole) noexcept : contour(std::move(contour)) { holes.emplace_back(std::move(hole)); }
    explicit ExPolygon(const Points& contour, const Points& hole) : contour(contour) { holes.emplace_back(hole); }
    explicit ExPolygon(Points&& contour, Polygon&& hole) noexcept : contour(std::move(contour)) { holes.emplace_back(std::move(hole)); }
    ExPolygon(std::initializer_list<Point> contour) : contour(contour) {}
    ExPolygon(std::initializer_list<Point> contour, std::initializer_list<Point> hole) : contour(contour), holes({hole}) {}

    virtual ~ExPolygon() = default;

    ExPolygon& operator=(const ExPolygon &other) = default;
    ExPolygon& operator=(ExPolygon &&other) noexcept = default;

    bool operator==(const ExPolygon& rhs) const;
    bool operator!=(const ExPolygon& rhs) const;

    bool empty() const;
    void clear();

    void scale(double factor);
    void scale(double factor_x, double factor_y);
    void translate(double x, double y);
    void translate(const Point& vector);
    void rotate(double angle);
    void rotate(double angle, const Point& center);

    double area() const;

    // Number of contours (outer contour with holes).
    size_t         num_contours() const;
    Polygon&       contour_or_hole(size_t idx);
    const Polygon& contour_or_hole(size_t idx) const;
};

using ExPolygons = std::vector<Domain::ExPolygon>;

} // namespace Slic3r::Domain
