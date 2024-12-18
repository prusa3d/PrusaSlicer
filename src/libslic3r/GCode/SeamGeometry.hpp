#ifndef libslic3r_SeamGeometry_hpp_
#define libslic3r_SeamGeometry_hpp_

#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/parallel_for.h>
#include <vector>
#include <algorithm>
#include <cstddef>
#include <functional>
#include <numeric>
#include <optional>
#include <utility>

#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/AABBTreeLines.hpp"
#include "libslic3r/Point.hpp"
#include "tcbspan/span.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/Polygon.hpp"

namespace Slic3r {
class Layer;

namespace AABBTreeLines {
template <typename LineType> class LinesDistancer;
}  // namespace AABBTreeLines
}

namespace Slic3r::Seams::Geometry {

struct Extrusion
{
    Extrusion(
        Polygon &&polygon,
        BoundingBox bounding_box,
        const double width,
        const ExPolygon &island_boundary
    );

    Extrusion(const Extrusion &) = delete;
    Extrusion(Extrusion &&) = default;
    Extrusion &operator=(const Extrusion &) = delete;
    Extrusion &operator=(Extrusion &&) = delete;

    Polygon polygon;
    BoundingBox bounding_box;
    double width;
    const ExPolygon &island_boundary;

    // At index 0 there is the bounding box of contour. Rest are the bounding boxes of holes in order.
    BoundingBoxes island_boundary_bounding_boxes;
};

using Extrusions = std::vector<Extrusion>;

std::vector<Extrusions> get_extrusions(tcb::span<const Slic3r::Layer *const> object_layers);

struct BoundedPolygon {
    Polygon polygon;
    BoundingBox bounding_box;
    bool is_hole{false};
    double offset_inside{};
};

using BoundedPolygons = std::vector<BoundedPolygon>;

BoundedPolygons project_to_geometry(const Geometry::Extrusions &external_perimeters, const double max_bb_distance);
std::vector<BoundedPolygons> project_to_geometry(const std::vector<Geometry::Extrusions> &extrusions, const double max_bb_distance);
std::vector<BoundedPolygons> convert_to_geometry(const std::vector<Geometry::Extrusions> &extrusions);

Vec2d get_polygon_normal(
    const std::vector<Vec2d> &points, const std::size_t index, const double min_arm_length
);

Vec2d get_normal(const Vec2d &vector);

std::pair<Vec2d, double> distance_to_segment_squared(const Linef &segment, const Vec2d &point);

using Mapping = std::vector<std::vector<std::size_t>>;
using MappingOperatorResult = std::optional<std::pair<std::size_t, double>>;
using MappingOperator = std::function<MappingOperatorResult(std::size_t, std::size_t)>;

/**
 * @brief Indirectly map list of lists into buckets.
 *
 * Look for chains of items accross the lists.
 * It may do this mapping: [[1, 2], [3, 4, 5], [6]] -> [[1, 4, 6], [2, 3], [5]].
 * It depends on the weights provided by the mapping operator.
 *
 * Same bucket cannot be choosen for multiple items in any of the inner lists.
 * Bucket is choosen **based on the weight** provided by the mapping operator. Multiple items from
 * the same list may want to claim the same bucket. In that case, the item with the biggest weight
 * wins the bucket. For example: [[1, 2], [3]] -> [[1, 3], [2]]
 *
 * @param list_sizes Vector of sizes of the original lists in a list.
 * @param mapping_operator Operator that takes layer index and item index on that layer as input
 * and returns the best fitting item index from the next layer, along with weight, representing how
 * good the fit is. It may return nullopt if there is no good fit.
 *
 * @return Mapping [outter_list_index][inner_list_index] -> bucket id and the number of buckets.
 */
std::pair<Mapping, std::size_t> get_mapping(
    const std::vector<std::size_t> &list_sizes, const MappingOperator &mapping_operator
);

std::vector<Vec2d> oversample_edge(const Vec2d &from, const Vec2d &to, const double max_distance);

template <typename NestedVector>
std::size_t get_flat_size(const NestedVector &nested_vector) {
    return std::accumulate(
        nested_vector.begin(), nested_vector.end(), std::size_t{0},
        [](const std::size_t sum, const auto &vector) { return sum + vector.size(); }
    );
}

template<typename NestedVector>
std::vector<std::pair<std::size_t, std::size_t>> get_flat_index2indices_table(
    const NestedVector &nested_vector
) {
    std::vector<std::pair<std::size_t, std::size_t>> result;
    for (std::size_t parent_index{0}; parent_index < nested_vector.size(); ++parent_index) {
        const auto &vector{nested_vector[parent_index]};
        for (std::size_t nested_index{0}; nested_index < vector.size(); ++nested_index) {
            result.push_back({parent_index, nested_index});
        }
    }
    return result;
}

template <typename NestedVector>
void iterate_nested(const NestedVector &nested_vector, const std::function<void(std::size_t, std::size_t)> &function) {
    std::size_t flat_size{Geometry::get_flat_size(nested_vector)};
    using Range = tbb::blocked_range<size_t>;
    const Range range{0, flat_size};

    std::vector<std::pair<std::size_t, std::size_t>> index_table{
        get_flat_index2indices_table(nested_vector)};

    // Iterate the shells as if it was flat.
    tbb::parallel_for(range, [&](Range range) {
        for (std::size_t index{range.begin()}; index < range.end(); ++index) {
            const auto[parent_index, nested_index]{index_table[index]};
            function(parent_index, nested_index);
        }
    });
}

void visit_forward(
    const std::size_t start_index,
    const std::size_t loop_size,
    const std::function<bool(std::size_t)> &visitor
);
void visit_backward(
    const std::size_t start_index,
    const std::size_t loop_size,
    const std::function<bool(std::size_t)> &visitor
);

std::vector<Vec2d> unscaled(const Points &points);

std::vector<Linef> unscaled(const Lines &lines);

Points scaled(const std::vector<Vec2d> &points);

std::vector<double> get_embedding_distances(
    const std::vector<Vec2d> &points, const AABBTreeLines::LinesDistancer<Linef> &perimeter_distancer
);

/**
 * @brief Calculate overhang angle for each of the points over the previous layer perimeters.
 *
 * Larger angle <=> larger overhang. E.g. floating box has overhang = PI / 2.
 *
 * @returns Angles in radians <0, PI / 2>.
 */
std::vector<double> get_overhangs(
    const std::vector<Vec2d> &points,
    const AABBTreeLines::LinesDistancer<Linef> &previous_layer_perimeter_distancer,
    const double layer_height
);

// Measured from outside, convex is positive
std::vector<double> get_vertex_angles(const std::vector<Vec2d> &points, const double min_arm_length);

double bounding_box_distance(const BoundingBox &a, const BoundingBox &b);

std::pair<std::size_t, double> pick_closest_bounding_box(
    const BoundingBox &to, const BoundingBoxes &choose_from
);

Polygon to_polygon(const ExtrusionLoop &loop);

enum class Direction1D {
    forward,
    backward
};

struct PointOnLine{
    Vec2d point;
    std::size_t line_index;
};

std::optional<PointOnLine> offset_along_lines(
    const Vec2d &point,
    const std::size_t loop_line_index,
    const Linesf &loop_lines,
    const double offset,
    const Direction1D direction
);

} // namespace Slic3r::Seams::Geometry

#endif // libslic3r_SeamGeometry_hpp_
