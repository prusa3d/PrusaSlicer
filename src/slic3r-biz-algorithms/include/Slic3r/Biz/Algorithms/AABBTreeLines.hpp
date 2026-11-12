#pragma once

#include "Point.hpp"
#include "Slic3r/Biz/Algorithms/AABBTreeIndirect.hpp"
#include "Slic3r/Biz/Algorithms/Line.hpp"
#include "Slic3r/Domain/Line.hpp"
#include <algorithm>
#include <cmath>
#include <type_traits>
#include <vector>

namespace Slic3r::Biz::Algorithms::AABBTreeLines {

namespace detail {

template<typename ALineType, typename ATreeType, typename AVectorType> struct IndexedLinesDistancer
{
    using LineType   = ALineType;
    using TreeType   = ATreeType;
    using VectorType = AVectorType;
    using ScalarType = typename VectorType::Scalar;

    const std::vector<LineType> &lines;
    const TreeType              &tree;

    const VectorType origin;

    inline VectorType closest_point_to_origin(size_t primitive_index, ScalarType &squared_distance) const
    {
        Domain::Advanced::Vec<typename LineType::Scalar, LineType::Dim> nearest_point;
        const LineType                               &line = lines[primitive_index];
        squared_distance = Biz::Algorithms::Line::line_alg::distance_to_squared(line, Domain::Advanced::Vec<typename LineType::Scalar, LineType::Dim>(origin.template cast<typename LineType::Scalar>()), &nearest_point);
        return nearest_point.template cast<ScalarType>();
    }
};

// returns number of intersections of ray starting in ray_origin and following the specified coordinate line with lines in tree
// first number is hits in positive direction of ray, second number hits in negative direction. returns neagtive numbers when ray_origin is
// on some line exactly.
template<typename LineType, typename TreeType, typename VectorType, int coordinate>
inline std::tuple<int, int> coordinate_aligned_ray_hit_count(size_t                       node_idx,
                                                             const TreeType              &tree,
                                                             const std::vector<LineType> &lines,
                                                             const VectorType            &ray_origin)
{
    static constexpr int other_coordinate = (coordinate + 1) % 2;
    using Scalar                          = typename LineType::Scalar;
    using Floating                        = typename std::conditional<std::is_floating_point<Scalar>::value, Scalar, double>::type;
    const auto &node                      = tree.node(node_idx);
    assert(node.is_valid());
    if (node.is_leaf()) {
        const LineType &line = lines[node.idx];
        if (ray_origin[other_coordinate] < std::min(line.a[other_coordinate], line.b[other_coordinate]) ||
            ray_origin[other_coordinate] >= std::max(line.a[other_coordinate], line.b[other_coordinate])) { 
                // the second inequality is nonsharp for a reason 
                //  without it, we may count contour border twice when the lines meet exactly at the spot of intersection. this prevents is
            return {0, 0};
        }

        Scalar line_max = std::max(line.a[coordinate], line.b[coordinate]);
        Scalar line_min = std::min(line.a[coordinate], line.b[coordinate]);
        if (ray_origin[coordinate] > line_max) {
            return {1, 0};
        } else if (ray_origin[coordinate] < line_min) {
            return {0, 1};
        } else {
            // find intersection of ray with line
            //  that is when ( line.a + t * (line.b - line.a) )[other_coordinate] == ray_origin[other_coordinate]
            //  t = ray_origin[oc] - line.a[oc] / (line.b[oc] - line.a[oc]);
            //  then we want to get value of intersection[ coordinate]
            //  val_c = line.a[c] + t * (line.b[c] - line.a[c]);
            //  Note that ray and line may overlap, when  (line.b[oc] - line.a[oc]) is zero
            //  In that case, we return negative number
            Floating distance_oc = line.b[other_coordinate] - line.a[other_coordinate];
            Floating t     = (ray_origin[other_coordinate] - line.a[other_coordinate]) / distance_oc;
            Floating val_c = line.a[coordinate] + t * (line.b[coordinate] - line.a[coordinate]);
            if (ray_origin[coordinate] > val_c) {
                return {1, 0};
            } else if (ray_origin[coordinate] < val_c) {
                return {0, 1};
            } else { // ray origin is on boundary
                return {-1, -1};
            }
        }
    } else {
        int         intersections_above = 0;
        int         intersections_below = 0;
        size_t      left_node_idx       = node_idx * 2 + 1;
        size_t      right_node_idx      = left_node_idx + 1;
        const auto &node_left           = tree.node(left_node_idx);
        const auto &node_right          = tree.node(right_node_idx);
        assert(node_left.is_valid());
        assert(node_right.is_valid());

        if (node_left.bbox.min()[other_coordinate] <= ray_origin[other_coordinate] &&
            node_left.bbox.max()[other_coordinate] >=
                ray_origin[other_coordinate]) {
            auto [above, below] = coordinate_aligned_ray_hit_count<LineType, TreeType, VectorType, coordinate>(left_node_idx, tree, lines,
                                                                                                               ray_origin);
            if (above < 0 || below < 0) return {-1, -1};
            intersections_above += above;
            intersections_below += below;
        }

        if (node_right.bbox.min()[other_coordinate] <= ray_origin[other_coordinate] &&
            node_right.bbox.max()[other_coordinate] >= ray_origin[other_coordinate]) {
            auto [above, below] = coordinate_aligned_ray_hit_count<LineType, TreeType, VectorType, coordinate>(right_node_idx, tree, lines,
                                                                                                               ray_origin);
            if (above < 0 || below < 0) return {-1, -1};
            intersections_above += above;
            intersections_below += below;
        }
        return {intersections_above, intersections_below};
    }
}

template<typename LineType, typename TreeType, typename VectorType>
inline void insert_intersections_with_line(std::vector<std::pair<VectorType, size_t>> &result,
                                           size_t                                      node_idx,
                                           const TreeType                             &tree,
                                           const std::vector<LineType>                &lines,
                                           const LineType                             &line,
                                           const typename TreeType::BoundingBox       &line_bb)
{
    const auto &node = tree.node(node_idx);
    assert(node.is_valid());
    if (node.is_leaf()) {
        VectorType intersection_pt;
        if (Biz::Algorithms::Line::line_alg::intersection(line, lines[node.idx], &intersection_pt)) {
            result.emplace_back(intersection_pt, node.idx);
        }
        return;
    }

    size_t      left_node_idx  = node_idx * 2 + 1;
    size_t      right_node_idx = left_node_idx + 1;
    const auto &node_left      = tree.node(left_node_idx);
    const auto &node_right     = tree.node(right_node_idx);
    assert(node_left.is_valid());
    assert(node_right.is_valid());

    if (node_left.bbox.intersects(line_bb)) {
        insert_intersections_with_line<LineType, TreeType, VectorType>(result, left_node_idx, tree, lines, line, line_bb);
    }

    if (node_right.bbox.intersects(line_bb)) {
        insert_intersections_with_line<LineType, TreeType, VectorType>(result, right_node_idx, tree, lines, line, line_bb);
    }

    //// NOTE: Non recursive implementation - for my case was slower ;-(     
    // std::vector<size_t> node_indicies_for_check; // evaluation queue
    // size_t approx_size = static_cast<size_t>(std::ceil(std::sqrt(tree.nodes().size())));
    // node_indicies_for_check.reserve(approx_size);
    // do {
    //     const auto &node = tree.node(node_index);
    //     assert(node.is_valid());
    //     if (node.is_leaf()) {
    //         VectorType intersection_pt;
    //         if (line_alg::intersection(line, lines[node.idx], &intersection_pt))
    //             result.emplace_back(intersection_pt, node.idx);
    //         node_index = 0;// clear next node
    //     } else {
    //         size_t      left_node_idx  = node_index * 2 + 1;
    //         size_t      right_node_idx = left_node_idx + 1;
    //         const auto &node_left      = tree.node(left_node_idx);
    //         const auto &node_right     = tree.node(right_node_idx);
    //         assert(node_left.is_valid());
    //         assert(node_right.is_valid());

    //        // Set next node index
    //        node_index = 0; // clear next node
    //        if (node_left.bbox.intersects(line_bb))
    //            node_index = left_node_idx;

    //        if (node_right.bbox.intersects(line_bb)) {
    //            if (node_index == 0)
    //                node_index = right_node_idx;
    //            else
    //                node_indicies_for_check.push_back(right_node_idx); // store for later evaluation
    //        }
    //    }

    //    if (node_index == 0 && !node_indicies_for_check.empty()) {
    //        // no direct next node take one from queue
    //        node_index = node_indicies_for_check.back();
    //        node_indicies_for_check.pop_back();
    //    }
    //} while (node_index != 0);
}

} // namespace detail

// Build a balanced AABB Tree over a vector of lines, balancing the tree
// on centroids of the lines.
// Epsilon is applied to the bounding boxes of the AABB Tree to cope with numeric inaccuracies
// during tree traversal.
template<typename LineType>
inline AABBTreeIndirect::Tree<LineType::Dim, typename LineType::Scalar> build_aabb_tree_over_indexed_lines(const std::vector<LineType> &lines)
{
    using TreeType = AABBTreeIndirect::Tree<LineType::Dim, typename LineType::Scalar>;
    //    using              CoordType      = typename TreeType::CoordType;
    using VectorType  = typename TreeType::VectorType;
    using BoundingBox = typename TreeType::BoundingBox;

    struct InputType
    {
        size_t             idx() const { return m_idx; }
        const BoundingBox &bbox() const { return m_bbox; }
        const VectorType  &centroid() const { return m_centroid; }

        size_t      m_idx;
        BoundingBox m_bbox;
        VectorType  m_centroid;
    };

    std::vector<InputType> input;
    input.reserve(lines.size());
    for (size_t i = 0; i < lines.size(); ++i) {
        const LineType &line = lines[i];
        InputType       n;
        n.m_idx      = i;
        n.m_centroid = ((line.a + line.b).template cast<double>() * 0.5).template cast<typename LineType::Scalar>();
        n.m_bbox     = BoundingBox(line.a, line.a);
        n.m_bbox.extend(line.b);
        input.emplace_back(n);
    }

    TreeType out;
    out.build(std::move(input));
    return out;
}

// Finding a closest line, its closest point and squared distance to the closest point
// Returns squared distance to the closest point or -1 if the input is empty.
// or no closer point than max_sq_dist
template<typename LineType, typename TreeType, typename VectorType>
inline typename VectorType::Scalar squared_distance_to_indexed_lines(
    const std::vector<LineType>        &lines,
    const TreeType                     &tree,
    const VectorType                   &point,
    size_t                             &hit_idx_out,
    VectorType                         &hit_point_out,
    typename VectorType::Scalar         max_sqr_dist = std::numeric_limits<typename VectorType::Scalar>::infinity())
{
    using Scalar = typename VectorType::Scalar;
    if (tree.empty()) return Scalar(-1);
    auto distancer = detail::IndexedLinesDistancer<LineType, TreeType, VectorType>{lines, tree, point};
    return AABBTreeIndirect::detail::squared_distance_to_indexed_primitives_recursive(distancer, size_t(0), Scalar(0), max_sqr_dist,
                                                                                      hit_idx_out, hit_point_out);
}

// Returns all lines within the given radius limit
template<typename LineType, typename TreeType, typename VectorType>
inline std::vector<size_t> all_lines_in_radius(const std::vector<LineType> &lines,
                                               const TreeType              &tree,
                                               const VectorType            &point,
                                               typename VectorType::Scalar  max_distance_squared)
{
    auto distancer = detail::IndexedLinesDistancer<LineType, TreeType, VectorType>{lines, tree, point};

    if (tree.empty()) { return {}; }

    std::vector<size_t> found_lines{};
    AABBTreeIndirect::detail::indexed_primitives_within_distance_squared_recurisve(distancer, size_t(0), max_distance_squared, found_lines);
    return found_lines;
}

// return 1 if true, -1 if false, 0 for point on contour (or if cannot be determined)
template<typename LineType, typename TreeType, typename VectorType>
inline int point_outside_closed_contours(const std::vector<LineType> &lines, const TreeType &tree, const VectorType &point)
{
    if (tree.empty()) { return 1; }

    auto [hits_above, hits_below] = detail::coordinate_aligned_ray_hit_count<LineType, TreeType, VectorType, 0>(0, tree, lines, point);
    if (hits_above < 0 || hits_below < 0) {
        return 0;
    } else if (hits_above % 2 == 1 && hits_below % 2 == 1) {
        return -1;
    } else if (hits_above % 2 == 0 && hits_below % 2 == 0) {
        return 1;
    } else { // this should not happen with closed contours. lets check it in Y direction
        auto [hits_above, hits_below] = detail::coordinate_aligned_ray_hit_count<LineType, TreeType, VectorType, 1>(0, tree, lines, point);
        if (hits_above < 0 || hits_below < 0) {
            return 0;
        } else if (hits_above % 2 == 1 && hits_below % 2 == 1) {
            return -1;
        } else if (hits_above % 2 == 0 && hits_below % 2 == 0) {
            return 1;
        } else { // both results were unclear
            return 0;
        }
    }
}

template<bool sorted, typename VectorType, typename LineType, typename TreeType>
inline std::vector<std::pair<VectorType, size_t>> get_intersections_with_line(const std::vector<LineType> &lines,
                                                                              const TreeType              &tree,
                                                                              const LineType              &line)
{
    if (tree.empty()) {
        return {};
    }
    auto line_bb = typename TreeType::BoundingBox(line.a, line.a);
    line_bb.extend(line.b);

    std::vector<std::pair<VectorType, size_t>> intersections; // result
    detail::insert_intersections_with_line(intersections, 0, tree, lines, line, line_bb);

    if (sorted) {
        using Floating =
            typename std::conditional<std::is_floating_point<typename LineType::Scalar>::value, typename LineType::Scalar, double>::type;

        std::vector<std::pair<Floating, std::pair<VectorType, size_t>>> points_with_sq_distance{};
        for (const auto &p : intersections) {
            points_with_sq_distance.emplace_back((p.first - line.a).template cast<Floating>().squaredNorm(), p);
        }
        std::sort(points_with_sq_distance.begin(), points_with_sq_distance.end(),
                  [](const std::pair<Floating, std::pair<VectorType, size_t>> &left,
                     std::pair<Floating, std::pair<VectorType, size_t>>       &right) { return left.first < right.first; });
        for (size_t i = 0; i < points_with_sq_distance.size(); i++) {
            intersections[i] = points_with_sq_distance[i].second;
        }
    }
    return intersections;
}

template<typename LineType> class LinesDistancer
{
public:
    using Scalar   = typename LineType::Scalar;
    using Floating = typename std::conditional<std::is_floating_point<Scalar>::value, Scalar, double>::type;
private:
    std::vector<LineType> lines;
    AABBTreeIndirect::Tree<2, Scalar> tree;

public:
    explicit LinesDistancer(const std::vector<LineType> &lines) : lines(lines)
    {
        tree = AABBTreeLines::build_aabb_tree_over_indexed_lines(this->lines);
    }

    explicit LinesDistancer(std::vector<LineType> &&lines) : lines(lines)
    {
        tree = AABBTreeLines::build_aabb_tree_over_indexed_lines(this->lines);
    }

    LinesDistancer() = default;

    // 1 true, -1 false, 0 cannot determine
    int outside(const Domain::Advanced::Vec<Scalar, 2> &point) const { return point_outside_closed_contours(lines, tree, point); }

    // negative sign means inside
    template<bool SIGNED_DISTANCE>
    std::tuple<Floating, size_t, Domain::Advanced::Vec<Floating, 2>> distance_from_lines_extra(const Domain::Advanced::Vec<Scalar, 2> &point) const
    {
        size_t           nearest_line_index_out = size_t(-1);
        Domain::Advanced::Vec<Floating, 2> nearest_point_out      = Domain::Advanced::Vec<Floating, 2>::Zero();
        Domain::Advanced::Vec<Floating, 2> p                      = point.template cast<Floating>();
        auto distance = AABBTreeLines::squared_distance_to_indexed_lines(lines, tree, p, nearest_line_index_out, nearest_point_out);

        if (distance < 0) {
            return {std::numeric_limits<Floating>::infinity(), nearest_line_index_out, nearest_point_out};
        }
        distance = sqrt(distance);

        if (SIGNED_DISTANCE) {
            distance *= outside(point);
        }

        return {distance, nearest_line_index_out, nearest_point_out};
    }

    template<bool SIGNED_DISTANCE> Floating distance_from_lines(const Domain::Advanced::Vec<Scalar, 2> &point) const
    {
        auto [dist, idx, np] = distance_from_lines_extra<SIGNED_DISTANCE>(point);
        return dist;
    }

    std::vector<size_t> all_lines_in_radius(const Domain::Advanced::Vec<Scalar, 2> &point, Floating radius) const
    {
        return AABBTreeLines::all_lines_in_radius(this->lines, this->tree, point.template cast<Floating>(), radius * radius);
    }

    template<bool sorted> std::vector<std::pair<Domain::Advanced::Vec<Scalar, 2>, size_t>> intersections_with_line(const LineType &line) const
    {
        return get_intersections_with_line<sorted, Domain::Advanced::Vec<Scalar, 2>>(lines, tree, line);
    }

    const LineType &get_line(size_t line_idx) const { return lines[line_idx]; }

    const std::vector<LineType> &get_lines() const { return lines; }
};

} // Slic3r::Biz::Algorithms::AABBTreeLines
