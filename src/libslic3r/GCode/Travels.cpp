#include "Travels.hpp"

#include <boost/math/special_functions/pow.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>
#include <utility>
#include <cassert>
#include <cinttypes>

#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Print.hpp"
#include "../GCode.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Exception.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/LayerRegion.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/libslic3r.h"
#include "tcbspan/span.hpp"
#include "libslic3r/ShortestPath.hpp"

namespace Slic3r::GCode {

static Lines extrusion_entity_to_lines(const ExtrusionEntity &e_entity)
{
    if (const auto *path = dynamic_cast<const ExtrusionPath *>(&e_entity)) {
        return to_lines(path->as_polyline());
    } else if (const auto *multipath = dynamic_cast<const ExtrusionMultiPath *>(&e_entity)) {
        return to_lines(multipath->as_polyline());
    } else if (const auto *loop = dynamic_cast<const ExtrusionLoop *>(&e_entity)) {
        return to_lines(loop->polygon());
    } else {
        throw Slic3r::InvalidArgument("Invalid argument supplied to TODO()");
    }

    return {};
}

AABBTreeLines::LinesDistancer<ObjectOrExtrusionLinef> get_previous_layer_distancer(
    const GCodeGenerator::ObjectsLayerToPrint &objects_to_print, const ExPolygons &slices
) {
    std::vector<ObjectOrExtrusionLinef> lines;
    for (const GCodeGenerator::ObjectLayerToPrint &object_to_print : objects_to_print) {
        if (const PrintObject *object = object_to_print.object(); object) {
            const size_t object_layer_idx = &object_to_print - &objects_to_print.front();
            for (const PrintInstance &instance : object->instances()) {
                const size_t instance_idx = &instance - &object->instances().front();
                for (const ExPolygon &polygon : slices)
                    for (const Line &line : polygon.lines())
                        lines.emplace_back(unscaled(Point{line.a + instance.shift}), unscaled(Point{line.b + instance.shift}), object_layer_idx, instance_idx);
            }
        }
    }

    return AABBTreeLines::LinesDistancer{std::move(lines)};
}

std::pair<AABBTreeLines::LinesDistancer<ObjectOrExtrusionLinef>, size_t> get_current_layer_distancer(const ObjectsLayerToPrint &objects_to_print)
{
    std::vector<ObjectOrExtrusionLinef> lines;
    size_t                              extrusion_entity_cnt = 0;
    for (const ObjectLayerToPrint &object_to_print : objects_to_print) {
        const size_t object_layer_idx = &object_to_print - &objects_to_print.front();
        if (const Layer *layer = object_to_print.object_layer; layer) {
            for (const PrintInstance &instance : layer->object()->instances()) {
                const size_t instance_idx = &instance - &layer->object()->instances().front();
                for (const LayerSlice &lslice : layer->lslices_ex) {
                    for (const LayerIsland &island : lslice.islands) {
                        const LayerRegion &layerm = *layer->get_region(island.perimeters.region());
                        for (uint32_t perimeter_id : island.perimeters) {
                            assert(dynamic_cast<const ExtrusionEntityCollection *>(layerm.perimeters().entities[perimeter_id]));
                            const auto *eec = static_cast<const ExtrusionEntityCollection *>(layerm.perimeters().entities[perimeter_id]);
                            for (const ExtrusionEntity *ee : *eec) {
                                if (ee->role().is_external_perimeter()) {
                                    for (const Line &line : extrusion_entity_to_lines(*ee))
                                        lines.emplace_back(unscaled(Point{line.a + instance.shift}), unscaled(Point{line.b + instance.shift}), object_layer_idx, instance_idx, ee);
                                }

                                ++extrusion_entity_cnt;
                            }
                        }
                    }
                }
            }
        }
    }

    return {AABBTreeLines::LinesDistancer{std::move(lines)}, extrusion_entity_cnt};
}

void TravelObstacleTracker::init_layer(const Layer &layer, const ObjectsLayerToPrint &objects_to_print)
{
    size_t extrusion_entity_cnt = 0;
    m_extruded_extrusion.clear();

    m_objects_to_print         = objects_to_print;
    m_previous_layer_distancer = get_previous_layer_distancer(m_objects_to_print, layer.lower_layer->lslices);

    std::tie(m_current_layer_distancer, extrusion_entity_cnt) = get_current_layer_distancer(m_objects_to_print);
    m_extruded_extrusion.reserve(extrusion_entity_cnt);
}

void TravelObstacleTracker::mark_extruded(const ExtrusionEntity *extrusion_entity, size_t object_layer_idx, size_t instance_idx)
{
    if (extrusion_entity->role().is_external_perimeter())
        this->m_extruded_extrusion.insert({int(object_layer_idx), int(instance_idx), extrusion_entity});
}

bool TravelObstacleTracker::is_extruded(const ObjectOrExtrusionLinef &line) const
{
    return m_extruded_extrusion.find({line.object_layer_idx, line.instance_idx, line.extrusion_entity}) != m_extruded_extrusion.end();
}

} // namespace Slic3r::GCode

