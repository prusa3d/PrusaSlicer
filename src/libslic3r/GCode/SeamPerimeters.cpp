#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/parallel_for.h>
#include <cstdint>
#include <iterator>
#include <tuple>
#include <limits>

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/GCode/SeamGeometry.hpp"
#include "libslic3r/GCode/SeamPerimeters.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/GCode/SeamPainting.hpp"
#include "libslic3r/MultiPoint.hpp"
#include "tcbspan/span.hpp"

namespace Slic3r::Seams::Perimeters::Impl {

std::vector<Vec2d> oversample_painted(
    const std::vector<Vec2d> &points,
    const std::function<bool(Vec3f, double)> &is_painted,
    const double slice_z,
    const double max_distance
) {
    std::vector<Vec2d> result;

    for (std::size_t index{0}; index < points.size(); ++index) {
        const Vec2d &point{points[index]};

        result.push_back(point);

        const std::size_t next_index{index == points.size() - 1 ? 0 : index + 1};
        const Vec2d &next_point{points[next_index]};
        const float next_point_distance{static_cast<float>((point - next_point).norm())};
        const Vec2d middle_point{(point + next_point) / 2.0};
        Vec3f point3d{to_3d(middle_point, slice_z).cast<float>()};
        if (is_painted(point3d, next_point_distance / 2.0)) {
            for (const Vec2d &edge_point :
                 Geometry::oversample_edge(point, next_point, max_distance)) {
                result.push_back(edge_point);
            }
        }
    }
    return result;
}

std::pair<std::vector<Vec2d>, std::vector<PointType>> remove_redundant_points(
    const std::vector<Vec2d> &points,
    const std::vector<PointType> &point_types,
    const double tolerance
) {
    std::vector<Vec2d> points_result;
    std::vector<PointType> point_types_result;

    auto range_start{points.begin()};

    for (auto iterator{points.begin()}; iterator != points.end(); ++iterator) {
        const std::int64_t index{std::distance(points.begin(), iterator)};
        if (next(iterator) == points.end() || point_types[index] != point_types[index + 1]) {
            std::vector<Vec2d> simplification_result;
            douglas_peucker<double>(
                range_start, next(iterator), std::back_inserter(simplification_result), tolerance,
                [](const Vec2d &point) { return point; }
            );

            points_result.insert(
                points_result.end(), simplification_result.begin(), simplification_result.end()
            );
            const std::vector<PointType>
                point_types_to_add(simplification_result.size(), point_types[index]);
            point_types_result.insert(
                point_types_result.end(), point_types_to_add.begin(), point_types_to_add.end()
            );

            range_start = next(iterator);
        }
    }

    return {points_result, point_types_result};
}

std::vector<PointType> get_point_types(
    const std::vector<Vec2d> &positions,
    const ModelInfo::Painting &painting,
    const double slice_z,
    const double painting_radius
) {
    std::vector<PointType> result;
    result.reserve(positions.size());
    using std::transform, std::back_inserter;
    transform(
        positions.begin(), positions.end(), back_inserter(result),
        [&](const Vec2d &point) {
            const Vec3f point3d{to_3d(point.cast<float>(), static_cast<float>(slice_z))};
            if (painting.is_blocked(point3d, painting_radius)) {
                return PointType::blocker;
            }
            if (painting.is_enforced(point3d, painting_radius)) {
                return PointType::enforcer;
            }
            return PointType::common;
        }
    );
    return result;
}

std::vector<PointClassification> classify_points(
    const std::vector<double> &embeddings,
    const std::optional<std::vector<double>> &overhangs,
    const double overhang_threshold,
    const double embedding_threshold
) {
    std::vector<PointClassification> result;
    result.reserve(embeddings.size());
    using std::transform, std::back_inserter;
    transform(
        embeddings.begin(), embeddings.end(), back_inserter(result),
        [&, i = 0](const double embedding) mutable {
            const unsigned index = i++;
            if (overhangs && overhangs->operator[](index) > overhang_threshold) {
                return PointClassification::overhang;
            }
            if (embedding > embedding_threshold) {
                return PointClassification::embedded;
            }
            return PointClassification::common;
        }
    );
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
            Geometry::visit_near_forward(index, angle_types.size(), [&](const std::size_t forward_index) {
                const double distance{(points[forward_index] - points[index]).norm()};
                if (distance > min_arm_length) {
                    return true;
                }
                if (angle_types[forward_index] == smooth_angle_type) {
                    resulting_type = angle_type;
                }
                return false;
            });
            Geometry::visit_near_backward(index, angle_types.size(), [&](const std::size_t backward_index) {
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
    const ModelInfo::Painting &painting,
    const LayerInfo &layer_info,
    const PerimeterParams &params,
    const double offset_inside
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

    auto is_painted{[&](const Vec3f &point, const double radius) {
        return painting.is_enforced(point, radius) || painting.is_blocked(point, radius);
    }};

    std::vector<Vec2d> perimeter_points{
        Impl::oversample_painted(points, is_painted, layer_info.slice_z, params.oversampling_max_distance)};

    std::vector<PointType> point_types{
        Impl::get_point_types(perimeter_points, painting, layer_info.slice_z, offset_inside > 0 ? offset_inside * 2 : params.painting_radius)};

    // Geometry converted from extrusions has non zero offset_inside.
    // Do not remomve redundant points for extrusions, becouse the redundant
    // points can be on overhangs.
    if (offset_inside < std::numeric_limits<double>::epsilon()) {
        // The following is optimization with significant impact. If in doubt, run
        // the "Seam benchmarks" test case in fff_print_tests.
        std::tie(perimeter_points, point_types) =
            Impl::remove_redundant_points(perimeter_points, point_types, params.simplification_epsilon);
    }

    const std::vector<double> embeddings{
        Geometry::get_embedding_distances(perimeter_points, layer_info.distancer)};
    std::optional<std::vector<double>> overhangs;
    if (layer_info.previous_distancer) {
        overhangs = Geometry::get_overhangs(
            perimeter_points, *layer_info.previous_distancer, layer_info.height
        );
    }
    std::vector<PointClassification> point_classifications{
        Impl::classify_points(embeddings, overhangs, params.overhang_threshold, params.embedding_threshold)};

    std::vector<double> smooth_angles{Geometry::get_vertex_angles(perimeter_points, params.smooth_angle_arm_length)};
    std::vector<double> angles{Geometry::get_vertex_angles(perimeter_points, params.sharp_angle_arm_length)};
    std::vector<AngleType> angle_types{
        Impl::get_angle_types(angles, params.convex_threshold, params.concave_threshold)};
    std::vector<AngleType> smooth_angle_types{
        Impl::get_angle_types(smooth_angles, params.convex_threshold, params.concave_threshold)};
    angle_types = Impl::merge_angle_types(angle_types, smooth_angle_types, perimeter_points, params.smooth_angle_arm_length);

    const bool is_hole{polygon.is_clockwise()};

    return Perimeter{
        layer_info.slice_z,
        layer_info.index,
        is_hole,
        std::move(perimeter_points),
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
                Perimeter::create(bounded_polygon.polygon, painting, layer_info, params, bounded_polygon.offset_inside),
                bounded_polygon.bounding_box};
        }
    );
    return result;
}

} // namespace Slic3r::Seams::Perimeter
