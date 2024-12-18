///|/ Copyright (c) Prusa Research 2020 - 2023 Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena,
/// Pavel Mikuš @Godrak
///|/ Copyright (c) SuperSlicer 2023 Remi Durand @supermerill
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/

#include <boost/filesystem/operations.hpp>
#include <boost/property_tree/json_parser.hpp>

#include "SeamPlacer.hpp"

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/GCode/SeamShells.hpp"
#include "libslic3r/GCode/SeamAligned.hpp"
#include "libslic3r/GCode/SeamRear.hpp"
#include "libslic3r/GCode/SeamRandom.hpp"
#include "libslic3r/GCode/ModelVisibility.hpp"
#include "libslic3r/GCode/SeamGeometry.hpp"

namespace Slic3r::Seams {

using ObjectPainting = std::map<const PrintObject*, ModelInfo::Painting>;

ObjectLayerPerimeters get_perimeters(
    SpanOfConstPtrs<PrintObject> objects,
    const Params &params,
    const ObjectPainting& object_painting,
    const std::function<void(void)> &throw_if_canceled
) {
    ObjectLayerPerimeters result;

    for (const PrintObject *print_object : objects) {
        const ModelInfo::Painting &painting{object_painting.at(print_object)};
        throw_if_canceled();

        const std::vector<Geometry::Extrusions> extrusions{
            Geometry::get_extrusions(print_object->layers())};
        const Perimeters::LayerInfos layer_infos{Perimeters::get_layer_infos(
            print_object->layers(), params.perimeter.elephant_foot_compensation
        )};
        const std::vector<Geometry::BoundedPolygons> projected{
            print_object->config().seam_position == spRandom ?
            Geometry::convert_to_geometry(extrusions) :
            Geometry::project_to_geometry(extrusions, params.max_distance)
        };
        Perimeters::LayerPerimeters perimeters{Perimeters::create_perimeters(projected, layer_infos, painting, params.perimeter)};

        throw_if_canceled();
        result.emplace(print_object, std::move(perimeters));
    }
    return result;
}

Perimeters::LayerPerimeters sort_to_layers(Shells::Shells<> &&shells) {
    const std::size_t layer_count{Shells::get_layer_count(shells)};
    Perimeters::LayerPerimeters result(layer_count);

    for (Shells::Shell<> &shell : shells) {
        for (Shells::Slice<> &slice : shell) {
            const BoundingBox bounding_box{Geometry::scaled(slice.boundary.positions)};
            result[slice.layer_index].push_back(
                Perimeters::BoundedPerimeter{std::move(slice.boundary), bounding_box}
            );
        }
    }
    return result;
}

ObjectSeams precalculate_seams(
    const Params &params,
    ObjectLayerPerimeters &&seam_data,
    const std::function<void(void)> &throw_if_canceled
) {
    ObjectSeams result;

    for (auto &[print_object, layer_perimeters] : seam_data) {
        switch (print_object->config().seam_position.value) {
        case spAligned: {
            const Transform3d transformation{print_object->trafo_centered()};
            const ModelVolumePtrs &volumes{print_object->model_object()->volumes};

            Slic3r::ModelInfo::Visibility
                points_visibility{transformation, volumes, params.visibility, throw_if_canceled};
            throw_if_canceled();
            const Aligned::VisibilityCalculator visibility_calculator{
                points_visibility, params.convex_visibility_modifier,
                params.concave_visibility_modifier};

            Shells::Shells<> shells{Shells::create_shells(std::move(layer_perimeters), params.max_distance)};
            result[print_object] = Aligned::get_object_seams(
                std::move(shells), visibility_calculator, params.aligned
            );
            break;
        }
        case spRear: {
            result[print_object] = Rear::get_object_seams(std::move(layer_perimeters), params.rear_tolerance, params.rear_y_offset);
            break;
        }
        case spRandom: {
            result[print_object] = Random::get_object_seams(std::move(layer_perimeters), params.random_seed);
            break;
        }
        case spNearest: {
            // Do not precalculate anything.
            break;
        }
        }
        throw_if_canceled();
    }
    return result;
}

Params Placer::get_params(const DynamicPrintConfig &config) {
    Params params{};

    params.perimeter.elephant_foot_compensation = config.opt_float("elefant_foot_compensation");
    if (config.opt_int("raft_layers") > 0) {
        params.perimeter.elephant_foot_compensation = 0.0;
    }
    params.random_seed = 1653710332u;

    params.aligned.max_detour = 1.0;
    params.aligned.continuity_modifier = 2.0;
    params.convex_visibility_modifier = 1.1;
    params.concave_visibility_modifier = 0.9;
    params.perimeter.overhang_threshold = Slic3r::Geometry::deg2rad(55.0);
    params.perimeter.convex_threshold = Slic3r::Geometry::deg2rad(10.0);
    params.perimeter.concave_threshold = Slic3r::Geometry::deg2rad(15.0);

    params.staggered_inner_seams = config.opt_bool("staggered_inner_seams");

    params.max_nearest_detour = 1.0;
    params.rear_tolerance = 1.0;
    params.rear_y_offset = 20;
    params.aligned.jump_visibility_threshold = 0.6;
    params.max_distance = 5.0;
    params.perimeter.oversampling_max_distance = 0.2;
    params.perimeter.embedding_threshold = 0.5;
    params.perimeter.painting_radius = 0.1;
    params.perimeter.simplification_epsilon = 0.001;
    params.perimeter.smooth_angle_arm_length = 0.5;
    params.perimeter.sharp_angle_arm_length = 0.25;

    params.visibility.raycasting_visibility_samples_count = 30000;
    params.visibility.fast_decimation_triangle_count_target = 16000;
    params.visibility.sqr_rays_per_sample_point = 5;

    return params;
}

void Placer::init(
    SpanOfConstPtrs<PrintObject> objects,
    const Params &params,
    const std::function<void(void)> &throw_if_canceled
) {
    BOOST_LOG_TRIVIAL(debug) << "SeamPlacer: init: start";

    ObjectPainting object_painting;
    for (const PrintObject *print_object : objects) {
        const Transform3d transformation{print_object->trafo_centered()};
        const ModelVolumePtrs &volumes{print_object->model_object()->volumes};
        object_painting.emplace(print_object, ModelInfo::Painting{transformation, volumes});
    }

    ObjectLayerPerimeters perimeters{get_perimeters(objects, params, object_painting, throw_if_canceled)};
    ObjectLayerPerimeters perimeters_for_precalculation;

    for (auto &[print_object, layer_perimeters] : perimeters) {
        if (print_object->config().seam_position.value == spNearest) {
            this->perimeters_per_layer[print_object] = std::move(layer_perimeters);
        } else {
            perimeters_for_precalculation[print_object] = std::move(layer_perimeters);
        }
    }

    this->params = params;
    this->seams_per_object = precalculate_seams(params, std::move(perimeters_for_precalculation), throw_if_canceled);

    BOOST_LOG_TRIVIAL(debug) << "SeamPlacer: init: end";
}

const SeamPerimeterChoice &choose_closest_seam(
    const std::vector<SeamPerimeterChoice> &seams, const Polygon &loop_polygon
) {
    BoundingBoxes choose_from;
    choose_from.reserve(seams.size());
    for (const SeamPerimeterChoice &choice : seams) {
        choose_from.push_back(choice.bounding_box);
    }

    const std::size_t choice_index{
        Geometry::pick_closest_bounding_box(loop_polygon.bounding_box(), choose_from).first};

    return seams[choice_index];
}

std::pair<std::size_t, Vec2d> project_to_extrusion_loop(
    const SeamChoice &seam_choice,
    const Perimeters::Perimeter &perimeter,
    const AABBTreeLines::LinesDistancer<Linef> &distancer
) {
    const bool is_at_vertex{seam_choice.previous_index == seam_choice.next_index};
    const Vec2d edge{
        perimeter.positions[seam_choice.next_index] -
        perimeter.positions[seam_choice.previous_index]};
    const Vec2d normal{
        is_at_vertex ?
            Geometry::get_polygon_normal(perimeter.positions, seam_choice.previous_index, 0.1) :
            Geometry::get_normal(edge)};

    double depth{distancer.distance_from_lines<false>(seam_choice.position)};
    const Vec2d final_position{seam_choice.position - normal * depth};

    auto [_, loop_line_index, loop_point] = distancer.distance_from_lines_extra<false>(final_position
    );
    return {loop_line_index, loop_point};
}

double get_angle(const SeamChoice &seam_choice, const Perimeters::Perimeter &perimeter) {
    const bool is_at_vertex{seam_choice.previous_index == seam_choice.next_index};
    return is_at_vertex ? perimeter.angles[seam_choice.previous_index] : 0.0;
}

SeamChoice to_seam_choice(
    const Geometry::PointOnLine &point_on_line, const Perimeters::Perimeter &perimeter
) {
    SeamChoice result;

    result.position = point_on_line.point;
    result.previous_index = point_on_line.line_index;
    result.next_index = point_on_line.line_index == perimeter.positions.size() - 1 ?
        0 :
        point_on_line.line_index + 1;
    return result;
}

boost::variant<Point, Scarf::Scarf> finalize_seam_position(
    const ExtrusionLoop &loop,
    const PrintRegion *region,
    SeamChoice seam_choice,
    const Perimeters::Perimeter &perimeter,
    const bool staggered_inner_seams,
    const bool flipped
) {
    const Polygon loop_polygon{Geometry::to_polygon(loop)};
    const bool do_staggering{staggered_inner_seams && loop.role() == ExtrusionRole::Perimeter};
    const double loop_width{loop.paths.empty() ? 0.0 : loop.paths.front().width()};

    const ExPolygon perimeter_polygon{Geometry::scaled(perimeter.positions)};
    const Linesf perimeter_lines{to_unscaled_linesf({perimeter_polygon})};
    const Linesf loop_lines{to_unscaled_linesf({ExPolygon{loop_polygon}})};
    const AABBTreeLines::LinesDistancer<Linef> distancer{loop_lines};

    auto [loop_line_index, loop_point]{
        project_to_extrusion_loop(seam_choice, perimeter, distancer)};

    const Geometry::Direction1D offset_direction{
        flipped ? Geometry::Direction1D::forward : Geometry::Direction1D::backward};

    // ExtrusionRole::Perimeter is inner perimeter.
    if (do_staggering) {
        const double depth = (loop_point - seam_choice.position).norm() -
            loop_width / 2.0;

        const double staggering_offset{depth};

        std::optional<Geometry::PointOnLine> staggered_point{Geometry::offset_along_lines(
            loop_point, seam_choice.previous_index, perimeter_lines, staggering_offset,
            offset_direction
        )};

        if (staggered_point) {
            seam_choice = to_seam_choice(*staggered_point, perimeter);
            std::tie(loop_line_index, loop_point) = project_to_extrusion_loop(seam_choice, perimeter, distancer);
        }
    }

    bool place_scarf_seam {
        region->config().scarf_seam_placement == ScarfSeamPlacement::everywhere
        || (region->config().scarf_seam_placement == ScarfSeamPlacement::countours && !perimeter.is_hole)
    };
    const bool is_smooth{
        seam_choice.previous_index != seam_choice.next_index ||
        perimeter.angle_types[seam_choice.previous_index] == Perimeters::AngleType::smooth
    };

    if (region->config().scarf_seam_only_on_smooth && !is_smooth) {
        place_scarf_seam = false;
    }

    if (region->config().scarf_seam_length.value <= std::numeric_limits<double>::epsilon()) {
        place_scarf_seam = false;
    }

    if (place_scarf_seam) {
        Scarf::Scarf scarf{};
        scarf.entire_loop = region->config().scarf_seam_entire_loop;
        scarf.max_segment_length = region->config().scarf_seam_max_segment_length;
        scarf.start_height = std::min(region->config().scarf_seam_start_height.get_abs_value(1.0), 1.0);

        const double offset{scarf.entire_loop ? 0.0 : region->config().scarf_seam_length.value};
        const std::optional<Geometry::PointOnLine> outter_scarf_start_point{Geometry::offset_along_lines(
            seam_choice.position,
            seam_choice.previous_index,
            perimeter_lines,
            offset,
            offset_direction
        )};
        if (!outter_scarf_start_point) {
            return scaled(loop_point);
        }

        if (loop.role() != ExtrusionRole::Perimeter) { // Outter perimeter
            scarf.start_point = scaled(project_to_extrusion_loop(
                to_seam_choice(*outter_scarf_start_point, perimeter),
                perimeter,
                distancer
            ).second);
            scarf.end_point = scaled(loop_point);
            scarf.end_point_previous_index = loop_line_index;
            return scarf;
        } else {
            Geometry::PointOnLine inner_scarf_end_point{
                *outter_scarf_start_point
            };

            if (region->config().external_perimeters_first.value) {
                const auto external_first_offset_direction{
                    offset_direction == Geometry::Direction1D::forward ?
                    Geometry::Direction1D::backward :
                    Geometry::Direction1D::forward
                };
                if (auto result{Geometry::offset_along_lines(
                    seam_choice.position,
                    seam_choice.previous_index,
                    perimeter_lines,
                    offset,
                    external_first_offset_direction
                )}) {
                    inner_scarf_end_point = *result;
                } else {
                    return scaled(seam_choice.position);
                }
            }

            if (!region->config().scarf_seam_on_inner_perimeters) {
                return scaled(inner_scarf_end_point.point);
            }

            const std::optional<Geometry::PointOnLine> inner_scarf_start_point{Geometry::offset_along_lines(
                inner_scarf_end_point.point,
                inner_scarf_end_point.line_index,
                perimeter_lines,
                offset,
                offset_direction
            )};

            if (!inner_scarf_start_point) {
                return scaled(inner_scarf_end_point.point);
            }

            scarf.start_point = scaled(project_to_extrusion_loop(
                to_seam_choice(*inner_scarf_start_point, perimeter),
                perimeter,
                distancer
            ).second);

            const auto [end_point_previous_index, end_point]{project_to_extrusion_loop(
                to_seam_choice(inner_scarf_end_point, perimeter),
                perimeter,
                distancer
            )};
            scarf.end_point = scaled(end_point);
            scarf.end_point_previous_index = end_point_previous_index;
            return scarf;
        }
    }

    return scaled(loop_point);
}

struct NearestCorner {
    Vec2d prefered_position;

