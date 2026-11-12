#pragma once

#include "Slic3r/Domain/MultiPoint.hpp"

namespace Slic3r::Domain {

class Polygon : public MultiPoint
{
public:
    Polygon() = default;
    Polygon(const Polygon& other) : MultiPoint(other.points) {}
    Polygon(Polygon&& other) noexcept : MultiPoint(std::move(other)) {}
    explicit Polygon(Points&& points_) noexcept : MultiPoint(std::move(points_)) {}
    Polygon(std::initializer_list<Point> points) : MultiPoint(points) {}
    explicit Polygon(const Points& points) : MultiPoint(points) {}

    ~Polygon() override = default;

    Polygon& operator=(const Polygon& other);
    Polygon& operator=(Polygon&& other) noexcept;

    bool operator==(const Polygon& rhs) const;
    bool operator!=(const Polygon& rhs) const;

    /**
     * Returns the last point, which for polygons equals to the first point.
     *
     * @return Reference to the last point.
     * @note Undefined behavior if empty.
     */
    const Point& last_point() const { return this->points.front(); }

    bool is_valid() const override;

    double length() const;
    double area() const;

    /**
     * Compute center of mass for this polygon.
     *
     * @return Center of mass for this polygon.
     * @note Source: https://en.wikipedia.org/wiki/Centroid
     */
    Point centroid() const;
};

using Polygons = std::vector<Domain::Polygon, Domain::PointsAllocator<Domain::Polygon>>;

} // namespace Slic3r::Domain
