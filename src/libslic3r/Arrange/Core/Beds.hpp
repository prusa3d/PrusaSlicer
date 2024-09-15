///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef BEDS_HPP
#define BEDS_HPP

#include <libslic3r/Point.hpp>
#include <libslic3r/ExPolygon.hpp>
#include <libslic3r/BoundingBox.hpp>
#include <libslic3r/ClipperUtils.hpp>
#include <boost/variant.hpp>
#include <boost/variant/variant.hpp>
#include <numeric>
#include <cmath>
#include <limits>
#include <type_traits>

#include "libslic3r/Polygon.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r { namespace arr2 {

// Bed types to be used with arrangement. Most generic bed is a simple polygon
// with holes, but other special bed types are also valid, like a bed without
// boundaries, or a special case of a rectangular or circular bed which leaves
// a lot of room for optimizations.

// Representing an unbounded bed.
struct InfiniteBed {
    Point center;
    explicit InfiniteBed(const Point &p = {0, 0}): center{p} {}
};

BoundingBox bounding_box(const InfiniteBed &bed);

inline InfiniteBed offset(const InfiniteBed &bed, coord_t) { return bed; }

struct RectangleBed {
    BoundingBox bb;

    explicit RectangleBed(const BoundingBox &bedbb) : bb{bedbb} {}
    explicit RectangleBed(coord_t w, coord_t h, Point c = {0, 0}):
        bb{{c.x() - w / 2, c.y() - h / 2}, {c.x() + w / 2, c.y() + h / 2}}
    {}

    coord_t width() const { return bb.size().x(); }
    coord_t height() const { return bb.size().y(); }
};

inline BoundingBox bounding_box(const RectangleBed &bed) { return bed.bb; }
inline RectangleBed offset(RectangleBed bed, coord_t v)
{
    bed.bb.offset(v);
    return bed;
}

Polygon to_rectangle(const BoundingBox &bb);

inline Polygon to_rectangle(const RectangleBed &bed)
{
    return to_rectangle(bed.bb);
}

class CircleBed {
    Point  m_center;
    double m_radius;

public:
    CircleBed(): m_center(0, 0), m_radius(NaNd) {}
    explicit CircleBed(const Point& c, double r)
        : m_center(c)
        , m_radius(r)
    {}

    double radius() const { return m_radius; }
    const Point& center() const { return m_center; }
};

// Function to approximate a circle with a convex polygon
Polygon approximate_circle_with_polygon(const CircleBed &bed, int nedges = 24);

inline BoundingBox bounding_box(const CircleBed &bed)
{
    auto r = static_cast<coord_t>(std::round(bed.radius()));
    Point R{r, r};

    return {bed.center() - R, bed.center() + R};
}
inline CircleBed offset(const CircleBed &bed, coord_t v)
{
    return CircleBed{bed.center(), bed.radius() + v};
}

struct IrregularBed { ExPolygons poly; };
inline BoundingBox bounding_box(const IrregularBed &bed)
{
    return get_extents(bed.poly);
}

inline IrregularBed offset(IrregularBed bed, coord_t v)
{
    bed.poly = offset_ex(bed.poly, v);
    return bed;
}

using ArrangeBed =
    boost::variant<InfiniteBed, RectangleBed, CircleBed, IrregularBed>;

inline BoundingBox bounding_box(const ArrangeBed &bed)
{
    BoundingBox ret;
    auto visitor = [&ret](const auto &b) { ret = bounding_box(b); };
    boost::apply_visitor(visitor, bed);

    return ret;
}

inline ArrangeBed offset(ArrangeBed bed, coord_t v)
{
    auto visitor = [v](auto &b) { b = offset(b, v); };
    boost::apply_visitor(visitor, bed);

    return bed;
}

inline double area(const BoundingBox &bb)
{
    auto bbsz = bb.size();
    return double(bbsz.x()) * bbsz.y();
}

inline double area(const RectangleBed &bed)
{
    auto bbsz = bed.bb.size();
    return double(bbsz.x()) * bbsz.y();
}

inline double area(const InfiniteBed &bed)
{
    return std::numeric_limits<double>::infinity();
}

inline double area(const IrregularBed &bed)
{
    return std::accumulate(bed.poly.begin(), bed.poly.end(), 0.,
                           [](double s, auto &p) { return s + p.area(); });
}

inline double area(const CircleBed &bed)
{
    return bed.radius() * bed.radius() * PI;
}

inline double area(const ArrangeBed &bed)
{
    double ret = 0.;
    auto visitor = [&ret](auto &b) { ret = area(b); };
    boost::apply_visitor(visitor, bed);

    return ret;
}

inline ExPolygons to_expolygons(const InfiniteBed &bed)
{
    return {ExPolygon{to_rectangle(RectangleBed{scaled(1000.), scaled(1000.)})}};
}

inline ExPolygons to_expolygons(const RectangleBed &bed)
{
    return {ExPolygon{to_rectangle(bed)}};
}

inline ExPolygons to_expolygons(const CircleBed &bed)
{
    return {ExPolygon{approximate_circle_with_polygon(bed)}};
}

inline ExPolygons to_expolygons(const IrregularBed &bed) { return bed.poly; }

inline ExPolygons to_expolygons(const ArrangeBed &bed)
{
    ExPolygons ret;
    auto visitor = [&ret](const auto &b) { ret = to_expolygons(b); };
    boost::apply_visitor(visitor, bed);

    return ret;
}

ArrangeBed to_arrange_bed(const Points &bedpts);

template<class Bed, class En = void> struct IsRectangular_ : public std::false_type {};
template<> struct IsRectangular_<RectangleBed>: public std::true_type {};
template<> struct IsRectangular_<BoundingBox>: public std::true_type {};

template<class Bed> static constexpr bool IsRectangular = IsRectangular_<Bed>::value;

} // namespace arr2

inline BoundingBox &bounding_box(BoundingBox &bb) { return bb; }
inline const BoundingBox &bounding_box(const BoundingBox &bb) { return bb; }
inline BoundingBox bounding_box(const Polygon &p) { return get_extents(p); }

} // namespace Slic3r

#endif // BEDS_HPP
