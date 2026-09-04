#include "Slic3r/Biz/Algorithms/ExPolygon.hpp"

#include "Slic3r/Biz/Algorithms/DouglasPeucker.hpp"
#include "Slic3r/Biz/Algorithms/Polygon.hpp"
#include "Slic3r/Utils.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/Algorithms/Polygon.hpp"
#include "Slic3r/Biz/Algorithms/ClipperUtils.hpp"

#include <numeric>

using namespace Slic3r::Biz::Algorithms;
namespace bb = Slic3r::Biz::Algorithms::BoundingBox;
namespace poly = Slic3r::Biz::Algorithms::Polygon;

namespace Slic3r::Biz::Algorithms::ExPolygon {

bool is_valid(const Domain::ExPolygon& expolygon)
{
    if (!expolygon.contour.is_valid() || !Algorithms::Polygon::is_counter_clockwise(expolygon.contour))
        return false;

    for (const Domain::Polygon& hole : expolygon.holes) {
        if (!hole.is_valid() || Algorithms::Polygon::is_counter_clockwise(hole))
            return false;
    }

    return true;
}

size_t count_points(const Domain::ExPolygon& expolygon)
{
    size_t n_points = expolygon.contour.points.size();
    for (const Domain::Polygon& hole : expolygon.holes) {
        n_points += hole.points.size();
    }

    return n_points;
}

size_t count_points(const Domain::ExPolygons& expolygons)
{
    size_t n_points = 0;
    for (const Domain::ExPolygon& expolygon : expolygons) {
        n_points += expolygon.contour.points.size();
        for (const Domain::Polygon& hole : expolygon.holes) {
            n_points += hole.points.size();
        }
    }

    return n_points;
}

size_t count_polygons(const Domain::ExPolygons& expolygons)
{
    return std::accumulate(
        expolygons.begin(), expolygons.end(), size_t{0},
        [](const size_t sum, const Domain::ExPolygon& ex) { return sum + ex.holes.size() + 1; }
    );
}

void rotate(Domain::ExPolygons& expolygons, const double angle)
{
    for (Domain::ExPolygon& expolygon : expolygons) {
        expolygon.rotate(angle);
    }
}

void translate(Domain::ExPolygons& expolygons, const Domain::Point& vector)
{
    for (Domain::ExPolygon& expolygon : expolygons) {
        expolygon.translate(vector);
    }
}

bool contains(const Domain::ExPolygon& expolygon, const Domain::Point& point, const bool border_result)
{
    if (!Polygon::contains(expolygon.contour, point, border_result))
        return false; // Outside the outer contour, not on the contour boundary.

    for (const Domain::Polygon& hole : expolygon.holes) {
        if (Polygon::contains(hole, point, !border_result))
            return false; // Inside a hole, not on the hole boundary.
    }

    return true;
}

bool contains(const Domain::ExPolygons& expolygons, const Domain::Point& point, const bool border_result)
{
    for (const Domain::ExPolygon& expolygon : expolygons) {
        if (ExPolygon::contains(expolygon, point, border_result))
            return true;
    }

    return false;
}

bool contains(const Domain::ExPolygon& expolygon, const Domain::Line& line)
{
    return ExPolygon::contains(expolygon, Domain::Polyline(line.a, line.b));
}

using Slic3r::Biz::Algorithms::ClipperUtils::diff_pl;

bool contains(const Domain::ExPolygon& expolygon, const Domain::Polyline& polyline)
{
    return diff_pl(polyline, expolygon).empty();
}

bool contains(const Domain::ExPolygon& expolygon, const Domain::Polylines& polylines)
{
    return diff_pl(polylines, expolygon).empty();
}

bool remove_consecutive_duplicate_points(Domain::ExPolygons& expolygons)
{
    if (expolygons.empty())
        return false;

    bool modified_contour = false;
    bool modified_holes   = false;
    for (Domain::ExPolygon& expolygon : expolygons) {
        modified_contour |= Polygon::remove_consecutive_duplicate_points(expolygon.contour);
        modified_holes   |= Polygon::remove_consecutive_duplicate_points(expolygon.holes);
    }

    // Remove of ExPolygons without contour.
    if (modified_contour) {
        std::erase_if(expolygons, [](const Domain::ExPolygon& ex) {
            return ex.contour.points.size() < 3;
        });
    }

    return modified_contour || modified_holes;
}

bool remove_small_expolygons_and_holes(Domain::ExPolygons& expolygons, const double min_area)
{
    if (expolygons.empty())
        return false;

    bool modified = false;
    for (Domain::ExPolygon& expolygon : expolygons) {
        if (std::abs(expolygon.area()) >= min_area) {
            // ExPolygon is big enough, so also check all its holes.
            modified |= Polygon::remove_small(expolygon.holes, min_area);
        } else {
            expolygon.contour.clear();
            expolygon.holes.clear();
            modified = true;
        }
    }

    std::erase_if(expolygons, [](const Domain::ExPolygon& expoly) {
        return expoly.contour.size() < 3;
    });

    return modified;
}

bool overlaps(const Domain::ExPolygon& expolygon, const Domain::ExPolygon &other_expolygon)
{
    if (expolygon.empty() || other_expolygon.empty())
        return false;

    using Slic3r::Biz::Algorithms::ClipperUtils::intersection_pl;
    Domain::Polylines pl_out = intersection_pl(to_polylines(other_expolygon), expolygon);

    // See unit test SCENARIO("Clipper diff with polyline", "[Clipper]")
    // for in which case the intersection_pl produces any intersection.
    return ! pl_out.empty() ||
        // If *this is completely inside other, then pl_out is empty, but the expolygons overlap. Test for that situation.
        Algorithms::ExPolygon::contains(other_expolygon, expolygon.contour.points.front());
}

Domain::Lines to_lines(const Domain::ExPolygon& expolygon)
{
    Domain::Lines lines;
    lines.reserve(ExPolygon::count_points(expolygon));

    Slic3r::append(lines, Polygon::to_lines(expolygon.contour));
    for (const Domain::Polygon& hole : expolygon.holes) {
        Slic3r::append(lines, Polygon::to_lines(hole));
    }

    return lines;
}

Domain::Lines to_lines(const Domain::ExPolygons& expolygons)
{
    Domain::Lines lines;
    lines.reserve(ExPolygon::count_points(expolygons));

    for (const Domain::ExPolygon& expolygon : expolygons) {
        Slic3r::append(lines, Polygon::to_lines(expolygon.contour));
        for (const Domain::Polygon& hole : expolygon.holes) {
            Slic3r::append(lines, Polygon::to_lines(hole));
        }
    }

    return lines;
}

Domain::Line2ds to_linesf(const Domain::ExPolygons& src, uint32_t count_lines)
{
    assert(count_lines == 0 || count_lines == Algorithms::ExPolygon::count_points(src));
    if (count_lines == 0)
        count_lines = Algorithms::ExPolygon::count_points(src);
    Domain::Line2ds lines;
    lines.reserve(count_lines);
    Domain::Vec2d prev_pd;
    auto to_lines = [&lines, &prev_pd](const Domain::Points& pts) {
        assert(pts.size() >= 3);
        if (pts.size() < 2)
            return;
        bool is_first = true;
        for (const Domain::Point& p : pts) {
            Domain::Vec2d pd = p.cast<double>();
            if (is_first) {
                is_first = false;
            } else {
                lines.emplace_back(prev_pd, pd);
            }
            prev_pd = pd;
        }
        lines.emplace_back(prev_pd, pts.front().cast<double>());
    };
    for (const Domain::ExPolygon& expoly : src) {
        to_lines(expoly.contour.points);
        for (const Domain::Polygon& hole : expoly.holes)
            to_lines(hole.points);
    }
    assert(lines.size() == count_lines);
    return lines;
}

Domain::ExPolygons simplify(const Domain::ExPolygon& expolygon, const double tolerance)
{
    using Slic3r::Biz::Algorithms::ClipperUtils::union_ex;
    return union_ex(ExPolygon::simplify_to_polygons(expolygon, tolerance));
}

Domain::ExPolygons simplify(const Domain::ExPolygons& expolygons, const double tolerance)
{
    Domain::ExPolygons out;
    out.reserve(expolygons.size());
    for (const Domain::ExPolygon& expolygon : expolygons) {
        Slic3r::append(out, ExPolygon::simplify(expolygon, tolerance));
    }

    return out;
}

Domain::Polygons simplify_to_polygons(const Domain::ExPolygon& expolygon, const double tolerance)
{
    Domain::Polygons pp;
    pp.reserve(expolygon.holes.size() + 1);

    // Contour
    {
        Domain::Polygon p = expolygon.contour;
        p.points.push_back(p.points.front());
        p.points = Algorithms::DouglasPeucker::douglas_peucker(p.points, tolerance);
        p.points.pop_back();
        pp.emplace_back(std::move(p));
    }

    // Holes
    for (Domain::Polygon p : expolygon.holes) {
        p.points.push_back(p.points.front());
        p.points = Algorithms::DouglasPeucker::douglas_peucker(p.points, tolerance);
        p.points.pop_back();
        pp.emplace_back(std::move(p));
    }

    using Slic3r::Biz::Algorithms::ClipperUtils::simplify_polygons;
    return simplify_polygons(pp);
}

double area(const Domain::ExPolygon& expolygon) { return expolygon.area(); }

double area(const Domain::ExPolygons& expolygons)
{
    return std::accumulate(
        expolygons.begin(), expolygons.end(), 0.,
        [](const double sum, const Domain::ExPolygon& ex) { return sum + ex.area(); }
    );
}

Domain::Polygons to_polygons(const Domain::ExPolygon& expolygon)
{
    Domain::Polygons polygons;
    polygons.reserve(expolygon.holes.size() + 1);
    polygons.emplace_back(expolygon.contour);
    Slic3r::append(polygons, expolygon.holes);
    return polygons;
}

Domain::Polygons to_polygons(const Domain::ExPolygons& expolygons)
{
    Domain::Polygons polygons;
    polygons.reserve(ExPolygon::count_polygons(expolygons));
    for (const Domain::ExPolygon& expolygon : expolygons) {
        polygons.emplace_back(expolygon.contour);
        Slic3r::append(polygons, expolygon.holes);
    }

    return polygons;
}

Domain::Polygons to_polygons(Domain::ExPolygon&& expolygon)
{
    Domain::Polygons polygons;
    polygons.reserve(expolygon.holes.size() + 1);
    polygons.emplace_back(std::move(expolygon.contour));
    Slic3r::append(polygons, std::move(expolygon.holes));
    return polygons;
}

Domain::Polygons to_polygons(Domain::ExPolygons&& expolygons)
{
    Domain::Polygons polygons;
    polygons.reserve(count_polygons(expolygons));
    for (Domain::ExPolygon& expolygon : expolygons) {
        polygons.emplace_back(std::move(expolygon.contour));
        Slic3r::append(polygons, std::move(expolygon.holes));
    }

    return polygons;
}

Domain::BoundingBox2crd get_extents(const Domain::ExPolygon& expolygon)
{
    return poly::get_extents(expolygon.contour);
}

Domain::BoundingBox2crd get_extents(const Domain::ExPolygons& expolygons)
{
    Domain::BoundingBox2crd bbox;
    for (const Domain::ExPolygon& expolygon : expolygons) {
        if (!expolygon.contour.points.empty()) {
            bbox = bb::merge(bbox, get_extents(expolygon));
        }
    }

    return bbox;
}

Domain::Points to_points(const Domain::ExPolygon& expolygon)
{
    Domain::Points points;
    points.reserve(ExPolygon::count_points(expolygon));
    Slic3r::append(points, expolygon.contour.points);
    for (const Domain::Polygon& hole : expolygon.holes) {
        Slic3r::append(points, hole.points);
    }

    return points;
}

Domain::Points to_points(const Domain::ExPolygons& expolygons)
{
    Domain::Points points;
    points.reserve(ExPolygon::count_points(expolygons));
    for (const Domain::ExPolygon& expolygon : expolygons) {
        Slic3r::append(points, expolygon.contour.points);
        for (const Domain::Polygon& hole : expolygon.holes) {
            Slic3r::append(points, hole.points);
        }
    }

    return points;
}

Domain::Polylines to_polylines(const Domain::ExPolygon& expolygon)
{
    if (expolygon.empty())
        return {};

    Domain::Polylines polylines;
    polylines.reserve(expolygon.holes.size() + 1);

    Domain::Points contour_points = expolygon.contour.points;
    contour_points.emplace_back(contour_points.front());
    polylines.emplace_back(std::move(contour_points));

    for (const Domain::Polygon& hole : expolygon.holes) {
        if (hole.points.empty())
            continue;

        Domain::Points hole_points = hole.points;
        hole_points.emplace_back(hole_points.front());
        polylines.emplace_back(std::move(hole_points));
    }

    return polylines;
}

Domain::Polylines to_polylines(const Domain::ExPolygons& expolygons)
{
    Domain::Polylines polylines;
    polylines.reserve(ExPolygon::count_polygons(expolygons));

    for (const Domain::ExPolygon& expolygon : expolygons) {
        if (expolygon.empty())
            continue;

        Domain::Points contour_points = expolygon.contour.points;
        contour_points.emplace_back(contour_points.front());
        polylines.emplace_back(std::move(contour_points));

        for (const Domain::Polygon& hole : expolygon.holes) {
            if (hole.points.empty())
                continue;

            Domain::Points hole_points = hole.points;
            hole_points.emplace_back(hole_points.front());
            polylines.emplace_back(std::move(hole_points));
        }
    }

    return polylines;
}

Domain::Polylines to_polylines(Domain::ExPolygon&& expolygon)
{
    if (expolygon.empty())
        return {};

    Domain::Polylines polylines;
    polylines.reserve(expolygon.holes.size() + 1);

    Domain::Points contour_points = std::move(expolygon.contour.points);
    contour_points.emplace_back(contour_points.front());
    polylines.emplace_back(std::move(contour_points));

    for (Domain::Polygon& hole : expolygon.holes) {
        if (hole.points.empty())
            continue;

        Domain::Points hole_points = std::move(hole.points);
        hole_points.emplace_back(hole_points.front());
        polylines.emplace_back(std::move(hole_points));
    }

    return polylines;
}

Domain::Polylines to_polylines(Domain::ExPolygons&& expolygons)
{
    Domain::Polylines polylines;
    polylines.reserve(ExPolygon::count_polygons(expolygons));

    for (Domain::ExPolygon& expolygon : expolygons) {
        if (expolygon.empty())
            continue;

        Domain::Points contour_points = std::move(expolygon.contour.points);
        contour_points.emplace_back(contour_points.front());
        polylines.emplace_back(std::move(contour_points));

        for (Domain::Polygon& hole : expolygon.holes) {
            if (hole.points.empty())
                continue;

            Domain::Points hole_points = std::move(hole.points);
            hole_points.emplace_back(hole_points.front());
            polylines.emplace_back(std::move(hole_points));
        }
    }

    return polylines;
}

} // namespace Slic3r::Biz::Algorithms::ExPolygon
