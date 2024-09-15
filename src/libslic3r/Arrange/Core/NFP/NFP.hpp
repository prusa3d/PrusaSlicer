///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef NFP_HPP
#define NFP_HPP

#include <libslic3r/ExPolygon.hpp>
#include <libslic3r/Arrange/Core/Beds.hpp>
#include <stdint.h>
#include <boost/variant.hpp>
#include <cinttypes>

#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"

namespace Slic3r {

template<class Unit = int64_t, class T>
Unit dotperp(const Vec<2, T> &a, const Vec<2, T> &b)
{
    return Unit(a.x()) * Unit(b.y()) - Unit(a.y()) * Unit(b.x());
}

// Convex-Convex nfp in linear time (fixed.size() + movable.size()),
// no memory allocations (if out param is used).
// FIXME: Currently broken for very sharp triangles.
Polygon nfp_convex_convex(const Polygon &fixed, const Polygon &movable);
void nfp_convex_convex(const Polygon &fixed, const Polygon &movable, Polygon &out);
Polygon nfp_convex_convex_legacy(const Polygon &fixed, const Polygon &movable);

Polygon ifp_convex_convex(const Polygon &fixed, const Polygon &movable);

ExPolygons ifp_convex(const arr2::RectangleBed &bed, const Polygon &convexpoly);
ExPolygons ifp_convex(const arr2::CircleBed &bed, const Polygon &convexpoly);
ExPolygons ifp_convex(const arr2::IrregularBed &bed, const Polygon &convexpoly);
inline ExPolygons ifp_convex(const arr2::InfiniteBed &bed, const Polygon &convexpoly)
{
    return {};
}

inline ExPolygons ifp_convex(const arr2::ArrangeBed &bed, const Polygon &convexpoly)
{
    ExPolygons ret;
    auto visitor = [&ret, &convexpoly](const auto &b) { ret = ifp_convex(b, convexpoly); };
    boost::apply_visitor(visitor, bed);

    return ret;
}

Vec2crd reference_vertex(const Polygon &outline);
Vec2crd reference_vertex(const ExPolygon &outline);
Vec2crd reference_vertex(const Polygons &outline);
Vec2crd reference_vertex(const ExPolygons &outline);

Vec2crd min_vertex(const Polygon &outline);

} // namespace Slic3r

#endif // NFP_HPP
