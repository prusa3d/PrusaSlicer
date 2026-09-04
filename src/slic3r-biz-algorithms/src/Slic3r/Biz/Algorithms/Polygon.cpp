#include "Slic3r/Biz/Algorithms/Polygon.hpp"

#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/Algorithms/DouglasPeucker.hpp"
#include "Slic3r/Biz/Algorithms/ExPolygon.hpp"
#include "Slic3r/Biz/Algorithms/Point.hpp"
#include "Slic3r/Biz/Algorithms/ClipperUtils.hpp"
#include "Slic3r/Biz/Algorithms/MultiPoint.hpp"
#include "Slic3r/Biz/Algorithms/Line.hpp"
#include "Slic3r/Exception.hpp"
#include "Slic3r/Math.hpp"
#include "Slic3r/Utils.hpp"

#include <numeric>

using namespace Slic3r::Biz::Algorithms;

namespace Slic3r::Biz::Algorithms::Polygon {

size_t count_points(const Domain::Polygons& polygons)
{
    return std::accumulate(
        polygons.begin(), polygons.end(), size_t{0},
        [](const size_t sum, const Domain::Polygon& p) { return sum + p.size(); }
    );
}

void reverse(Domain::Polygons& polygons)
{
    for (Domain::Polygon& polygon : polygons) {
        polygon.reverse();
    }
}

void rotate(Domain::Polygons& polygons, const double angle)
{
    const double cos_angle = cos(angle);
    const double sin_angle = sin(angle);
    for (Domain::Polygon& polygon : polygons) {
        polygon.rotate(cos_angle, sin_angle);
    }
}

void append(Domain::Polygons& dst, const Domain::ExPolygon& expolygon)
{
    dst.reserve(dst.size() + expolygon.holes.size() + 1);
    dst.emplace_back(expolygon.contour);
    Slic3r::append(dst, expolygon.holes);
}

void append(Domain::Polygons& dst, const Domain::ExPolygons& expolygons)
{
    dst.reserve(dst.size() + ExPolygon::count_polygons(expolygons));
    for (const Domain::ExPolygon& expolygon : expolygons) {
        dst.emplace_back(expolygon.contour);
        Slic3r::append(dst, expolygon.holes);
    }
}

void append(Domain::Polygons& dst, Domain::ExPolygon&& expolygon)
{
    dst.reserve(dst.size() + expolygon.holes.size() + 1);
    dst.emplace_back(std::move(expolygon.contour));
    Slic3r::append(dst, std::move(expolygon.holes));
}

void append(Domain::Polygons& dst, Domain::ExPolygons&& expolygons)
{
    dst.reserve(dst.size() + Algorithms::ExPolygon::count_polygons(expolygons));
    for (Domain::ExPolygon& expolygon : expolygons) {
        dst.emplace_back(std::move(expolygon.contour));
        Slic3r::append(dst, std::move(expolygon.holes));
    }
}

bool has_consecutive_duplicate_points(const Domain::Polygon& polygon)
{
    return Point::has_consecutive_duplicate_points(polygon.points);
}

bool remove_consecutive_duplicate_points(Domain::Polygon& polygon, const bool check_first_and_last)
{
    return Point::remove_consecutive_duplicate_points(polygon.points, check_first_and_last);
}

bool remove_consecutive_duplicate_points(Domain::Polygons& polygons, const bool check_first_and_last)
{
    if (polygons.empty())
        return false;

    bool modified = false;
    for (Domain::Polygon& polygon : polygons) {
        modified |= remove_consecutive_duplicate_points(polygon, check_first_and_last);
    }

    // Remove empty or invalid polygons.
    std::erase_if(polygons, [](const Domain::Polygon& p) { return p.points.size() < 3; });
    return modified;
}

bool remove_degenerate(Domain::Polygons& polygons)
{
    return std::erase_if(polygons, [](const Domain::Polygon& p) { return p.points.size() < 3; }) > 0;
}

bool remove_small(Domain::Polygons& polygons, const double polygon_min_area)
{
    return std::erase_if(polygons, [=](const Domain::Polygon& p) { return std::abs(p.area()) < polygon_min_area; }) > 0;
}

int closest_point_index(const Domain::Polygon& polygon, const Domain::Point& point)
{
    return Slic3r::Biz::Algorithms::MultiPoint::closest_point_index(polygon, point);
}

Domain::Polygon scaled(const std::vector<Domain::Vec2d>& points)
{
    return Domain::Polygon(Point::scaled(points));
}

Domain::Polygon translated(const Domain::Polygon& polygon, const Domain::Point& offset)
{
    Domain::Polygon result(polygon);
    result.translate(offset);
    return result;
}

bool is_counter_clockwise(const Domain::Polygon& polygon)
{
    return ClipperLib::Orientation(polygon.points);
}

bool is_clockwise(const Domain::Polygon& polygon)
{
    return !Polygon::is_counter_clockwise(polygon);
}

bool is_convex(const Domain::Polygon& polygon)
{
    if (polygon.size() < 3)
        return false;

    Domain::Point p0 = polygon.points[polygon.points.size() - 2];
    Domain::Point p1 = polygon.points[polygon.points.size() - 1];
    for (const Domain::Point& p2 : polygon.points) {
        int64_t det = Slic3r::cross2((p1 - p0).cast<int64_t>(), (p2 - p1).cast<int64_t>());
        if (det < 0)
            return false;

        p0 = p1;
        p1 = p2;
    }

    return true;
}

bool make_counter_clockwise(Domain::Polygon& polygon)
{
    if (!Polygon::is_counter_clockwise(polygon)) {
        polygon.reverse();
        return true;
    }

    return false;
}

bool make_clockwise(Domain::Polygon& polygon)
{
    if (Polygon::is_counter_clockwise(polygon)) {
        polygon.reverse();
        return true;
    }
    return false;
}

Domain::Polyline split_at_vertex(const Domain::Polygon& polygon, const Domain::Point& point)
{
    // Find index of point.
    for (const Domain::Point& pt : polygon.points) {
        if (pt == point) {
            const size_t pt_idx = &pt - &polygon.points.front();
            return Polygon::split_at_index(polygon, pt_idx);
        }
    }

    throw Slic3r::InvalidArgument("Point not found");
    return {};
}

Domain::Polyline split_at_index(const Domain::Polygon& polygon, const size_t index)
{
    Domain::Polyline polyline;
    polyline.points.reserve(polygon.points.size() + 1);
    for (auto it = polygon.points.begin() + static_cast<int>(index); it != polygon.points.end(); ++it) {
        polyline.points.push_back(*it);
    }

    for (auto it = polygon.points.begin(); it != polygon.points.begin() + static_cast<int>(index) + 1; ++it) {
        polyline.points.push_back(*it);
    }

    return polyline;
}

Domain::Polyline split_at_first_point(const Domain::Polygon& polygon)
{
    return Polygon::split_at_index(polygon, 0);
}

std::optional<Domain::Point> intersection(const Domain::Polygon& polygon, const Domain::Line& line)
{
    if (polygon.points.size() < 2)
        return std::nullopt;

    Domain::Point intersection_pt;
    if (Algorithms::Line::intersection(Domain::Line(polygon.points.front(), polygon.points.back()), line, intersection_pt))
        return intersection_pt;

    for (size_t idx = 1; idx < polygon.points.size(); ++idx) {
        if (Algorithms::Line::intersection(Domain::Line(polygon.points[idx - 1], polygon.points[idx]), line, intersection_pt))
            return intersection_pt;
    }

    return std::nullopt;
}

Domain::Polygons simplify(const Domain::Polygon& polygon, double tolerance)
{
    // Works on CCW polygons only, CW contour will be reoriented to CCW by Clipper's simplify_polygons()!
    assert(Polygon::is_counter_clockwise(polygon));

    // Repeat the first point at the end in order to apply Douglas-Peucker on the whole polygon.
    Domain::Points points = polygon.points;
    points.push_back(points.front());
    Domain::Polygon p(Algorithms::DouglasPeucker::douglas_peucker(points, tolerance));
    p.points.pop_back();

    Domain::Polygons pp;
    pp.push_back(p);

    return Algorithms::ClipperUtils::simplify_polygons(pp);
}

bool contains(const Domain::Polygon& polygon, const Domain::Point& point, const bool border_result)
{
    if (const int poly_count_inside = ClipperLib::PointInPolygon(point, polygon.points); poly_count_inside == -1) {
        return border_result;
    } else {
        return (poly_count_inside % 2) == 1;
    }
}

bool contains(const Domain::Polygons& polygons, const Domain::Point& point, const bool border_result)
{
    int poly_count_inside = 0;
    for (const Domain::Polygon& polygon : polygons) {
        const int is_inside_this_poly = ClipperLib::PointInPolygon(point, polygon.points);
        if (is_inside_this_poly == -1)
            return border_result;

        poly_count_inside += is_inside_this_poly;
    }

    return (poly_count_inside % 2) == 1;
}

double total_length(const Domain::Polygons& polygons)
{
    return std::accumulate(
        polygons.begin(), polygons.end(), 0.,
        [](const double sum, const Domain::Polygon& p) { return sum + p.length(); }
    );
}

double area(const Domain::Points& polygon_pts) { return Domain::Polygon(polygon_pts).area(); }

double area(const Domain::Polygon& polygon) { return polygon.area(); }

double area(const Domain::Polygons& polygons)
{
    return std::accumulate(
        polygons.begin(), polygons.end(), 0.,
        [](const double sum, const Domain::Polygon& p) { return sum + p.area(); }
    );
}

Domain::Points to_points(const Domain::Polygon& polygon) { return polygon.points; }

Domain::Points to_points(const Domain::Polygons& polygons)
{
    Domain::Points points;
    points.reserve(Polygon::count_points(polygons));
    for (const Domain::Polygon& polygon : polygons) {
        Slic3r::append(points, polygon.points);
    }

    return points;
}

Domain::Lines to_lines(const Domain::Polygon& polygon)
{
    if (polygon.size() < 3)
        return {};

    Domain::Lines lines;
    lines.reserve(polygon.size());
    for (size_t idx = 1; idx < polygon.size(); ++idx) {
        lines.emplace_back(polygon.points[idx - 1], polygon.points[idx]);
    }

    lines.emplace_back(polygon.points.back(), polygon.points.front());

    return lines;
}

Domain::Lines to_lines(const Domain::Polygons& polygons)
{
    Domain::Lines lines;
    lines.reserve(Polygon::count_points(polygons));
    for (const Domain::Polygon& polygon : polygons) {
        for (size_t idx = 1; idx < polygon.size(); ++idx) {
            lines.emplace_back(polygon.points[idx - 1], polygon.points[idx]);
        }

        lines.emplace_back(polygon.points.back(), polygon.points.front());
    }

    return lines;
}

Domain::Polygons to_polygons(const Domain::VecOfPoints& paths)
{
    Domain::Polygons out;
    out.reserve(paths.size());
    for (const Domain::Points& path : paths)
        out.emplace_back(path);
    return out;
}

Domain::Polygons to_polygons(Domain::VecOfPoints&& paths)
{
    Domain::Polygons out;
    out.reserve(paths.size());
    for (Domain::Points& path : paths)
        out.emplace_back(std::move(path));
    return out;
}

Domain::BoundingBox2crd get_extents(const Domain::Polygon& polygon)
{
    return BoundingBox::construct(polygon.points);
}

Domain::BoundingBox2crd get_extents(const Domain::Polygons& polygons)
{
    Domain::BoundingBox2crd bounding_box;
    for (const Domain::Polygon& polygon : polygons) {
        bounding_box = BoundingBox::merge(bounding_box, Polygon::get_extents(polygon));
    }

    return bounding_box;
}

Domain::ExPolygons to_expolygons(const Domain::Polygons& polygons)
{
    Domain::ExPolygons expolygons;
    expolygons.reserve(polygons.size());
    for (const Domain::Polygon& polygon : polygons) {
        expolygons.emplace_back(polygon);
    }

    return expolygons;
}

Domain::ExPolygons to_expolygons(Domain::Polygons&& polygons)
{
    Domain::ExPolygons expolygons;
    expolygons.reserve(polygons.size());

    for (Domain::Polygon& polygon : polygons) {
        expolygons.emplace_back(std::move(polygon));
    }

    return expolygons;
}

Domain::Polyline to_polyline(const Domain::Polygon& polygon)
{
    Domain::Polyline out;
    out.points.reserve(polygon.size() + 1);
    out.points.assign(polygon.points.begin(), polygon.points.end());
    out.points.emplace_back(polygon.points.front());
    return out;
}

Domain::Polylines to_polylines(const Domain::Polygons& polygons)
{
    Domain::Polylines out;
    out.reserve(polygons.size());
    for (const Domain::Polygon& polygon : polygons) {
        out.emplace_back(to_polyline(polygon));
    }

    return out;
}

Domain::Polylines to_polylines(Domain::Polygons&& polygons)
{
    Domain::Polylines polylines;
    polylines.assign(polygons.size(), Domain::Polyline());
    size_t idx = 0;
    for (Domain::Polygon& polygon : polygons) {
        Domain::Polyline& pl = polylines[idx++];
        pl.points = std::move(polygon.points);
        pl.points.push_back(pl.points.front());
    }

    assert(idx == polylines.size());
    return polylines;
}

} // namespace Slic3r::Biz::Algorithms::Polygon
