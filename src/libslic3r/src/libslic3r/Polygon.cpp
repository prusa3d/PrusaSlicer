#include <ankerl/unordered_dense.h>
#include <cmath>
#include <limits>
#include <cinttypes>
#include <cstring>

#include "Slic3r/Biz/Algorithms/DouglasPeucker.hpp"
#include "Slic3r/Biz/Algorithms/Line.hpp"
#include "Slic3r/Biz/Algorithms/Polygon.hpp"
#include "Slic3r/Exception.hpp"
#include "Polygon.hpp"
#include "Polyline.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/MultiPoint.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/libslic3r.h"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"

using namespace Slic3r::Biz;

namespace Slic3r {

namespace BB = Biz::Algorithms::BoundingBox;

// Projection of a point onto the polygon.
Point point_projection(const Polygon &polygon, const Point &point)
{
    Point proj = point;
    double dmin = std::numeric_limits<double>::max();
    if (! polygon.points.empty()) {
        for (size_t i = 0; i < polygon.points.size(); ++ i) {
            const Point &pt0 = polygon.points[i];
            const Point &pt1 = polygon.points[(i + 1 == polygon.points.size()) ? 0 : i + 1];
            double d = (point - pt0).cast<double>().norm();
            if (d < dmin) {
                dmin = d;
                proj = pt0;
            }
            d = (point - pt1).cast<double>().norm();
            if (d < dmin) {
                dmin = d;
                proj = pt1;
            }
            Vec2d v1(double(pt1(0) - pt0(0)), double(pt1(1) - pt0(1)));
            double div = v1.squaredNorm();
            if (div > 0.) {
                Vec2d v2(double(point(0) - pt0(0)), double(point(1) - pt0(1)));
                double t = v1.dot(v2) / div;
                if (t > 0. && t < 1.) {
                    Point foot(coord_t(floor(double(pt0(0)) + t * v1(0) + 0.5)), coord_t(floor(double(pt0(1)) + t * v1(1) + 0.5)));
                    d = (point - foot).cast<double>().norm();
                    if (d < dmin) {
                        dmin = d;
                        proj = foot;
                    }
                }
            }
        }
    }
    return proj;
}

BoundingBox get_extents(const Polygon& poly)
{
    const auto bb{Algorithms::Polygon::get_extents(poly)};
    BoundingBox result{bb.min, bb.max};
    result.defined = bb.defined;
    return result;
}

BoundingBox get_extents(const Polygons &polygons)
{
    const auto bb{Algorithms::Polygon::get_extents(polygons)};
    BoundingBox result{bb.min, bb.max};
    result.defined = bb.defined;
    return result;
}

BoundingBox get_extents_rotated(const Polygon &poly, double angle) 
{ 
    return get_extents_rotated(poly.points, angle);
}

extern std::vector<BoundingBox> get_extents_vector(const Polygons &polygons)
{
    std::vector<BoundingBox> out;
    out.reserve(polygons.size());
    for (Polygons::const_iterator it = polygons.begin(); it != polygons.end(); ++ it)
        out.push_back(get_extents(*it));
    return out;
}

// Polygon must be valid (at least three points), collinear points and duplicate points removed.
bool polygon_is_convex(const Points &poly)
{
    if (poly.size() < 3)
        return false;

    Point p0 = poly[poly.size() - 2];
    Point p1 = poly[poly.size() - 1];
    for (size_t i = 0; i < poly.size(); ++ i) {
        Point p2 = poly[i];
        auto det = cross2((p1 - p0).cast<int64_t>(), (p2 - p1).cast<int64_t>());
        if (det < 0)
            return false;
        p0 = p1;
        p1 = p2;
    }
    return true;
}

bool has_duplicate_points(const Polygons &polys)
{
#if 1
    // Check globally.
#if 0
    // Detect duplicates by sorting with quicksort. It is quite fast, but ankerl::unordered_dense is around 1/4 faster.
    Points allpts;
    allpts.reserve(count_points(polys));
    for (const Polygon &poly : polys)
        allpts.insert(allpts.end(), poly.points.begin(), poly.points.end());
    return has_duplicate_points(std::move(allpts));
#else
    // Detect duplicates by inserting into an ankerl::unordered_dense hash set, which is is around 1/4 faster than qsort.
    struct PointHash {
        uint64_t operator()(const Point &p) const noexcept {
            uint64_t h;
            static_assert(sizeof(h) == sizeof(p));
            memcpy(&h, &p, sizeof(p));
            return ankerl::unordered_dense::detail::wyhash::hash(h);
        }
    };
    ankerl::unordered_dense::set<Point, PointHash> allpts;
    allpts.reserve(Algorithms::Polygon::count_points(polys));
    for (const Polygon &poly : polys)
        for (const Point &pt : poly.points)
        if (! allpts.insert(pt).second)
            // Duplicate point was discovered.
            return true;
    return false;
#endif
#else
    // Check per contour.
    for (const Polygon &poly : polys)
        if (has_duplicate_points(poly))
            return true;
    return false;
#endif
}

static inline bool is_stick(const Point &p1, const Point &p2, const Point &p3)
{
    Point v1 = p2 - p1;
    Point v2 = p3 - p2;
    int64_t dir = int64_t(v1(0)) * int64_t(v2(0)) + int64_t(v1(1)) * int64_t(v2(1));
    if (dir > 0)
        // p3 does not turn back to p1. Do not remove p2.
        return false;
    double l2_1 = double(v1(0)) * double(v1(0)) + double(v1(1)) * double(v1(1));
    double l2_2 = double(v2(0)) * double(v2(0)) + double(v2(1)) * double(v2(1));
    if (dir == 0)
        // p1, p2, p3 may make a perpendicular corner, or there is a zero edge length.
        // Remove p2 if it is coincident with p1 or p2.
        return l2_1 == 0 || l2_2 == 0;
    // p3 turns back to p1 after p2. Are p1, p2, p3 collinear?
    // Calculate distance from p3 to a segment (p1, p2) or from p1 to a segment(p2, p3),
    // whichever segment is longer
    double cross = double(v1(0)) * double(v2(1)) - double(v2(0)) * double(v1(1));
    double dist2 = cross * cross / std::max(l2_1, l2_2);
    return dist2 < EPSILON * EPSILON;
}

bool remove_sticks(Polygon &poly)
{
    bool modified = false;
    size_t j = 1;
    for (size_t i = 1; i + 1 < poly.points.size(); ++ i) {
        if (! is_stick(poly[j-1], poly[i], poly[i+1])) {
            // Keep the point.
            if (j < i)
                poly.points[j] = poly.points[i];
            ++ j;
        }
    }
    if (++ j < poly.points.size()) {
        poly.points[j-1] = poly.points.back();
        poly.points.erase(poly.points.begin() + j, poly.points.end());
        modified = true;
    }
    while (poly.points.size() >= 3 && is_stick(poly.points[poly.points.size()-2], poly.points.back(), poly.points.front())) {
        poly.points.pop_back();
        modified = true;
    }
    while (poly.points.size() >= 3 && is_stick(poly.points.back(), poly.points.front(), poly.points[1]))
        poly.points.erase(poly.points.begin());
    return modified;
}

bool remove_sticks(Polygons &polys)
{
    bool modified = false;
    size_t j = 0;
    for (size_t i = 0; i < polys.size(); ++ i) {
        modified |= remove_sticks(polys[i]);
        if (polys[i].points.size() >= 3) {
            if (j < i) 
                std::swap(polys[i].points, polys[j].points);
            ++ j;
        }
    }
    if (j < polys.size())
        polys.erase(polys.begin() + j, polys.end());
    return modified;
}

void remove_collinear(Polygon &poly)
{
    if (poly.points.size() > 2) {
        // copy points and append both 1 and last point in place to cover the boundaries
        Points pp;
        pp.reserve(poly.points.size()+2);
        pp.push_back(poly.points.back());
        pp.insert(pp.begin()+1, poly.points.begin(), poly.points.end());
        pp.push_back(poly.points.front());
        // delete old points vector. Will be re-filled in the loop
        poly.points.clear();

        size_t i = 0;
        size_t k = 0;
        while (i < pp.size()-2) {
            k = i+1;
            const Point &p1 = pp[i];
            while (k < pp.size()-1) {
                const Point &p2 = pp[k];
                const Point &p3 = pp[k+1];
                if(Algorithms::Line::distance_to(Line(p1, p3), p2) < SCALED_EPSILON) {
                    k++;
                } else {
                    if(i > 0) poly.points.push_back(p1); // implicitly removes the first point we appended above
                    i = k;
                    break;
                }
            }
            if(k > pp.size()-2) break; // all remaining points are collinear and can be skipped
        }
        poly.points.push_back(pp[i]);
    }
}

static inline void simplify_polygon_impl(const Points &points, double tolerance, bool strictly_simple, Polygons &out)
{
    Points simplified = Algorithms::DouglasPeucker::douglas_peucker(points, tolerance);
    // then remove the last (repeated) point.
    simplified.pop_back();
    // Simplify the decimated contour by ClipperLib.
    bool ccw = ClipperLib::Area(simplified) > 0.;
    for (Points& path : ClipperLib::SimplifyPolygons(ClipperUtils::SinglePathProvider(simplified), ClipperLib::pftNonZero, strictly_simple)) {
        if (!ccw)
            // ClipperLib likely reoriented negative area contours to become positive. Reverse holes back to CW.
            std::reverse(path.begin(), path.end());
        out.emplace_back(std::move(path));
    }
}

Polygons polygons_simplify(Polygons &&source_polygons, double tolerance, bool strictly_simple /* = true */)
{
    Polygons out;
    out.reserve(source_polygons.size());
    for (Polygon &source_polygon : source_polygons) {
        // Run Douglas / Peucker simplification algorithm on an open polyline (by repeating the first point at the end of the polyline),
        source_polygon.points.emplace_back(source_polygon.points.front());
        simplify_polygon_impl(source_polygon.points, tolerance, strictly_simple, out);
    }
    return out;
}

Polygons polygons_simplify(const Polygons &source_polygons, double tolerance, bool strictly_simple /* = true */)
{
    Polygons out;
    out.reserve(source_polygons.size());
    for (const Polygon &source_polygon : source_polygons) {
        // Run Douglas / Peucker simplification algorithm on an open polyline (by repeating the first point at the end of the polyline),
        simplify_polygon_impl(Algorithms::Polygon::to_polyline(source_polygon).points, tolerance, strictly_simple, out);
    }
    return out;
}

// Do polygons match? If they match, they must have the same topology,
// however their contours may be rotated.
bool polygons_match(const Polygon &l, const Polygon &r)
{
    if (l.size() != r.size())
        return false;
    auto it_l = std::find(l.points.begin(), l.points.end(), r.points.front());
    if (it_l == l.points.end())
        return false;
    auto it_r = r.points.begin();
    for (; it_l != l.points.end(); ++ it_l, ++ it_r)
        if (*it_l != *it_r)
            return false;
    it_l = l.points.begin();
    for (; it_r != r.points.end(); ++ it_l, ++ it_r)
        if (*it_l != *it_r)
            return false;
    return true;
}

Polygon make_circle(double radius, double error)
{
    double angle = 2. * acos(1. - error / radius);
    size_t num_segments = size_t(ceil(2. * M_PI / angle));
    return make_circle_num_segments(radius, num_segments);
}

Polygon make_circle_num_segments(double radius, size_t num_segments)
{
    Polygon out;
    out.points.reserve(num_segments);
    double angle_inc = 2.0 * M_PI / num_segments;
    for (size_t i = 0; i < num_segments; ++ i) {
        const double angle = angle_inc * i;
        out.points.emplace_back(coord_t(cos(angle) * radius), coord_t(sin(angle) * radius));
    }
    return out;
}

BoundingBox ColorPolygon::bounding_box() const
{
    return BB::construct(this->points);
}

} // namespace Slic3r

namespace Slic3r::Biz::Algorithms::Polygon {

// TODO: Temporary proxy method that will be removed after migration to BoundingBox2crd.
Slic3r::BoundingBox get_bounding_box(const Slic3r::Polygon& poly)
{
    const Domain::BoundingBox2crd bbox = BoundingBox::construct(poly.points);
    return Slic3r::BoundingBox{bbox.min, bbox.max};
}

} // namespace Slic3r::Biz::Algorithms::Polygon