    std::optional<SeamChoice> operator()(
        const Perimeters::Perimeter &perimeter,
        const Perimeters::PointType point_type,
        const Perimeters::PointClassification point_classification
    ) const {
        std::optional<SeamChoice> corner_candidate;
        double min_distance{std::numeric_limits<double>::infinity()};
        for (std::size_t i{0}; i < perimeter.positions.size(); ++i) {
            if (perimeter.point_types[i] == point_type &&
                perimeter.point_classifications[i] == point_classification &&
                perimeter.angle_types[i] != Perimeters::AngleType::smooth) {
                const Vec2d &point{perimeter.positions[i]};
                const double distance{(prefered_position - point).norm()};
                if (!corner_candidate || distance < min_distance) {
                    corner_candidate = {i, i, point};
                    min_distance = distance;
                }
            }
        }
        return corner_candidate;
    }
};

std::pair<SeamChoice, std::size_t> place_seam_near(
    const std::vector<Perimeters::BoundedPerimeter> &layer_perimeters,
    const ExtrusionLoop &loop,
    const Point &position,
    const double max_detour
) {
    BoundingBoxes choose_from;
    choose_from.reserve(layer_perimeters.size());
    for (const Perimeters::BoundedPerimeter &perimeter : layer_perimeters) {
        choose_from.push_back(perimeter.bounding_box);
    }

    const Polygon loop_polygon{Geometry::to_polygon(loop)};

    const std::size_t choice_index{
        Geometry::pick_closest_bounding_box(loop_polygon.bounding_box(), choose_from).first};

    const NearestCorner nearest_corner{unscaled(position)};
    const std::optional<SeamChoice> corner_choice{
        Seams::maybe_choose_seam_point(layer_perimeters[choice_index].perimeter, nearest_corner)};

    if (corner_choice) {
        return {*corner_choice, choice_index};
    }

    const Seams::Aligned::Impl::Nearest nearest{unscaled(position), max_detour};
    const SeamChoice nearest_choice{
        Seams::choose_seam_point(layer_perimeters[choice_index].perimeter, nearest)};

    return {nearest_choice, choice_index};
}

int get_perimeter_count(const Layer *layer){
    int count{0};
    for (const LayerRegion *layer_region : layer->regions()) {
        for (const ExtrusionEntity *ex_entity : layer_region->perimeters()) {
            if (ex_entity->is_collection()) { //collection of inner, outer, and overhang perimeters
                count += static_cast<const ExtrusionEntityCollection*>(ex_entity)->entities.size();
            }
            else {
                count += 1;
            }
        }
    }
    return count;
}

boost::variant<Point, Scarf::Scarf> Placer::place_seam(
    const Layer *layer, const PrintRegion *region, const ExtrusionLoop &loop, const bool flipped, const Point &last_pos
) const {
    const PrintObject *po = layer->object();
    // Must not be called with supprot layer.
    assert(dynamic_cast<const SupportLayer *>(layer) == nullptr);
    // Object layer IDs are incremented by the number of raft layers.
    assert(layer->id() >= po->slicing_parameters().raft_layers());
    const size_t layer_index = layer->id() - po->slicing_parameters().raft_layers();

    if (po->config().seam_position.value == spNearest) {
        const std::vector<Perimeters::BoundedPerimeter> &perimeters{
            this->perimeters_per_layer.at(po)[layer_index]};
        const auto [seam_choice, perimeter_index] =
            place_seam_near(perimeters, loop, last_pos, this->params.max_nearest_detour);
        return finalize_seam_position(
            loop, region, seam_choice, perimeters[perimeter_index].perimeter,
            this->params.staggered_inner_seams, flipped
        );
    } else {
        const std::vector<SeamPerimeterChoice> &seams_on_perimeters{this->seams_per_object.at(po)[layer_index]};

        // Special case.
        // If there are only two perimeters and the current perimeter is hole (clockwise).
        const int perimeter_count{get_perimeter_count(layer)};
        const bool has_2_or_3_perimeters{perimeter_count == 2 || perimeter_count == 3};
        if (has_2_or_3_perimeters) {
            if (seams_on_perimeters.size() == 2 &&
                seams_on_perimeters[0].perimeter.is_hole !=
                    seams_on_perimeters[1].perimeter.is_hole) {
                const SeamPerimeterChoice &seam_perimeter_choice{
                    seams_on_perimeters[0].perimeter.is_hole ? seams_on_perimeters[1] :
                                                               seams_on_perimeters[0]};
                return finalize_seam_position(
                    loop, region, seam_perimeter_choice.choice, seam_perimeter_choice.perimeter,
                    this->params.staggered_inner_seams, flipped
                );
            }
        }

        const SeamPerimeterChoice &seam_perimeter_choice{
            choose_closest_seam(seams_on_perimeters, Geometry::to_polygon(loop))};
        return finalize_seam_position(
            loop, region, seam_perimeter_choice.choice, seam_perimeter_choice.perimeter,
            this->params.staggered_inner_seams, flipped
        );
    }
}
} // namespace Slic3r::Seams
