#pragma once

#include "Slic3r/Domain/Point.hpp"
#include <boost/polygon/segment_concept.hpp>

namespace Slic3r::Domain {

class Line
{
public:
    static constexpr std::size_t Dim    = 2;
    using                        Scalar = Point::Scalar;

    Point a;
    Point b;

    Line() = default;
    Line(const Point& a, const Point& b) : a(a), b(b) {}

    virtual ~Line() = default;

    bool operator==(const Line& rhs) const;
    bool operator!=(const Line& rhs) const;

    double length() const;
    double orientation() const;
    double direction() const;
    Point midpoint() const;

    Vec2crd vector() const;
    Vec2crd normal() const;

    void scale(double factor);
    void translate(const Point& v);
    void translate(coord_t x, coord_t y);
    void rotate(double angle, const Point& center);
    void reverse();
    void extend(double offset);

    double perp_signed_distance_to(const Point& point) const;
    double perp_distance_to(const Point& point) const;

    bool is_parallel_to(const Line& line) const;
    bool is_parallel_to(double angle) const;
    bool is_perpendicular_to(const Line& line) const;
};

using Lines = std::vector<Line>;

class Line2d
{
public:
    static const constexpr int Dim    = 2;
    using                      Scalar = Vec2d::Scalar;

    Vec2d a;
    Vec2d b;

    Line2d(): a(Vec2d::Zero()), b(Vec2d::Zero()) {}
    Line2d(const Vec2d& a, const Vec2d& b): a(a), b(b) {}

    virtual ~Line2d() = default;
};

using Line2ds = std::vector<Line2d>;

class Line3d
{
public:
    static const constexpr int Dim    = 3;
    using                      Scalar = Vec3d::Scalar;

    Vec3d a;
    Vec3d b;

    Line3d(): a(Vec3d::Zero()), b(Vec3d::Zero()) {}
    Line3d(const Vec3d& a, const Vec3d& b): a(a), b(b) {}

    virtual ~Line3d() = default;

    Vec3d   vector() const;
    double  length() const;

    void    scale(double factor);
};

using Line3ds = std::vector<Line3d>;



} // namespace Slic3r::Domain

namespace boost { namespace polygon {
    template <>
    struct geometry_concept<Slic3r::Domain::Line> { typedef segment_concept type; };

    template <>
    struct segment_traits<Slic3r::Domain::Line> {
        typedef Slic3r::Domain::coord_t coordinate_type;
        typedef Slic3r::Domain::Point point_type;

        static inline point_type get(const Slic3r::Domain::Line& line, direction_1d dir) {
            return dir.to_int() ? line.b : line.a;
        }
    };
} }