namespace Slic3r::GCode::Impl::Travels {

ElevatedTravelFormula::ElevatedTravelFormula(const ElevatedTravelParams &params)
    : smoothing_from(params.slope_end - params.blend_width / 2.0)
    , smoothing_to(params.slope_end + params.blend_width / 2.0)
    , blend_width(params.blend_width)
    , lift_height(params.lift_height)
    , slope_end(params.slope_end) {
    if (smoothing_from < 0) {
        smoothing_from = params.slope_end;
        smoothing_to = params.slope_end;
    }
}

double parabola(const double x, const double a, const double b, const double c) {
    return a * x * x + b * x + c;
}

double ElevatedTravelFormula::slope_function(double distance_from_start) const {
    if (distance_from_start < this->slope_end) {
        const double lift_percent = distance_from_start / this->slope_end;
        return lift_percent * this->lift_height;
    } else {
        return this->lift_height;
    }
}

double ElevatedTravelFormula::operator()(const double distance_from_start) const {
    if (distance_from_start > this->smoothing_from && distance_from_start < this->smoothing_to) {
        const double slope = this->lift_height / this->slope_end;

        // This is a part of a parabola going over a specific
        // range and with specific end slopes.
        const double a = -slope / 2.0 / this->blend_width;
        const double b = slope * this->smoothing_to / this->blend_width;
        const double c = this->lift_height + a * boost::math::pow<2>(this->smoothing_to);
        return parabola(distance_from_start, a, b, c);
    }
    return slope_function(distance_from_start);
}

Points3 generate_flat_travel(tcb::span<const Point> xy_path, const float elevation) {
    Points3 result;
    result.reserve(xy_path.size());
    for (const Point &point : xy_path) {
        result.emplace_back(point.x(), point.y(), scaled(elevation));
    }
    return result;
}

Vec2d place_at_segment(
    const Vec2d &current_point, const Vec2d &previous_point, const double distance
) {
    Vec2d direction = (current_point - previous_point).normalized();
    return previous_point + direction * distance;
}

std::vector<DistancedPoint> slice_xy_path(
    tcb::span<const Point> xy_path, tcb::span<const double> sorted_distances
) {
    assert(xy_path.size() >= 2);
    std::vector<DistancedPoint> result;
    result.reserve(xy_path.size() + sorted_distances.size());
    double total_distance{0};
    result.emplace_back(DistancedPoint{xy_path.front(), 0});
    Point previous_point = result.front().point;
    std::size_t offset{0};
    for (const Point &point : xy_path.subspan(1)) {
        Vec2d unscaled_point{unscaled(point)};
        Vec2d unscaled_previous_point{unscaled(previous_point)};
        const double current_segment_length = (unscaled_point - unscaled_previous_point).norm();
        for (const double distance_to_add : sorted_distances.subspan(offset)) {
            if (distance_to_add <= total_distance + current_segment_length) {
                Point to_place = scaled(place_at_segment(
                    unscaled_point, unscaled_previous_point, distance_to_add - total_distance
                ));
                if (to_place != previous_point && to_place != point) {
                    result.emplace_back(DistancedPoint{to_place, distance_to_add});
                }
                ++offset;
            } else {
                break;
            }
        }
        total_distance += current_segment_length;
        result.emplace_back(DistancedPoint{point, total_distance});
        previous_point = point;
    }
    return result;
}

Points3 generate_elevated_travel(
    const tcb::span<const Point> xy_path,
    const std::vector<double> &ensure_points_at_distances,
    const double initial_elevation,
    const std::function<double(double)> &elevation
) {
    Points3 result{};

    std::vector<DistancedPoint> extended_xy_path = slice_xy_path(xy_path, ensure_points_at_distances);
    result.reserve(extended_xy_path.size());

    for (const DistancedPoint &point : extended_xy_path) {
        result.emplace_back(
            point.point.x(), point.point.y(),
            scaled(initial_elevation + elevation(point.distance_from_start))
        );
    }

    return result;
}

struct Intersection
{
    int  object_layer_idx = -1;
    int  instance_idx     = -1;
    bool is_inside        = false;

