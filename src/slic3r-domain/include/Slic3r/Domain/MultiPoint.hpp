#pragma once

#include "Slic3r/Domain/Point.hpp"

namespace Slic3r::Domain {

class MultiPoint
{
public:
    using iterator = Points::iterator;
    using const_iterator = Points::const_iterator;

    Points points;

    MultiPoint() = default;
    MultiPoint(const MultiPoint& other) = default;
    MultiPoint(MultiPoint&& other) noexcept : points(std::move(other.points)) {}
    MultiPoint(std::initializer_list<Point> list) : points(list) {}
    explicit MultiPoint(const Points& points) : points(points) {}
    explicit MultiPoint(Points&& points_) noexcept : points(std::move(points_)) {}

    virtual ~MultiPoint() = default;

    MultiPoint& operator=(const MultiPoint& other);
    MultiPoint& operator=(MultiPoint&& other) noexcept;

    Point& operator[](Points::size_type idx);
    const Point& operator[](Points::size_type idx) const;

    bool operator==(const MultiPoint& rhs) const;
    bool operator!=(const MultiPoint& rhs) const;

    const Point& front()       const { return this->points.front(); }
    const Point& back()        const { return this->points.back(); }
    const Point& first_point() const { return this->front(); }

    size_t size()     const { return this->points.size(); }
    bool   empty()    const { return this->points.empty(); }
    void   clear()          { this->points.clear(); }

    virtual void reverse();
    virtual bool is_valid() const;

    void append(const Point& point);
    void append(const Points& src_points);
    void append(const Points::const_iterator& begin, const Points::const_iterator& end);
    void append(Points&& src_points);

    void rotate(double angle);
    void rotate(double cos_angle, double sin_angle);
    void rotate(double angle, const Point& center);

    void translate(double x, double y);
    void translate(const Point &vector);

    void scale(double factor);
    void scale(double factor_x, double factor_y);

    inline Points::iterator               begin()         { return points.begin(); }
    inline Points::iterator               end()           { return points.end(); }
    inline Points::const_iterator         begin()   const { return points.cbegin(); }
    inline Points::const_iterator         end()     const { return points.cend(); }
    inline Points::const_iterator         cbegin()  const { return points.cbegin(); }
    inline Points::const_iterator         cend()    const { return points.cend(); }
    inline Points::reverse_iterator       rbegin()        { return points.rbegin(); }
    inline Points::reverse_iterator       rend()          { return points.rend(); }
    inline Points::const_reverse_iterator rbegin()  const { return points.crbegin(); }
    inline Points::const_reverse_iterator rend()    const { return points.crend(); }
    inline Points::const_reverse_iterator crbegin() const { return points.crbegin(); }
    inline Points::const_reverse_iterator crend()   const { return points.crend(); }

    /**
     * Finds the index of a point exactly equal to the given point.
     *
     * @param query_pt The point to search for.
     * @return Index of the matching point, or -1 if no such point exists.
     */
    int find_point(const Point& query_pt) const;
};

} // namespace Slic3r::Domain
