#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/parallel_for.h>
#include <cstdint>
#include <iterator>
#include <tuple>
#include <limits>

#include <boost/hana/functional/overload_linearly.hpp>

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/GCode/SeamGeometry.hpp"
#include "libslic3r/GCode/SeamPerimeters.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/GCode/SeamPainting.hpp"
#include "libslic3r/MultiPoint.hpp"
#include "tcbspan/span.hpp"

namespace Slic3r::Seams::Perimeters::Impl {
PerimeterPoints oversample_painted(
    PerimeterPoints &points,
    const std::function<bool(Vec3f, double)> &is_painted,
    const double slice_z,
    const double max_distance
) {
    PerimeterPoints result;

    for (std::size_t index{0}; index < points.size(); ++index) {
        result.push_back(points[index]);

        const Vec2d &point{points[index].position};
        const std::size_t next_index{index == points.size() - 1 ? 0 : index + 1};
        const Vec2d &next_point{points[next_index].position};
        const float next_point_distance{static_cast<float>((point - next_point).norm())};
        const Vec2d middle_point{(point + next_point) / 2.0};
        Vec3f point3d{to_3d(middle_point, slice_z).cast<float>()};
        if (is_painted(point3d, next_point_distance / 2.0)) {
            for (const Vec2d &edge_point : Geometry::oversample_edge(point, next_point, max_distance)) {
                PerimeterPoint perimeter_point;
                perimeter_point.position = edge_point;
                if (points[next_index].classification != PointClassification::common) {
                    perimeter_point.classification = points[next_index].classification;
                }
                if (points[index].classification != PointClassification::common) {
                    perimeter_point.classification = points[index].classification;
                }
                result.push_back(std::move(perimeter_point));
            }
        }
    }
    return result;
}

PerimeterPoints remove_redundant_points(
    const PerimeterPoints &points,
    const double tolerance
) {
    PerimeterPoints result;

    auto range_start{points.begin()};

    for (auto iterator{points.begin()}; iterator != points.end(); ++iterator) {
        const std::int64_t index{std::distance(points.begin(), iterator)};
        if (
            next(iterator) == points.end()
            || points[index].type != points[index + 1].type
            || points[index].classification != points[index + 1].classification
        ) {
            douglas_peucker<double>(
                range_start, next(iterator), std::back_inserter(result), tolerance,
                [](const PerimeterPoint &point) {
                    return point.position;
                }
            );

            range_start = next(iterator);
        }
    }

    return result;
}

PerimeterPoints get_point_types(
    const PerimeterPoints &perimeter_points,
    const ModelInfo::Painting &painting,
    const double slice_z,
    const double painting_radius
) {
    PerimeterPoints result;
    result.reserve(perimeter_points.size());
    using std::transform, std::back_inserter;
    transform(
        perimeter_points.begin(), perimeter_points.end(), back_inserter(result),
        [&](PerimeterPoint point) {
            const Vec3f point3d{to_3d(point.position.cast<float>(), static_cast<float>(slice_z))};
            if (painting.is_blocked(point3d, painting_radius)) {
                point.type = PointType::blocker;
            } else if (painting.is_enforced(point3d, painting_radius)) {
                point.type = PointType::enforcer;
            } else {
                point.type = PointType::common;
            }
            return point;
        }
    );
    return result;
}

void project_overhang(
    PerimeterPoints &points,
    const AABBTreeLines::LinesDistancer<Linef> &points_distancer,
    const Geometry::Overhang &overhang,
    std::map<int, PerimeterPoints>& output
) {

    const auto [start_distance, start_line_index, start_point]{
        points_distancer.distance_from_lines_extra<false>(
            unscaled(overhang.start)
        )
    };

    PerimeterPoint common_start_point{};
    common_start_point.position = start_point;
    common_start_point.classification = PointClassification::common;
    output[start_line_index].push_back(common_start_point);

    PerimeterPoint perimeter_start_point{};
    perimeter_start_point.position = start_point;
    perimeter_start_point.classification = PointClassification::overhang;
    output[start_line_index].push_back(perimeter_start_point);


    const auto [end_distance, end_line_index, end_point]{
        points_distancer.distance_from_lines_extra<false>(
            unscaled(overhang.end)
        )
    };
    PerimeterPoint perimeter_end_point{};
    perimeter_end_point.position = end_point;
    perimeter_end_point.classification = PointClassification::overhang;
    output[end_line_index].push_back(perimeter_end_point);

    PerimeterPoint common_end_point{};
    common_end_point.position = end_point;
    common_end_point.classification = PointClassification::common;
    output[end_line_index].push_back(common_end_point);
}

double get_overhang_angle(
    const Vec2d& point,
    const AABBTreeLines::LinesDistancer<Linef> &previous_layer_perimeter_distancer,
    const double layer_height
) {
    const double distance{previous_layer_perimeter_distancer.distance_from_lines<true>(point)};
    return distance > 0 ? M_PI / 2 - std::atan(layer_height / distance) : 0.0;
}

Linesf to_lines(const PerimeterPoints &points) {
    Linesf lines;
    for (std::size_t i{0}; i < points.size(); ++i) {
        const std::size_t current_index{i};
        const std::size_t next_index{i == points.size() - 1 ? 0 : i + 1};
        const Vec2d a{points[current_index].position};
        const Vec2d b{points[next_index].position};
        lines.push_back(Linef{a, b});
    }
    return lines;
}

PerimeterPoints classify_overhangs(
    PerimeterPoints &&points,
    const Geometry::Overhangs &overhangs,
    const LayerInfo &layer_info,
    const double overhang_threshold
) {
    using boost::apply_visitor;
    using boost::hana::overload_linearly;

    PerimeterPoints classified_points{std::move(points)};

    if (!layer_info.previous_distancer) {
        return classified_points;
    }

    const AABBTreeLines::LinesDistancer<Linef> points_distancer{to_lines(classified_points)};

    std::map<int, PerimeterPoints> points_to_add_to_lines;
    for (const auto &overhang : overhangs) {
        apply_visitor(overload_linearly(
            [&](const Geometry::Overhang& overhang) {
                project_overhang(
                    classified_points,
                    points_distancer,
                    overhang,
                    points_to_add_to_lines
                );
            },
            [&](const Geometry::LoopOverhang&) {
                for (PerimeterPoint &point : classified_points) {
                    point.classification = PointClassification::overhang;
                }
            }
        ), overhang);
    }

    PerimeterPoints result;

    for (std::size_t i{0}; i < classified_points.size(); ++i) {
        PerimeterPoint &point{classified_points[i]};
        if (point.classification != PointClassification::overhang) {
            const double overhang_aangle{
                get_overhang_angle(point.position, *layer_info.previous_distancer, layer_info.height)};
            point.classification = overhang_aangle > overhang_threshold ?
                PointClassification::overhang :
                point.classification;
        }
        result.push_back(point);
        if (points_to_add_to_lines.count(i) > 0) {
            for (const PerimeterPoint &point : points_to_add_to_lines[i]) {
                result.push_back(point);
            }
        }
    }

    return result;
}

PerimeterPoints classify_points(
    PerimeterPoints &&points,
    const Geometry::Overhangs &overhangs,
    const double embedding_threshold,
    const LayerInfo& layer_info,
    const double overhang_threshold
) {
    PerimeterPoints result{classify_overhangs(std::move(points), overhangs, layer_info, overhang_threshold)};

    for (PerimeterPoint& point : result) {
        if (point.classification != PointClassification::common) {
            continue;
        }
        // This is an optimization avoiding distance_from_lines<true> which is expensive.
        const double embedding_distance{layer_info.distancer.distance_from_lines<false>(point.position)};
        if (embedding_distance < embedding_threshold) {
            continue;
        }
        if (layer_info.distancer.outside(point.position) == 1) {
            continue;
        }

        point.classification = PointClassification::embedded;
    }

    return result;
}

std::vector<AngleType> get_angle_types(
    const std::vector<double> &angles, const double convex_threshold, const double concave_threshold
) {
    std::vector<AngleType> result;
    using std::transform, std::back_inserter;
    transform(angles.begin(), angles.end(), back_inserter(result), [&](const double angle) {
        if (angle > convex_threshold) {
            return AngleType::convex;
        }
        if (angle < -concave_threshold) {
            return AngleType::concave;
        }
        return AngleType::smooth;
    });
    return result;
}

std::vector<AngleType> merge_angle_types(
    const std::vector<AngleType> &angle_types,
    const std::vector<AngleType> &smooth_angle_types,
    const std::vector<Vec2d> &points,
    const double min_arm_length
) {
    std::vector<AngleType> result;
    result.reserve(angle_types.size());
    for (std::size_t index{0}; index < angle_types.size(); ++index) {
        const AngleType &angle_type{angle_types[index]};
        const AngleType &smooth_angle_type{smooth_angle_types[index]};

        AngleType resulting_type{angle_type};

        if (smooth_angle_type != angle_type && smooth_angle_type != AngleType::smooth) {
            resulting_type = smooth_angle_type;

            // Check if there is a sharp angle in the vicinity. If so, do not use the smooth angle.
            Geometry::visit_forward(index, angle_types.size(), [&](const std::size_t forward_index) {
                const double distance{(points[forward_index] - points[index]).norm()};
                if (distance > min_arm_length) {
                    return true;
                }
                if (angle_types[forward_index] == smooth_angle_type) {
                    resulting_type = angle_type;
                }
                return false;
            });
            Geometry::visit_backward(index, angle_types.size(), [&](const std::size_t backward_index) {
                const double distance{(points[backward_index] - points[index]).norm()};
                if (distance > min_arm_length) {
                    return true;
                }
                if (angle_types[backward_index] == smooth_angle_type) {
                    resulting_type = angle_type;
                }
                return false;
            });
        }
        result.push_back(resulting_type);
    }
    return result;
}

PerimeterPoints get_perimeter_points(const std::vector<Vec2d> &points){
    PerimeterPoints perimeter_points;
    std::transform(
        points.begin(),
        points.end(),
        std::back_inserter(perimeter_points),
        [](const Vec2d &point){
            PerimeterPoint perimeter_point;
            perimeter_point.position = point;
            return perimeter_point;
        }
    );
    return perimeter_points;
}

} // namespace Slic3r::Seams::Perimeters::Impl

