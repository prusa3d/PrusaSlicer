// The following code for merging circles into arches originates from https://github.com/FormerLurker/ArcWelderLib

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Arc Welder: Anti-Stutter Library
//
// Compresses many G0/G1 commands into G2/G3(arc) commands where possible, ensuring the tool paths stay within the specified resolution.
// This reduces file size and the number of gcodes per second.
//
// Uses the 'Gcode Processor Library' for gcode parsing, position processing, logging, and other various functionality.
//
// Copyright(C) 2021 - Brad Hochgesang
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// This program is free software : you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as published
// by the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
// GNU Affero General Public License for more details.
//
//
// You can contact the author at the following email address: 
// FormerLurker@pm.me
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "ArcWelder.hpp"

#include <boost/container/small_vector.hpp>
#include <array>
#include <cstdint>
#include <iterator>

#include "Circle.hpp"
#include "../MultiPoint.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/Point.hpp"

namespace Slic3r { namespace Geometry { namespace ArcWelder {

Points arc_discretize(const Point &p1, const Point &p2, const double radius, const bool ccw, const double deviation)
{
    Vec2d  center = arc_center(p1.cast<double>(), p2.cast<double>(), radius, ccw);
    double angle  = arc_angle(p1.cast<double>(), p2.cast<double>(), radius);
    assert(angle > 0);

    double r           = std::abs(radius);
    size_t num_steps   = arc_discretization_steps(r, angle, deviation);
    double angle_step  = angle / num_steps;

    Points out;
    out.reserve(num_steps + 1);
    out.emplace_back(p1);
    if (! ccw)
        angle_step *= -1.;
    for (size_t i = 1; i < num_steps; ++ i)
        out.emplace_back(p1.rotated(angle_step * i, center.cast<coord_t>()));
    out.emplace_back(p2);
    return out;
}

struct Circle
{
    Point  center;
    double radius;
};

// Interpolate three points with a circle.
// Returns false if the three points are collinear or if the radius is bigger than maximum allowed radius.
static std::optional<Circle> try_create_circle(const Point &p1, const Point &p2, const Point &p3, const double max_radius)
{
    if (auto center = Slic3r::Geometry::try_circle_center(p1.cast<double>(), p2.cast<double>(), p3.cast<double>(), SCALED_EPSILON); center) {
        Point c = center->cast<coord_t>();
        if (double r = sqrt(double((c - p1).cast<int64_t>().squaredNorm())); r <= max_radius)
            return std::make_optional<Circle>({ c, float(r) });
    }
    return {};
}

// Returns a closest point on the segment.
// Returns false if the closest point is not inside the segment, but at its boundary.
static bool foot_pt_on_segment(const Point &p1, const Point &p2, const Point &pt, Point &out)
{
    Vec2i64 v21 = (p2 - p1).cast<int64_t>();
    int64_t l2  = v21.squaredNorm();
    if (l2 > int64_t(SCALED_EPSILON)) {
        if (int64_t t = (pt - p1).cast<int64_t>().dot(v21);
            t >= int64_t(SCALED_EPSILON) && t < l2 - int64_t(SCALED_EPSILON)) {
            out = p1 + ((double(t) / double(l2)) * v21.cast<double>()).cast<coord_t>();
            return true;
        }
    }
    // The segment is short or the closest point is an end point.
    return false;
}

static inline bool circle_approximation_sufficient(const Circle &circle, const Points::const_iterator begin, const Points::const_iterator end, const double tolerance)
{
    // The circle was calculated from the 1st and last point of the point sequence, thus the fitting of those points does not need to be evaluated.
    assert(end - begin >= 3);

    // Test the 1st point.
    if (double distance_from_center = (*begin - circle.center).cast<double>().norm();
        std::abs(distance_from_center - circle.radius) > tolerance)
        return false;

    for (auto it = std::next(begin); it != end; ++ it) {
        if (double distance_from_center = (*it - circle.center).cast<double>().norm();
            std::abs(distance_from_center - circle.radius) > tolerance)
            return false;
        Point closest_point;
        if (foot_pt_on_segment(*std::prev(it), *it, circle.center, closest_point)) {
            if (double distance_from_center = (closest_point - circle.center).cast<double>().norm();
                std::abs(distance_from_center - circle.radius) > tolerance)
                return false;
        }
    }
    return true;
}

#if 0
static inline bool get_deviation_sum_squared(const Circle &circle, const Points::const_iterator begin, const Points::const_iterator end, const double tolerance, double &total_deviation)
{
    // The circle was calculated from the 1st and last point of the point sequence, thus the fitting of those points does not need to be evaluated.
    assert(std::abs((*begin - circle.center).cast<double>().norm() - circle.radius) < SCALED_EPSILON);
    assert(std::abs((*std::prev(end) - circle.center).cast<double>().norm() - circle.radius) < SCALED_EPSILON);
    assert(end - begin >= 3);

    total_deviation = 0;

    const double tolerance2 = sqr(tolerance);
    for (auto it = std::next(begin); std::next(it) != end; ++ it)
        if (double deviation2 = sqr((*it - circle.center).cast<double>().norm() - circle.radius); deviation2 > tolerance2)
            return false;
        else
            total_deviation += deviation2;

    for (auto it = begin; std::next(it) != end; ++ it) {
        Point closest_point;
        if (foot_pt_on_segment(*it, *std::next(it), circle.center, closest_point)) {
            if (double deviation2 = sqr((closest_point - circle.center).cast<double>().norm() - circle.radius); deviation2 > tolerance2)
                return false;
            else
                total_deviation += deviation2;
        }
    }

    return true;
}
#endif

double arc_fit_variance(const Point &start_pos, const Point &end_pos, const float radius, bool is_ccw,
    const Points::const_iterator begin, const Points::const_iterator end)
{
    const Vec2d  center = arc_center(start_pos.cast<double>(), end_pos.cast<double>(), double(radius), is_ccw);
    const double r      = std::abs(radius);

    // The circle was calculated from the 1st and last point of the point sequence, thus the fitting of those points does not need to be evaluated.
    assert(std::abs((begin->cast<double>() - center).norm() - r) < SCALED_EPSILON);
    assert(std::abs((std::prev(end)->cast<double>() - center).norm() - r) < SCALED_EPSILON);
    assert(end - begin >= 3);

    double total_deviation = 0;
    size_t cnt = 0;
    for (auto it = begin; std::next(it) != end; ++ it) {
        if (it != begin) {
            total_deviation += sqr((it->cast<double>() - center).norm() - r);
            ++ cnt;
        }
        Point closest_point;
        if (foot_pt_on_segment(*it, *std::next(it), center.cast<coord_t>(), closest_point)) {
            total_deviation += sqr((closest_point.cast<double>() - center).cast<double>().norm() - r);
            ++ cnt;
        }
    }

    return total_deviation / double(cnt);
}

double arc_fit_max_deviation(const Point &start_pos, const Point &end_pos, const float radius, bool is_ccw,
    const Points::const_iterator begin, const Points::const_iterator end)
{
    const Vec2d  center = arc_center(start_pos.cast<double>(), end_pos.cast<double>(), double(radius), is_ccw);
    const double r      = std::abs(radius);

    // The circle was calculated from the 1st and last point of the point sequence, thus the fitting of those points does not need to be evaluated.
    assert(std::abs((begin->cast<double>() - center).norm() - r) < SCALED_EPSILON);
    assert(std::abs((std::prev(end)->cast<double>() - center).norm() - r) < SCALED_EPSILON);
    assert(end - begin >= 3);

    double max_deviation        = 0;
    double max_signed_deviation = 0;
    for (auto it = begin; std::next(it) != end; ++ it) {
        if (it != begin) {
            double signed_deviation = (it->cast<double>() - center).norm() - r;
            double deviation = std::abs(signed_deviation);
            if (deviation > max_deviation) {
                max_deviation = deviation;
                max_signed_deviation = signed_deviation;
            }
        }
        Point closest_point;
        if (foot_pt_on_segment(*it, *std::next(it), center.cast<coord_t>(), closest_point)) {
            double signed_deviation = (closest_point.cast<double>() - center).cast<double>().norm() - r;
            double deviation = std::abs(signed_deviation);
            if (deviation > max_deviation) {
                max_deviation = deviation;
                max_signed_deviation = signed_deviation;
            }
        }
    }
    return max_signed_deviation;
}

static inline int sign(const int64_t i)
{
    return i > 0 ? 1 : i < 0 ? -1 : 0;
}

static std::optional<Circle> try_create_circle(const Points::const_iterator begin, const Points::const_iterator end, const double max_radius, const double tolerance)
{
    std::optional<Circle> out;
    size_t size = end - begin;
    if (size == 3) {
        // Fit the circle throuh the three input points.
        out = try_create_circle(*begin, *std::next(begin), *std::prev(end), max_radius);
        if (out) {
            // Fit the center point and the two center points of the two edges with non-linear least squares.
            std::array<Vec2d, 3> fpts;
            Vec2d center_point = out->center.cast<double>();
            Vec2d first_point  = begin->cast<double>();
            Vec2d mid_point    = std::next(begin)->cast<double>();
            Vec2d last_point   = std::prev(end)->cast<double>();
            fpts[0] = 0.5 * (first_point + mid_point);
            fpts[1] = mid_point;
            fpts[2] = 0.5 * (mid_point + last_point);
            const double radius = (first_point - center_point).norm();
            if (std::abs((fpts[0] - center_point).norm() - radius) < 2. * tolerance &&
                std::abs((fpts[2] - center_point).norm() - radius) < 2. * tolerance) {
                if (std::optional<Vec2d> opt_center = ArcWelder::arc_fit_center_gauss_newton_ls(first_point, last_point,
                        center_point, fpts.begin(), fpts.end(), 3);
                    opt_center) {
                    out->center = opt_center->cast<coord_t>();
                    out->radius = (out->radius > 0 ? 1.f : -1.f) * (*opt_center - first_point).norm();
                }
                if (! circle_approximation_sufficient(*out, begin, end, tolerance))
                    out.reset();
            } else
                out.reset();
        }
    } else {        
        std::optional<Circle> circle;
        {
            // Try to fit a circle to first, middle and last point.
            auto mid = begin + (end - begin) / 2;    
            circle = try_create_circle(*begin, *mid, *std::prev(end), max_radius);
            if (// Use twice the tolerance for fitting the initial circle.
                // Early exit if such approximation is grossly inaccurate, thus the tolerance could not be achieved.
                circle && ! circle_approximation_sufficient(*circle, begin, end, tolerance * 2))
                circle.reset();
        } 
        if (! circle) {
            // Find an intersection point of the polyline to be fitted with the bisector of the arc chord.
            // At such a point the distance of a polyline to an arc wrt. the circle center (or circle radius) will have a largest gradient
            // of all points on the polyline to be fitted.
            Vec2i64 first_point = begin->cast<int64_t>();
            Vec2i64 last_point  = std::prev(end)->cast<int64_t>();
            Vec2i64 v = last_point - first_point;
            Vec2d   vd = v.cast<double>();
            double  ld = v.squaredNorm();
            if (ld > sqr(scaled<double>(0.0015))) {
                Vec2i64 c = (first_point.cast<int64_t>() + last_point.cast<int64_t>()) / 2;
                Vec2i64 prev_point = first_point;
                int     prev_side = sign(v.dot(prev_point - c));
                assert(prev_side != 0);
                Point   point_on_bisector;
    #ifndef NDEBUG
                point_on_bisector = { std::numeric_limits<coord_t>::max(), std::numeric_limits<coord_t>::max() };
    #endif // NDEBUG
                for (auto it = std::next(begin); it != end; ++ it) {
                    Vec2i64 this_point = it->cast<int64_t>();
                    int64_t d         = v.dot(this_point - c);
                    int     this_side = sign(d);
                    int     sideness  = this_side * prev_side;
                    if (sideness < 0) {
                        // Calculate the intersection point.
                        Vec2d p = c.cast<double>() + vd * double(d) / ld;
                        point_on_bisector = p.cast<coord_t>();
                        break;
                    } 
                    if (sideness == 0) {
                        // this_point is on the bisector.
                        assert(prev_side != 0);
                        assert(this_side == 0);
                        point_on_bisector = this_point.cast<coord_t>();
                        break;
                    }
                    prev_point = this_point;
                    prev_side  = this_side;
                }
                // point_on_bisector must be set
                assert(point_on_bisector.x() != std::numeric_limits<coord_t>::max() && point_on_bisector.y() != std::numeric_limits<coord_t>::max());
                circle = try_create_circle(*begin, point_on_bisector, *std::prev(end), max_radius);
                if (// Use twice the tolerance for fitting the initial circle.
                    // Early exit if such approximation is grossly inaccurate, thus the tolerance could not be achieved.
                    circle && ! circle_approximation_sufficient(*circle, begin, end, tolerance * 2))
                    circle.reset();
            }
        }
        if (circle) {
            // Fit the arc between the end points by least squares.
            // Optimize over all points along the path and the centers of the segments.
            boost::container::small_vector<Vec2d, 16> fpts;
            Vec2d first_point = begin->cast<double>();
            Vec2d last_point  = std::prev(end)->cast<double>();
            Vec2d prev_point  = first_point;            
            for (auto it = std::next(begin); it != std::prev(end); ++ it) {
                Vec2d this_point = it->cast<double>();
                fpts.emplace_back(0.5 * (prev_point + this_point));
                fpts.emplace_back(this_point);
                prev_point = this_point;
            }
            fpts.emplace_back(0.5 * (prev_point + last_point));
            std::optional<Vec2d> opt_center = ArcWelder::arc_fit_center_gauss_newton_ls(first_point, last_point,
                circle->center.cast<double>(), fpts.begin(), fpts.end(), 5);
            if (opt_center) {
                // Fitted radius must not be excessively large. If so, it is better to fit with a line segment.
                if (const double r2 = (*opt_center - first_point).squaredNorm(); r2 < max_radius * max_radius) {
                    circle->center = opt_center->cast<coord_t>();
                    circle->radius = (circle->radius > 0 ? 1.f : -1.f) * sqrt(r2);
                    if (circle_approximation_sufficient(*circle, begin, end, tolerance)) {
                        out = circle;
                    } else {
                        //FIXME One may consider adjusting the arc to fit the worst offender as a last effort,
                        // however Vojtech is not sure whether it is worth it.
                    }
                }
            }
        }
/*
        // From the original arc welder.
        // Such a loop makes the time complexity of the arc fitting an ugly O(n^3).
        else {
            // Find the circle with the least deviation, if one exists.
            double least_deviation = std::numeric_limits<double>::max();
            double current_deviation;
            for (auto it = std::next(begin); std::next(it) != end; ++ it)
                if (std::optional<Circle> circle = try_create_circle(*begin, *it, *std::prev(end), max_radius); 
                    circle && get_deviation_sum_squared(*circle, begin, end, tolerance, current_deviation)) {
                    if (current_deviation < least_deviation) {
                        out = circle;
                        least_deviation = current_deviation;
                    }
                }
        }
*/
    }
    return out;
}

// ported from ArcWelderLib/ArcWelder/segmented/shape.h class "arc"
class Arc {
public:
    Point  start_point;
    Point  end_point;
    Point  center;
    double radius;
    Orientation direction { Orientation::Unknown };
};

// Return orientation of a polyline with regard to the center.
// Successive points are expected to take less than a PI angle step.
Orientation arc_orientation(
    const Point                 &center,
    const Points::const_iterator begin,
    const Points::const_iterator end)
{
    assert(end - begin >= 3);
    // Assumption: Two successive points of a single segment span an angle smaller than PI.
    Vec2i64 vstart  = (*begin - center).cast<int64_t>();
    Vec2i64 vprev   = vstart;
    int     arc_dir = 0;
    for (auto it = std::next(begin); it != end; ++ it) {
        Vec2i64 v = (*it - center).cast<int64_t>();
        int     dir = sign(cross2(vprev, v));
        if (dir == 0) {
            // Ignore radial segments.
        } else if (arc_dir * dir < 0) {
            // The path turns back and overextrudes. Such path is likely invalid, but the arc interpolation should
            // rather maintain such an invalid path instead of covering it up.
            // Don't replace such a path with an arc.
            return {};
        } else {
            // Success, either establishing the direction for the first time, or moving in the same direction as the last time.
            arc_dir = dir;
            vprev = v;
        }
    }
    return arc_dir == 0 ? 
        // All points are radial wrt. the center, this is unexpected.
        Orientation::Unknown :
        // Arc is valid, either CCW or CW.
        arc_dir > 0 ? Orientation::CCW : Orientation::CW;
}

static inline std::optional<Arc> try_create_arc_impl(
    const Circle                &circle,
    const Points::const_iterator begin,
    const Points::const_iterator end,
    const double                 tolerance,
    const double                 path_tolerance_percent)
{
    assert(end - begin >= 3);
    // Assumption: Two successive points of a single segment span an angle smaller than PI.
    Orientation orientation = arc_orientation(circle.center, begin, end);
    if (orientation == Orientation::Unknown)
        return {};

    Vec2i64 vstart = (*begin - circle.center).cast<int64_t>();
    Vec2i64 vend   = (*std::prev(end) - circle.center).cast<int64_t>();
    double  angle  = atan2(double(cross2(vstart, vend)), double(vstart.dot(vend)));
    if (orientation == Orientation::CW)
        angle *= -1.;
    if (angle < 0)
        angle += 2. * M_PI;
    assert(angle >= 0. && angle < 2. * M_PI + EPSILON);

    // Check the length against the original length.
    // This can trigger simply due to the differing path lengths
    // but also could indicate that the vector calculation above
    // got wrong direction
    const double arc_length                     = circle.radius * angle;
    const double approximate_length             = length(begin, end);
    assert(approximate_length > 0);
    const double arc_length_difference_relative = (arc_length - approximate_length) / approximate_length;

    if (std::fabs(arc_length_difference_relative) >= path_tolerance_percent) {
        return {};
    } else {
        assert(circle_approximation_sufficient(circle, begin, end, tolerance + SCALED_EPSILON));
        return std::make_optional<Arc>(Arc{
            *begin,
            *std::prev(end),
            circle.center,
            angle > M_PI ? - circle.radius : circle.radius,
            orientation
        });
    }
}

static inline std::optional<Arc> try_create_arc(
    const Points::const_iterator begin,
    const Points::const_iterator end,
    double                       max_radius             = default_scaled_max_radius,
    double                       tolerance              = default_scaled_resolution,
    double                       path_tolerance_percent = default_arc_length_percent_tolerance)
{
    std::optional<Circle> circle = try_create_circle(begin, end, max_radius, tolerance);
    if (! circle)
        return {};
    return try_create_arc_impl(*circle, begin, end, tolerance, path_tolerance_percent);
}

float arc_angle(const Vec2f &start_pos, const Vec2f &end_pos, Vec2f &center_pos, bool is_ccw)
{
    if ((end_pos - start_pos).squaredNorm() < sqr(1e-6)) {
        // If start equals end, full circle is considered.
        return float(2. * M_PI);
    } else {
        Vec2f v1 = start_pos - center_pos;
        Vec2f v2 = end_pos   - center_pos;
        if (! is_ccw)
            std::swap(v1, v2);
        float radian = atan2(cross2(v1, v2), v1.dot(v2));
        return radian < 0 ? float(2. * M_PI) + radian : radian;
    }
}

float arc_length(const Vec2f &start_pos, const Vec2f &end_pos, Vec2f &center_pos, bool is_ccw)
{
    return (center_pos - start_pos).norm() * arc_angle(start_pos, end_pos, center_pos, is_ccw);
}

// Reduces polyline in the <begin, end) range in place,
// returns the new end iterator.
static inline Segments::iterator douglas_peucker_in_place(Segments::iterator begin, Segments::iterator end, const double tolerance)
{
    return douglas_peucker<int64_t>(begin, end, begin, tolerance, [](const Segment &s) { return s.point; });
}

Path fit_path(const Points &src_in, double tolerance, double fit_circle_percent_tolerance)
{
    assert(tolerance >= 0);
    assert(fit_circle_percent_tolerance >= 0);
    double tolerance2 = Slic3r::sqr(tolerance);

    Path out;
    out.reserve(src_in.size());
    if (tolerance <= 0 || src_in.size() <= 2) {
        // No simplification, just convert.
        std::transform(src_in.begin(), src_in.end(), std::back_inserter(out), [](const Point &p) -> Segment { return { p }; });
    } else if (double tolerance_fine = std::max(0.03 * tolerance, scaled<double>(0.000060)); 
        fit_circle_percent_tolerance <= 0 || tolerance_fine > 0.5 * tolerance) {
        // Convert and simplify to a polyline.
        std::transform(src_in.begin(), src_in.end(), std::back_inserter(out), [](const Point &p) -> Segment { return { p }; });
        out.erase(douglas_peucker_in_place(out.begin(), out.end(), tolerance), out.end());
    } else {
        // Simplify the polyline first using a fine threshold.
        Points src = douglas_peucker(src_in, tolerance_fine);
        // Perform simplification & fitting.
        // Index of the start of a last polyline, which has not yet been decimated.
        int begin_pl_idx = 0;
        out.push_back({ src.front(), 0.f });
        for (auto it = std::next(src.begin()); it != src.end();) {
            // Minimum 2 additional points required for circle fitting.
            auto begin = std::prev(it);
            auto end   = std::next(it);
            assert(end <= src.end());
            std::optional<Arc> arc;
            while (end != src.end()) {
                auto next_end = std::next(end);
                if (std::optional<Arc> this_arc = try_create_arc(
                                                        begin, next_end,
                                                        ArcWelder::default_scaled_max_radius,
                                                        tolerance, fit_circle_percent_tolerance);
                    this_arc) {
                    // Could extend the arc by one point.
                    assert(this_arc->direction != Orientation::Unknown);
                    arc = this_arc;
                    end = next_end;
                    if (end == src.end())
                        // No way to extend the arc.
                        goto fit_end;
                    // Now try to expand the arc by adding points one by one. That should be cheaper than a full arc fit test.
                    while (std::next(end) != src.end()) {
                        assert(end == next_end);
                        {
                            Vec2i64 v1 = arc->start_point.cast<int64_t>() - arc->center.cast<int64_t>();
                            Vec2i64 v2 = arc->end_point.cast<int64_t>() - arc->center.cast<int64_t>();
                            do {
                                if (std::abs((arc->center.cast<double>() - next_end->cast<double>()).norm() - arc->radius) >= tolerance ||
                                    inside_arc_wedge_vectors(v1, v2,
                                        arc->radius > 0, arc->direction == Orientation::CCW,
                                        next_end->cast<int64_t>() - arc->center.cast<int64_t>()))
                                    // Cannot extend the current arc with this new point.
                                    break;
                            } while (++ next_end != src.end());
                        }
                        if (next_end == end)
                            // No additional point could be added to a current arc.
                            break;
                        // Try to fit a new arc to the extended set of points.
                        // last_tested_failed set to invalid value, no test failed yet.
                        auto last_tested_failed = src.begin();
                        for (;;) {
                            this_arc = try_create_arc(
                                begin, next_end,
                                ArcWelder::default_scaled_max_radius,
                                tolerance, fit_circle_percent_tolerance);
                            if (this_arc) {
                                arc = this_arc;
                                end = next_end;
                                if (last_tested_failed == src.begin()) {
                                    // First run of the loop, the arc was extended fully.
                                    if (end == src.end())
                                        goto fit_end;
                                    // Otherwise try to extend the arc with another sample.
                                    break;
                                }
                            } else
                                last_tested_failed = next_end;
                            // Take half of the interval up to the failed point.
                            next_end = end + (last_tested_failed - end) / 2;
                            if (next_end == end)
                                // Backed to the last successfull sample.
                                goto fit_end;
                            // Otherwise try to extend the arc up to next_end in another iteration.
                        }
                    }
                } else {
                    // The last arc was the best we could get.
                    break;
                }
            }
        fit_end:
#if 1
            if (arc) {
                // Check whether the arc end points are not too close with the risk of quantizing the arc ends to the same point on G-code export.
                if ((arc->end_point - arc->start_point).cast<double>().squaredNorm() < 2. * sqr(scaled<double>(0.0011))) {
                    // Arc is too short. Skip it, decimate a polyline instead.
                    arc.reset();
                } else {
                    // Test whether the arc is so flat, that it could be replaced with a straight segment.
                    Line line(arc->start_point, arc->end_point);
                    bool arc_valid = false;
                    for (auto it2 = std::next(begin); it2 != std::prev(end); ++ it2)
                        if (line_alg::distance_to_squared(line, *it2) > tolerance2) {
                            // Polyline could not be fitted by a line segment, thus the arc is considered valid.
                            arc_valid = true;
                            break;
                        }
                    if (! arc_valid)
                        // Arc should be fitted by a line segment. Skip it, decimate a polyline instead.
                        arc.reset();
                }
            }
#endif
            if (arc) {
                // printf("Arc radius: %lf, length: %lf\n", unscaled<double>(arc->radius), arc_length(arc->start_point.cast<double>(), arc->end_point.cast<double>(), arc->radius));
                // If there is a trailing polyline, decimate it first before saving a new arc.
                if (out.size() - begin_pl_idx > 2) {
                    // Decimating linear segmens only.
                    assert(std::all_of(out.begin() + begin_pl_idx + 1, out.end(), [](const Segment &seg) { return seg.linear(); }));
                    out.erase(douglas_peucker_in_place(out.begin() + begin_pl_idx, out.end(), tolerance), out.end());
                    assert(out.back().linear());
                }
#ifndef NDEBUG
                // Check for a very short linear segment, that connects two arches. Such segment should not be created.
                if (out.size() - begin_pl_idx > 1) {
                    const Point& p1 = out[begin_pl_idx].point;
                    const Point& p2 = out.back().point;
                    assert((p2 - p1).cast<double>().squaredNorm() > sqr(scaled<double>(0.0011)));
                }
#endif
                // Save the index of an end of the new circle segment, which may become the first point of a possible future polyline.
                begin_pl_idx = int(out.size());
                // This will be the next point to try to add.
                it = end;
                // Add the new arc.
                assert(*begin == arc->start_point);
                assert(*std::prev(it) == arc->end_point);
                assert(out.back().point == arc->start_point);
                out.push_back({ arc->end_point, float(arc->radius), arc->direction });
#if 0
                // Verify that all the source points are at tolerance distance from the interpolated path.
                {
                    const Segment &seg_start = *std::prev(std::prev(out.end()));
                    const Segment &seg_end   = out.back();
                    const Vec2d    center    = arc_center(seg_start.point.cast<double>(), seg_end.point.cast<double>(), double(seg_end.radius), seg_end.ccw());
                    assert(seg_start.point == *begin);
                    assert(seg_end.point == *std::prev(end));
                    assert(arc_orientation(center.cast<coord_t>(), begin, end) == arc->direction);
                    for (auto it = std::next(begin); it != end; ++ it) {
                        Point  ptstart = *std::prev(it);
                        Point  ptend   = *it;
                        Point  closest_point;
                        if (foot_pt_on_segment(ptstart, ptend, center.cast<coord_t>(), closest_point)) {
                            double distance_from_center = (closest_point.cast<double>() - center).norm();
                            assert(std::abs(distance_from_center - std::abs(seg_end.radius)) < tolerance + SCALED_EPSILON);
                        }
                        Vec2d  v     = (ptend - ptstart).cast<double>();
                        double len   = v.norm();
                        auto num_segments = std::min<size_t>(10, ceil(2. * len / fit_circle_percent_tolerance));
                        for (size_t i = 0; i < num_segments; ++ i) {
                            Point p = ptstart + (v * (double(i) / double(num_segments))).cast<coord_t>();
                            assert(i == 0 || inside_arc_wedge(seg_start.point.cast<double>(), seg_end.point.cast<double>(), center, seg_end.radius > 0, seg_end.ccw(), p.cast<double>()));
                            double d2 = sqr((p.cast<double>() - center).norm() - std::abs(seg_end.radius));
                            assert(d2 < sqr(tolerance + SCALED_EPSILON));
                        }
                    }
                }
#endif
            } else {
                // Arc is not valid, append a linear segment.
                out.push_back({ *it ++ });
            }
        }
        if (out.size() - begin_pl_idx > 2)
            // Do the final polyline decimation.
            out.erase(douglas_peucker_in_place(out.begin() + begin_pl_idx, out.end(), tolerance), out.end());
    }

#if 0
    // Verify that all the source points are at tolerance distance from the interpolated path.
    for (auto it = std::next(src_in.begin()); it != src_in.end(); ++ it) {
        Point  start = *std::prev(it);
        Point  end   = *it;
        Vec2d  v     = (end - start).cast<double>();
        double len   = v.norm();
        auto num_segments = std::min<size_t>(10, ceil(2. * len / fit_circle_percent_tolerance));
        for (size_t i = 0; i <= num_segments; ++ i) {
            Point p = start + (v * (double(i) / double(num_segments))).cast<coord_t>();
            PathSegmentProjection proj = point_to_path_projection(out, p);
            assert(proj.valid());
            assert(proj.distance2 < sqr(tolerance + SCALED_EPSILON));
        }
    }
#endif

    return out;
}

void reverse(Path &path)
{
    if (path.size() > 1) {
        auto prev = path.begin();
        for (auto it = std::next(prev); it != path.end(); ++ it) {
            prev->radius      = it->radius;
            prev->orientation = it->orientation == Orientation::CCW ? Orientation::CW : Orientation::CCW;
            prev = it;
        }
        path.back().radius = 0;
        std::reverse(path.begin(), path.end());
    }
}

double clip_start(Path &path, const double len)
{
    reverse(path);
    double remaining = clip_end(path, len);
    reverse(path);
    // Return remaining distance to go.
    return remaining;
}

double clip_end(Path &path, double distance)
{
    while (distance > 0) {
        const Segment last = path.back();
        path.pop_back();
        if (path.empty())
            break;
        if (last.linear()) {
            // Linear segment
            Vec2d  v    = (path.back().point - last.point).cast<double>();
            double lsqr = v.squaredNorm();
            if (lsqr > sqr(distance)) {
                path.push_back({ last.point + (v * (distance / sqrt(lsqr))).cast<coord_t>() });
                // Length to go is zero.
                return 0;
            }
            distance -= sqrt(lsqr);
        } else {
            // Circular segment
            double angle = arc_angle(path.back().point.cast<double>(), last.point.cast<double>(), last.radius);
            double len   = std::abs(last.radius) * angle;
            if (len > distance) {
                // Rotate the segment end point in reverse towards the start point.
                if (last.ccw())
                    angle *= -1.;

                const double rotate_by_angle = angle * (distance / len);

                // When we are clipping the arc with a negative radius (we are taking the longer angle here),
                // we have to check if we still need to take the longer angle after clipping.
                // Otherwise, we must flip the radius sign to take the shorter angle.
                const bool flip_radius_sign = last.radius < 0 && std::abs(angle) > M_PI && std::abs(angle - rotate_by_angle) <= M_PI;

                path.push_back({last.point.rotated(rotate_by_angle, arc_center(path.back().point.cast<double>(), last.point.cast<double>(), double(last.radius), last.ccw()).cast<coord_t>()),
                                (flip_radius_sign ? -last.radius : last.radius), last.orientation});

                // Length to go is zero.
                return 0;
            }
            distance -= len;
        }
    }

    // Return remaining distance to go.
    assert(distance >= 0);
    return distance;
}

PathSegmentProjection point_to_path_projection(const Path &path, const Point &point, double search_radius2)
{
    assert(path.size() != 1);
    // initialized to "invalid" state.
    PathSegmentProjection out;
    out.distance2 = search_radius2;
    if (path.size() < 2 || path.front().point == point) {
        // First point is the closest point.
        if (path.empty()) {
            // No closest point available.
        } else if (const Point p0 = path.front().point; p0 == point) {
            out.segment_id = 0;
            out.point      = p0;
            out.distance2  = 0;
        } else if (double d2 = (p0 - point).cast<double>().squaredNorm(); d2 < out.distance2) {
            out.segment_id = 0;
            out.point      = p0;
            out.distance2  = d2;
        }
    } else {
        assert(path.size() >= 2);
        // min_point_it will contain an end point of a segment with a closest projection found
        // or path.cbegin() if no such closest projection closer than search_radius2 was found.
        auto  min_point_it = path.cbegin();
        Point prev         = path.front().point;
        for (auto it = std::next(path.cbegin()); it != path.cend(); ++ it) {
            if (it->linear()) {
                // Linear segment
                Point proj;
                // distance_to_squared() will possibly return the start or end point of a line segment.
                if (double d2 = line_alg::distance_to_squared(Line(prev, it->point), point, &proj); d2 < out.distance2) {
                    out.point     = proj;
                    out.distance2 = d2;
                    min_point_it  = it;
                }
            } else {
                // Circular arc
                Vec2i64 center = arc_center(prev.cast<double>(), it->point.cast<double>(), double(it->radius), it->ccw()).cast<int64_t>();
                // Test whether point is inside the wedge.
                Vec2i64 v1 = prev.cast<int64_t>() - center;
                Vec2i64 v2 = it->point.cast<int64_t>() - center;
                Vec2i64 vp = point.cast<int64_t>() - center;
                if (inside_arc_wedge_vectors(v1, v2, it->radius > 0, it->ccw(), vp)) {
                    // Distance of the radii.
                    const auto r = double(std::abs(it->radius));
                    const auto rtest = sqrt(double(vp.squaredNorm()));
                    if (double d2 = sqr(rtest - r); d2 < out.distance2) {
                        if (rtest > SCALED_EPSILON)
                            // Project vp to the arc.
                            out.point = center.cast<coord_t>() + (vp.cast<double>() * (r / rtest)).cast<coord_t>();
                        else
                            // Test point is very close to the center of the radius. Any point of the arc is the closest.
                            // Pick the start.
                            out.point = prev;
                        out.distance2 = d2;
                        out.center = center.cast<coord_t>();
                        min_point_it  = it;
                    }
                } else {
                    // Distance to the start point.
                    if (double d2 = double((v1 - vp).squaredNorm()); d2 < out.distance2) {
                        out.point     = prev;
                        out.distance2 = d2;
                        min_point_it  = it;
                    }
                }
            }
            prev = it->point;
        }
        if (! path.back().linear()) {
            // Calculate distance to the end point.
            if (double d2 = (path.back().point - point).cast<double>().squaredNorm(); d2 < out.distance2) {
                out.point     = path.back().point;
                out.distance2 = d2;
                min_point_it  = std::prev(path.end());
            }
        }
        // If a new closes point was found, it is closer than search_radius2.
        assert((min_point_it == path.cbegin()) == (out.distance2 == search_radius2));
        // Output is not valid yet.
        assert(! out.valid());
        if (min_point_it != path.cbegin()) {
            // Make it valid by setting the segment.
            out.segment_id = std::prev(min_point_it) - path.begin();
            assert(out.valid());
        }
    }

    assert(! out.valid() || (out.segment_id >= 0 && out.segment_id < path.size()));
    return out;
}

std::pair<Path, Path> split_at(const Path &path, const PathSegmentProjection &proj, const double min_segment_length)
{
    assert(proj.valid());
    assert(! proj.valid() || (proj.segment_id >= 0 && proj.segment_id < path.size()));
    assert(path.size() > 1);
    std::pair<Path, Path> out;
    if (! proj.valid() || proj.segment_id + 1 == path.size() || (proj.segment_id + 2 == path.size() && proj.point == path.back().point))
        out.first = path;
    else if (proj.segment_id == 0 && proj.point == path.front().point)
        out.second = path;
    else {
        // Path will likely be split to two pieces.
        assert(proj.valid() && proj.segment_id >= 0 && proj.segment_id + 1 < path.size());
        const Segment &start = path[proj.segment_id];
        const Segment &end   = path[proj.segment_id + 1];
        bool           split_segment = true;
        int            split_segment_id = proj.segment_id;
        if (int64_t d2 = (proj.point - start.point).cast<int64_t>().squaredNorm(); d2 < sqr(min_segment_length)) {
            split_segment = false;
            int64_t d22 = (proj.point - end.point).cast<int64_t>().squaredNorm();
            if (d22 < d2)
                // Split at the end of the segment.
                ++ split_segment_id;
        } else if (int64_t d2 = (proj.point - end.point).cast<int64_t>().squaredNorm(); d2 < sqr(min_segment_length)) {
            ++ split_segment_id;
            split_segment = false;
        }
        if (split_segment) {
            out.first.assign(path.begin(), path.begin() + split_segment_id + 2);
            out.second.assign(path.begin() + split_segment_id, path.end());
            assert(out.first[out.first.size() - 2] == start);
            assert(out.first.back() == end);
            assert(out.second.front() == start);
            assert(out.second[1] == end);
            assert(out.first.size() + out.second.size() == path.size() + 2);
            assert(out.first.back().radius == out.second[1].radius);
            out.first.back().point = proj.point;
            out.second.front().point = proj.point;
            if (end.radius < 0) {
                // A large arc (> PI) was split.
                // At least one of the two arches that were created by splitting the original arch will become smaller.
                // Make the radii of those arches that became < PI positive.
                // In case of a projection onto an arc, proj.center should be filled in and valid.
                auto vstart = (start.point - proj.center).cast<int64_t>();
                auto vend   = (end.point - proj.center).cast<int64_t>();
                auto vproj  = (proj.point - proj.center).cast<int64_t>();
                if ((cross2(vstart, vproj) > 0) == end.ccw())
                    // Make the radius of a minor arc positive.
                    out.first.back().radius *= -1.f;
                if ((cross2(vproj, vend) > 0) == end.ccw())
                    // Make the radius of a minor arc positive.
                    out.second[1].radius *= -1.f;
                assert(out.first.size() > 1);
                assert(out.second.size() > 1);
                out.second.front().radius = 0;
            }
        } else {
            assert(split_segment_id >= 0 && split_segment_id < path.size());
            if (split_segment_id + 1 == int(path.size()))
                out.first = path;
            else if (split_segment_id == 0)
                out.second = path;
            else {
                // Split at the start of proj.segment_id.
                out.first.assign(path.begin(), path.begin() + split_segment_id + 1);
                out.second.assign(path.begin() + split_segment_id, path.end());
                assert(out.first.size() + out.second.size() == path.size() + 1);
                assert(out.first.back() == (split_segment_id == proj.segment_id ? start : end));
                assert(out.second.front() == (split_segment_id == proj.segment_id ? start : end));
                assert(out.first.size() > 1);
                assert(out.second.size() > 1);
                out.second.front().radius = 0;
            }
        }
    }

    return out;
}

std::pair<Path, Path> split_at(const Path &path, const Point &point, const double min_segment_length)
{
    return split_at(path, point_to_path_projection(path, point), min_segment_length);
}

} } } // namespace Slic3r::Geometry::ArcWelder
