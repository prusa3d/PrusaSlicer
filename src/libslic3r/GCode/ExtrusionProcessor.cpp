#include "ExtrusionProcessor.hpp"
#include <string>

namespace Slic3r { namespace ExtrusionProcessor {

ExtrusionPaths calculate_and_split_overhanging_extrusions(const ExtrusionPath                             &path,
                                                          const AABBTreeLines::LinesDistancer<Linef>      &unscaled_prev_layer,
                                                          const AABBTreeLines::LinesDistancer<CurledLine> &prev_layer_curled_lines)
{
    std::vector<ExtendedPoint>           extended_points = estimate_points_properties<true, true, true, true>(path.polyline.points,
                                                                                                    unscaled_prev_layer, path.width());
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
            for (const ExtrusionPath &p : loop->paths) {
                auto paths = calculate_and_split_overhanging_extrusions(p, unscaled_prev_layer, prev_layer_curled_lines);
                new_loop.paths.insert(new_loop.paths.end(), paths.begin(), paths.end());
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


std::pair<float,float> calculate_overhang_speed(const ExtrusionAttributes &attributes,
                              const FullPrintConfig     &config,
                              size_t                     extruder_id,
                              float                      external_perim_reference_speed,
                              float                      default_speed)
{
    assert(attributes.overhang_attributes.has_value());
    std::vector<std::pair<int, ConfigOptionFloatOrPercent>> overhangs_with_speeds = {
        {100, ConfigOptionFloatOrPercent{default_speed, false}}};
    if (config.enable_dynamic_overhang_speeds) {
        overhangs_with_speeds = {{0, config.overhang_speed_0},
                                 {25, config.overhang_speed_1},
                                 {50, config.overhang_speed_2},
                                 {75, config.overhang_speed_3},
                                 {100, ConfigOptionFloatOrPercent{default_speed, false}}};
    }

    std::vector<std::pair<int, ConfigOptionInts>> overhang_with_fan_speeds = {{100, ConfigOptionInts{0}}};
    if (config.enable_dynamic_fan_speeds.get_at(extruder_id)) {
        overhang_with_fan_speeds = {{0, config.overhang_fan_speed_0},
                                    {25, config.overhang_fan_speed_1},
                                    {50, config.overhang_fan_speed_2},
                                    {75, config.overhang_fan_speed_3},
                                    {100, ConfigOptionInts{0}}};
    }

    float                  speed_base = external_perim_reference_speed > 0 ? external_perim_reference_speed : default_speed;
    std::map<float, float> speed_sections;
    for (size_t i = 0; i < overhangs_with_speeds.size(); i++) {
        float distance = attributes.width * (1.0 - (overhangs_with_speeds[i].first / 100.0));
        float speed    = overhangs_with_speeds[i].second.percent ? (speed_base * overhangs_with_speeds[i].second.value / 100.0) :
                                                                   overhangs_with_speeds[i].second.value;
        if (speed < EPSILON)
            speed = speed_base;
        speed_sections[distance] = speed;
    }

    std::map<float, float> fan_speed_sections;
    for (size_t i = 0; i < overhang_with_fan_speeds.size(); i++) {
        float distance               = attributes.width * (1.0 - (overhang_with_fan_speeds[i].first / 100.0));
        float fan_speed              = overhang_with_fan_speeds[i].second.get_at(extruder_id);
        fan_speed_sections[distance] = fan_speed;
    }

    auto interpolate_speed = [](const std::map<float, float> &values, float distance) {
        auto upper_dist = values.lower_bound(distance);
        if (upper_dist == values.end()) {
            return values.rbegin()->second;
        }
        if (upper_dist == values.begin()) {
            return upper_dist->second;
        }

        auto  lower_dist = std::prev(upper_dist);
        float t          = (distance - lower_dist->first) / (upper_dist->first - lower_dist->first);
        return (1.0f - t) * lower_dist->second + t * upper_dist->second;
    };

    float extrusion_speed   = std::min(interpolate_speed(speed_sections, attributes.overhang_attributes->start_distance_from_prev_layer),
                                       interpolate_speed(speed_sections, attributes.overhang_attributes->end_distance_from_prev_layer));
    float curled_base_speed = interpolate_speed(speed_sections,
                                                attributes.width * attributes.overhang_attributes->proximity_to_curled_lines);
    float final_speed       = std::min(curled_base_speed, extrusion_speed);
    float fan_speed = std::min(interpolate_speed(fan_speed_sections, attributes.overhang_attributes->start_distance_from_prev_layer),
                               interpolate_speed(fan_speed_sections, attributes.overhang_attributes->end_distance_from_prev_layer));
    float external_perimeter_fan_speed = config.external_perimeter_fan_speed.get_at(extruder_id);
    if (attributes.role.is_external_perimeter() && external_perimeter_fan_speed > fan_speed)
        fan_speed = external_perimeter_fan_speed;

    if (!config.enable_dynamic_overhang_speeds) {
        final_speed = -1;
    }
    if (!config.enable_dynamic_fan_speeds.get_at(extruder_id)) {
        fan_speed = -1;
    }

    return {final_speed, fan_speed};
}

}} // namespace Slic3r::ExtrusionProcessor
