#include "libslic3r/GCode/SeamChoice.hpp"

#include <vector>

namespace Slic3r::Seams {
std::optional<SeamChoice> maybe_choose_seam_point(
    const Perimeters::Perimeter &perimeter, const SeamPicker &seam_picker
) {
    using Perimeters::PointType;
    using Perimeters::PointClassification;

    std::vector<PointType>
        type_search_order{PointType::enforcer, PointType::common, PointType::blocker};
    std::vector<PointClassification> classification_search_order{
        PointClassification::embedded, PointClassification::common, PointClassification::overhang};
    for (const PointType point_type : type_search_order) {
        for (const PointClassification point_classification : classification_search_order) {
            if (std::optional<SeamChoice> seam_choice{
                    seam_picker(perimeter, point_type, point_classification)}) {
                return seam_choice;
            }
        }
        if (!Perimeters::extract_points(perimeter, point_type).empty()) {
            return std::nullopt;
        }
    }

    return std::nullopt;
}

SeamChoice choose_seam_point(const Perimeters::Perimeter &perimeter, const SeamPicker &seam_picker) {
    using Perimeters::PointType;
    using Perimeters::PointClassification;

    std::optional<SeamChoice> seam_choice{maybe_choose_seam_point(perimeter, seam_picker)};

    if (seam_choice) {
        return *seam_choice;
    }

    // Failed to choose any reasonable point!
    return SeamChoice{0, 0, perimeter.positions.front()};
}

std::optional<SeamChoice> choose_degenerate_seam_point(const Perimeters::Perimeter &perimeter) {
    if (!perimeter.positions.empty()) {
        return SeamChoice{0, 0, perimeter.positions.front()};
    }
    return std::nullopt;
}

} // namespace Slic3r::Seams
