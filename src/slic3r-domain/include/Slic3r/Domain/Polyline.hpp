#pragma once

#include "Slic3r/Domain/MultiPoint.hpp"

namespace Slic3r::Domain {

class Polyline : public MultiPoint
{
public:
    using MultiPoint::append;

    Polyline() = default;
    Polyline(const Polyline& other) : MultiPoint(other.points) {}
    Polyline(Polyline&& other) noexcept : MultiPoint(std::move(other)) {}
    Polyline(std::initializer_list<Point> points) : MultiPoint(points) {}
    explicit Polyline(const Points& points) : MultiPoint(points) {}
    explicit Polyline(Points&& points) noexcept: MultiPoint(std::move(points)) {}
    explicit Polyline(const Point& p1, const Point& p2)
    {
        this->points.reserve(2);
        this->points.emplace_back(p1);
        this->points.emplace_back(p2);
    }

    ~Polyline() override = default;

    Polyline& operator=(const Polyline& other);
    Polyline& operator=(Polyline&& other) noexcept;

    bool operator==(const Polyline& rhs) const;
    bool operator!=(const Polyline& rhs) const;

    void append(const Polyline& src);
    void append(Polyline&& src);

    /**
     * Returns the last point.
     *
     * @return Reference to the last point.
     * @note Undefined behavior if empty.
     */
    const Point& last_point() const;

    bool is_closed() const;

    double length() const;
};

using Polylines = std::vector<Polyline>;

} // namespace Slic3r::Domain
