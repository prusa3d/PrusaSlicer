#include "libslic3r/GCode/SeamRear.hpp"

#include <algorithm>
#include <optional>
#include <utility>

#include "libslic3r/AABBTreeLines.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/GCode/SeamChoice.hpp"
#include "libslic3r/GCode/SeamPerimeters.hpp"
#include "libslic3r/GCode/SeamShells.hpp"

namespace Slic3r::Seams::Rear {
using Perimeters::PointType;
using Perimeters::PointClassification;

namespace Impl {

BoundingBoxf get_bounding_box(const Shells::Shell<> &shell) {
    BoundingBoxf result;
    for (const Shells::Slice<> &slice : shell) {
        result.merge(BoundingBoxf{slice.boundary.positions});
    }
    return result;
}

struct RearestPointCalculator {
    double rear_tolerance;
    double rear_y_offset;
    BoundingBoxf bounding_box;

    std::optional<SeamChoice> operator()(
        const Perimeters::Perimeter &perimeter,
        const PointType point_type,
        const PointClassification point_classification
    ) {
        std::vector<PerimeterLine> possible_lines;
        for (std::size_t i{0}; i < perimeter.positions.size() - 1; ++i) {
            if (perimeter.point_types[i] != point_type) {
                continue;
            }
            if (perimeter.point_classifications[i] != point_classification) {
                continue;
            }
            if (perimeter.point_types[i + 1] != point_type) {
                continue;
            }
            if (perimeter.point_classifications[i + 1] != point_classification) {
                continue;
            }
            possible_lines.push_back(PerimeterLine{perimeter.positions[i], perimeter.positions[i+1], i, i + 1});
        }
        if (possible_lines.empty()) {
            return std::nullopt;
        }
        const BoundingBoxf bounding_box{perimeter.positions};
        const AABBTreeLines::LinesDistancer<PerimeterLine> possible_distancer{possible_lines};
        const double center_x{(bounding_box.max.x() + bounding_box.min.x()) / 2.0};
        const Vec2d prefered_position{center_x, bounding_box.max.y() + rear_y_offset};
        auto [_, line_index, point] = possible_distancer.distance_from_lines_extra<false>(prefered_position);
        const Vec2d location_at_bb{center_x, bounding_box.max.y()};
        auto [_d, line_index_at_bb, point_bb] = possible_distancer.distance_from_lines_extra<false>(location_at_bb);
        const double y_distance{point.y() - point_bb.y()};

        Vec2d result{point};
        if (y_distance < 0) {
            result = point_bb;
        } else if (y_distance <= rear_tolerance) {
            const double factor{y_distance / rear_tolerance};
            result = factor * point +  (1 - factor) * point_bb;
        }

        if (bounding_box.max.y() - result.y() > rear_tolerance) {
            for (const PerimeterLine &line : possible_lines) {
                if (line.a.y() > result.y()) {
                    result = line.a;
                }
                if (line.b.y() > result.y()) {
                    result = line.b;
                }
            }
        }

        return SeamChoice{possible_lines[line_index].previous_index, possible_lines[line_index].next_index, result};
    }
};
} // namespace Impl

std::vector<std::vector<SeamPerimeterChoice>> get_object_seams(
    std::vector<std::vector<Perimeters::BoundedPerimeter>> &&perimeters,
    const double rear_tolerance,
    const double rear_y_offset
) {
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
                BoundingBoxf bounding_box{unscaled(perimeter.bounding_box)};
                const SeamChoice seam_choice{Seams::choose_seam_point(
                    perimeter.perimeter,
                    Impl::RearestPointCalculator{rear_tolerance, rear_y_offset, bounding_box}
                )};
                result.back().push_back(
                    SeamPerimeterChoice{seam_choice, std::move(perimeter.perimeter)}
                );
            }
        }
    }

    return result;
}
} // namespace Slic3r::Seams::Rear
