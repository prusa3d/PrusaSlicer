///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/

#include <cstdlib>

#include <arrange/Beds.hpp>

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Point.hpp"

namespace Slic3r { namespace arr2 {

BoundingBox bounding_box(const InfiniteBed &bed)
{
    BoundingBox ret;
    using C = coord_t;

    // It is important for Mx and My to be strictly less than half of the
    // range of type C. width(), height() and area() will not overflow this way.
    C Mx = C((std::numeric_limits<C>::lowest() + 2 * bed.center.x()) / 4.01);
    C My = C((std::numeric_limits<C>::lowest() + 2 * bed.center.y()) / 4.01);

    ret.max = bed.center - Point{Mx, My};
    ret.min = bed.center + Point{Mx, My};

    return ret;
}

Polygon to_rectangle(const BoundingBox &bb)
{
    Polygon ret;
    ret.points = {
        bb.min,
        Point{bb.max.x(), bb.min.y()},
        bb.max,
        Point{bb.min.x(), bb.max.y()}
    };

    return ret;
}

Polygon approximate_circle_with_polygon(const arr2::CircleBed &bed, int nedges)
{
    Polygon ret;

    double angle_incr = (2 * M_PI) / nedges; // Angle increment for each edge
    double angle = 0; // Starting angle

    // Loop to generate vertices for each edge
    for (int i = 0; i < nedges; i++) {
        // Calculate coordinates of the vertices using trigonometry
        auto x = bed.center().x() + static_cast<coord_t>(bed.radius() * std::cos(angle));
        auto y = bed.center().y() + static_cast<coord_t>(bed.radius() * std::sin(angle));

        // Add vertex to the vector
        ret.points.emplace_back(x, y);

        // Update the angle for the next iteration
        angle += angle_incr;
    }

    return ret;
}

inline coord_t width(const BoundingBox &box)
{
    return box.max.x() - box.min.x();
}
inline coord_t height(const BoundingBox &box)
{
    return box.max.y() - box.min.y();
}
inline double poly_area(const Points &pts)
{
    return std::abs(Polygon::area(pts));
}
inline double distance_to(const Point &p1, const Point &p2)
{
    double dx = p2.x() - p1.x();
    double dy = p2.y() - p1.y();
    return std::sqrt(dx * dx + dy * dy);
}

static CircleBed to_circle(const Point &center, const Points &points, const Vec2crd &gap)
{
    std::vector<double> vertex_distances;
    double              avg_dist = 0;

    for (const Point &pt : points) {
        double distance = distance_to(center, pt);
        vertex_distances.push_back(distance);
        avg_dist += distance;
    }

    avg_dist /= vertex_distances.size();

    CircleBed ret(center, avg_dist, gap);
    for (auto el : vertex_distances) {
        if (std::abs(el - avg_dist) > 10 * SCALED_EPSILON) {
            ret = {};
            break;
        }
    }

    return ret;
}

template<class Fn> auto call_with_bed(const Points &bed, const Vec2crd &gap, Fn &&fn)
{
    if (bed.empty())
        return fn(InfiniteBed{});
    else if (bed.size() == 1)
        return fn(InfiniteBed{bed.front()});
    else {
        auto      bb    = BoundingBox(bed);
        CircleBed circ  = to_circle(bb.center(), bed, gap);
        auto      parea = poly_area(bed);

        if ((1.0 - parea / area(bb)) < 1e-3) {
            return fn(RectangleBed{bb, gap});
        } else if (!std::isnan(circ.radius()) && (1.0 - parea / area(circ)) < 1e-2)
            return fn(circ);
        else
            return fn(IrregularBed{{ExPolygon(bed)}, gap});
    }
}

ArrangeBed to_arrange_bed(const Points &bedpts, const Vec2crd &gap)
{
    ArrangeBed ret;

    call_with_bed(bedpts, gap, [&](const auto &bed) { ret = bed; });

    return ret;
}

}} // namespace Slic3r::arr2
