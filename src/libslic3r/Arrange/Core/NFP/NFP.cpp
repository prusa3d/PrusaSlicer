///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef NFP_CPP
#define NFP_CPP

#include "NFP.hpp"

#include "CircularEdgeIterator.hpp"
#include "NFPConcave_Tesselate.hpp"
#include "libslic3r/Arrange/Core/Beds.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/libslic3r.h"

#if !defined(_MSC_VER) && defined(__SIZEOF_INT128__) && !defined(__APPLE__)
namespace Slic3r { using LargeInt = __int128; }
#else
#include <boost/multiprecision/integer.hpp>

namespace Slic3r { using LargeInt = boost::multiprecision::int128_t; }
#endif

#include <boost/rational.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <utility>
#include <vector>
#include <cassert>

namespace Slic3r {

static bool line_cmp(const Line& e1, const Line& e2)
{
    using Ratio = boost::rational<LargeInt>;

    const Vec<2, int64_t> ax(1, 0); // Unit vector for the X axis

    Vec<2, int64_t> p1 = (e1.b - e1.a).cast<int64_t>();
    Vec<2, int64_t> p2 = (e2.b - e2.a).cast<int64_t>();

    // Quadrant mapping array. The quadrant of a vector can be determined
    // from the dot product of the vector and its perpendicular pair
    // with the unit vector X axis. The products will carry the values
    // lcos = dot(p, ax) = l * cos(phi) and
    // lsin = -dotperp(p, ax) = l * sin(phi) where
    // l is the length of vector p. From the signs of these values we can
    // construct an index which has the sign of lcos as MSB and the
    // sign of lsin as LSB. This index can be used to retrieve the actual
    // quadrant where vector p resides using the following map:
    // (+ is 0, - is 1)
    // cos | sin | decimal | quadrant
    //  +  |  +  |    0    |    0
    //  +  |  -  |    1    |    3
    //  -  |  +  |    2    |    1
    //  -  |  -  |    3    |    2
    std::array<int, 4> quadrants {0, 3, 1, 2 };

    std::array<int, 2> q {0, 0}; // Quadrant indices for p1 and p2

    using TDots = std::array<int64_t, 2>;
    TDots lcos { p1.dot(ax), p2.dot(ax) };
    TDots lsin { -dotperp(p1, ax), -dotperp(p2, ax) };

    // Construct the quadrant indices for p1 and p2
    for(size_t i = 0; i < 2; ++i) {
        if (lcos[i] == 0)
            q[i] = lsin[i] > 0 ? 1 : 3;
        else if (lsin[i] == 0)
            q[i] = lcos[i] > 0 ? 0 : 2;
        else
            q[i] = quadrants[((lcos[i] < 0) << 1) + (lsin[i] < 0)];
    }

    if (q[0] == q[1]) { // only bother if p1 and p2 are in the same quadrant
        auto lsq1 = p1.squaredNorm();     // squared magnitudes, avoid sqrt
        auto lsq2 = p2.squaredNorm();     // squared magnitudes, avoid sqrt

        // We will actually compare l^2 * cos^2(phi) which saturates the
        // cos function. But with the quadrant info we can get the sign back
        int sign = q[0] == 1 || q[0] == 2 ? -1 : 1;

        // If Ratio is an actual rational type, there is no precision loss
        auto pcos1 = Ratio(lcos[0]) / lsq1 * sign * lcos[0];
        auto pcos2 = Ratio(lcos[1]) / lsq2 * sign * lcos[1];

        return q[0] < 2 ? pcos1 > pcos2 : pcos1 < pcos2;
    }

    // If in different quadrants, compare the quadrant indices only.
    return q[0] < q[1];
}

static inline bool vsort(const Vec2crd& v1, const Vec2crd& v2)
{
    return v1.y() == v2.y() ? v1.x() < v2.x() : v1.y() < v2.y();
}

ExPolygons ifp_convex(const arr2::RectangleBed &obed, const Polygon &convexpoly)
{
    ExPolygon ret;

    auto sbox = bounding_box(convexpoly);
    auto sboxsize = sbox.size();
    coord_t sheight = sboxsize.y();
    coord_t swidth  = sboxsize.x();
    Point   sliding_top = reference_vertex(convexpoly);
    auto    leftOffset   = sliding_top.x() - sbox.min.x();
    auto    rightOffset  = sliding_top.x() - sbox.max.x();
    coord_t topOffset    = 0;
    auto    bottomOffset = sheight;

    auto bedbb = obed.bb;
//    bedbb.offset(1);
    auto bedsz = bedbb.size();
    auto boxWidth  = bedsz.x();
    auto boxHeight = bedsz.y();

    auto bedMinx = bedbb.min.x();
    auto bedMiny = bedbb.min.y();
    auto bedMaxx = bedbb.max.x();
    auto bedMaxy = bedbb.max.y();

    Polygon innerNfp{ Point{bedMinx + leftOffset, bedMaxy + topOffset},
                      Point{bedMaxx + rightOffset, bedMaxy + topOffset},
                      Point{bedMaxx + rightOffset, bedMiny + bottomOffset},
                      Point{bedMinx + leftOffset, bedMiny + bottomOffset},
                      Point{bedMinx + leftOffset, bedMaxy + topOffset} };

    if (sheight <= boxHeight && swidth <= boxWidth)
        ret.contour = std::move(innerNfp);

    return {ret};
}

Polygon ifp_convex_convex(const Polygon &fixed, const Polygon &movable)
{
    auto subnfps = reserve_polygons(fixed.size());

    // For each edge of the bed polygon, determine the nfp of convexpoly and
    // the zero area polygon formed by the edge. The union of all these sub-nfps
    // will contain a hole that is the actual ifp.
    auto lrange = line_range(fixed);
    for (const Line l : lrange) { // Older mac compilers generate warnging if line_range is called in-place
        Polygon fixed = {l.a, l.b};
        subnfps.emplace_back(nfp_convex_convex_legacy(fixed, movable));
    }

    // Do the union and then keep only the holes (should be only one or zero, if
    // the convexpoly cannot fit into the bed)
    Polygons ifp = union_(subnfps);
    Polygon ret;

    // find the first hole
    auto it = std::find_if(ifp.begin(), ifp.end(), [](const Polygon &subifp){
        return subifp.is_clockwise();
    });

    if (it != ifp.end()) {
        ret = std::move(*it);
        std::reverse(ret.begin(), ret.end());
    }

    return ret;
}

ExPolygons ifp_convex(const arr2::CircleBed &bed, const Polygon &convexpoly)
{
    Polygon circle = approximate_circle_with_polygon(bed);

    return {ExPolygon{ifp_convex_convex(circle, convexpoly)}};
}

ExPolygons ifp_convex(const arr2::IrregularBed &bed, const Polygon &convexpoly)
{
    auto bb = get_extents(bed.poly);
    bb.offset(scaled(1.));

    Polygon rect = arr2::to_rectangle(bb);

    ExPolygons blueprint = diff_ex(rect, bed.poly);
    Polygons ifp;
    for (const ExPolygon &part : blueprint) {
        Polygons triangles = Slic3r::convex_decomposition_tess(part);
        for (const Polygon &tr : triangles) {
            Polygon subifp = nfp_convex_convex_legacy(tr, convexpoly);
            ifp.emplace_back(std::move(subifp));
        }
    }

    ifp = union_(ifp);

    Polygons ret;

    std::copy_if(ifp.begin(), ifp.end(), std::back_inserter(ret),
                 [](const Polygon &p) { return p.is_clockwise(); });

    for (Polygon &p : ret)
        std::reverse(p.begin(), p.end());

    return to_expolygons(ret);
}

Vec2crd reference_vertex(const Polygon &poly)
{
    Vec2crd ret{std::numeric_limits<coord_t>::min(),
                std::numeric_limits<coord_t>::min()};

    auto it = std::max_element(poly.points.begin(), poly.points.end(), vsort);
    if (it != poly.points.end())
        ret = std::max(ret, static_cast<const Vec2crd &>(*it), vsort);

    return ret;
}

Vec2crd reference_vertex(const ExPolygon &expoly)
{
    return reference_vertex(expoly.contour);
}

Vec2crd reference_vertex(const Polygons &outline)
{
    Vec2crd ret{std::numeric_limits<coord_t>::min(),
                std::numeric_limits<coord_t>::min()};

    for (const Polygon &poly : outline)
        ret = std::max(ret, reference_vertex(poly), vsort);

    return ret;
}

Vec2crd reference_vertex(const ExPolygons &outline)
{
    Vec2crd ret{std::numeric_limits<coord_t>::min(),
                std::numeric_limits<coord_t>::min()};

    for (const ExPolygon &expoly : outline)
        ret = std::max(ret, reference_vertex(expoly), vsort);

    return ret;
}

Vec2crd min_vertex(const Polygon &poly)
{
    Vec2crd ret{std::numeric_limits<coord_t>::max(),
                std::numeric_limits<coord_t>::max()};

    auto it = std::min_element(poly.points.begin(), poly.points.end(), vsort);
    if (it != poly.points.end())
        ret = std::min(ret, static_cast<const Vec2crd&>(*it), vsort);

    return ret;
}

// Find the vertex corresponding to the edge with minimum angle to X axis.
// Only usable with CircularEdgeIterator<> template.
template<class It> It find_min_anglex_edge(It from)
{
    bool found = false;
    auto it = from;
    while (!found ) {
        found = !line_cmp(*it, *std::next(it));
        ++it;
    }

    return it;
}

// Only usable if both fixed and movable polygon is convex. In that case,
// their edges are already sorted by angle to X axis, only the starting
// (lowest X axis) edge needs to be found first.
void nfp_convex_convex(const Polygon &fixed, const Polygon &movable, Polygon &poly)
{
    if (fixed.empty() || movable.empty())
        return;

    // Clear poly and adjust its capacity. Nothing happens if poly is
    // already sufficiently large and and empty.
    poly.clear();
    poly.points.reserve(fixed.size() + movable.size());

    // Find starting positions on the fixed and moving polygons
    auto it_fx = find_min_anglex_edge(CircularEdgeIterator{fixed});
    auto it_mv = find_min_anglex_edge(CircularReverseEdgeIterator{movable});

    // End positions are at the same vertex after completing one full circle
    auto end_fx = it_fx + fixed.size();
    auto end_mv = it_mv + movable.size();

    // Pos zero is just fine as starting point:
    poly.points.emplace_back(0, 0);

    // Output iterator adapter for std::merge
    struct OutItAdaptor {
        using value_type [[maybe_unused]] = Line;
        using difference_type [[maybe_unused]] = std::ptrdiff_t;
        using pointer [[maybe_unused]] = Line*;
        using reference [[maybe_unused]] = Line& ;
        using iterator_category [[maybe_unused]] = std::output_iterator_tag;

        Polygon *outpoly;
        OutItAdaptor(Polygon &out) : outpoly{&out} {}

        OutItAdaptor &operator *() { return *this; }
        void operator=(const Line &l)
        {
            // Yielding l.b, offsetted so that l.a touches the last vertex in
            // in outpoly
            outpoly->points.emplace_back(l.b + outpoly->back() - l.a);
        }

        OutItAdaptor& operator++() { return *this; };
    };

    // Use std algo to merge the edges from the two polygons
    std::merge(it_fx, end_fx, it_mv, end_mv, OutItAdaptor{poly}, line_cmp);
}

Polygon nfp_convex_convex(const Polygon &fixed, const Polygon &movable)
{
    Polygon ret;
    nfp_convex_convex(fixed, movable, ret);

    return ret;
}

static void buildPolygon(const std::vector<Line>& edgelist,
                         Polygon& rpoly,
                         Point& top_nfp)
{
    auto& rsh = rpoly.points;

    rsh.reserve(2 * edgelist.size());

    // Add the two vertices from the first edge into the final polygon.
    rsh.emplace_back(edgelist.front().a);
    rsh.emplace_back(edgelist.front().b);

    // Sorting function for the nfp reference vertex search

    // the reference (rightmost top) vertex so far
    top_nfp = *std::max_element(std::cbegin(rsh), std::cend(rsh), vsort);

    auto tmp = std::next(std::begin(rsh));

    // Construct final nfp by placing each edge to the end of the previous
    for(auto eit = std::next(edgelist.begin()); eit != edgelist.end(); ++eit) {
        auto d = *tmp - eit->a;
        Vec2crd p = eit->b + d;

        rsh.emplace_back(p);

        // Set the new reference vertex
        if (vsort(top_nfp, p))
            top_nfp = p;

        tmp = std::next(tmp);
    }
}

Polygon nfp_convex_convex_legacy(const Polygon &fixed, const Polygon &movable)
{
    assert (!fixed.empty());
    assert (!movable.empty());

    Polygon rsh;   // Final nfp placeholder
    Point   max_nfp;
    std::vector<Line> edgelist;

    auto cap = fixed.points.size() + movable.points.size();

    // Reserve the needed memory
    edgelist.reserve(cap);
    rsh.points.reserve(cap);

    auto add_edge = [&edgelist](const Point &v1, const Point &v2) {
        Line e{v1, v2};
        if ((e.b - e.a).cast<int64_t>().squaredNorm() > 0)
            edgelist.emplace_back(e);
    };

    Point max_fixed = fixed.points.front();
    { // place all edges from fixed into edgelist
        auto first = std::cbegin(fixed);
        auto next = std::next(first);

        while(next != std::cend(fixed)) {
            add_edge(*(first), *(next));
            max_fixed = std::max(max_fixed, *first, vsort);

            ++first; ++next;
        }

        add_edge(*std::crbegin(fixed), *std::cbegin(fixed));
        max_fixed = std::max(max_fixed, *std::crbegin(fixed), vsort);
    }

    Point max_movable = movable.points.front();
    Point min_movable = movable.points.front();
    { // place all edges from movable into edgelist
        auto first = std::cbegin(movable);
        auto next = std::next(first);

        while(next != std::cend(movable)) {
            add_edge(*(next), *(first));
            min_movable = std::min(min_movable, *first, vsort);
            max_movable = std::max(max_movable, *first, vsort);

            ++first; ++next;
        }

        add_edge(*std::cbegin(movable), *std::crbegin(movable));
        min_movable = std::min(min_movable, *std::crbegin(movable), vsort);
        max_movable = std::max(max_movable, *std::crbegin(movable), vsort);
    }

    std::sort(edgelist.begin(), edgelist.end(), line_cmp);

    buildPolygon(edgelist, rsh, max_nfp);

    auto dtouch = max_fixed - min_movable;
    auto top_other = max_movable + dtouch;
    auto dnfp = top_other - max_nfp;
    rsh.translate(dnfp);

    return rsh;
}

} // namespace Slic3r

#endif // NFP_CPP
