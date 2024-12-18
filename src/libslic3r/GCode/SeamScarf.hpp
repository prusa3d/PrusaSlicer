#ifndef libslic3r_SeamScarf_hpp_
#define libslic3r_SeamScarf_hpp_

#include "libslic3r/ExtrusionEntity.hpp"
#include "tcbspan/span.hpp"

namespace Slic3r::GCode {
    struct SmoothPathElement;
    using SmoothPath = std::vector<SmoothPathElement>;
}

namespace Slic3r::Seams::Scarf {

struct Scarf
{
    Point start_point;
    Point end_point;
    std::size_t end_point_previous_index{};
    double max_segment_length{};
    bool entire_loop{};
    double start_height{};
};

using SmoothingFunction = std::function<GCode::SmoothPath(tcb::span<const ExtrusionPath>)>;

namespace Impl {
struct PathPoint
{
    Point point;
    std::size_t path_index{};
    std::size_t previous_point_on_path_index{};
};

PathPoint get_path_point(
    const ExtrusionPaths &paths, const Point &point, const std::size_t global_index
);

std::pair<ExtrusionPath, ExtrusionPath> split_path(
    const ExtrusionPath &path, const Point &point, const std::size_t point_previous_index
);

ExtrusionPaths split_paths(ExtrusionPaths &&paths, const PathPoint &path_point);

double get_length(tcb::span<const GCode::SmoothPathElement> smooth_path);

GCode::SmoothPath convert_to_smooth(tcb::span<const ExtrusionPath> paths);

/**
 * @param count: Points count including the first and last point.
 */
Points linspace(const Point &from, const Point &to, const std::size_t count);

Points ensure_max_distance(const Points &points, const double max_distance);

ExtrusionPaths ensure_scarf_resolution(
    ExtrusionPaths &&paths, const std::size_t scarf_paths_count, const double max_distance
);

GCode::SmoothPath lineary_increase_extrusion_height(
    GCode::SmoothPath &&smooth_path, const double start_height
);

GCode::SmoothPath lineary_readuce_extrusion_amount(
    GCode::SmoothPath &&smooth_path
);

GCode::SmoothPath elevate_scarf(
    const ExtrusionPaths &paths,
    const std::size_t scarf_paths_count,
    const SmoothingFunction &apply_smoothing,
    const double start_height
);

std::optional<PathPoint> get_point_offset_from_end(const ExtrusionPaths &paths, const double length);

std::optional<PathPoint> find_path_point_from_end(
    const ExtrusionPaths &paths,
    const Point &point,
    const double tolerance
);
} // namespace Impl

std::pair<GCode::SmoothPath, std::size_t> add_scarf_seam(
    ExtrusionPaths &&paths,
    const Scarf &scarf,
    const SmoothingFunction &apply_smoothing,
    const bool flipped
);
} // namespace Slic3r::Seams::Scarf

#endif // libslic3r_SeamScarf_hpp_
