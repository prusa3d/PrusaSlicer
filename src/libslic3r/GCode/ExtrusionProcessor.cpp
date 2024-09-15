#include "ExtrusionProcessor.hpp"

#include <cassert>
#include <iterator>
#include <map>
#include <optional>
#include <cstddef>

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/Exception.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/Polygon.hpp"

namespace Slic3r::ExtrusionProcessor {

ExtrusionPaths calculate_and_split_overhanging_extrusions(const ExtrusionPath                             &path,
                                                          const AABBTreeLines::LinesDistancer<Linef>      &unscaled_prev_layer,
                                                          const AABBTreeLines::LinesDistancer<CurledLine> &prev_layer_curled_lines)
{
    ExtrusionProcessor::PropertiesEstimationConfig config{};
    config.add_corners = true;
    config.prev_layer_boundary_offset = true;
    config.flow_width = path.width();
    std::vector<ExtendedPoint> extended_points = estimate_points_properties<true>(
        path.polyline.points, unscaled_prev_layer, config
    );

    std::vector<std::pair<float, float>> calculated_distances(extended_points.size());

    for (size_t i = 0; i < extended_points.size(); i++) {
        const ExtendedPoint &curr = extended_points[i];
        const ExtendedPoint &next = extended_points[i + 1 < extended_points.size() ? i + 1 : i];

        // The following code artifically increases the distance to provide slowdown for extrusions that are over curled lines
        float        proximity_to_curled_lines = 0.0;
        const double dist_limit                = 10.0 * path.width();
        {
            Vec2d middle       = 0.5 * (curr.position + next.position);
            auto  line_indices = prev_layer_curled_lines.all_lines_in_radius(Point::new_scale(middle), scale_(dist_limit));
            if (!line_indices.empty()) {
                double len = (next.position - curr.position).norm();
                // For long lines, there is a problem with the additional slowdown. If by accident, there is small curled line near the middle
                // of this long line
                //  The whole segment gets slower unnecesarily. For these long lines, we do additional check whether it is worth slowing down.
                // NOTE that this is still quite rough approximation, e.g. we are still checking lines only near the middle point
                // TODO maybe split the lines into smaller segments before running this alg? but can be demanding, and GCode will be huge
                if (len > 8) {
                    Vec2d dir   = Vec2d(next.position - curr.position) / len;
                    Vec2d right = Vec2d(-dir.y(), dir.x());

                    Polygon box_of_influence = {
                        scaled(Vec2d(curr.position + right * dist_limit)),
                        scaled(Vec2d(next.position + right * dist_limit)),
                        scaled(Vec2d(next.position - right * dist_limit)),
                        scaled(Vec2d(curr.position - right * dist_limit)),
                    };

                    double projected_lengths_sum = 0;
                    for (size_t idx : line_indices) {
                        const CurledLine &line   = prev_layer_curled_lines.get_line(idx);
                        Lines             inside = intersection_ln({{line.a, line.b}}, {box_of_influence});
                        if (inside.empty())
                            continue;
                        double projected_length = abs(dir.dot(unscaled(Vec2d((inside.back().b - inside.back().a).cast<double>()))));
                        projected_lengths_sum += projected_length;
                    }
                    if (projected_lengths_sum < 0.4 * len) {
                        line_indices.clear();
                    }
                }

                for (size_t idx : line_indices) {
                    const CurledLine &line                 = prev_layer_curled_lines.get_line(idx);
                    float             distance_from_curled = unscaled(line_alg::distance_to(line, Point::new_scale(middle)));
                    float proximity = (1.0 - (distance_from_curled / dist_limit)) * (1.0 - (distance_from_curled / dist_limit)) *
                                      (line.curled_height / (path.height() * 10.0f)); // max_curled_height_factor from SupportSpotGenerator
                    proximity_to_curled_lines = std::max(proximity_to_curled_lines, proximity);
                }
            }
        }
        calculated_distances[i].first  = std::max(curr.distance, next.distance);
        calculated_distances[i].second = proximity_to_curled_lines;
    }

    ExtrusionPaths      result;
    ExtrusionAttributes new_attrs = path.attributes();
    new_attrs.overhang_attributes = std::optional<OverhangAttributes>(
        {calculated_distances[0].first, calculated_distances[0].first, calculated_distances[0].second});
    result.emplace_back(new_attrs);
    result.back().polyline.append(Point::new_scale(extended_points[0].position));
    size_t sequence_start_index = 0;
    for (size_t i = 1; i < extended_points.size(); i++) {
        result.back().polyline.append(Point::new_scale(extended_points[i].position));
        result.back().overhang_attributes_mutable()->end_distance_from_prev_layer = extended_points[i].distance;

        if (std::abs(calculated_distances[sequence_start_index].first - calculated_distances[i].first) < 0.001 * path.attributes().width &&
            std::abs(calculated_distances[sequence_start_index].second - calculated_distances[i].second) < 0.001) {
            // do not start new path, the attributes are similar enough
            // NOTE: a larger tolerance may be applied here. However, it makes the gcode preview much less smooth
            // (But it has very likely zero impact on the print quality.)
        } else if (i + 1 < extended_points.size()) { // do not start new path if this is last point!
            // start new path, parameters differ
            new_attrs.overhang_attributes->start_distance_from_prev_layer = calculated_distances[i].first;
            new_attrs.overhang_attributes->end_distance_from_prev_layer   = calculated_distances[i].first;
            new_attrs.overhang_attributes->proximity_to_curled_lines      = calculated_distances[i].second;
            sequence_start_index                                          = i;
            result.emplace_back(new_attrs);
            result.back().polyline.append(Point::new_scale(extended_points[i].position));
        }
    }

    return result;
};

ExtrusionEntityCollection calculate_and_split_overhanging_extrusions(const ExtrusionEntityCollection            *ecc,
                                                                     const AABBTreeLines::LinesDistancer<Linef> &unscaled_prev_layer,
                                                                     const AABBTreeLines::LinesDistancer<CurledLine> &prev_layer_curled_lines)
{
    ExtrusionEntityCollection result{};
    result.no_sort = ecc->no_sort;
    for (const auto *e : ecc->entities) {
        if (auto *col = dynamic_cast<const ExtrusionEntityCollection *>(e)) {
            result.append(calculate_and_split_overhanging_extrusions(col, unscaled_prev_layer, prev_layer_curled_lines));
        } else if (auto *loop = dynamic_cast<const ExtrusionLoop *>(e)) {
            ExtrusionLoop new_loop = *loop;
            new_loop.paths.clear();

            ExtrusionPaths paths{loop->paths};
            if (!paths.empty()) {
                ExtrusionPath& first_path{paths.front()};
                ExtrusionPath& last_path{paths.back()};

                if (first_path.attributes() == last_path.attributes()) {
                    if (first_path.polyline.size() > 1 && last_path.polyline.size() > 2) {
                        const Line start{first_path.polyline.front(), *std::next(first_path.polyline.begin())};
                        const Line end{last_path.polyline.back(), *std::next(last_path.polyline.rbegin())};

                        if (std::abs(start.direction() - end.direction()) < 1e-5) {
                            first_path.polyline.points.front() = *std::next(last_path.polyline.points.rbegin());
                            last_path.polyline.points.pop_back();
                        }
                    }
                }
            }

            for (const ExtrusionPath &p : paths) {
                auto resulting_paths = calculate_and_split_overhanging_extrusions(p, unscaled_prev_layer, prev_layer_curled_lines);
                new_loop.paths.insert(new_loop.paths.end(), resulting_paths.begin(), resulting_paths.end());
            }
            result.append(new_loop);
        } else if (auto *mp = dynamic_cast<const ExtrusionMultiPath *>(e)) {
            ExtrusionMultiPath new_mp = *mp;
            new_mp.paths.clear();
            for (const ExtrusionPath &p : mp->paths) {
                auto paths = calculate_and_split_overhanging_extrusions(p, unscaled_prev_layer, prev_layer_curled_lines);
                new_mp.paths.insert(new_mp.paths.end(), paths.begin(), paths.end());
            }
            result.append(new_mp);
        } else if (auto *op = dynamic_cast<const ExtrusionPathOriented *>(e)) {
            auto paths = calculate_and_split_overhanging_extrusions(*op, unscaled_prev_layer, prev_layer_curled_lines);
            for (const ExtrusionPath &p : paths) {
                result.append(ExtrusionPathOriented(p.polyline, p.attributes()));
            }
        } else if (auto *p = dynamic_cast<const ExtrusionPath *>(e)) {
            auto paths = calculate_and_split_overhanging_extrusions(*p, unscaled_prev_layer, prev_layer_curled_lines);
            result.append(paths);
        } else {
            throw Slic3r::InvalidArgument("Unknown extrusion entity type");
        }
    }
    return result;
};

static std::map<float, float> calc_print_speed_sections(const ExtrusionAttributes &attributes,
                                                        const FullPrintConfig     &config,
                                                        const float                external_perimeter_reference_speed,
                                                        const float                default_speed)
{
    struct OverhangWithSpeed
    {
        int                        percent;
        ConfigOptionFloatOrPercent print_speed;
    };

    std::vector<OverhangWithSpeed> overhangs_with_speeds = {{100, ConfigOptionFloatOrPercent{default_speed, false}}};
    if (config.enable_dynamic_overhang_speeds) {
        overhangs_with_speeds = {{  0, config.overhang_speed_0},
                                 { 25, config.overhang_speed_1},
                                 { 50, config.overhang_speed_2},
                                 { 75, config.overhang_speed_3},
                                 {100, ConfigOptionFloatOrPercent{default_speed, false}}};
    }

    const float            speed_base = external_perimeter_reference_speed > 0 ? external_perimeter_reference_speed : default_speed;
    std::map<float, float> speed_sections;
    for (OverhangWithSpeed &overhangs_with_speed : overhangs_with_speeds) {
        const float distance = attributes.width * (1.f - (float(overhangs_with_speed.percent) / 100.f));
        float       speed    = float(overhangs_with_speed.print_speed.get_abs_value(speed_base));

        if (speed < EPSILON) {
            speed = speed_base;
        }

        speed_sections[distance] = speed;
    }

    return speed_sections;
}

static std::map<float, float> calc_fan_speed_sections(const ExtrusionAttributes &attributes,
                                                      const FullPrintConfig     &config,
                                                      const size_t               extruder_id)
{
    struct OverhangWithFanSpeed
    {
        int              percent;
        ConfigOptionInts fan_speed;
    };

    std::vector<OverhangWithFanSpeed> overhang_with_fan_speeds = {{100, ConfigOptionInts{0}}};
    if (config.enable_dynamic_fan_speeds.get_at(extruder_id)) {
        overhang_with_fan_speeds = {{  0, config.overhang_fan_speed_0},
                                    { 25, config.overhang_fan_speed_1},
                                    { 50, config.overhang_fan_speed_2},
                                    { 75, config.overhang_fan_speed_3},
                                    {100, ConfigOptionInts{0}}};
    }

    std::map<float, float> fan_speed_sections;
    for (OverhangWithFanSpeed &overhang_with_fan_speed : overhang_with_fan_speeds) {
        float distance               = attributes.width * (1.f - (float(overhang_with_fan_speed.percent) / 100.f));
        float fan_speed              = float(overhang_with_fan_speed.fan_speed.get_at(extruder_id));
        fan_speed_sections[distance] = fan_speed;
    }

    return fan_speed_sections;
}

OverhangSpeeds calculate_overhang_speed(const ExtrusionAttributes  &attributes,
                                        const FullPrintConfig      &config,
                                        const size_t                extruder_id,
                                        const float                 external_perimeter_reference_speed,
                                        const float                 default_speed,
                                        const std::optional<float> &current_fan_speed)
{
    assert(attributes.overhang_attributes.has_value());

    auto interpolate_speed = [](const std::map<float, float> &values, float distance) {
        auto upper_dist = values.lower_bound(distance);
        if (upper_dist == values.end()) {
            return values.rbegin()->second;
        } else if (upper_dist == values.begin()) {
            return upper_dist->second;
        }

        auto  lower_dist = std::prev(upper_dist);
        float t          = (distance - lower_dist->first) / (upper_dist->first - lower_dist->first);
        return (1.0f - t) * lower_dist->second + t * upper_dist->second;
    };

    const std::map<float, float> speed_sections     = calc_print_speed_sections(attributes, config, external_perimeter_reference_speed, default_speed);
    const std::map<float, float> fan_speed_sections = calc_fan_speed_sections(attributes, config, extruder_id);

    const float extrusion_speed   = std::min(interpolate_speed(speed_sections, attributes.overhang_attributes->start_distance_from_prev_layer),
                                             interpolate_speed(speed_sections, attributes.overhang_attributes->end_distance_from_prev_layer));
    const float curled_base_speed = interpolate_speed(speed_sections, attributes.width * attributes.overhang_attributes->proximity_to_curled_lines);

    const float fan_speed         = std::min(interpolate_speed(fan_speed_sections, attributes.overhang_attributes->start_distance_from_prev_layer),
                                             interpolate_speed(fan_speed_sections, attributes.overhang_attributes->end_distance_from_prev_layer));

    OverhangSpeeds overhang_speeds = {std::min(curled_base_speed, extrusion_speed), fan_speed};
    if (!config.enable_dynamic_overhang_speeds) {
        overhang_speeds.print_speed = -1;
    }

    if (!config.enable_dynamic_fan_speeds.get_at(extruder_id)) {
        overhang_speeds.fan_speed = -1;
    } else if (current_fan_speed.has_value() && (fan_speed < *current_fan_speed) && (*current_fan_speed - fan_speed) <= MIN_FAN_SPEED_NEGATIVE_CHANGE_TO_EMIT) {
        // Always allow the fan speed to be increased without any hysteresis, but the speed will be decreased only when it exceeds a limit for minimum change.
        overhang_speeds.fan_speed = *current_fan_speed;
    }

    return overhang_speeds;
}

} // namespace Slic3r::ExtrusionProcessor
