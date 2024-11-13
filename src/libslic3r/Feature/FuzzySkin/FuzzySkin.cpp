#include <random>

#include "libslic3r/Algorithm/LineSegmentation/LineSegmentation.hpp"
#include "libslic3r/Arachne/utils/ExtrusionJunction.hpp"
#include "libslic3r/Arachne/utils/ExtrusionLine.hpp"
#include "libslic3r/PerimeterGenerator.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/PrintConfig.hpp"

#include "FuzzySkin.hpp"

using namespace Slic3r;

namespace Slic3r::Feature::FuzzySkin {

// Produces a random value between 0 and 1. Thread-safe.
static double random_value()
{
    thread_local std::random_device rd;
    // Hash thread ID for random number seed if no hardware rng seed is available
    thread_local std::mt19937                           gen(rd.entropy() > 0 ? rd() : std::hash<std::thread::id>()(std::this_thread::get_id()));
    thread_local std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(gen);
}

// Defines a <double> lerp function equal in functionality to the lerp function defined in c++20.
static double lerp(double a, double b, double t)
{
    return a + t * (b - a);
}

void fuzzy_polyline(Points &points, const bool closed, const double fuzzy_skin_thickness, const double fuzzy_skin_point_dist)
{
    /* Fuzzification configuration setup */
    Points out;

    const double line_unit_length = 2./3. * fuzzy_skin_point_dist; // The unit lengths of line segments, equal to both the minimum length of the line, as well as the delta between minimum and maximum line length.
    const double point_min_delta = 2e-1 * line_unit_length; // The radius in which reference points might get dropped if they are next to original corner points.
    const int n_point = points.size();
    int n_seg = points.size();
    // Reduce the number of segments by 1 for open lines, or pre-closed loops since no segment exists between the first and last points in these cases.
    if (!closed or (closed and (points[0] == points[n_seg - 1])))
        --n_seg;

    double total_length = 0;
    for (int i = 0; i < n_seg; ++i) {
        total_length += (points[(i + 1) % n_point] - points[i]).cast<double>().norm();
    }

    out.reserve(n_seg + std::ceil(total_length / line_unit_length));

    /* Fuzzification loop variable initialisation */
    Vec2d seg_dir;
    Vec2d seg_perp = closed ?
                     perp((points[0] - points[-1 % n_seg]).cast<double>().normalized()) :
                     perp((points[1] - points[0]).cast<double>().normalized());
    Point p_ref = points[0]; // The reference point for the current line segment (= the first corner)

    double x_prev = 0;
    double x_next = total_length < (2. * line_unit_length) ? total_length : line_unit_length + random_value() * std::min(line_unit_length, total_length - 2 * line_unit_length);

    double x_prev_corner = 0; // Will be properly set in the first corner point loop
    double x_next_corner = 0;
    int corner_idx = 0;

    double y_0 = (2. * random_value() - 1.) * fuzzy_skin_thickness;
    double y_prev = y_0;
    double y_next = (2. * random_value() - 1.) * fuzzy_skin_thickness;

    /* Fuzzification loop: */
    while (x_prev < total_length) {
        // Add any interim corner points from the original line
        while (x_next_corner <= x_next) {
            // Don't add the last point, since it has some special behaviour
            if (corner_idx == n_seg)
                break;
            double y = lerp(y_prev, y_next, (x_next_corner - x_prev) / (x_next - x_prev));
            Vec2d prev_perp = seg_perp;

            p_ref = points[corner_idx];
            Vec2d seg = (points[(corner_idx + 1) % n_point] - p_ref).cast<double>();
            double seg_length = seg.norm();
            seg_dir = seg.normalized();
            seg_perp = perp(seg_dir);

            Vec2d corner_perp = seg_perp.dot(prev_perp) > -0.99 ? Vec2d((seg_perp + prev_perp).normalized()) : seg_dir;
            out.emplace_back(p_ref + (y * corner_perp).cast<coord_t>());

            x_prev_corner = x_next_corner;
            x_next_corner += seg_length;
            ++corner_idx;
        }
        // Add the next mid-segment fuzzy point
        // Only add the point if it is not too close to an existing interim corner point to prevent point spam
        if (!((x_next - x_prev_corner) < point_min_delta or (x_next_corner - x_next) < point_min_delta))
            out.emplace_back(p_ref + ((x_next - x_prev_corner) * seg_dir + y_next * seg_perp).cast<coord_t>());

        x_prev = x_next;
        x_next = x_prev > total_length - (2. * line_unit_length) ? total_length : x_prev + line_unit_length + random_value() * std::min(line_unit_length, total_length - x_prev - 2. * line_unit_length);

        y_prev = y_next;
        y_next = (closed and x_next == total_length) ? y_0 : (2. * random_value() - 1.) * fuzzy_skin_thickness;
    }
    // Add the closing corner
    if (closed)
        out.emplace_back(out[0]);
    else
        out.emplace_back(points[n_seg] + (y_next * seg_perp).cast<coord_t>());

    out.shrink_to_fit();

    points = std::move(out);
}

void fuzzy_polygon(Polygon &polygon, double fuzzy_skin_thickness, double fuzzy_skin_point_distance)
{
    fuzzy_polyline(polygon.points, true, fuzzy_skin_thickness, fuzzy_skin_point_distance);
}

void fuzzy_extrusion_line(Arachne::ExtrusionLine &ext_lines, const double fuzzy_skin_thickness, const double fuzzy_skin_point_dist)
{
    // Fuzzification configuration setup
    const bool closed = ext_lines.is_closed;
    const std::vector<Arachne::ExtrusionJunction> &points = ext_lines.junctions;

    std::vector<Arachne::ExtrusionJunction> out;

    const double line_unit_length = 2./3. * fuzzy_skin_point_dist; // The unit lengths of line segments, equal to both the minimum length of the line, as well as the delta between minimum and maximum line length.
    const double point_min_delta = 2e-1 * line_unit_length; // The radius in which reference points might get dropped if they are next to original corner points
    const int n_point = ext_lines.size();
    int n_seg = n_point;
    // Reduce the number of segments by 1 for open lines, or pre-closed loops since no segment exists between the first and last points in these cases.
    if (!closed or (closed and (points[0].p == points[n_seg - 1].p)))
        --n_seg;

    double total_length = 0;
    for (int i = 0; i < n_seg; ++i) {
        total_length += (points[(i + 1) % n_point].p - points[i].p).cast<double>().norm();
    }

    out.reserve(n_seg + std::ceil(total_length / line_unit_length));

    // Fuzzification loop variable initialisation
    Vec2d seg_dir;
    Vec2d seg_perp = closed ?
                     perp((points[0].p - points[-1 % n_seg].p).cast<double>().normalized()) :
                     perp((points[1].p - points[0].p).cast<double>().normalized());
    Arachne::ExtrusionJunction p_ref = points[0]; // The reference point for the current line segment (= the first corner)

    double x_prev = 0;
    double x_next = total_length < (2. * line_unit_length) ? total_length : line_unit_length + random_value() * std::min(line_unit_length, total_length - 2 * line_unit_length);

    double x_prev_corner = 0; // Will be properly set in the first corner point loop
    double x_next_corner = 0;
    int corner_idx = 0;

    double y_0 = (2. * random_value() - 1.) * fuzzy_skin_thickness;
    double y_prev = y_0;
    double y_next = (2. * random_value() - 1.) * fuzzy_skin_thickness;

    /* Fuzzification loop: */
    while (x_prev < total_length) {
        // Add any interim corner points from the original line
        while (x_next_corner <= x_next) {
            // Don't add the last point, since it has some special behaviour
            if (corner_idx == n_seg)
                break;
            double y = lerp(y_prev, y_next, (x_next_corner - x_prev) / (x_next - x_prev));
            Vec2d prev_perp = seg_perp;

            p_ref = points[corner_idx];
            Vec2d seg = (points[(corner_idx + 1) % n_point].p - p_ref.p).cast<double>();
            double seg_length = seg.norm();
            seg_dir = seg.normalized();
            seg_perp = perp(seg_dir);

            Vec2d corner_perp = seg_perp.dot(prev_perp) > -0.99 ? Vec2d((seg_perp + prev_perp).normalized()) : seg_dir;
            out.emplace_back(p_ref.p + (y * corner_perp).cast<coord_t>(), p_ref.w, p_ref.perimeter_index);

            x_prev_corner = x_next_corner;
            x_next_corner += seg_length;
            ++corner_idx;
        }
        // Add the next mid-segment fuzzy point
        // Only add the point if it is not too close to an existing interim corner point to prevent point spam
        if (!((x_next - x_prev_corner) < point_min_delta or (x_next_corner - x_next) < point_min_delta))
            out.emplace_back(p_ref.p + ((x_next - x_prev_corner) * seg_dir + y_next * seg_perp).cast<coord_t>(), p_ref.w, p_ref.perimeter_index);

        x_prev = x_next;
        x_next = x_prev > total_length - (2. * line_unit_length) ? total_length : x_prev + line_unit_length + random_value() * std::min(line_unit_length, total_length - x_prev - 2. * line_unit_length);

        y_prev = y_next;
        y_next = (closed and x_next == total_length) ? y_0 : (2. * random_value() - 1.) * fuzzy_skin_thickness;
    }
    // Add the closing corner
    if (closed)
        out.emplace_back(out[0]);
    else
        out.emplace_back(points[n_seg].p + (y_next * seg_perp).cast<coord_t>(), p_ref.w, p_ref.perimeter_index);

    out.shrink_to_fit();

    ext_lines.junctions = std::move(out);
}

bool should_fuzzify(const PrintRegionConfig &config, const size_t layer_idx, const size_t perimeter_idx, const bool is_contour)
{
    const FuzzySkinType fuzzy_skin_type = config.fuzzy_skin.value;

    if (fuzzy_skin_type == FuzzySkinType::None || layer_idx <= 0) {
        return false;
    }

    const bool fuzzify_contours = perimeter_idx == 0;
    const bool fuzzify_holes    = fuzzify_contours && fuzzy_skin_type == FuzzySkinType::All;

    return is_contour ? fuzzify_contours : fuzzify_holes;
}

Polygon apply_fuzzy_skin(const Polygon &polygon, const PrintRegionConfig &base_config, const PerimeterRegions &perimeter_regions, const size_t layer_idx, const size_t perimeter_idx, const bool is_contour)
{
    using namespace Slic3r::Algorithm::LineSegmentation;

    auto apply_fuzzy_skin_on_polygon = [&layer_idx, &perimeter_idx, &is_contour](const Polygon &polygon, const PrintRegionConfig &config) -> Polygon {
        if (should_fuzzify(config, layer_idx, perimeter_idx, is_contour)) {
            Polygon fuzzified_polygon = polygon;
            fuzzy_polygon(fuzzified_polygon, scaled<double>(config.fuzzy_skin_thickness.value), scaled<double>(config.fuzzy_skin_point_dist.value));

            return fuzzified_polygon;
        } else {
            return polygon;
        }
    };

    if (perimeter_regions.empty()) {
        return apply_fuzzy_skin_on_polygon(polygon, base_config);
    }

    PolylineRegionSegments segments = polygon_segmentation(polygon, base_config, perimeter_regions);
    if (segments.size() == 1) {
        const PrintRegionConfig &config = segments.front().config;
        return apply_fuzzy_skin_on_polygon(polygon, config);
    }

    Polygon fuzzified_polygon;
    for (PolylineRegionSegment &segment : segments) {
        const PrintRegionConfig &config = segment.config;
        if (should_fuzzify(config, layer_idx, perimeter_idx, is_contour)) {
            fuzzy_polyline(segment.polyline.points, false, scaled<double>(config.fuzzy_skin_thickness.value), scaled<double>(config.fuzzy_skin_point_dist.value));
        }

        assert(!segment.polyline.empty());
        if (segment.polyline.empty()) {
            continue;
        } else if (!fuzzified_polygon.empty() && fuzzified_polygon.back() == segment.polyline.front()) {
            // Remove the last point to avoid duplicate points.
            fuzzified_polygon.points.pop_back();
        }

        Slic3r::append(fuzzified_polygon.points, std::move(segment.polyline.points));
    }

    assert(!fuzzified_polygon.empty());
    if (fuzzified_polygon.front() == fuzzified_polygon.back()) {
        // Remove the last point to avoid duplicity between the first and the last point.
        fuzzified_polygon.points.pop_back();
    }

    return fuzzified_polygon;
}

Arachne::ExtrusionLine apply_fuzzy_skin(const Arachne::ExtrusionLine &extrusion, const PrintRegionConfig &base_config, const PerimeterRegions &perimeter_regions, const size_t layer_idx, const size_t perimeter_idx, const bool is_contour)
{
    using namespace Slic3r::Algorithm::LineSegmentation;
    using namespace Slic3r::Arachne;

    if (perimeter_regions.empty()) {
        if (should_fuzzify(base_config, layer_idx, perimeter_idx, is_contour)) {
            ExtrusionLine fuzzified_extrusion = extrusion;
            fuzzy_extrusion_line(fuzzified_extrusion, scaled<double>(base_config.fuzzy_skin_thickness.value), scaled<double>(base_config.fuzzy_skin_point_dist.value));

            return fuzzified_extrusion;
        } else {
            return extrusion;
        }
    }

    ExtrusionRegionSegments segments = extrusion_segmentation(extrusion, base_config, perimeter_regions);
    ExtrusionLine           fuzzified_extrusion;

    for (ExtrusionRegionSegment &segment : segments) {
        const PrintRegionConfig &config = segment.config;
        if (should_fuzzify(config, layer_idx, perimeter_idx, is_contour)) {
            fuzzy_extrusion_line(segment.extrusion, scaled<double>(config.fuzzy_skin_thickness.value), scaled<double>(config.fuzzy_skin_point_dist.value));
        }

        assert(!segment.extrusion.empty());
        if (segment.extrusion.empty()) {
            continue;
        } else if (!fuzzified_extrusion.empty() && fuzzified_extrusion.back().p == segment.extrusion.front().p) {
            // Remove the last point to avoid duplicate points (We don't care if the width of both points is different.).
            fuzzified_extrusion.junctions.pop_back();
        }

        Slic3r::append(fuzzified_extrusion.junctions, std::move(segment.extrusion.junctions));
    }

    assert(!fuzzified_extrusion.empty());

    return fuzzified_extrusion;
}

} // namespace Slic3r::Feature::FuzzySkin
