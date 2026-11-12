#ifndef SLA_BOOSTADAPTER_HPP
#define SLA_BOOSTADAPTER_HPP

#include "Slic3r/Domain/BoundingBox.hpp"
#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/Line.hpp"
#include "Slic3r/Domain/Point.hpp"
#include "Slic3r/Domain/Polygon.hpp"
#include "Slic3r/Domain/Polyline.hpp"
#include "Slic3r/Domain/Types.hpp"

#include <boost/geometry.hpp>

namespace Slic3r {
template<int N, class T> using LegacyVec = Slic3r::Domain::Advanced::Vec<T, N>;
} // namespace Slic3r

namespace boost {

namespace geometry::traits {

/* ************************************************************************** */
/* Point concept adaptation ************************************************* */
/* ************************************************************************** */

template<> struct tag<Slic3r::Domain::Point> {
    using type = point_tag;
};

template<> struct coordinate_type<Slic3r::Domain::Point> {
    using type = Slic3r::Domain::coord_t;
};

template<> struct coordinate_system<Slic3r::Domain::Point> {
    using type = cs::cartesian;
};

template<> struct dimension<Slic3r::Domain::Point>: boost::mpl::int_<2> {};

template<std::size_t d> struct access<Slic3r::Domain::Point, d > {
    static inline Slic3r::Domain::coord_t get(Slic3r::Domain::Point const& a) {
        return a(d);
    }

    static inline void set(Slic3r::Domain::Point& a, Slic3r::Domain::coord_t const& value) {
        a(d) = value;
    }
};

// For Vec<N, T> ///////////////////////////////////////////////////////////////

template<int N, class T> struct tag<Slic3r::LegacyVec<N, T>> {
    using type = point_tag;
};

template<int N, class T> struct coordinate_type<Slic3r::LegacyVec<N, T>> {
    using type = T;
};

template<int N, class T> struct coordinate_system<Slic3r::LegacyVec<N, T>> {
    using type = cs::cartesian;
};

template<int N, class T> struct dimension<Slic3r::LegacyVec<N, T>>: boost::mpl::int_<N> {};

template<int N, class T, std::size_t d> struct access<Slic3r::LegacyVec<N, T>, d> {
    static inline T get(Slic3r::LegacyVec<N, T> const& a) {
        return a(d);
    }

    static inline void set(Slic3r::LegacyVec<N, T>& a, T const& value) {
        a(d) = value;
    }
};

/* ************************************************************************** */
/* Box concept adaptation *************************************************** */
/* ************************************************************************** */

template<> struct tag<Slic3r::Domain::BoundingBox2crd> {
    using type = box_tag;
};

template<> struct point_type<Slic3r::Domain::BoundingBox2crd> {
    using type = Slic3r::Domain::Point;
};

template<std::size_t d>
struct indexed_access<Slic3r::Domain::BoundingBox2crd, 0, d> {
    static inline Slic3r::Domain::coord_t get(Slic3r::Domain::BoundingBox2crd const& box) {
        return box.min(d);
    }
    static inline void set(Slic3r::Domain::BoundingBox2crd &box, Slic3r::Domain::coord_t const& coord) {
        box.min(d) = coord;
    }
};

template<std::size_t d>
struct indexed_access<Slic3r::Domain::BoundingBox2crd, 1, d> {
    static inline Slic3r::Domain::coord_t get(Slic3r::Domain::BoundingBox2crd const& box) {
        return box.max(d);
    }
    static inline void set(Slic3r::Domain::BoundingBox2crd &box, Slic3r::Domain::coord_t const& coord) {
        box.max(d) = coord;
    }
};

template<> struct tag<Slic3r::Domain::BoundingBox3f> {
    using type = box_tag;
};

template<> struct point_type<Slic3r::Domain::BoundingBox3f> {
    using type = Slic3r::Domain::Vec3f;
};

template<std::size_t d>
struct indexed_access<Slic3r::Domain::BoundingBox3f, 0, d> {
    static inline Slic3r::Domain::coord_t get(Slic3r::Domain::BoundingBox3f const& box) {
        return box.min(d);
    }
    static inline void set(Slic3r::Domain::BoundingBox3f &box, Slic3r::Domain::coord_t const& coord) {
        box.min(d) = coord;
    }
};

template<std::size_t d>
struct indexed_access<Slic3r::Domain::BoundingBox3f, 1, d> {
    static inline Slic3r::Domain::coord_t get(Slic3r::Domain::BoundingBox3f const& box) {
        return box.max(d);
    }
    static inline void set(Slic3r::Domain::BoundingBox3f &box, Slic3r::Domain::coord_t const& coord) {
        box.max(d) = coord;
    }
};


/* ************************************************************************** */
/* Segment concept adaptaion ************************************************ */
/* ************************************************************************** */

template<> struct tag<Slic3r::Domain::Line> {
    using type = segment_tag;
};

template<> struct point_type<Slic3r::Domain::Line> {
    using type = Slic3r::Domain::Point;
};

template<> struct indexed_access<Slic3r::Domain::Line, 0, 0> {
    static inline Slic3r::Domain::coord_t get(Slic3r::Domain::Line const& l) { return l.a.x(); }
    static inline void set(Slic3r::Domain::Line &l, Slic3r::Domain::coord_t c) { l.a.x() = c; }
};

template<> struct indexed_access<Slic3r::Domain::Line, 0, 1> {
    static inline Slic3r::Domain::coord_t get(Slic3r::Domain::Line const& l) { return l.a.y(); }
    static inline void set(Slic3r::Domain::Line &l, Slic3r::Domain::coord_t c) { l.a.y() = c; }
};

template<> struct indexed_access<Slic3r::Domain::Line, 1, 0> {
    static inline Slic3r::Domain::coord_t get(Slic3r::Domain::Line const& l) { return l.b.x(); }
    static inline void set(Slic3r::Domain::Line &l, Slic3r::Domain::coord_t c) { l.b.x() = c; }
};

template<> struct indexed_access<Slic3r::Domain::Line, 1, 1> {
    static inline Slic3r::Domain::coord_t get(Slic3r::Domain::Line const& l) { return l.b.y(); }
    static inline void set(Slic3r::Domain::Line &l, Slic3r::Domain::coord_t c) { l.b.y() = c; }
};

/* ************************************************************************** */
/* Polyline concept adaptation ********************************************** */
/* ************************************************************************** */

template<> struct tag<Slic3r::Domain::Polyline> {
    using type = linestring_tag;
};

/* ************************************************************************** */
/* Polygon concept adaptation *********************************************** */
/* ************************************************************************** */

// Ring implementation /////////////////////////////////////////////////////////

// Boost would refer to ClipperLib::Path (alias Slic3r::Domain::ExPolygon) as a ring
template<> struct tag<Slic3r::Domain::Polygon> {
    using type = ring_tag;
};

template<> struct point_order<Slic3r::Domain::Polygon> {
    static const order_selector value = counterclockwise;
};

// All our Paths should be closed for the bin packing application
template<> struct closure<Slic3r::Domain::Polygon> {
    static const constexpr closure_selector value = closure_selector::open;
};

// Polygon implementation //////////////////////////////////////////////////////

template<> struct tag<Slic3r::Domain::ExPolygon> {
    using type = polygon_tag;
};

template<> struct exterior_ring<Slic3r::Domain::ExPolygon> {
    static inline Slic3r::Domain::Polygon& get(Slic3r::Domain::ExPolygon& p)
    {
        return p.contour;
    }
    static inline Slic3r::Domain::Polygon const& get(Slic3r::Domain::ExPolygon const& p)
    {
        return p.contour;
    }
};

template<> struct ring_const_type<Slic3r::Domain::ExPolygon> {
    using type = const Slic3r::Domain::Polygon&;
};

template<> struct ring_mutable_type<Slic3r::Domain::ExPolygon> {
    using type = Slic3r::Domain::Polygon&;
};

template<> struct interior_const_type<Slic3r::Domain::ExPolygon> {
    using type = const Slic3r::Domain::Polygons&;
};

template<> struct interior_mutable_type<Slic3r::Domain::ExPolygon> {
    using type = Slic3r::Domain::Polygons&;
};

template<>
struct interior_rings<Slic3r::Domain::ExPolygon> {

