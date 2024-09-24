///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/partition_2.h>
#include <CGAL/Partition_traits_2.h>
#include <CGAL/property_map.h>
#include <CGAL/Polygon_vertical_decomposition_2.h>
#include <iterator>
#include <utility>
#include <vector>
#include <cstddef>

#include "NFP.hpp"
#include "NFPConcave_CGAL.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r {

using K = CGAL::Exact_predicates_inexact_constructions_kernel;
using Partition_traits_2 = CGAL::Partition_traits_2<K, CGAL::Pointer_property_map<K::Point_2>::type >;
using Point_2 = Partition_traits_2::Point_2;
using Polygon_2 = Partition_traits_2::Polygon_2;  // a polygon of indices

ExPolygons nfp_concave_concave_cgal(const ExPolygon &fixed, const ExPolygon &movable)
{
    Polygons fixed_decomp = convex_decomposition_cgal(fixed);
    Polygons movable_decomp = convex_decomposition_cgal(movable);

    auto refs_mv = reserve_vector<Vec2crd>(movable_decomp.size());

    for (const Polygon &p : movable_decomp)
        refs_mv.emplace_back(reference_vertex(p));

    auto nfps = reserve_polygons(fixed_decomp.size() *movable_decomp.size());

    Vec2crd ref_whole = reference_vertex(movable);
    for (const Polygon &fixed_part : fixed_decomp) {
        size_t mvi = 0;
        for (const Polygon &movable_part : movable_decomp) {
            Polygon subnfp = nfp_convex_convex(fixed_part, movable_part);
            const Vec2crd &ref_mp = refs_mv[mvi];
            auto d = ref_whole - ref_mp;
            subnfp.translate(d);
            nfps.emplace_back(subnfp);
            mvi++;
        }
    }

    return union_ex(nfps);
}

// TODO: holes
Polygons convex_decomposition_cgal(const ExPolygon &expoly)
{
    CGAL::Polygon_vertical_decomposition_2<K> decomp;

    CGAL::Polygon_2<K> contour;
    for (auto &p : expoly.contour.points)
        contour.push_back({unscaled(p.x()), unscaled(p.y())});

    CGAL::Polygon_with_holes_2<K> cgalpoly{contour};
    for (const Polygon &h : expoly.holes) {
        CGAL::Polygon_2<K> hole;
        for (auto &p : h.points)
            hole.push_back({unscaled(p.x()), unscaled(p.y())});

        cgalpoly.add_hole(hole);
    }

    std::vector<CGAL::Polygon_2<K>> out;
    decomp(cgalpoly, std::back_inserter(out));

    Polygons ret;
    for (auto &pwh : out) {
        Polygon poly;
        for (auto &p : pwh)
            poly.points.emplace_back(scaled(p.x()), scaled(p.y()));
        ret.emplace_back(std::move(poly));
    }

    return ret; //convex_decomposition_cgal(expoly.contour);
}

Polygons convex_decomposition_cgal(const Polygon &poly)
{
    auto pts = reserve_vector<K::Point_2>(poly.size());

    for (const Point &p : poly.points)
        pts.emplace_back(unscaled(p.x()), unscaled(p.y()));

    Partition_traits_2 traits(CGAL::make_property_map(pts));

    Polygon_2 polyidx;
    for (size_t i = 0; i < pts.size(); ++i)
        polyidx.push_back(i);

    std::vector<Polygon_2> outp;

    CGAL::optimal_convex_partition_2(polyidx.vertices_begin(),
                                     polyidx.vertices_end(),
                                     std::back_inserter(outp),
                                     traits);

    Polygons ret;
    for (const Polygon_2& poly : outp){
        Polygon r;
        for(Point_2 p : poly.container())
            r.points.emplace_back(scaled(pts[p].x()), scaled(pts[p].y()));

        ret.emplace_back(std::move(r));
    }

    return ret;
}

} // namespace Slic3r
