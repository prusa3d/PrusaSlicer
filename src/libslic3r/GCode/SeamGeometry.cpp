#include "libslic3r/GCode/SeamGeometry.hpp"

#include <numeric>
#include <cmath>
#include <iterator>
#include <limits>
#include <cinttypes>

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/AABBTreeLines.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/LayerRegion.hpp"

namespace Slic3r::Seams::Geometry {

namespace MappingImpl {

/**
 * @brief Return 0, 1, ..., size - 1.
 */
std::vector<std::size_t> range(std::size_t size) {
    std::vector<std::size_t> result(size);
    std::iota(result.begin(), result.end(), 0);
    return result;
}

/**
 * @brief A link between lists.
 */
struct Link
{
    std::size_t bucket_id;
    double weight;
};

/**
 * @brief Get optional values. Replace any nullopt Links with new_bucket_id and increment new_bucket_id.
 *
 * @param links A list of optional links.
 * @param new_bucket_id In-out parameter incremented on each nullopt replacement.
 */
std::vector<std::size_t> assign_buckets(
    const std::vector<std::optional<Link>> &links, std::size_t &new_bucket_id
) {
    std::vector<std::size_t> result;
    std::transform(
        links.begin(), links.end(), std::back_inserter(result),
        [&](const std::optional<Link> &link) {
            if (link) {
                return link->bucket_id;
            }
            return new_bucket_id++;
        }
    );
    return result;
}
} // namespace MappingImpl

Vec2d get_normal(const Vec2d &vector) { return Vec2d{vector.y(), -vector.x()}.normalized(); }

Vec2d get_polygon_normal(
    const std::vector<Vec2d> &points, const std::size_t index, const double min_arm_length
) {
    std::optional<std::size_t> previous_index;
    std::optional<std::size_t> next_index;

    visit_near_forward(index, points.size(), [&](const std::size_t index_candidate) {
        if (index == index_candidate) {
            return false;
        }
        const double distance{(points[index_candidate] - points[index]).norm()};
        if (distance > min_arm_length) {
            next_index = index_candidate;
            return true;
        }
        return false;
    });
    visit_near_backward(index, points.size(), [&](const std::size_t index_candidate) {
        const double distance{(points[index_candidate] - points[index]).norm()};
        if (distance > min_arm_length) {
            previous_index = index_candidate;
            return true;
        }
        return false;
    });

    if (previous_index && next_index) {
        const Vec2d previous_normal{
            Geometry::get_normal(points.at(index) - points.at(*previous_index))};
        const Vec2d next_normal{Geometry::get_normal(points.at(*next_index) - points.at(index))};
        return (previous_normal + next_normal).normalized();
    }
    return Vec2d::Zero();
}

std::pair<Vec2d, double> distance_to_segment_squared(const Linef &segment, const Vec2d &point) {
    Vec2d segment_point;
    const double distance{line_alg::distance_to_squared(segment, point, &segment_point)};
    return {segment_point, distance};
}

std::pair<Mapping, std::size_t> get_mapping(
    const std::vector<std::size_t> &list_sizes, const MappingOperator &mapping_operator
) {
    using namespace MappingImpl;

    std::vector<std::vector<std::size_t>> result;
    result.reserve(list_sizes.size());
    result.push_back(range(list_sizes.front()));

    std::size_t new_bucket_id{result.back().size()};

    for (std::size_t layer_index{0}; layer_index < list_sizes.size() - 1; ++layer_index) {
        // Current layer is already assigned mapping.

        // Links on the next layer to the current layer.
        std::vector<std::optional<Link>> links(list_sizes[layer_index + 1]);

        for (std::size_t item_index{0}; item_index < list_sizes[layer_index]; ++item_index) {
            const MappingOperatorResult next_item{
                mapping_operator(layer_index, item_index)};
            if (next_item) {
                const auto [index, weight] = *next_item;
                const Link link{result.back()[item_index], weight};
                if (!links[index] || links[index]->weight < link.weight) {
                    links[index] = link;
                }
            }
        }
        result.push_back(assign_buckets(links, new_bucket_id));
    }
    return {result, new_bucket_id};
}

Extrusion::Extrusion(
    Polygon &&polygon,
    BoundingBox bounding_box,
    const double width,
    const ExPolygon &island_boundary
)
    : polygon(polygon)
    , bounding_box(std::move(bounding_box))
    , width(width)
    , island_boundary(island_boundary) {
    this->island_boundary_bounding_boxes.push_back(island_boundary.contour.bounding_box());

    std::transform(
        this->island_boundary.holes.begin(), this->island_boundary.holes.end(),
        std::back_inserter(this->island_boundary_bounding_boxes),
        [](const Polygon &polygon) { return polygon.bounding_box(); }
    );
}

Geometry::Extrusions get_external_perimeters(const Slic3r::Layer &layer, const LayerSlice &slice) {
    std::vector<Geometry::Extrusion> result;
    for (const LayerIsland &island : slice.islands) {
        const LayerRegion &layer_region = *layer.get_region(island.perimeters.region());
        for (const uint32_t perimeter_id : island.perimeters) {
            const auto collection{static_cast<const ExtrusionEntityCollection *>(
                layer_region.perimeters().entities[perimeter_id]
            )};
            for (const ExtrusionEntity *entity : *collection) {
                if (entity->role().is_external_perimeter()) {
                    Polygon polygon{entity->as_polyline().points};
                    const BoundingBox bounding_box{polygon.bounding_box()};
                    const double width{layer_region.flow(FlowRole::frExternalPerimeter).width()};
                    result.emplace_back(std::move(polygon), bounding_box, width, island.boundary);
                }
            }
        }
    }
    return result;
}

std::vector<Extrusions> get_extrusions(tcb::span<const Slic3r::Layer *const> object_layers) {
    std::vector<Extrusions> result;
    result.reserve(object_layers.size());

    for (const Slic3r::Layer *object_layer : object_layers) {
        Extrusions extrusions;

        for (const LayerSlice &slice : object_layer->lslices_ex) {
            std::vector<Extrusion> external_perimeters{
                get_external_perimeters(*object_layer, slice)};
            for (Geometry::Extrusion &extrusion : external_perimeters) {
                extrusions.push_back(std::move(extrusion));
            }
        }

        result.push_back(std::move(extrusions));
    }

    return result;
}

BoundedPolygons project_to_geometry(const Geometry::Extrusions &external_perimeters, const double max_bb_distance) {
    BoundedPolygons result;
    result.reserve(external_perimeters.size());

    using std::transform, std::back_inserter;

    transform(
        external_perimeters.begin(), external_perimeters.end(), back_inserter(result),
        [&](const Geometry::Extrusion &external_perimeter) {
            const auto [choosen_index, _]{Geometry::pick_closest_bounding_box(
                external_perimeter.bounding_box,
                external_perimeter.island_boundary_bounding_boxes
            )};

            const double distance{Geometry::bounding_box_distance(
                external_perimeter.island_boundary_bounding_boxes[choosen_index],
                external_perimeter.bounding_box
            )};

            if (distance > max_bb_distance) {
                Polygons expanded_extrusion{expand(external_perimeter.polygon, Slic3r::scaled(external_perimeter.width / 2.0))};
                if (!expanded_extrusion.empty()) {
                    return BoundedPolygon{
                        expanded_extrusion.front(), expanded_extrusion.front().bounding_box(), external_perimeter.polygon.is_clockwise(), 0.0
                    };
                }
            }

            const bool is_hole{choosen_index != 0};
            const Polygon &adjacent_boundary{
                !is_hole ? external_perimeter.island_boundary.contour :
                           external_perimeter.island_boundary.holes[choosen_index - 1]};
            return BoundedPolygon{adjacent_boundary, external_perimeter.island_boundary_bounding_boxes[choosen_index], is_hole, 0.0};
        }
    );
    return result;
}

std::vector<BoundedPolygons> project_to_geometry(const std::vector<Geometry::Extrusions> &extrusions, const double max_bb_distance) {
    std::vector<BoundedPolygons> result(extrusions.size());

    for (std::size_t layer_index{0}; layer_index < extrusions.size(); ++layer_index) {
        result[layer_index] = project_to_geometry(extrusions[layer_index], max_bb_distance);
    }

    return result;
}

std::vector<BoundedPolygons> convert_to_geometry(const std::vector<Geometry::Extrusions> &extrusions) {
    std::vector<BoundedPolygons> result;
    result.reserve(extrusions.size());

    for (const Geometry::Extrusions &layer : extrusions) {
        result.emplace_back();

        using std::transform, std::back_inserter;
        transform(
            layer.begin(), layer.end(), back_inserter(result.back()),
            [&](const Geometry::Extrusion &extrusion) {
                return BoundedPolygon{
                    extrusion.polygon, extrusion.bounding_box, extrusion.polygon.is_clockwise(), extrusion.width / 2.0
                };
            }
        );
    }

    return result;
}

std::vector<Vec2d> oversample_edge(const Vec2d &from, const Vec2d &to, const double max_distance) {
    const double total_distance{(from - to).norm()};
    const auto points_count{static_cast<std::size_t>(std::ceil(total_distance / max_distance)) + 1};
    if (points_count < 3) {
        return {};
    }
    const double step_size{total_distance / (points_count - 1)};
    const Vec2d step_vector{step_size * (to - from).normalized()};
    std::vector<Vec2d> result;
    result.reserve(points_count - 2);
    for (std::size_t i{1}; i < points_count - 1; ++i) {
        result.push_back(from + i * step_vector);
    }
    return result;
}

void visit_near_forward(
    const std::size_t start_index,
    const std::size_t loop_size,
    const std::function<bool(std::size_t)> &visitor
) {
    std::size_t last_index{loop_size - 1};
    std::size_t index{start_index};
    for (unsigned _{0}; _ < 30; ++_) { // Do not visit too far.
        if (visitor(index)) {
            return;
        }
        index = index == last_index ? 0 : index + 1;
    }
}

void visit_near_backward(
    const std::size_t start_index,
    const std::size_t loop_size,
    const std::function<bool(std::size_t)> &visitor
) {
    std::size_t last_index{loop_size - 1};
    std::size_t index{start_index == 0 ? loop_size - 1 : start_index - 1};
    for (unsigned _{0}; _ < 30; ++_) { // Do not visit too far.
        if (visitor(index)) {
            return;
        }
        index = index == 0 ? last_index : index - 1;
    }
}

std::vector<Vec2d> unscaled(const Points &points) {
    std::vector<Vec2d> result;
    result.reserve(points.size());
    using std::transform, std::begin, std::end, std::back_inserter;
    transform(begin(points), end(points), back_inserter(result), [](const Point &point) {
        return unscaled(point);
    });
    return result;
}

std::vector<Linef> unscaled(const Lines &lines) {
    std::vector<Linef> result;
    result.reserve(lines.size());
    std::transform(lines.begin(), lines.end(), std::back_inserter(result), [](const Line &line) {
        return Linef{unscaled(line.a), unscaled(line.b)};
    });
    return result;
}

Points scaled(const std::vector<Vec2d> &points) {
    Points result;
    for (const Vec2d &point : points) {
        result.push_back(Slic3r::scaled(point));
    }
    return result;
}

std::vector<double> get_embedding_distances(
    const std::vector<Vec2d> &points, const AABBTreeLines::LinesDistancer<Linef> &perimeter_distancer
) {
    std::vector<double> result;
    result.reserve(points.size());
    using std::transform, std::back_inserter;
    transform(points.begin(), points.end(), back_inserter(result), [&](const Vec2d &point) {
        const double distance{perimeter_distancer.distance_from_lines<true>(point)};
        return distance < 0 ? -distance : 0.0;
    });
    return result;
}

std::vector<double> get_overhangs(
    const std::vector<Vec2d> &points,
    const AABBTreeLines::LinesDistancer<Linef> &previous_layer_perimeter_distancer,
    const double layer_height
) {
    std::vector<double> result;
    result.reserve(points.size());
    using std::transform, std::back_inserter;
    transform(points.begin(), points.end(), back_inserter(result), [&](const Vec2d &point) {
        const double distance{previous_layer_perimeter_distancer.distance_from_lines<true>(point)};
        return distance > 0 ? M_PI / 2 - std::atan(layer_height / distance) : 0.0;
    });
    return result;
}

// Measured from outside, convex is positive
std::vector<double> get_vertex_angles(const std::vector<Vec2d> &points, const double min_arm_length) {
    std::vector<double> result;
    result.reserve(points.size());

    for (std::size_t index{0}; index < points.size(); ++index) {
        std::optional<std::size_t> previous_index;
        std::optional<std::size_t> next_index;

        visit_near_forward(index, points.size(), [&](const std::size_t index_candidate) {
            if (index == index_candidate) {
                return false;
            }
            const double distance{(points[index_candidate] - points[index]).norm()};
            if (distance > min_arm_length) {
                next_index = index_candidate;
                return true;
            }
            return false;
        });
        visit_near_backward(index, points.size(), [&](const std::size_t index_candidate) {
            const double distance{(points[index_candidate] - points[index]).norm()};
            if (distance > min_arm_length) {
                previous_index = index_candidate;
                return true;
            }
            return false;
        });

        if (previous_index && next_index) {
            const Vec2d &previous_point = points[*previous_index];
            const Vec2d &point = points[index];
            const Vec2d &next_point = points[*next_index];
            result.push_back(-angle((point - previous_point), (next_point - point)));
        } else {
            result.push_back(0.0);
        }
    }

    return result;
}

double bounding_box_distance(const BoundingBox &a, const BoundingBox &b) {
    const double bb_max_distance{unscaled(Point{a.max - b.max}).norm()};
    const double bb_min_distance{unscaled(Point{a.min - b.min}).norm()};
    return std::max(bb_max_distance, bb_min_distance);
}

std::pair<std::size_t, double> pick_closest_bounding_box(
    const BoundingBox &to, const BoundingBoxes &choose_from
) {
    double min_distance{std::numeric_limits<double>::infinity()};
    std::size_t choosen_index{0};

    for (std::size_t i{0}; i < choose_from.size(); ++i) {
        const BoundingBox &candidate{choose_from[i]};
        const double distance{bounding_box_distance(candidate, to)};

        if (distance < min_distance) {
            choosen_index = i;
            min_distance = distance;
        }
    }
    return {choosen_index, min_distance};
}

Polygon to_polygon(const ExtrusionLoop &loop) {
    Points loop_points{};
    for (const ExtrusionPath &path : loop.paths) {
        for (const Point &point : path.polyline.points) {
            loop_points.push_back(point);
        }
    }
    return Polygon{loop_points};
}
} // namespace Slic3r::Seams::Geometry
