#include "libslic3r/GCode/SeamAligned.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <utility>

#include "libslic3r/GCode/SeamGeometry.hpp"
#include "libslic3r/GCode/ModelVisibility.hpp"
#include "libslic3r/KDTreeIndirect.hpp"
#include "libslic3r/Line.hpp"
#include "tcbspan/span.hpp"

namespace Slic3r::Seams::Aligned {
using Perimeters::PointType;
using Perimeters::PointClassification;

namespace Impl {
const Perimeters::Perimeter::PointTrees &pick_trees(
    const Perimeters::Perimeter &perimeter, const PointType point_type
) {
    switch (point_type) {
    case PointType::enforcer: return perimeter.enforced_points;
    case PointType::blocker: return perimeter.blocked_points;
    case PointType::common: return perimeter.common_points;
    }
    throw std::runtime_error("Point trees for point type do not exist.");
}

const Perimeters::Perimeter::OptionalPointTree &pick_tree(
    const Perimeters::Perimeter::PointTrees &point_trees,
    const PointClassification &point_classification
) {
    switch (point_classification) {
    case PointClassification::overhang: return point_trees.overhanging_points;
    case PointClassification::embedded: return point_trees.embedded_points;
    case PointClassification::common: return point_trees.common_points;
    }
    throw std::runtime_error("Point tree for classification does not exist.");
}

SeamChoice pick_seam_option(const Perimeters::Perimeter &perimeter, const SeamOptions &options) {
    const std::vector<PointType> &types{perimeter.point_types};
    const std::vector<PointClassification> &classifications{perimeter.point_classifications};
    const std::vector<Vec2d> &positions{perimeter.positions};

    unsigned closeset_point_value =
        get_point_value(types.at(options.closest), classifications[options.closest]);

    if (options.snapped) {
        unsigned snapped_point_value =
            get_point_value(types.at(*options.snapped), classifications[*options.snapped]);
        if (snapped_point_value >= closeset_point_value) {
            const Vec2d position{positions.at(*options.snapped)};
            return {*options.snapped, *options.snapped, position};
        }
    }

    unsigned adjacent_point_value =
        get_point_value(types.at(options.adjacent), classifications[options.adjacent]);
    if (adjacent_point_value < closeset_point_value) {
        const Vec2d position = positions[options.closest];
        return {options.closest, options.closest, position};
    }

    const std::size_t next_index{options.adjacent_forward ? options.adjacent : options.closest};
    const std::size_t previous_index{options.adjacent_forward ? options.closest : options.adjacent};
    return {previous_index, next_index, options.on_edge};
}

std::optional<std::size_t> snap_to_angle(
    const Vec2d &point,
    const std::size_t search_start,
    const Perimeters::Perimeter &perimeter,
    const double max_detour
) {
    using Perimeters::AngleType;
    const std::vector<Vec2d> &positions{perimeter.positions};
    const std::vector<AngleType> &angle_types{perimeter.angle_types};

    std::optional<std::size_t> match;
    double min_distance{std::numeric_limits<double>::infinity()};
    AngleType angle_type{AngleType::convex};

    const auto visitor{[&](const std::size_t index) {
        const double distance = (positions[index] - point).norm();
        if (distance > max_detour) {
            return true;
        }
        if (angle_types[index] == angle_type &&
            distance < min_distance) {
            match = index;
            min_distance = distance;
            return true;
        }
        return false;
    }};
    Geometry::visit_backward(search_start, positions.size(), visitor);
    Geometry::visit_forward(search_start, positions.size(), visitor);
    if (match) {
        return match;
    }

    min_distance = std::numeric_limits<double>::infinity();
    angle_type = AngleType::concave;

    Geometry::visit_backward(search_start, positions.size(), visitor);
    Geometry::visit_forward(search_start, positions.size(), visitor);

    return match;
}

SeamOptions get_seam_options(
    const Perimeters::Perimeter &perimeter,
    const Vec2d &prefered_position,
    const Perimeters::Perimeter::PointTree &points_tree,
    const double max_detour
) {
    const std::vector<Vec2d> &positions{perimeter.positions};

    const std::size_t closest{find_closest_point(points_tree, prefered_position.head<2>())};
    std::size_t previous{closest == 0 ? positions.size() - 1 : closest - 1};
    std::size_t next{closest == positions.size() - 1 ? 0 : closest + 1};

    const Vec2d previous_adjacent_point{positions[previous]};
    const Vec2d closest_point{positions[closest]};
    const Vec2d next_adjacent_point{positions[next]};

    const Linef previous_segment{previous_adjacent_point, closest_point};
    const auto [previous_point, previous_distance] =
        Geometry::distance_to_segment_squared(previous_segment, prefered_position);
    const Linef next_segment{closest_point, next_adjacent_point};
    const auto [next_point, next_distance] =
        Geometry::distance_to_segment_squared(next_segment, prefered_position);

    const bool adjacent_forward{next_distance < previous_distance};
    const Vec2d nearest_point{adjacent_forward ? next_point : previous_point};
    const std::size_t adjacent{adjacent_forward ? next : previous};

    std::optional<std::size_t> snapped{
        snap_to_angle(nearest_point.head<2>(), closest, perimeter, max_detour)};

    return {
        closest, adjacent, adjacent_forward, snapped, nearest_point,
    };
}

std::optional<SeamChoice> LeastVisible::operator()(
    const Perimeters::Perimeter &perimeter,
    const PointType point_type,
    const PointClassification point_classification
) const {
    std::optional<size_t> chosen_index;
    double visibility{std::numeric_limits<double>::infinity()};

    for (std::size_t i{0}; i < perimeter.positions.size(); ++i) {
        if (perimeter.point_types[i] != point_type ||
            perimeter.point_classifications[i] != point_classification) {
            continue;
        }
        const Vec2d point{perimeter.positions[i]};
        const double point_visibility{precalculated_visibility[i]};

        if (point_visibility < visibility) {
            visibility = point_visibility;
            chosen_index = i;
        }
    }

    if (chosen_index) {
        return {{*chosen_index, *chosen_index, perimeter.positions[*chosen_index]}};
    }
    return std::nullopt;
}

std::optional<SeamChoice> Nearest::operator()(
    const Perimeters::Perimeter &perimeter,
    const PointType point_type,
    const PointClassification point_classification
) const {
    const Perimeters::Perimeter::PointTrees &trees{pick_trees(perimeter, point_type)};
    const Perimeters::Perimeter::OptionalPointTree &tree = pick_tree(trees, point_classification);
    if (tree) {
        const SeamOptions options{get_seam_options(perimeter, prefered_position, *tree, max_detour)};
        return pick_seam_option(perimeter, options);
    }
    return std::nullopt;
}
} // namespace Impl

double VisibilityCalculator::operator()(
    const SeamChoice &choice, const Perimeters::Perimeter &perimeter
) const {
    double visibility = points_visibility.calculate_point_visibility(
        to_3d(choice.position, perimeter.slice_z).cast<float>()
    );

    const double angle{
        choice.previous_index == choice.next_index ? perimeter.angles[choice.previous_index] : 0.0};
    visibility +=
        get_angle_visibility_modifier(angle, convex_visibility_modifier, concave_visibility_modifier);
    return visibility;
}

double VisibilityCalculator::get_angle_visibility_modifier(
    double angle,
    const double convex_visibility_modifier,
    const double concave_visibility_modifier
) {
    const double weight_max{angle > 0 ? convex_visibility_modifier : concave_visibility_modifier};
    angle = std::abs(angle);
    const double right_angle{M_PI / 2.0};
    if (angle > right_angle) {
        return -weight_max;
    }
    const double angle_linear_weight{angle / right_angle};
    // It is smooth and at angle 0 slope is equal to `angle linear weight`, at right angle the slope is 0 and value is equal to weight max.
    const double angle_smooth_weight{angle / right_angle * weight_max + (right_angle - angle) / right_angle * angle_linear_weight};
    return -angle_smooth_weight;
}

std::vector<Vec2d> get_starting_positions(const Shells::Shell<> &shell) {
    const Perimeters::Perimeter &perimeter{shell.front().boundary};

    std::vector<Vec2d> enforcers{Perimeters::extract_points(perimeter, Perimeters::PointType::enforcer)};
    if (!enforcers.empty()) {
        return enforcers;
    }
    std::vector<Vec2d> common{Perimeters::extract_points(perimeter, Perimeters::PointType::common)};
    if (!common.empty()) {
        return common;
    }
    return perimeter.positions;
}

struct LeastVisiblePoint
{
    SeamChoice choice;
    double visibility;
};

struct SeamCandidate {
    std::vector<SeamChoice> choices;
    std::vector<double> visibilities;
};

std::vector<SeamChoice> get_shell_seam(
    const Shells::Shell<> &shell,
    const std::function<SeamChoice(const Perimeters::Perimeter &, std::size_t)> &chooser
) {
    std::vector<SeamChoice> result;
    result.reserve(shell.size());
    for (std::size_t i{0}; i < shell.size(); ++i) {
        const Shells::Slice<> &slice{shell[i]};
        if (slice.boundary.is_degenerate) {
            if (std::optional<SeamChoice> seam_choice{
                    choose_degenerate_seam_point(slice.boundary)}) {
                result.push_back(*seam_choice);
            } else {
                result.emplace_back();
            }
        } else {
            const SeamChoice choice{chooser(slice.boundary, i)};
            result.push_back(choice);
        }
    }
    return result;
}

SeamCandidate get_seam_candidate(
    const Shells::Shell<> &shell,
    const Vec2d &starting_position,
    const SeamChoiceVisibility &visibility_calculator,
    const Params &params,
    const std::vector<std::vector<double>> &precalculated_visibility,
    const std::vector<LeastVisiblePoint> &least_visible_points
) {
    using Perimeters::Perimeter, Perimeters::AngleType;

    std::vector<double> choice_visibilities(shell.size(), 1.0);
    std::vector<SeamChoice> choices{
        get_shell_seam(shell, [&, previous_position{starting_position}](const Perimeter &perimeter, std::size_t slice_index) mutable {
            SeamChoice candidate{Seams::choose_seam_point(
                perimeter, Impl::Nearest{previous_position, params.max_detour}
            )};
            const bool is_too_far{
                (candidate.position - previous_position).norm() > params.max_detour};
            const LeastVisiblePoint &least_visible{least_visible_points[slice_index]};

            const bool is_on_edge{
                candidate.previous_index == candidate.next_index &&
                perimeter.angle_types[candidate.next_index] != AngleType::smooth};

            if (is_on_edge) {
                choice_visibilities[slice_index] = precalculated_visibility[slice_index][candidate.previous_index];
            } else {
                choice_visibilities[slice_index] =
                    visibility_calculator(candidate, perimeter);
            }
            const bool is_too_visible{
                choice_visibilities[slice_index] >
                least_visible.visibility + params.jump_visibility_threshold};
            const bool can_be_on_edge{
                perimeter.angle_types[least_visible.choice.next_index] != AngleType::smooth};
            if (is_too_far || (can_be_on_edge  && is_too_visible)) {
                candidate = least_visible.choice;
            }
            previous_position = candidate.position;
            return candidate;
        })};
    return {std::move(choices), std::move(choice_visibilities)};
}

using ShellVertexVisibility = std::vector<std::vector<double>>;

std::vector<ShellVertexVisibility> get_shells_vertex_visibility(
    const Shells::Shells<> &shells, const SeamChoiceVisibility &visibility_calculator
) {
    std::vector<ShellVertexVisibility> result;

    result.reserve(shells.size());
    std::transform(
        shells.begin(), shells.end(), std::back_inserter(result),
        [](const Shells::Shell<> &shell) { return ShellVertexVisibility(shell.size()); }
    );

    Geometry::iterate_nested(shells, [&](const std::size_t shell_index, const std::size_t slice_index) {
        const Shells::Shell<> &shell{shells[shell_index]};
        const Shells::Slice<> &slice{shell[slice_index]};
        const std::vector<Vec2d> &positions{slice.boundary.positions};

        for (std::size_t point_index{0}; point_index < positions.size(); ++point_index) {
            result[shell_index][slice_index].emplace_back(visibility_calculator(
                SeamChoice{point_index, point_index, positions[point_index]}, slice.boundary
            ));
        }
    });
    return result;
}

using ShellLeastVisiblePoints = std::vector<LeastVisiblePoint>;

std::vector<ShellLeastVisiblePoints> get_shells_least_visible_points(
    const Shells::Shells<> &shells,
    const std::vector<ShellVertexVisibility> &precalculated_visibility
) {
    std::vector<ShellLeastVisiblePoints> result;

    result.reserve(shells.size());
    std::transform(
        shells.begin(), shells.end(), std::back_inserter(result),
        [](const Shells::Shell<> &shell) { return ShellLeastVisiblePoints(shell.size()); }
    );

    Geometry::iterate_nested(shells, [&](const std::size_t shell_index, const std::size_t slice_index) {
        const Shells::Shell<> &shell{shells[shell_index]};
        const Shells::Slice<> &slice{shell[slice_index]};
        const SeamChoice least_visibile{
            Seams::choose_seam_point(slice.boundary, Impl::LeastVisible{precalculated_visibility[shell_index][slice_index]})};

        const double visibility{precalculated_visibility[shell_index][slice_index][least_visibile.previous_index]};
        result[shell_index][slice_index] = LeastVisiblePoint{least_visibile, visibility};
    });
    return result;
}

using ShellStartingPositions = std::vector<Vec2d>;

std::vector<ShellStartingPositions> get_shells_starting_positions(
    const Shells::Shells<> &shells
) {
    std::vector<ShellStartingPositions> result;
    for (const Shells::Shell<> &shell : shells) {
        std::vector<Vec2d> starting_positions{get_starting_positions(shell)};
        result.push_back(std::move(starting_positions));
    }
    return result;
}

using ShellSeamCandidates = std::vector<SeamCandidate>;

std::vector<ShellSeamCandidates> get_shells_seam_candidates(
    const Shells::Shells<> &shells,
    const std::vector<ShellStartingPositions> &starting_positions,
    const SeamChoiceVisibility &visibility_calculator,
    const std::vector<ShellVertexVisibility> &precalculated_visibility,
    const std::vector<ShellLeastVisiblePoints> &least_visible_points,
    const Params &params
) {
    std::vector<ShellSeamCandidates> result;

    result.reserve(starting_positions.size());
    std::transform(
        starting_positions.begin(), starting_positions.end(), std::back_inserter(result),
        [](const ShellStartingPositions &positions) { return ShellSeamCandidates(positions.size()); }
    );

    Geometry::iterate_nested(starting_positions, [&](const std::size_t shell_index, const std::size_t starting_position_index){
        const Shells::Shell<> &shell{shells[shell_index]};
        using Perimeters::Perimeter, Perimeters::AngleType;

        result[shell_index][starting_position_index] = get_seam_candidate(
            shell,
            starting_positions[shell_index][starting_position_index],
            visibility_calculator,
            params,
            precalculated_visibility[shell_index],
            least_visible_points[shell_index]
        );
    });
    return result;
}

std::vector<SeamChoice> get_shell_seam(
    const Shells::Shell<> &shell,
    std::vector<SeamCandidate> seam_candidates,
    const Perimeters::Perimeter::OptionalPointTree &previous_points,
    const Params &params
) {
    std::vector<SeamChoice> seam;
    double visibility{std::numeric_limits<double>::infinity()};

    for (std::size_t i{0}; i < seam_candidates.size(); ++i) {
        using Perimeters::Perimeter, Perimeters::AngleType;

        SeamCandidate seam_candidate{seam_candidates[i]};
        const Vec2d first_point{seam_candidate.choices.front().position};

        std::optional<Vec2d> closest_point;
        if (previous_points) {
            std::size_t closest_point_index{find_closest_point(*previous_points, first_point)};
            Vec2d point;
            point.x() = previous_points->coordinate(closest_point_index, 0);
            point.y() = previous_points->coordinate(closest_point_index, 1);
            closest_point = point;
        }

        std::optional<double> previous_distance;
        if (closest_point) {
            previous_distance = (*closest_point - first_point).norm();
        }
        const bool is_near_previous{closest_point && *previous_distance < params.max_detour};

        double seam_candidate_visibility{
            is_near_previous ? -params.continuity_modifier *
                    (params.max_detour - *previous_distance) / params.max_detour :
                               0.0};
        for (std::size_t slice_index{}; slice_index < shell.size(); ++slice_index) {
            seam_candidate_visibility += seam_candidate.visibilities[slice_index];
        }

        if (seam_candidate_visibility < visibility) {
            seam = std::move(seam_candidate.choices);
            visibility = seam_candidate_visibility;
        }
    }

    return seam;
}

std::vector<std::vector<SeamPerimeterChoice>> get_object_seams(
    Shells::Shells<> &&shells,
    const SeamChoiceVisibility &visibility_calculator,
    const Params &params
) {
    const std::vector<ShellVertexVisibility> precalculated_visibility{
        get_shells_vertex_visibility(shells, visibility_calculator)};

    const std::vector<ShellLeastVisiblePoints> least_visible_points{
        get_shells_least_visible_points(shells, precalculated_visibility)
    };

    const std::vector<ShellStartingPositions> starting_positions{
        get_shells_starting_positions(shells)
    };

    const std::vector<ShellSeamCandidates> seam_candidates{
        get_shells_seam_candidates(
            shells,
            starting_positions,
            visibility_calculator,
            precalculated_visibility,
            least_visible_points,
            params
        )
    };

    std::vector<std::vector<SeamPerimeterChoice>> layer_seams(get_layer_count(shells));

    for (std::size_t shell_index{0}; shell_index < shells.size(); ++shell_index) {
        Shells::Shell<> &shell{shells[shell_index]};

        if (shell.empty()) {
            continue;
        }

        const std::size_t layer_index{shell.front().layer_index};
        tcb::span<const SeamPerimeterChoice> previous_seams{
            layer_index == 0 ?  tcb::span<const SeamPerimeterChoice>{} : layer_seams[layer_index - 1]};
        std::vector<Vec2d> previous_seams_positions;
        std::transform(
            previous_seams.begin(), previous_seams.end(),
            std::back_inserter(previous_seams_positions),
            [](const SeamPerimeterChoice &seam) { return seam.choice.position; }
        );

        Perimeters::Perimeter::OptionalPointTree previous_seams_positions_tree;
        const Perimeters::Perimeter::IndexToCoord index_to_coord{previous_seams_positions};
        if (!previous_seams_positions.empty()) {
            previous_seams_positions_tree =
                Perimeters::Perimeter::PointTree{index_to_coord, index_to_coord.positions.size()};
        }

        std::vector<SeamChoice> seam{
            Aligned::get_shell_seam(shell, seam_candidates[shell_index], previous_seams_positions_tree, params)};

        for (std::size_t perimeter_id{}; perimeter_id < shell.size(); ++perimeter_id) {
            const SeamChoice &choice{seam[perimeter_id]};
            Perimeters::Perimeter &perimeter{shell[perimeter_id].boundary};
            layer_seams[shell[perimeter_id].layer_index].emplace_back(choice, std::move(perimeter));
        }
    }
    return layer_seams;
}

} // namespace Slic3r::Seams::Aligned