    static inline Slic3r::Domain::Polygons& get(Slic3r::Domain::ExPolygon& p) { return p.holes; }

    static inline const Slic3r::Domain::Polygons& get(Slic3r::Domain::ExPolygon const& p)
    {
        return p.holes;
    }
};

/* ************************************************************************** */
/* MultiPolygon concept adaptation ****************************************** */
/* ************************************************************************** */

template<> struct tag<Slic3r::Domain::ExPolygons> {
    using type = multi_polygon_tag;
};

} // namespace geometry::traits

template<> struct range_value<std::vector<Slic3r::Domain::Vec2d>> {
    using type = Slic3r::Domain::Vec2d;
};

template<>
struct range_value<Slic3r::Domain::Polyline> {
    using type = Slic3r::Domain::Point;
};

// This is an addition to the ring implementation of Polygon concept
template<>
struct range_value<Slic3r::Domain::Polygon> {
    using type = Slic3r::Domain::Point;
};

template<>
struct range_value<Slic3r::Domain::Polygons> {
    using type = Slic3r::Domain::Polygon;
};

template<>
struct range_value<Slic3r::Domain::ExPolygons> {
    using type = Slic3r::Domain::ExPolygon;
};

} // namespace boost

#endif // SLABOOSTADAPTER_HPP
