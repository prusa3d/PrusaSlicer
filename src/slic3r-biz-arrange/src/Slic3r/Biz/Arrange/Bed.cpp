#include "Slic3r/Biz/Arrange/Bed.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/Biz/Algorithms/ClipperUtils.hpp"
#include "Slic3r/Biz/Algorithms/Polygon.hpp"
#include "Slic3r/Biz/Algorithms/ExPolygon.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include <algorithm>
#include "Slic3r/Biz/Algorithms/Scaling.hpp"
#include "Slic3r/Biz/Arrange/NFP.hpp"
#include "Slic3r/Biz/Arrange/Utils.hpp"
#include "Slic3r/Biz/Arrange/Tesselate.hpp"
#include <boost/rational.hpp>
#include <numeric>
#include <numbers>

#if !defined(_MSC_VER) && defined(__SIZEOF_INT128__) && !defined(__APPLE__)
namespace {
using LargeInt = __int128;
} // namespace
#else
#include <boost/multiprecision/integer.hpp>

namespace {
using LargeInt = boost::multiprecision::int128_t;
} // namespace
#endif

namespace Slic3r::Biz::Arrange {

using Biz::Algorithms::ExPolygon::get_extents;
using Biz::Algorithms::Polygon::get_extents;
using Biz::Algorithms::Scaling::scaled;
using Domain::BoundingBox2crd;
using Domain::coord_t;
using Domain::ExPolygon;
using Domain::ExPolygons;
using Domain::Polygon;
using Domain::Polygons;
using Domain::Vec2big;
using Domain::Vec2crd;

namespace {
Polygon approximate_circle_with_polygon(const Vec2crd center, const double radius, int nedges)
{
    Polygon ret;

    double angle_incr = (2 * M_PI) / nedges; // Angle increment for each edge
    double angle      = 0; // Starting angle

    // Loop to generate vertices for each edge
    for (int i = 0; i < nedges; i++) {
        // Calculate coordinates of the vertices using trigonometry
        auto x = center.x() + static_cast<coord_t>(radius * std::cos(angle));
        auto y = center.y() + static_cast<coord_t>(radius * std::sin(angle));

        // Add vertex to the vector
        ret.points.emplace_back(x, y);

        // Update the angle for the next iteration
        angle += angle_incr;
    }

    return ret;
}
} // namespace

InfiniteBed::InfiniteBed(const Domain::Vec2crd& center) : m_center{center} {}

BoundingBox2crd InfiniteBed::bounding_box() const
{
    BoundingBox2crd ret;
    using C = coord_t;

    // It is important for Mx and My to be strictly less than half of the
    // range of type C. width(), height() and area() will not overflow this way.
    C Mx = C((std::numeric_limits<C>::lowest() + 2 * m_center.x()) / 4.01);
    C My = C((std::numeric_limits<C>::lowest() + 2 * m_center.y()) / 4.01);

    ret.max = m_center - Vec2crd{Mx, My};
    ret.min = m_center + Vec2crd{Mx, My};

    return ret;
}

ExPolygons InfiniteBed::ifp_convex(const Polygon& convexpoly) const
{
    return {};
}

double InfiniteBed::area() const
{
    PANIC("Can't get area of infinite bed!");
}

RectangleBed::RectangleBed(
    const Domain::BoundingBox2crd& bb,
    const PivotPoint pivot_point,
    const Domain::BedSegments segments
) :
    m_bb{bb},
    m_pivot_point{pivot_point},
    m_segments{segments}
{}

BoundingBox2crd RectangleBed::bounding_box() const
{
    return m_bb;
}

ExPolygons RectangleBed::ifp_convex(const Polygon& convexpoly) const
{
    ExPolygon ret;

    auto sbox           = get_extents(convexpoly);
    auto sboxsize       = Algorithms::BoundingBox::sizes(sbox);
    coord_t sheight     = sboxsize.y();
    coord_t swidth      = sboxsize.x();
    Vec2crd sliding_top = reference_vertex(convexpoly);
    auto leftOffset     = sliding_top.x() - sbox.min.x();
    auto rightOffset    = sliding_top.x() - sbox.max.x();
    coord_t topOffset   = 0;
    auto bottomOffset   = sheight;

    auto bedbb = m_bb;

    auto bedsz     = Algorithms::BoundingBox::sizes(bedbb);
    auto boxWidth  = bedsz.x();
    auto boxHeight = bedsz.y();

    auto bedMinx = bedbb.min.x();
    auto bedMiny = bedbb.min.y();
    auto bedMaxx = bedbb.max.x();
    auto bedMaxy = bedbb.max.y();

    Polygon innerNfp{
        Vec2crd{bedMinx + leftOffset, bedMaxy + topOffset},
        Vec2crd{bedMaxx + rightOffset, bedMaxy + topOffset},
        Vec2crd{bedMaxx + rightOffset, bedMiny + bottomOffset},
        Vec2crd{bedMinx + leftOffset, bedMiny + bottomOffset},
        Vec2crd{bedMinx + leftOffset, bedMaxy + topOffset}
    };

    if (sheight <= boxHeight && swidth <= boxWidth) {
        ret.contour = std::move(innerNfp);
    }

    return {ret};
}

double RectangleBed::area() const
{
    auto bbsz = Biz::Algorithms::BoundingBox::sizes(m_bb);
    return double(bbsz.x()) * bbsz.y();
}

PivotPoint RectangleBed::pivot_point() const
{
    return m_pivot_point;
}

Domain::BedSegments RectangleBed::segments() const
{
    return m_segments;
}

CircleBed::CircleBed(const Domain::Point center, const double radius) :
    m_center{center},
    m_radius{radius}
{}

BoundingBox2crd CircleBed::bounding_box() const
{
    auto r = static_cast<Domain::coord_t>(std::round(m_radius));
    Vec2crd R{r, r};

    return {m_center - R, m_center + R};
}

ExPolygons CircleBed::ifp_convex(const Polygon& convexpoly) const
{
    constexpr int nedges{24};
    const Polygon circle{approximate_circle_with_polygon(m_center, m_radius, nedges)};
    return {ExPolygon{ifp_convex_convex(circle, convexpoly)}};
}

double CircleBed::area() const
{
    return m_radius * m_radius * std::numbers::pi;
}

IrregularBed::IrregularBed(const Domain::ExPolygons& poly) : m_poly(poly) {}

BoundingBox2crd IrregularBed::bounding_box() const
{
    return get_extents(m_poly);
}

ExPolygons IrregularBed::ifp_convex(const Polygon& convexpoly) const
{
    auto bb = Algorithms::ExPolygon::get_extents(m_poly);
    bb      = Algorithms::BoundingBox::inflated(bb, scaled(1.));

    Polygon rect = to_rectangle(bb);

    ExPolygons blueprint = Algorithms::ClipperUtils::diff_ex(rect, m_poly);
    Polygons ifp;
    for (const ExPolygon& part : blueprint) {
        Polygons triangles = convex_decomposition_tess(part);
        for (const Polygon& tr : triangles) {
            Polygon subifp = nfp_convex_convex_legacy(tr, convexpoly);
            ifp.emplace_back(std::move(subifp));
        }
    }

    ifp = Algorithms::ClipperUtils::union_(ifp);

    Polygons ret;

    std::copy_if(ifp.begin(), ifp.end(), std::back_inserter(ret), [](const Polygon& p) {
        return Algorithms::Polygon::is_clockwise(p);
    });

    for (Polygon& p : ret)
        std::reverse(p.begin(), p.end());

    return Algorithms::Polygon::to_expolygons(ret);
}

double IrregularBed::area() const
{
    return std::accumulate(m_poly.begin(), m_poly.end(), 0., [](double s, auto& p) {
        return s + p.area();
    });
}

} // namespace Slic3r::Biz::Arrange