    bool is_print_instance_equal(const ObjectOrExtrusionLinef &print_istance) {
        return this->object_layer_idx == print_istance.object_layer_idx && this->instance_idx == print_istance.instance_idx;
    }
};

double get_first_crossed_line_distance(
    tcb::span<const Line> xy_path,
    const AABBTreeLines::LinesDistancer<ObjectOrExtrusionLinef> &distancer,
    const ObjectsLayerToPrint &objects_to_print,
    const std::function<bool(const ObjectOrExtrusionLinef &)> &predicate,
    const bool ignore_starting_object_intersection
) {
    assert(!xy_path.empty());
    if (xy_path.empty())
        return std::numeric_limits<double>::max();

    const Point path_first_point = xy_path.front().a;
    double traversed_distance = 0;
    bool skip_intersection = ignore_starting_object_intersection;
    Intersection first_intersection;

    for (const Line &line : xy_path) {
        const ObjectOrExtrusionLinef                    unscaled_line = {unscaled(line.a), unscaled(line.b)};
        const std::vector<std::pair<Vec2d, size_t>>     intersections = distancer.intersections_with_line<true>(unscaled_line);

        if (intersections.empty())
            continue;

        if (!objects_to_print.empty() && ignore_starting_object_intersection && first_intersection.object_layer_idx == -1) {
            const ObjectOrExtrusionLinef &intersection_line = distancer.get_line(intersections.front().second);
            const Point shift = objects_to_print[intersection_line.object_layer_idx].layer()->object()->instances()[intersection_line.instance_idx].shift;
            const Point shifted_first_point = path_first_point - shift;
            const bool contain_first_point = expolygons_contain(objects_to_print[intersection_line.object_layer_idx].layer()->lslices, shifted_first_point);

            first_intersection = {intersection_line.object_layer_idx, intersection_line.instance_idx, contain_first_point};
        }

        for (const auto &intersection : intersections) {
            const ObjectOrExtrusionLinef &intersection_line = distancer.get_line(intersection.second);
            const double distance = traversed_distance + (unscaled_line.a - intersection.first).norm();
            if (distance <= EPSILON)
                continue;

            // There is only one external border for each object, so when we cross this border,
            // we definitely know that we are outside the object.
            if (skip_intersection && first_intersection.is_print_instance_equal(intersection_line) && first_intersection.is_inside) {
                skip_intersection = false;
                continue;
            }

            if (!predicate(intersection_line))
                continue;

            return distance;
        }

        traversed_distance += (unscaled_line.a - unscaled_line.b).norm();
    }

    return std::numeric_limits<double>::max();
}

double get_obstacle_adjusted_slope_end(const Lines &xy_path, const GCode::TravelObstacleTracker &obstacle_tracker) {
    const double previous_layer_crossed_line = get_first_crossed_line_distance(
        xy_path, obstacle_tracker.previous_layer_distancer(), obstacle_tracker.objects_to_print()
    );
    const double current_layer_crossed_line = get_first_crossed_line_distance(
        xy_path, obstacle_tracker.current_layer_distancer(), obstacle_tracker.objects_to_print(),
        [&obstacle_tracker](const ObjectOrExtrusionLinef &line) { return obstacle_tracker.is_extruded(line); }
    );

    return std::min(previous_layer_crossed_line, current_layer_crossed_line);
}

struct SmoothingParams
{
    double blend_width{};
    unsigned points_count{1};
};

SmoothingParams get_smoothing_params(
    const double lift_height,
    const double slope_end,
    unsigned extruder_id,
    const double travel_length,
    const FullPrintConfig &config
) {
    if (config.gcode_flavor != gcfMarlinFirmware)
        // Smoothing is supported only on Marlin.
        return {0, 1};

    const double slope = lift_height / slope_end;
    const double max_machine_z_velocity = config.machine_max_feedrate_z.get_at(extruder_id);
    const double max_xy_velocity =
        Vec2d{
            config.machine_max_feedrate_x.get_at(extruder_id),
            config.machine_max_feedrate_y.get_at(extruder_id)}
            .norm();

    const double xy_acceleration = config.machine_max_acceleration_travel.get_at(extruder_id);

    const double xy_acceleration_time = max_xy_velocity / xy_acceleration;
    const double xy_acceleration_distance = 1.0 / 2.0 * xy_acceleration *
        boost::math::pow<2>(xy_acceleration_time);

    if (travel_length < xy_acceleration_distance) {
        return {0, 1};
    }

    const double max_z_velocity = std::min(max_xy_velocity * slope, max_machine_z_velocity);
    const double deceleration_time = max_z_velocity /
        config.machine_max_acceleration_z.get_at(extruder_id);
    const double deceleration_xy_distance = deceleration_time * max_xy_velocity;

    const double blend_width = slope_end > deceleration_xy_distance / 2.0 ? deceleration_xy_distance :
                                                                          slope_end * 2.0;

    const unsigned points_count = blend_width > 0 ?
        std::ceil(max_z_velocity / config.machine_max_jerk_z.get_at(extruder_id)) :
        1;

    if (blend_width <= 0     // When there is no blend with, there is no need for smoothing.
        || points_count > 6  // That would be way to many points. Do not do it at all.
        || points_count <= 0 // Always return at least one point.
    )
        return {0, 1};

    return {blend_width, points_count};
}

ElevatedTravelParams get_elevated_traval_params(
    const Polyline& xy_path,
    const FullPrintConfig &config,
    const unsigned extruder_id,
    const GCode::TravelObstacleTracker &obstacle_tracker
) {
    ElevatedTravelParams elevation_params{};
    if (!config.travel_ramping_lift.get_at(extruder_id)) {
        elevation_params.slope_end = 0;
        elevation_params.lift_height = config.retract_lift.get_at(extruder_id);
        elevation_params.blend_width = 0;
        return elevation_params;
    }
    elevation_params.lift_height = config.travel_max_lift.get_at(extruder_id);

    const double slope_deg = config.travel_slope.get_at(extruder_id);

    if (slope_deg >= 90 || slope_deg <= 0) {
        elevation_params.slope_end = 0;
    } else {
        const double slope_rad = slope_deg * (M_PI / 180); // rad
        elevation_params.slope_end = elevation_params.lift_height / std::tan(slope_rad);
    }

    const double obstacle_adjusted_slope_end = get_obstacle_adjusted_slope_end(xy_path.lines(), obstacle_tracker);
    if (obstacle_adjusted_slope_end < elevation_params.slope_end)
        elevation_params.slope_end = obstacle_adjusted_slope_end;

    SmoothingParams smoothing_params{get_smoothing_params(
        elevation_params.lift_height, elevation_params.slope_end, extruder_id,
        unscaled(xy_path.length()), config
    )};

    elevation_params.blend_width = smoothing_params.blend_width;
    elevation_params.parabola_points_count = smoothing_params.points_count;
    return elevation_params;
}

std::vector<double> linspace(const double from, const double to, const unsigned count) {
    if (count == 0) {
        return {};
    }
    std::vector<double> result;
    result.reserve(count);
    if (count == 1) {
        result.emplace_back((from + to) / 2.0);
        return result;
    }
    const double step = (to - from) / count;
    for (unsigned i = 0; i < count - 1; ++i) {
        result.emplace_back(from + i * step);
    }
    result.emplace_back(to); // Make sure the last value is exactly equal to the value of "to".
    return result;
}

Points3 generate_travel_to_extrusion(
    const Polyline &xy_path,
    const FullPrintConfig &config,
    const unsigned extruder_id,
    const double initial_elevation,
    const GCode::TravelObstacleTracker &obstacle_tracker,
    const Point &xy_path_coord_origin
) {
    const double upper_limit = config.retract_lift_below.get_at(extruder_id);
    const double lower_limit = config.retract_lift_above.get_at(extruder_id);
    if ((lower_limit > 0 && initial_elevation < lower_limit) ||
        (upper_limit > 0 && initial_elevation > upper_limit)) {
        return generate_flat_travel(xy_path.points, initial_elevation);
    }

    Points global_xy_path;
    for (const Point &point : xy_path.points) {
        global_xy_path.emplace_back(point + xy_path_coord_origin);
    }

    ElevatedTravelParams elevation_params{get_elevated_traval_params(
        Polyline{std::move(global_xy_path)}, config, extruder_id, obstacle_tracker
    )};

    const std::vector<double> ensure_points_at_distances = linspace(
        elevation_params.slope_end - elevation_params.blend_width / 2.0,
        elevation_params.slope_end + elevation_params.blend_width / 2.0,
        elevation_params.parabola_points_count
    );
    Points3 result{generate_elevated_travel(
        xy_path.points, ensure_points_at_distances, initial_elevation,
        ElevatedTravelFormula{elevation_params}
    )};

    result.emplace_back(xy_path.back().x(), xy_path.back().y(), scaled(initial_elevation));
    return result;
}
} // namespace Slic3r::GCode::Impl::Travels
