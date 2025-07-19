#include "libslic3r/GCode/SeamScarf.hpp"
#include "libslic3r/GCode/SmoothPath.hpp"

namespace Slic3r::Seams::Scarf {

namespace Impl {
PathPoint get_path_point(
    const ExtrusionPaths &paths, const Point &point, const std::size_t global_index
) {
    std::size_t path_start_index{0};
    for (std::size_t path_index{0}; path_index < paths.size(); ++path_index) {
        const ExtrusionPath &path{paths[path_index]};
        if (global_index - path_start_index < path.size()) {
            return {point, path_index, global_index - path_start_index};
        }
        path_start_index += path.size();
    }
    throw std::runtime_error("Failed translating global path index!");
}

std::pair<ExtrusionPath, ExtrusionPath> split_path(
    const ExtrusionPath &path, const Point &point, const std::size_t point_previous_index
) {
    if (static_cast<int>(point_previous_index) >= static_cast<int>(path.size()) - 1) {
        throw std::runtime_error(
            "Invalid path split index " + std::to_string(point_previous_index) +
            " for path of size " + std::to_string(path.size()) + "!"
        );
    }
    const Point previous_point{path.polyline.points.at(point_previous_index)};
    const Point next_point{path.polyline.points.at(point_previous_index + 1)};

    Polyline first;
    for (std::size_t i{0}; i <= point_previous_index; ++i) {
        first.points.push_back(path.polyline[i]);
    }
    first.points.push_back(point);
    Polyline second;
    second.points.push_back(point);

    for (std::size_t i{point_previous_index + 1}; i < path.size(); ++i) {
        second.points.push_back(path.polyline[i]);
    }

    return {ExtrusionPath{first, path.attributes()}, ExtrusionPath{second, path.attributes()}};
}

ExtrusionPaths split_paths(ExtrusionPaths &&paths, const PathPoint &path_point) {
    ExtrusionPaths result{std::move(paths)};
    std::pair<ExtrusionPath, ExtrusionPath> split{
        split_path(result[path_point.path_index], path_point.point, path_point.previous_point_on_path_index)};

    auto path_iterator{result.begin() + path_point.path_index};
    path_iterator = result.erase(path_iterator);
    path_iterator = result.insert(path_iterator, split.second);
    result.insert(path_iterator, split.first);

    return result;
}

double get_length(tcb::span<const GCode::SmoothPathElement> smooth_path) {
    if (smooth_path.empty() || smooth_path.front().path.empty()) {
        return 0;
    }

    double result{0};

    Point previous_point{smooth_path.front().path.front().point};

    for (const GCode::SmoothPathElement &element : smooth_path) {
        for (const Geometry::ArcWelder::Segment &segment : element.path) {
            result += (segment.point - previous_point).cast<double>().norm();
            previous_point = segment.point;
        }
    }
    return result;
}

GCode::SmoothPath convert_to_smooth(tcb::span<const ExtrusionPath> paths) {
    GCode::SmoothPath result;
    for (const ExtrusionPath &path : paths) {
        Geometry::ArcWelder::Path smooth_path;
        for (const Point &point : path.polyline) {
            smooth_path.push_back(Geometry::ArcWelder::Segment{point});
        }
        result.push_back({path.attributes(), smooth_path});
    }

    return result;
}

Points linspace(const Point &from, const Point &to, const std::size_t count) {
    if (count < 2) {
        throw std::runtime_error("Invalid count for linspace!");
    }

    Points result;
    result.push_back(from);

    Point offset{(to - from) / (count - 1)};
    for (std::size_t i{1}; i < count - 1; ++i) {
        result.push_back(from + i * offset);
    }
    result.push_back(to);

    return result;
}

Points ensure_max_distance(const Points &points, const double max_distance) {
    if (points.size() < 2) {
        return points;
    }

    Points result;
    result.push_back(points.front());
    for (std::size_t i{1}; i < points.size(); ++i) {
        const Point &current_point{points[i]};
        const Point &previous_point{points[i - 1]};
        const double distance = (current_point - previous_point).cast<double>().norm();

        if (distance > max_distance) {
            const std::size_t points_count{
                static_cast<std::size_t>(std::ceil(distance / max_distance)) + 1};
            const Points subdivided{linspace(previous_point, current_point, points_count)};
            result.insert(result.end(), std::next(subdivided.begin()), subdivided.end());
        } else {
            result.push_back(current_point);
        }
    }
    return result;
}

ExtrusionPaths ensure_scarf_resolution(
    ExtrusionPaths &&paths, const std::size_t scarf_paths_count, const double max_distance
) {
    ExtrusionPaths result{std::move(paths)};
    auto scarf{tcb::span{result}.first(scarf_paths_count)};

    for (ExtrusionPath &path : scarf) {
        path.polyline.points = ensure_max_distance(path.polyline.points, max_distance);
    }

    return result;
}

GCode::SmoothPath lineary_increase_extrusion_height(
    GCode::SmoothPath &&smooth_path, const double start_height
) {
    GCode::SmoothPath result{std::move(smooth_path)};
    const double length{get_length(result)};
    double distance{0};

    std::optional<Point> previous_point{};
    for (GCode::SmoothPathElement &element : result) {
        for (Geometry::ArcWelder::Segment &segment : element.path) {
            if (!previous_point) {
                segment.e_fraction = 0;
                segment.height_fraction = start_height;
            } else {
                distance += (segment.point - *previous_point).cast<double>().norm();

                if (distance >= length) {
                    segment.e_fraction = 1.0;
                    segment.height_fraction = 1.0;
                } else {
                    segment.e_fraction = distance / length;
                    segment.height_fraction = start_height + (distance / length) * (1.0 - start_height);
                }
            }
            previous_point = segment.point;
        }
    }

    return result;
}

GCode::SmoothPath lineary_readuce_extrusion_amount(
    GCode::SmoothPath &&smooth_path
) {
    GCode::SmoothPath result{std::move(smooth_path)};
    const double length{get_length(result)};
    double distance{0};

    std::optional<Point> previous_point{};
    for (GCode::SmoothPathElement &element : result) {
        for (Geometry::ArcWelder::Segment &segment : element.path) {
            if (!previous_point) {
                segment.e_fraction = 1.0;
            } else {
                distance += (segment.point - *previous_point).cast<double>().norm();

                if (distance >= length) {
                    segment.e_fraction = 0.0;
                } else {
                    segment.e_fraction = 1.0 - distance / length;
                }
            }
            previous_point = segment.point;
        }
    }

    return result;
}

GCode::SmoothPath elevate_scarf(
    const ExtrusionPaths &paths,
    const std::size_t scarf_paths_count,
    const std::function<GCode::SmoothPath(tcb::span<const ExtrusionPath>)> &apply_smoothing,
    const double start_height
) {
    const auto scarf_at_start{tcb::span{paths}.first(scarf_paths_count)};
    GCode::SmoothPath first_segment{convert_to_smooth(scarf_at_start)};
    first_segment =
        lineary_increase_extrusion_height(std::move(first_segment), start_height);

    std::size_t normal_extrusions_size{paths.size() - 2 * scarf_paths_count};
    const auto normal_extrusions{
        tcb::span{paths}.subspan(scarf_paths_count, normal_extrusions_size)};
    const GCode::SmoothPath middle_segment{apply_smoothing(normal_extrusions)};

    const auto scarf_at_end{tcb::span{paths}.last(scarf_paths_count)};
    GCode::SmoothPath last_segment{convert_to_smooth(scarf_at_end)};
    last_segment =
        lineary_readuce_extrusion_amount(std::move(last_segment));

    first_segment.insert(first_segment.end(), middle_segment.begin(), middle_segment.end());
    first_segment.insert(first_segment.end(), last_segment.begin(), last_segment.end());

    return first_segment;
}

bool is_on_line(const Point &point, const Line &line, const double tolerance) {
    return line.distance_to_squared(point) < tolerance * tolerance;
}

std::optional<PathPoint> find_path_point_from_end(
    const ExtrusionPaths &paths,
    const Point &point,
    const double tolerance
) {
    if (paths.empty()) {
        return std::nullopt;
    }
    for (int path_index{static_cast<int>(paths.size() - 1)}; path_index >= 0; --path_index) {
        const ExtrusionPath &path{paths[path_index]};
        if (path.polyline.size() < 2) {
            throw std::runtime_error(
                "Invalid path: less than two points: " + std::to_string(path.size()) + "!"
            );
        }
        for (int point_index{static_cast<int>(path.polyline.size() - 2)}; point_index >= 0;
             --point_index) {
            const Point &previous_point{path.polyline[point_index + 1]};
            const Point &current_point{path.polyline[point_index]};
            const Line line{previous_point, current_point};
            if (is_on_line(point, line, tolerance)) {
                return PathPoint{
                    point,
                    static_cast<size_t>(path_index), static_cast<size_t>(point_index)
                };
            }
        }
    }
    return std::nullopt;
}

std::optional<PathPoint> get_point_offset_from_end(const ExtrusionPaths &paths, const double length) {
    double distance{0.0};

    if (paths.empty()) {
        return std::nullopt;
    }
    for (int path_index{static_cast<int>(paths.size() - 1)}; path_index >= 0; --path_index) {
        const ExtrusionPath &path{paths[path_index]};
        if (path.polyline.size() < 2) {
            throw std::runtime_error(
                "Invalid path: less than two points: " + std::to_string(path.size()) + "!"
            );
        }
        for (int point_index{static_cast<int>(path.polyline.size() - 2)}; point_index >= 0;
             --point_index) {
            const Point &previous_point{path.polyline[point_index + 1]};
            const Point &current_point{path.polyline[point_index]};
            const Vec2d edge{(current_point - previous_point).cast<double>()};
            const double edge_length{edge.norm()};
            const Vec2d edge_direction{edge.normalized()};
            if (distance + edge_length > length) {
                return PathPoint{
                    previous_point + (edge_direction * (length - distance)).cast<int>(),
                    static_cast<size_t>(path_index), static_cast<size_t>(point_index)};
            }
            distance += edge_length;
        }
    }
    return std::nullopt;
}

ExtrusionPaths reverse(ExtrusionPaths &&paths) {
    ExtrusionPaths result{std::move(paths)};
    std::reverse(result.begin(), result.end());
    for (ExtrusionPath &path : result) {
        std::reverse(path.polyline.begin(), path.polyline.end());
    }
    return result;
}
} // namespace Impl

std::pair<GCode::SmoothPath, std::size_t> add_scarf_seam(
    ExtrusionPaths &&paths,
    const Scarf &scarf,
    const std::function<GCode::SmoothPath(tcb::span<const ExtrusionPath>)> &apply_smoothing,
    const bool flipped
) {
    Impl::PathPoint end_point{
        Impl::get_path_point(paths, scarf.end_point, scarf.end_point_previous_index)};

    const ExtrusionPath &path{paths[end_point.path_index]};
    if (end_point.previous_point_on_path_index == static_cast<int>(path.size()) - 1) {
        // Last point of the path is picked. This is invalid for splitting.
        if (static_cast<int>(end_point.path_index) < static_cast<int>(paths.size()) - 2) {
            // First point of next path and last point of previous path should be the same.
            // Pick the first point of the next path.
            end_point = {end_point.point, end_point.path_index + 1, 0};
        } else {
            // There is no next path.
            // This should be very rare case.
            if (end_point.previous_point_on_path_index == 0) {
                throw std::runtime_error("Could not split path!");
            }
            end_point = {end_point.point, end_point.path_index, end_point.previous_point_on_path_index - 1};
        }
    }

    paths = split_paths(std::move(paths), end_point);

    // End with scarf.
    std::rotate(paths.begin(), std::next(paths.begin(), end_point.path_index + 1), paths.end());

    if (flipped) {
        paths = Impl::reverse(std::move(paths));
    }

    std::optional<Impl::PathPoint> start_point;
    if (!scarf.entire_loop) {
        const double tolerance{scaled(1e-2 /* mm */)};
        start_point = Impl::find_path_point_from_end(paths, scarf.start_point, tolerance);
    }
    if (!start_point) {
        start_point = Impl::PathPoint{
            paths.front().first_point(),
            0,
            0
        };
    }
    paths = split_paths(std::move(paths), *start_point);

    const std::size_t scarf_paths_count{paths.size() - start_point->path_index - 1};
    // Start with scarf.
    std::rotate(paths.begin(), std::next(paths.begin(), start_point->path_index + 1), paths.end());

    const double max_distance{scale_(scarf.max_segment_length)};
    paths = Impl::ensure_scarf_resolution(std::move(paths), scarf_paths_count, max_distance);
    // This reserve protects agains iterator invalidation.
    paths.reserve(paths.size() + scarf_paths_count);
    std::copy_n(paths.begin(), scarf_paths_count, std::back_inserter(paths));

    GCode::SmoothPath smooth_path{Impl::elevate_scarf(paths, scarf_paths_count, apply_smoothing, scarf.start_height)};
    return {std::move(smooth_path), scarf_paths_count};
}
} // namespace Slic3r::Seams::Scarf