namespace Slic3r::Seams::Perimeters {

LayerInfos get_layer_infos(
    tcb::span<const Slic3r::Layer* const> object_layers, const double elephant_foot_compensation
) {
    LayerInfos result(object_layers.size());

    using Range = tbb::blocked_range<size_t>;
    const Range range{0, object_layers.size()};
    tbb::parallel_for(range, [&](Range range) {
        for (std::size_t layer_index{range.begin()}; layer_index < range.end(); ++layer_index) {
            result[layer_index] = LayerInfo::create(
                *object_layers[layer_index], layer_index, elephant_foot_compensation
            );
        }
    });
    return result;
}

LayerInfo LayerInfo::create(
    const Slic3r::Layer &object_layer,
    const std::size_t index,
    const double elephant_foot_compensation
) {
    AABBTreeLines::LinesDistancer<Linef> perimeter_distancer{
        to_unscaled_linesf({object_layer.lslices})};

    using PreviousLayerDistancer = std::optional<AABBTreeLines::LinesDistancer<Linef>>;
    PreviousLayerDistancer previous_layer_perimeter_distancer;
    if (object_layer.lower_layer != nullptr) {
        previous_layer_perimeter_distancer = PreviousLayerDistancer{
            to_unscaled_linesf(object_layer.lower_layer->lslices)};
    }

    return {
        std::move(perimeter_distancer),
        std::move(previous_layer_perimeter_distancer),
        index,
        object_layer.height,
        object_layer.slice_z,
        index == 0 ? elephant_foot_compensation : 0.0};
}

double Perimeter::IndexToCoord::operator()(const size_t index, size_t dim) const {
    return positions[index][dim];
}

Perimeter::PointTrees get_kd_trees(
    const PointType point_type,
    const std::vector<PointType> &all_point_types,
    const std::vector<PointClassification> &point_classifications,
    const Perimeter::IndexToCoord &index_to_coord
) {
    std::vector<std::size_t> overhang_indexes;
    std::vector<std::size_t> embedded_indexes;
    std::vector<std::size_t> common_indexes;
    for (std::size_t i{0}; i < all_point_types.size(); ++i) {
        if (all_point_types[i] == point_type) {
            switch (point_classifications[i]) {
            case PointClassification::overhang: overhang_indexes.push_back(i); break;
            case PointClassification::embedded: embedded_indexes.push_back(i); break;
            case PointClassification::common: common_indexes.push_back(i); break;
            }
        }
    }
    Perimeter::PointTrees trees;
    if (!overhang_indexes.empty()) {
        trees.overhanging_points = Perimeter::PointTree{index_to_coord};
        trees.overhanging_points->build(overhang_indexes);
    }
    if (!embedded_indexes.empty()) {
        trees.embedded_points = Perimeter::PointTree{index_to_coord};
        trees.embedded_points->build(embedded_indexes);
    }
    if (!common_indexes.empty()) {
        trees.common_points = Perimeter::PointTree{index_to_coord};
        trees.common_points->build(common_indexes);
    }
    return trees;
}

Perimeter::Perimeter(
    const double slice_z,
    const std::size_t layer_index,
    const bool is_hole,
    std::vector<Vec2d> &&positions,
    std::vector<double> &&angles,
    std::vector<PointType> &&point_types,
    std::vector<PointClassification> &&point_classifications,
    std::vector<AngleType> &&angle_types
)
    : slice_z(slice_z)
    , layer_index(layer_index)
    , is_hole(is_hole)
    , positions(std::move(positions))
    , angles(std::move(angles))
    , index_to_coord(IndexToCoord{tcb::span{this->positions}})
    , point_types(std::move(point_types))
    , point_classifications(std::move(point_classifications))
    , angle_types(std::move(angle_types))
    , enforced_points(get_kd_trees(
          PointType::enforcer, this->point_types, this->point_classifications, this->index_to_coord
      ))
    , common_points(get_kd_trees(
          PointType::common, this->point_types, this->point_classifications, this->index_to_coord
      ))
    , blocked_points(get_kd_trees(
          PointType::blocker, this->point_types, this->point_classifications, this->index_to_coord
      )) {}

Perimeter Perimeter::create_degenerate(
    std::vector<Vec2d> &&points, const double slice_z, const std::size_t layer_index
) {
    std::vector<PointType> point_types(points.size(), PointType::common);
    std::vector<PointClassification>
        point_classifications(points.size(), PointClassification::common);
    std::vector<double> angles(points.size());
    std::vector<AngleType> angle_types(points.size(), AngleType::smooth);
    Perimeter perimeter{
        slice_z,
        layer_index,
        false,
        std::move(points),
        std::move(angles),
        std::move(point_types),
        std::move(point_classifications),
        std::move(angle_types)};
    perimeter.is_degenerate = true;
    return perimeter;
}

Perimeter Perimeter::create(
    const Polygon &polygon,
    const Geometry::Overhangs &overhangs,
    const ModelInfo::Painting &painting,
    const LayerInfo &layer_info,
    const PerimeterParams &params
) {
    if (polygon.size() < 3) {
        return Perimeter::create_degenerate(
            Geometry::unscaled(polygon.points), layer_info.slice_z, layer_info.index
        );
    }
    std::vector<Vec2d> points;
    if (layer_info.elephant_foot_compensation > 0) {
        const Polygons expanded{expand(polygon, scaled(layer_info.elephant_foot_compensation))};
        if (expanded.empty()) {
            points = Geometry::unscaled(polygon.points);
        } else {
            points = Geometry::unscaled(expanded.front().points);
        }
    } else {
        points = Geometry::unscaled(polygon.points);
    }

    PerimeterPoints perimeter_points{Impl::get_perimeter_points(points)};

    perimeter_points = Impl::classify_points(
        std::move(perimeter_points),
        overhangs,
        params.embedding_threshold,
        layer_info,
        params.overhang_threshold
    );

    const auto is_painted{[&](const Vec3f &point, const double radius) {
        return painting.is_enforced(point, radius) || painting.is_blocked(point, radius);
    }};

    perimeter_points = Impl::oversample_painted(
        perimeter_points,
        is_painted,
        layer_info.slice_z,
        params.oversampling_max_distance
    );

    perimeter_points = Impl::get_point_types(perimeter_points, painting, layer_info.slice_z, params.painting_radius);

    perimeter_points = Impl::remove_redundant_points(perimeter_points, params.simplification_epsilon);

    std::vector<Vec2d> positions{};
    std::vector<PointType> point_types{};
    std::vector<PointClassification> point_classifications{};

    for (const PerimeterPoint &point : perimeter_points) {
        positions.push_back(point.position);
        point_types.push_back(point.type);
        point_classifications.push_back(point.classification);
    }

    std::vector<double> smooth_angles{Geometry::get_vertex_angles(positions, params.smooth_angle_arm_length)};
    std::vector<double> angles{Geometry::get_vertex_angles(positions, params.sharp_angle_arm_length)};
    std::vector<AngleType> angle_types{
        Impl::get_angle_types(angles, params.convex_threshold, params.concave_threshold)};
    std::vector<AngleType> smooth_angle_types{
        Impl::get_angle_types(smooth_angles, params.convex_threshold, params.concave_threshold)};
    angle_types = Impl::merge_angle_types(angle_types, smooth_angle_types, positions, params.smooth_angle_arm_length);

    const bool is_hole{polygon.is_clockwise()};

    return Perimeter{
        layer_info.slice_z,
        layer_info.index,
        is_hole,
        std::move(positions),
        std::move(angles),
        std::move(point_types),
        std::move(point_classifications),
        std::move(angle_types)};
}

LayerPerimeters create_perimeters(
    const std::vector<Geometry::BoundedPolygons> &polygons,
    const std::vector<LayerInfo> &layer_infos,
    const ModelInfo::Painting &painting,
    const PerimeterParams &params
) {
    LayerPerimeters result;
    result.reserve(polygons.size());

    std::transform(
        polygons.begin(), polygons.end(), std::back_inserter(result),
        [](const Geometry::BoundedPolygons &layer) { return BoundedPerimeters(layer.size()); }
    );

    Geometry::iterate_nested(
        polygons,
        [&](const std::size_t layer_index, const std::size_t polygon_index) {
            const Geometry::BoundedPolygons &layer{polygons[layer_index]};
            const Geometry::BoundedPolygon &bounded_polygon{layer[polygon_index]};
            const LayerInfo &layer_info{layer_infos[layer_index]};
            result[layer_index][polygon_index] = BoundedPerimeter{
                Perimeter::create(
                    bounded_polygon.polygon,
                    bounded_polygon.overhangs,
                    painting,
                    layer_info,
                    params
                ),
                bounded_polygon.bounding_box};
        }
    );
    return result;
}

std::optional<PointOnPerimeter> offset_along_perimeter(
    const PointOnPerimeter &point,
    const Perimeter& perimeter,
    const double offset,
    const Seams::Geometry::Direction1D direction,
    const std::function<bool(const Perimeter&, const std::size_t)> &early_stop_condition
) {
    using Dir = Seams::Geometry::Direction1D;

    const Linef initial_line{
        perimeter.positions[point.previous_index], perimeter.positions[point.next_index]};
    double distance{
        direction == Dir::forward ?
            (initial_line.b - point.position).norm() :
            (point.position - initial_line.a).norm()};

    if (distance >= offset) {
        const Vec2d edge_direction{(initial_line.b - initial_line.a).normalized()};
        const Vec2d offset_point{direction == Dir::forward ? Vec2d{point.position + offset * edge_direction} : Vec2d{point.position - offset * edge_direction}};
        return {{point.previous_index, point.next_index, offset_point}};
    }

    std::optional<PointOnPerimeter> offset_point;

    bool skip_first{direction == Dir::forward};
    const auto visitor{[&](std::size_t index) {
        if (skip_first) {
            skip_first = false;
            return false;
        }

        const std::size_t previous_index{
            direction == Dir::forward ?
                (index == 0 ? perimeter.positions.size() - 1 : index - 1) :
                (index == perimeter.positions.size() - 1 ? 0 : index + 1)};

        const Vec2d previous_point{perimeter.positions[previous_index]};
        const Vec2d next_point{perimeter.positions[index]};
        const Vec2d edge{next_point - previous_point};

        if (early_stop_condition(perimeter, index)) {
            offset_point = PointOnPerimeter{previous_index, previous_index, perimeter.positions[previous_index]};
            return true;
        }

        if (distance + edge.norm() > offset) {
            const double remaining_distance{offset - distance};
            const Vec2d result{previous_point + remaining_distance * edge.normalized()};

            if (direction == Dir::forward) {
                offset_point = PointOnPerimeter{previous_index, index, result};
            } else {
                offset_point = PointOnPerimeter{index, previous_index, result};
            }
            return true;
        }

        distance += edge.norm();

        return false;
    }};

    if (direction == Dir::forward) {
        Geometry::visit_forward(point.next_index, perimeter.positions.size(), visitor);
    } else {
        Geometry::visit_backward(point.previous_index, perimeter.positions.size(), visitor);
    }

    return offset_point;
}

unsigned get_point_value(const PointType point_type, const PointClassification point_classification) {
    // Better be explicit than smart.
    switch (point_type) {
    case PointType::enforcer:
        switch (point_classification) {
        case PointClassification::embedded: return 9;
        case PointClassification::common: return 8;
        case PointClassification::overhang: return 7;
        }
    case PointType::common:
        switch (point_classification) {
        case PointClassification::embedded: return 6;
        case PointClassification::common: return 5;
        case PointClassification::overhang: return 4;
        }
    case PointType::blocker:
        switch (point_classification) {
        case PointClassification::embedded: return 3;
        case PointClassification::common: return 2;
        case PointClassification::overhang: return 1;
        }
    }
    return 0;
}

} // namespace Slic3r::Seams::Perimeter
