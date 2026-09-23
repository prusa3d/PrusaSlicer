#include "Wipe.hpp"

#include <string_view>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <cinttypes>

#include "../GCode.hpp"
#include "Slic3r/Biz/libpgcode/Utils.hpp"
#include "libslic3r/Extruder.hpp"
#include "libslic3r/GCode/GCodeWriter.hpp"
#include "libslic3r/GCode/SmoothPath.hpp"
#include "libslic3r/Geometry/ArcWelder.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/libslic3r.h"

using namespace std::string_view_literals;

namespace Slic3r::GCode {

void Wipe::init(const PrintConfigView &config, const std::vector<unsigned int> &extruders)
{
    this->reset_path();

    // Firmware retraction owns its own E motion; retain the native path there.
    m_orca_rules = config.get<bool>("orca_wipe_compatibility") && !config.get<bool>("use_firmware_retraction");
    m_role_based_speed = config.get<bool>("role_based_wipe_speed");
    m_wipe_speed = config.get<std::vector<Domain::FloatOrPercentage>>("wipe_speed");
    m_wipe_distance = config.get<std::vector<double>>("wipe_distance");
    m_retract_after_wipe = config.get<std::vector<Domain::Percentage>>("retract_after_wipe");

    // Calculate maximum wipe length to accumulate by the wipe cache.
    // Paths longer than wipe_xy should never be needed for the wipe move.
    double wipe_xy = 0;
    const bool multimaterial = extruders.size() > 1;
    for (auto id : extruders)
        if (config.get<std::vector<bool>>("wipe").at(id)) {
            if (m_orca_rules) {
                wipe_xy = std::max(wipe_xy, m_wipe_distance.at(id));
                continue;
            }
            // Wipe length to extrusion ratio.
            const double xy_to_e = this->calc_xy_to_e_ratio(
                config.get<std::vector<double>>("retract_speed"),
                config.get<double>("travel_speed"),
                id
            );
            wipe_xy = std::max(
                wipe_xy,
                config.get<std::vector<double>>("retract_length").at(id) / xy_to_e
            );
            if (multimaterial)
                wipe_xy = std::max(wipe_xy, config.get<std::vector<double>>("retract_length_toolchange").at(id) / xy_to_e);
        }

    if (wipe_xy == 0)
        this->disable();
    else
        this->enable(wipe_xy);
}

void Wipe::set_path(SmoothPath &&path) {
    this->reset_path();

    if (this->enabled() && !path.empty()) {
        const coord_t wipe_len_max_scaled = scaled(m_wipe_len_max);
        m_path = std::move(path.front().path);
        int64_t len = Geometry::ArcWelder::estimate_path_length(m_path);
        for (auto it = std::next(path.begin()); len < wipe_len_max_scaled && it != path.end(); ++it) {
            if (it->path_attributes.role.is_bridge())
                break; // Do not perform a wipe on bridges.
            assert(it->path.size() >= 2);
            assert(m_path.back().point == it->path.front().point);
            if (m_path.back().point != it->path.front().point)
                // ExtrusionMultiPath is interrupted in some place. This should not really happen.
                break;
            len += Geometry::ArcWelder::estimate_path_length(it->path);
            m_path.insert(m_path.end(), it->path.begin() + 1, it->path.end());
        }
    }

    assert(m_path.empty() || m_path.size() > 1);
}

std::string Wipe::wipe_orca(
    GCodeGenerator& gcodegen, const std::vector<double>& retract_speed, double travel_speed, bool toolchange)
{
    auto& writer = gcodegen.writer();
    const auto& extruder = *writer.extruder();
    const auto id = extruder.id();
    const double limit = m_wipe_distance.at(id);
    if (!has_path() || limit <= EPSILON) {
        reset_path();
        return {};
    }

    // Orca wipes a polyline from the actual nozzle position. Linearize cached
    // arcs within 0.01 mm, then clip the quantized XY path to the requested length.
    std::vector<Vec2d> points{gcodegen.point_to_gcode_quantized(*gcodegen.last_position)};
    double length = 0.;
    const auto append = [&](const Point& point) {
        const Vec2d previous = points.back();
        Vec2d next = gcodegen.point_to_gcode_quantized(point + m_offset);
        const double distance = (next - previous).norm();
        if (distance <= EPSILON) return false;
        const bool done = distance >= limit - length;
        if (done)
            next = GCodeFormatter::quantize(Vec2d(previous + (next - previous) * ((limit - length) / distance)));
        if (next != previous) {
            length += (next - previous).norm();
            points.push_back(next);
        }
        return done;
    };
    bool done = false;
    for (size_t i = 1; i < m_path.size() && !done; ++i) {
        if (m_path[i].linear()) done = append(m_path[i].point);
        else {
            const auto arc = Geometry::ArcWelder::arc_discretize(m_path[i-1].point, m_path[i].point,
                m_path[i].radius, m_path[i].ccw(), scaled<double>(0.01));
            for (auto it = std::next(arc.begin()); it != arc.end() && !done; ++it) done = append(*it);
        }
    }
    if (points.size() < 2 || length <= EPSILON) {
        reset_path();
        return {};
    }

    const double speed = std::max(10., m_role_based_speed ? writer.current_speed() / 60.
        : m_wipe_speed.at(id).get_abs_value(travel_speed));
    const double area = writer.config.use_volumetric_e
        ? extruder.filament_diameter() * extruder.filament_diameter() * M_PI / 4. : 1.;
    const double target = std::max(0., toolchange ? extruder.retract_length_toolchange() : extruder.retract_length());
    double before = target * std::clamp(extruder.retract_before_wipe(), 0., 1.);
    const double after = std::min(target - before,
        target * std::clamp(m_retract_after_wipe.at(id).get_abs_value(1.), 0., 1.));
    double during = std::min(std::max(0., target - std::max(before, extruder.retracted() / area) - after),
        std::max(0., retract_speed.at(id)) * length / speed);
    // Move any retract amount that cannot fit within the wipe to the start,
    // without taking the configured after-wipe reserve.
    before = std::max(before, target - after - during);
    std::string gcode = writer.retract_to_length(before, toolchange);
    during = std::min(during, std::max(0., target - extruder.retracted() / area - after));
    const double total_e = GCodeFormatter::quantize_e(during * area);
    gcode += ";" + std::string{Biz::libpgcode::reserved_tag(Biz::libpgcode::Tags::Wipe_Start)} + "\n";
    gcode += writer.set_speed(speed * 60., {}, gcodegen.enable_cooling_markers() ? ";_WIPE"sv : ""sv);
    double travelled = 0., emitted_e = 0.;
    for (size_t i = 1; i < points.size(); ++i) {
        travelled += (points[i] - points[i-1]).norm();
        const double cumulative_e = i + 1 == points.size() ? total_e
            : GCodeFormatter::quantize_e(total_e * travelled / length);
        gcode += writer.extrude_to_xy(points[i], -(cumulative_e - emitted_e), "wipe and retract");
        emitted_e = cumulative_e;
    }
    gcode += ";" + std::string{Biz::libpgcode::reserved_tag(Biz::libpgcode::Tags::Wipe_End)} + "\n";
    gcodegen.last_position = gcodegen.gcode_to_point(points.back());
    reset_path();
    return gcode;
}

std::string Wipe::wipe(
    GCodeGenerator& gcodegen,
    const std::vector<double>& retract_speed,
    double travel_speed,
    bool toolchange
)
{
    if (m_orca_rules) return wipe_orca(gcodegen, retract_speed, travel_speed, toolchange);
    std::string gcode;
    const Extruder &extruder = *gcodegen.writer().extruder();
    static constexpr const std::string_view wipe_retract_comment = "wipe and retract"sv;

    // Remaining quantized retraction length.
    if (double retract_length = extruder.retract_to_go(toolchange ? extruder.retract_length_toolchange() : extruder.retract_length());
        retract_length > 0 && this->has_path()) {
        // Delayed emitting of a wipe start tag.
        bool wiped = false;
        const double wipe_speed = this->calc_wipe_speed(travel_speed);
        auto start_wipe = [&wiped, &gcode, &gcodegen, wipe_speed](){
            if (! wiped) {
                wiped = true;
                gcode += ";" + std::string{Biz::libpgcode::reserved_tag(Biz::libpgcode::Tags::Wipe_Start)} + "\n";
                gcode += gcodegen.writer().set_speed(wipe_speed * 60, {}, gcodegen.enable_cooling_markers() ? ";_WIPE"sv : ""sv);
            }
        };
        const double xy_to_e    = this->calc_xy_to_e_ratio(retract_speed, travel_speed, extruder.id());
        auto         wipe_linear = [&gcode, &gcodegen, &retract_length, xy_to_e](const Vec2d &prev_quantized, Vec2d &p) {
            Vec2d  p_quantized = GCodeFormatter::quantize(p);
            if (p_quantized == prev_quantized) {
                p = p_quantized;
                return false;
            }
            double segment_length = (p_quantized - prev_quantized).norm();
            // Quantize E axis as it is to be extruded as a whole segment.
            double dE = GCodeFormatter::quantize_e(xy_to_e * segment_length);
            bool   done = false;
            if (dE > retract_length - EPSILON) {
                if (dE > retract_length + EPSILON)
                    // Shorten the segment.
                    p = GCodeFormatter::quantize(Vec2d(prev_quantized + (p - prev_quantized) * (retract_length / dE)));
                else
                    p = p_quantized;
                dE   = retract_length;
                done = true;
            } else
                p = p_quantized;
            gcode += gcodegen.writer().extrude_to_xy(p, -dE, wipe_retract_comment);
            retract_length -= dE;
            return done;
        };
        auto         wipe_arc = [&gcode, &gcodegen, &retract_length, xy_to_e, &wipe_linear](
            const Vec2d &prev_quantized, Vec2d &p, double radius_in, const bool ccw) {
            Vec2d  p_quantized = GCodeFormatter::quantize(p);
            if (p_quantized == prev_quantized) {
                p = p_quantized;
                return false;
            }
            // Use the exact radius for calculating the IJ values, no quantization.
            double radius = radius_in;
            if (radius == 0)
                // Degenerated arc after quantization. Process it as if it was a line segment.
                return wipe_linear(prev_quantized, p);
            Vec2d  center = Geometry::ArcWelder::arc_center(prev_quantized.cast<double>(), p_quantized.cast<double>(), double(radius), ccw);
            float  angle  = Geometry::ArcWelder::arc_angle(prev_quantized.cast<double>(), p_quantized.cast<double>(), double(radius));
            assert(angle > 0);
            double segment_length = angle * std::abs(radius);
            double dE = GCodeFormatter::quantize_e(xy_to_e * segment_length);
            bool   done = false;
            if (dE > retract_length - EPSILON) {
                if (dE > retract_length + EPSILON) {
                    // Shorten the segment. Recalculate the arc from the unquantized end coordinate.
                    center = Geometry::ArcWelder::arc_center(prev_quantized.cast<double>(), p.cast<double>(), double(radius), ccw);
                    angle = Geometry::ArcWelder::arc_angle(prev_quantized.cast<double>(), p.cast<double>(), double(radius));
                    segment_length = angle * std::abs(radius);
                    dE = xy_to_e * segment_length;
                    p = GCodeFormatter::quantize(
                            Vec2d(center + Eigen::Rotation2D((ccw ? angle : -angle) * (retract_length / dE)) * (prev_quantized - center)));
                } else
                    p = p_quantized;
                dE   = retract_length;
                done = true;
            } else
                p = p_quantized;
            assert(dE > 0);
            {
                // Calculate quantized IJ circle center offset.
                Vec2d ij = GCodeFormatter::quantize(Vec2d(center - prev_quantized));
                if (ij == Vec2d::Zero())
                    // Degenerated arc after quantization. Process it as if it was a line segment.
                    return wipe_linear(prev_quantized, p);
                // The arc is valid.
                gcode += gcodegen.writer().extrude_to_xy_G2G3IJ(
                    p, ij, ccw, -dE, wipe_retract_comment);
            }
            retract_length -= dE;
            return done;
        };
        // Start with the current position, which may be different from the wipe path start in case of loop clipping.
        Vec2d prev = gcodegen.point_to_gcode_quantized(*gcodegen.last_position);
        auto  it   = this->path().begin();
        Vec2d p    = gcodegen.point_to_gcode(it->point + m_offset);
        ++ it;
        bool done = false;
        if (p != prev) {
            start_wipe();
            done = wipe_linear(prev, p);
        }
        if (! done) {
            prev = p;
            auto end = this->path().end();
            for (; it != end && ! done; ++ it) {
                p = gcodegen.point_to_gcode(it->point + m_offset);
                if (p != prev) {
                    start_wipe();
                    if (it->linear() ?
                        wipe_linear(prev, p) :
                        wipe_arc(prev, p, unscaled<double>(it->radius), it->ccw()))
                        break;
                    prev = p;
                }
            }
        }
        if (wiped) {
            // add tag for processor
            assert(p == GCodeFormatter::quantize(p));
            gcode += ";" + std::string{Biz::libpgcode::reserved_tag(Biz::libpgcode::Tags::Wipe_End)} + "\n";
            gcodegen.last_position = gcodegen.gcode_to_point(p);
        }
    }

    // Prevent wiping again on the same path.
    this->reset_path();
    return gcode;
}

// Make a little move inwards before leaving loop after path was extruded,
// thus the current extruder position is at the end of a path and the path
// may not be closed in case the loop was clipped to hide a seam.
std::optional<Point> wipe_hide_seam(const SmoothPath &path, const bool path_reversed, const double wipe_length)
{
    assert(! path.empty());
    assert(path.front().path.size() >= 2);
    assert(path.back().path.size() >= 2);

    // Heuristics for estimating whether there is a chance that the wipe move will fit inside a small perimeter
    // or that the wipe move direction could be calculated with reasonable accuracy.
    if (longer_than(path, 2.5 * wipe_length)) {
        // The print head will be moved away from path end inside the island.
        Point p_current = path.back().path.back().point;
        Point p_next = path.front().path.front().point;
        Point p_prev;
        {
            // Is the seam hiding gap large enough already?
            double l = wipe_length - (p_next - p_current).cast<double>().norm();
            if (l > 0) {
                // Not yet.
                std::optional<Point> n = sample_path_point_at_distance_from_start(path, l);
                assert(n);
                if (! n)
                    // Wipe move cannot be calculated, the loop is not long enough. This should not happen due to the longer_than() test above.
                    return {};
            }
            if (std::optional<Point> p = sample_path_point_at_distance_from_start(path, wipe_length); p)
                p_prev = *p;
            else
                // Wipe move cannot be calculated, the loop is not long enough. This should not happen due to the longer_than() test above.
                return {};
        }
        // Detect angle between last and first segment.
        // The side depends on the original winding order of the polygon (left for contours, right for holes).
        double angle_inside = angle(p_next - p_current, p_prev - p_current);
        assert(angle_inside >= -M_PI && angle_inside <= M_PI);
        // 3rd of this angle will be taken, thus make the angle monotonic before interpolation.
        if (path_reversed) {
            if (angle_inside > 0)
                angle_inside -= 2.0 * M_PI;
        } else {
            if (angle_inside < 0)
                angle_inside += 2.0 * M_PI;
        }
        // Rotate the forward segment inside by 1/3 of the wedge angle.
        auto v_rotated = Eigen::Rotation2D(angle_inside) * (p_next - p_current).cast<double>().normalized();
        return p_current + (v_rotated * wipe_length).cast<coord_t>();
    }

    return {};
}

} // namespace Slic3r::GCode
