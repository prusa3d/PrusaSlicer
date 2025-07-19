#include <random>
#include <algorithm>
#include <limits>
#include <utility>

#include "libslic3r/GCode/SeamRandom.hpp"
#include "libslic3r/GCode/SeamChoice.hpp"
#include "libslic3r/Point.hpp"

namespace Slic3r::Seams::Random {
using Perimeters::PointType;
using Perimeters::PointClassification;

namespace Impl {
std::vector<PerimeterSegment> get_segments(
    const Perimeters::Perimeter &perimeter,
    const PointType point_type,
    const PointClassification point_classification
) {
    const std::vector<Vec2d> &positions{perimeter.positions};
    const std::vector<PointType> &point_types{perimeter.point_types};
    const std::vector<PointClassification> &point_classifications{perimeter.point_classifications};

    std::optional<double> current_begin;
    std::optional<std::size_t> current_begin_index;
    Vec2d previous_position{positions.front()};
    double distance{0.0};
    std::vector<PerimeterSegment> result;
    for (std::size_t i{0}; i <= positions.size(); ++i) {
        const std::size_t index{i == positions.size() ? 0 : i};
        const double previous_distance{distance};
        distance += (positions[index] - previous_position).norm();
        previous_position = positions[index];

        if (point_types[index] == point_type &&
            point_classifications[index] == point_classification) {
            if (!current_begin) {
                current_begin = distance;
                current_begin_index = index;
            }
        } else {
            if (current_begin) {
                result.push_back(PerimeterSegment{*current_begin, previous_distance, *current_begin_index});
            }
            current_begin = std::nullopt;
            current_begin_index = std::nullopt;
        }
    }

    if (current_begin) {
        result.push_back(PerimeterSegment{*current_begin, distance, *current_begin_index});
    }
    return result;
}

PerimeterSegment pick_random_segment(
    const std::vector<PerimeterSegment> &segments, std::mt19937 &random_engine
) {
    double length{0.0};
    for (const PerimeterSegment &segment : segments) {
        length += segment.length();
    }

    std::uniform_real_distribution<double> distribution{0.0, length};
    double random_distance{distribution(random_engine)};

    double distance{0.0};
    return *std::find_if(segments.begin(), segments.end(), [&](const PerimeterSegment &segment) {
        if (random_distance >= distance && random_distance <= distance + segment.length()) {
            return true;
        }
        distance += segment.length();
        return false;
    });
}

SeamChoice pick_random_point(
    const PerimeterSegment &segment, const Perimeters::Perimeter &perimeter, std::mt19937 &random_engine
) {
    const std::vector<Vec2d> &positions{perimeter.positions};

    if (segment.length() < std::numeric_limits<double>::epsilon()) {
        return {segment.begin_index, segment.begin_index, positions[segment.begin_index]};
    }

    std::uniform_real_distribution<double> distribution{0.0, segment.length()};
    const double random_distance{distribution(random_engine)};

    double distance{0.0};
    std::size_t previous_index{segment.begin_index};
    for (std::size_t i{segment.begin_index + 1}; i <= perimeter.positions.size(); ++i) {
        const std::size_t index{i == perimeter.positions.size() ? 0 : i};
        const Vec2d edge{positions[index] - positions[previous_index]};

        if (distance + edge.norm() >= random_distance) {
            std::size_t current_index{index};
            if (random_distance - distance < std::numeric_limits<double>::epsilon()) {
                current_index = previous_index;
            } else if (distance + edge.norm() - random_distance < std::numeric_limits<double>::epsilon()) {
                previous_index = index;
            }

            const double remaining_distance{random_distance - distance};
            const Vec2d position{positions[previous_index] + remaining_distance * edge.normalized()};
            return {previous_index, current_index, position};
        }

        distance += edge.norm();
        previous_index = index;
    }

    // Should be unreachable.
    return {segment.begin_index, segment.begin_index, positions[segment.begin_index]};
}

std::optional<SeamChoice> Random::operator()(
    const Perimeters::Perimeter &perimeter,
    const PointType point_type,
    const PointClassification point_classification
) const {
    std::vector<PerimeterSegment> segments{
        get_segments(perimeter, point_type, point_classification)};

    if (!segments.empty()) {
        const PerimeterSegment segment{pick_random_segment(segments, random_engine)};
        return pick_random_point(segment, perimeter, random_engine);
    }
    return std::nullopt;
}
} // namespace Impl

std::vector<std::vector<SeamPerimeterChoice>> get_object_seams(
    Perimeters::LayerPerimeters &&perimeters, const unsigned fixed_seed
) {
    std::mt19937 random_engine{fixed_seed};
    const Impl::Random random{random_engine};

    std::vector<std::vector<SeamPerimeterChoice>> result;

    for (std::vector<Perimeters::BoundedPerimeter> &layer : perimeters) {
        result.emplace_back();
        for (Perimeters::BoundedPerimeter &perimeter : layer) {
            if (perimeter.perimeter.is_degenerate) {
                std::optional<Seams::SeamChoice> seam_choice{
                    Seams::choose_degenerate_seam_point(perimeter.perimeter)};
                if (seam_choice) {
                    result.back().push_back(
                        SeamPerimeterChoice{*seam_choice, std::move(perimeter.perimeter)}
                    );
                } else {
                    result.back().push_back(SeamPerimeterChoice{SeamChoice{}, std::move(perimeter.perimeter)});
                }
            } else {
                result.back().push_back(SeamPerimeterChoice{
                    Seams::choose_seam_point(perimeter.perimeter, random),
                    std::move(perimeter.perimeter)});
            }
        }
    }
    return result;
}
} // namespace Slic3r::Seams::Random
