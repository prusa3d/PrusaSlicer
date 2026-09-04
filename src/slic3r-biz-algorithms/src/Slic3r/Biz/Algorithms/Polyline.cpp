#include "Slic3r/Biz/Algorithms/Polyline.hpp"

#include "Slic3r/Domain/BoundingBox.hpp"
#include "Slic3r/Domain/Point.hpp"
#include "Slic3r/Domain/Line.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/Algorithms/DouglasPeucker.hpp"
#include "Slic3r/Biz/Algorithms/Line.hpp"
#include "Slic3r/Biz/Algorithms/MultiPoint.hpp"
#include "Slic3r/Biz/Algorithms/Point.hpp"

namespace Slic3r::Biz::Algorithms::Polyline {

using namespace Slic3r::Domain;
using namespace Slic3r::Biz::Algorithms;

void reverse(Domain::Polyline& polyline)
{
    std::reverse(polyline.points.begin(), polyline.points.end());
}

Domain::Polyline reversed(const Domain::Polyline& polyline)
{
    Domain::Polyline polyline_out = polyline;
    reverse(polyline_out);
    return polyline_out;
}

int find_point(const Domain::Polyline& polyline, const Domain::Point& query_pt, const double scaled_epsilon)
{
    return MultiPoint::find_point(polyline, query_pt, scaled_epsilon);
}

bool has_consecutive_duplicate_points(const Domain::Polyline& polyline)
{
    return Point::has_consecutive_duplicate_points(polyline.points);
}

bool remove_consecutive_duplicate_points(Domain::Polyline& polyline)
{
    return Point::remove_consecutive_duplicate_points(polyline.points);
}

bool remove_consecutive_duplicate_points(Domain::Polylines& polylines)
{
    if (polylines.empty())
        return false;

    bool modified = false;
    for (Domain::Polyline& polyline : polylines) {
        modified |= remove_consecutive_duplicate_points(polyline);
    }

    // Remove empty or invalid polylines.
    std::erase_if(polylines, [](const Domain::Polyline& p) { return p.points.size() < 2; });
    return modified;
}

bool remove_degenerate(Domain::Polylines& polylines)
{
    return std::erase_if(polylines, [](const Domain::Polyline& p) { return p.points.size() < 2; }) > 0;
}

void clip_end(Domain::Polyline& polyline, double distance)
{
    while (distance > 0) {
        const Vec2d last_point = polyline.last_point().cast<double>();

        polyline.points.pop_back();
        if (polyline.points.empty())
            break;

        const Vec2d v = polyline.last_point().cast<double>() - last_point;
        const double lsqr = v.squaredNorm();
        if (lsqr > distance * distance) {
            polyline.points.emplace_back((last_point + v * (distance / sqrt(lsqr))).cast<coord_t>());
            return;
        }

        distance -= std::sqrt(lsqr);
    }
}

void clip_start(Domain::Polyline& polyline, double distance)
{
    Polyline::reverse(polyline);
    Polyline::clip_end(polyline, distance);

    if (polyline.points.size() >= 2) {
        Polyline::reverse(polyline);
    }
}

void extend_end(Domain::Polyline& polyline, double distance)
{
    const Vec2d v = (polyline.points.back() - *(polyline.points.end() - 2)).cast<double>().normalized();
    polyline.points.back() += (v * distance).cast<coord_t>();
}

void extend_start(Domain::Polyline& polyline, double distance)
{
    const Vec2d v = (polyline.points.front() - polyline.points[1]).cast<double>().normalized();
    polyline.points.front() += (v * distance).cast<coord_t>();
}

Domain::Polyline scaled(const std::vector<Domain::Vec2d>& points)
{
    return Domain::Polyline(Point::scaled(points));
}

Domain::BoundingBox2crd get_bounding_box(const Domain::Polyline& polyline)
{
    return BoundingBox::construct(polyline.points);
}

Domain::BoundingBox2crd get_bounding_box(const Domain::Polylines& polylines)
{
    Domain::BoundingBox2crd bounding_box;
    for (const Domain::Polyline& polyline : polylines) {
        bounding_box = BoundingBox::merge(bounding_box, get_bounding_box(polyline));
    }

    return bounding_box;
}

Domain::Lines to_lines(const Domain::Polyline& polyline)
{
    if (polyline.points.size() < 2)
        return {};

    Lines lines;
    lines.reserve(polyline.points.size() - 1);
    for (Points::const_iterator it = polyline.points.begin(); it != polyline.points.end() - 1; ++it) {
        lines.emplace_back(*it, *(it + 1));
    }

    return lines;
}

Domain::Lines to_lines(const Domain::Polylines& polylines)
{
    const size_t lines_cnt = total_lines_count(polylines);

    Lines lines;
    lines.reserve(lines_cnt);
    for (const Domain::Polyline& polyline : polylines) {
        for (Points::const_iterator it = polyline.points.begin(); it != polyline.points.end() - 1; ++it) {
            lines.emplace_back(*it, *(it + 1));
        }
    }

    return lines;
}

double total_length(const Domain::Polylines& polylines)
{
    double total_length = 0;
    for (const Domain::Polyline& pl : polylines) {
        total_length += pl.length();
    }

    return total_length;
}

size_t total_lines_count(const Domain::Polylines& polylines)
{
    size_t lines_cnt = 0;
    for (const Domain::Polyline& polyline : polylines) {
        if (polyline.points.size() > 1) {
            lines_cnt += polyline.points.size() - 1;
        }
    }

    return lines_cnt;
}

bool is_straight(const Domain::Polyline& polyline)
{
    // Check that each segment's direction is equal to the line connecting
    // first point and last point. (Checking each line against the previous
    // one would cause the error to accumulate.)
    double dir = Domain::Line(polyline.first_point(), polyline.last_point()).direction();
    for (const Domain::Line& line : to_lines(polyline)) {
        if (!line.is_parallel_to(dir))
            return false;
    }

    return true;
}

void simplify(Domain::Polyline& polyline, const double tolerance)
{
    polyline.points = Algorithms::DouglasPeucker::douglas_peucker(polyline.points, tolerance);
}

Domain::Polyline simplified(const Domain::Polyline& polyline, const double tolerance)
{
    Domain::Polyline simplified_polyline = polyline;
    Polyline::simplify(simplified_polyline, tolerance);
    return simplified_polyline;
}

std::pair<Domain::Polyline, Domain::Polyline> split_at_point(const Domain::Polyline& polyline, const Domain::Point& split_point)
{
    if (polyline.size() < 2)
        return {polyline, Domain::Polyline{}};

    if (polyline.points.front() == split_point)
        return {Domain::Polyline{split_point}, polyline};

    auto          min_dist2    = std::numeric_limits<double>::max();
    auto          min_point_it = polyline.points.cbegin();
    Domain::Point prev         = polyline.points.front();
    for (auto it = polyline.points.cbegin() + 1; it != polyline.points.cend(); ++it) {
        Domain::Point proj;
        if (double d2 = Algorithms::Line::distance_to_squared(Domain::Line(prev, *it), split_point, proj);
            d2 < min_dist2) {
            min_dist2    = d2;
            min_point_it = it;
        }

        prev = *it;
    }

    Domain::Polyline first_part;
    first_part.points.assign(polyline.points.cbegin(), min_point_it);
    if (first_part.points.back() != split_point) {
        first_part.points.emplace_back(split_point);
    }

    Domain::Polyline second_part;
    second_part.points = {split_point};
    if (*min_point_it == split_point) {
        ++min_point_it;
    }

    second_part.points.insert(second_part.points.end(), min_point_it, polyline.points.cend());

    return {std::move(first_part), std::move(second_part)};
}

double length(const Domain::Points& polyline_pts)
{
    return length(polyline_pts.cbegin(), polyline_pts.cend());
}

double length(const Points::const_iterator polyline_pts_begin, const Points::const_iterator polyline_pts_end)
{
    if (polyline_pts_begin == polyline_pts_end)
        return 0.;

    double total_length = 0.;
    for (auto curr_it = std::next(polyline_pts_begin); curr_it != polyline_pts_end; ++curr_it) {
        auto prev_it = std::prev(curr_it);
        total_length += (*curr_it - *prev_it).cast<double>().norm();
    }

    return total_length;
}

Domain::Polygons to_polygons(const Domain::Polylines& polylines)
{
    Domain::Polygons out;
    out.reserve(polylines.size());
    for (const Domain::Polyline& polyline : polylines) {
        if (polyline.empty())
            continue;

        out.emplace_back(polyline.points);
    }

    return out;
}

} // namespace Slic3r::Biz::Algorithms::Polyline
