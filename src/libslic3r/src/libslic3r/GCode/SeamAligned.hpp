#ifndef libslic3r_SeamAligned_hpp_
#define libslic3r_SeamAligned_hpp_

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

#include "libslic3r/GCode/SeamPerimeters.hpp"
#include "libslic3r/GCode/SeamChoice.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/GCode/SeamShells.hpp"

namespace Slic3r::ModelInfo {
    struct Visibility;
}

namespace Slic3r::Seams::Aligned {

using SeamChoiceVisibility  = std::function<double(const SeamChoice &, const Perimeters::Perimeter &)>;

namespace Impl {
struct SeamOptions
{
    std::size_t closest;
    std::size_t adjacent;
    bool adjacent_forward;
    std::optional<std::size_t> snapped;
    Vec2d on_edge;
};

SeamChoice pick_seam_option(const Perimeters::Perimeter &perimeter, const SeamOptions &options);

std::optional<std::size_t> snap_to_angle(
    const Vec2d &point,
    const std::size_t search_start,
    const Perimeters::Perimeter &perimeter,
    const double max_detour
);

SeamOptions get_seam_options(
    const Perimeters::Perimeter &perimeter,
    const Vec2d &prefered_position,
    const Perimeters::Perimeter::PointTree &points_tree,
    const double max_detour
);

struct Nearest
{
    Vec2d prefered_position;
    double max_detour;

    std::optional<SeamChoice> operator()(
        const Perimeters::Perimeter &perimeter,
        const Perimeters::PointType point_type,
        const Perimeters::PointClassification point_classification
    ) const;
};

struct LeastVisible
{
    std::optional<SeamChoice> operator()(
        const Perimeters::Perimeter &perimeter,
        const Perimeters::PointType point_type,
        const Perimeters::PointClassification point_classification
    ) const;

    const std::vector<double> &precalculated_visibility;
};
}


struct VisibilityCalculator
{
    const Slic3r::ModelInfo::Visibility &points_visibility;
    double convex_visibility_modifier;
    double concave_visibility_modifier;

    double operator()(const SeamChoice &choice, const Perimeters::Perimeter &perimeter) const;

private:
    static double get_angle_visibility_modifier(
        const double angle,
        const double convex_visibility_modifier,
        const double concave_visibility_modifier
    );
};

struct Params {
    double max_detour{};
    double jump_visibility_threshold{};
    double continuity_modifier{};
};

std::vector<std::vector<SeamPerimeterChoice>> get_object_seams(
    Shells::Shells<> &&shells,
    const SeamChoiceVisibility& visibility_calculator,
    const Params& params
);

} // namespace Slic3r::Seams::Aligned

#endif // libslic3r_SeamAligned_hpp_
