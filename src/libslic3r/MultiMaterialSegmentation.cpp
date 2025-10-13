///|/ Copyright (c) Prusa Research 2021 - 2023 Vojtěch Bubník @bubnikv, Lukáš Hejl @hejllukas
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include <boost/log/trivial.hpp>
#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/parallel_for.h>
#include <boost/container_hash/hash.hpp>
#include <utility>
#include <Eigen/Geometry>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <vector>
#include <cassert>
#include <cstdlib>

#include "BoundingBox.hpp"
#include "ClipperUtils.hpp"
#include "Layer.hpp"
#include "Print.hpp"
#include "Geometry/VoronoiUtils.hpp"
#include "MutablePolygon.hpp"
#include "admesh/stl.h"
#include "libslic3r/AABBTreeLines.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/format.hpp"
#include "libslic3r/Geometry/VoronoiOffset.hpp"
#include "libslic3r/LayerRegion.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Surface.hpp"
#include "libslic3r/SVG.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"
#include "libslic3r/TriangleSelector.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/libslic3r.h"
#include "MultiMaterialSegmentation.hpp"

constexpr bool MM_SEGMENTATION_DEBUG_GRAPH                = false;
constexpr bool MM_SEGMENTATION_DEBUG_REGIONS              = false;
constexpr bool MM_SEGMENTATION_DEBUG_INPUT                = false;
constexpr bool MM_SEGMENTATION_DEBUG_FILTERED_COLOR_LINES = false;
constexpr bool MM_SEGMENTATION_DEBUG_COLOR_RANGES         = false;
constexpr bool MM_SEGMENTATION_DEBUG_COLORIZED_POLYGONS   = false;
constexpr bool MM_SEGMENTATION_DEBUG_TOP_BOTTOM           = false;

namespace Slic3r {

const constexpr double POLYGON_FILTER_MIN_AREA_SCALED                 = scaled<double>(0.1f);
const constexpr double POLYGON_FILTER_MIN_OFFSET_SCALED               = scaled<double>(0.01f);
const constexpr double POLYGON_COLOR_FILTER_DISTANCE_SCALED           = scaled<double>(0.2);
const constexpr double POLYGON_COLOR_FILTER_TOLERANCE_SCALED          = scaled<double>(0.02);
const constexpr double INPUT_POLYGONS_FILTER_TOLERANCE_SCALED         = scaled<double>(0.001);
const constexpr double MM_SEGMENTATION_MAX_PROJECTION_DISTANCE_SCALED = scaled<double>(0.4);
const constexpr double MM_SEGMENTATION_MAX_SNAP_DISTANCE_SCALED       = scaled<double>(0.01);
const constexpr double MM_SEGMENTATION_SNAP_ANGLE_THRESHOLD           = PI / 12.;
const constexpr double MM_SEGMENTATION_SNAP_ANGLE_MAX_DISTANCE_SCALED = scaled<double>(0.1);

enum VD_ANNOTATION : Voronoi::VD::cell_type::color_type {
    VERTEX_ON_CONTOUR = 1,
    DELETED           = 2
};

struct ColorLine {
    static const constexpr int Dim = 2;
    using Scalar = Point::Scalar;

    ColorLine(const Point &a, const Point &b, ColorPolygon::Color color) : a(a), b(b), color(color) {}

    Point               a;
    Point               b;
    ColorPolygon::Color color;

    Line line() const { return {a, b}; }
};

using ColorLines = std::vector<ColorLine>;

struct ColorChange {
    explicit ColorChange(double t, uint8_t color_next) : t(t), color_next(color_next) {}

    // Relative position on the line from range <0, 1>
    double  t          = 0.;
    // Color after (including) t value on the line.
    uint8_t color_next = 0;

    friend bool operator<(const ColorChange &lhs, const ColorChange &rhs) { return lhs.t < rhs.t; }
};

using ColorChanges = std::vector<ColorChange>;

struct ColorProjectionRange {
    ColorProjectionRange() = delete;

    ColorProjectionRange(double from_t, double from_distance, double to_t, double to_distance, ColorPolygon::Color color)
        : from_t(from_t), from_distance(from_distance), to_t(to_t), to_distance(to_distance), color(color)
    {}

    double              from_t        = 0.;
    double              from_distance = 0.;

    double              to_t          = 0.;
    double              to_distance   = 0.;

    ColorPolygon::Color color         = 0;

    bool contains(const double t) const { return this->from_t <= t && t <= to_t; }

    double distance_at(const double t) const {
        assert(this->to_t != this->from_t);
        return (t - this->from_t) / (this->to_t - this->from_t) * (this->to_distance - this->from_distance) + this->from_distance;
    }

    friend bool operator<(const ColorProjectionRange &lhs, const ColorProjectionRange &rhs) {
        return lhs.from_t < rhs.from_t || (lhs.from_t == rhs.from_t && lhs.from_distance < rhs.from_distance);
    }

    friend bool operator==(const ColorProjectionRange &lhs, const ColorProjectionRange &rhs) {
        return lhs.from_t == rhs.from_t && lhs.from_distance == rhs.from_distance && lhs.to_t == rhs.to_t && lhs.to_distance == rhs.to_distance && lhs.color == rhs.color;
    }
};

using ColorProjectionRanges = std::vector<ColorProjectionRange>;

struct ColorProjectionLine
{
    explicit ColorProjectionLine(const Line &line) : a(line.a), b(line.b){};

    Point        a;
    Point        b;

    ColorProjectionRanges color_projection_ranges;
    ColorChanges          color_changes;
};

using ColorProjectionLines = std::vector<ColorProjectionLine>;

struct ColorProjectionExPolygon
{
    ColorProjectionLines              contour;
    std::vector<ColorProjectionLines> holes;

    size_t polygons_count() const { return holes.size() + 1; }

    size_t lines_count() const
    {
        size_t lines_cnt = this->contour.size();
        for (const ColorProjectionLines &hole : this->holes) {
            lines_cnt += hole.size();
        }

        return lines_cnt;
    }
};

using ColorProjectionExPolygons = std::vector<ColorProjectionExPolygon>;

size_t lines_count(const ColorProjectionExPolygons &color_projection_expolygons)
{
    return std::accumulate(color_projection_expolygons.begin(), color_projection_expolygons.end(), size_t(0),
                           [](const size_t acc, const ColorProjectionExPolygon &color_projection_expolygon) {
                               return acc + color_projection_expolygon.lines_count();
                           });
}

struct ColorProjectionLineWrapper
{
    static const constexpr int Dim = 2;
    using Scalar = Point::Scalar;

    explicit ColorProjectionLineWrapper(ColorProjectionLine *const color_projection_line) : a(color_projection_line->a), b(color_projection_line->b), color_projection_line(color_projection_line){};

    const Point                a;
    const Point                b;
    ColorProjectionLine *const color_projection_line;
};

struct ColorPoint
{
    Point   p;
    uint8_t color_prev;
    uint8_t color_next;

    explicit ColorPoint(const Point &p, uint8_t color_prev, uint8_t color_next) : p(p), color_prev(color_prev), color_next(color_next) {}
};

using ColorPoints = std::vector<ColorPoint>;


[[maybe_unused]] static void export_graph_to_svg(const std::string &path, const Voronoi::VD& vd, const std::vector<ColoredLines>& colored_polygons) {
    const coordf_t                 stroke_width = scaled<coordf_t>(0.05f);
    const BoundingBox              bbox         = get_extents(colored_polygons);

    SVG svg(path.c_str(), bbox);
    for (const ColoredLines &colored_lines : colored_polygons)
        for (const ColoredLine &colored_line : colored_lines)
            svg.draw(colored_line.line, "black", stroke_width);

    for (const Voronoi::VD::vertex_type &vertex : vd.vertices()) {
        if (Geometry::VoronoiUtils::is_in_range<coord_t>(vertex)) {
            if (const Point pt = Geometry::VoronoiUtils::to_point(&vertex).cast<coord_t>(); vertex.color() == VD_ANNOTATION::VERTEX_ON_CONTOUR) {
                svg.draw(pt, "blue", coord_t(stroke_width));
            } else if (vertex.color() != VD_ANNOTATION::DELETED) {
                svg.draw(pt, "green", coord_t(stroke_width));
            }
        }
    }

    for (const Voronoi::VD::edge_type &edge : vd.edges()) {
        if (edge.is_infinite() || !Geometry::VoronoiUtils::is_in_range<coord_t>(edge))
            continue;

        const Point from = Geometry::VoronoiUtils::to_point(edge.vertex0()).cast<coord_t>();
        const Point to   = Geometry::VoronoiUtils::to_point(edge.vertex1()).cast<coord_t>();

        if (edge.color() != VD_ANNOTATION::DELETED)
            svg.draw(Line(from, to), "red", stroke_width);
    }
}

[[maybe_unused]] static void export_regions_to_svg(const std::string &path, const std::vector<ExPolygons> &regions, const ExPolygons &lslices) {
    const std::vector<std::string> colors       = {"blue", "cyan", "red", "orange", "magenta", "pink", "purple", "yellow"};
    const coordf_t                 stroke_width = scaled<coordf_t>(0.05);
    const BoundingBox              bbox         = get_extents(lslices);

    ::Slic3r::SVG svg(path.c_str(), bbox);
    svg.draw_outline(lslices, "green", "lime", stroke_width);

    for (const ExPolygons &by_extruder : regions) {
        if (const size_t extrude_idx = &by_extruder - &regions.front(); extrude_idx < colors.size()) {
            svg.draw(by_extruder, colors[extrude_idx]);
        } else {
            svg.draw(by_extruder, "black");
        }
    }
}

[[maybe_unused]] void export_processed_input_expolygons_to_svg(const std::string &path, const LayerRegionPtrs &regions, const ExPolygons &processed_input_expolygons) {
    const coordf_t stroke_width = scaled<coordf_t>(0.05);
    BoundingBox    bbox         = get_extents(regions);
    bbox.merge(get_extents(processed_input_expolygons));

    ::Slic3r::SVG svg(path.c_str(), bbox);

    for (LayerRegion *region : regions) {
        for (const Surface &surface : region->slices()) {
            svg.draw_outline(surface, "blue", "cyan", stroke_width);
        }
    }

    svg.draw_outline(processed_input_expolygons, "red", "pink", stroke_width);
}

[[maybe_unused]] static void export_color_polygons_points_to_svg(const std::string &path, const std::vector<ColorPoints> &color_polygons_points, const ExPolygons &lslices) {
    const std::vector<std::string> colors       = {"aqua", "black", "blue", "fuchsia", "gray", "green", "lime", "maroon", "navy", "olive", "purple", "red", "silver", "teal", "yellow"};
    const coordf_t                 stroke_width = scaled<coordf_t>(0.02);
    const BoundingBox              bbox         = get_extents(lslices);

    ::Slic3r::SVG svg(path.c_str(), bbox);

    for (const ColorPoints &color_polygon_points : color_polygons_points) {
        for (size_t pt_idx = 1; pt_idx < color_polygon_points.size(); ++pt_idx) {
            const ColorPoint &prev_color_pt = color_polygon_points[pt_idx - 1];
            const ColorPoint &curr_color_pt = color_polygon_points[pt_idx];
            svg.draw(Line(prev_color_pt.p, curr_color_pt.p), colors[prev_color_pt.color_next]);
        }

        svg.draw(Line(color_polygon_points.back().p, color_polygon_points.front().p), colors[color_polygon_points.back().color_next], stroke_width);
    }
}

[[maybe_unused]] static void export_color_polygons_to_svg(const std::string &path, const ColorPolygons &color_polygons, const ExPolygons &lslices) {
    const std::vector<std::string> colors        = {"blue", "cyan", "red", "orange", "pink", "yellow", "magenta", "purple", "black"};
    const std::string              default_color = "black";
    const coordf_t                 stroke_width  = scaled<coordf_t>(0.05);
    const BoundingBox              bbox          = get_extents(lslices);

    ::Slic3r::SVG svg(path.c_str(), bbox);
    for (const ColorPolygon &color_polygon : color_polygons) {
        for (size_t pt_idx = 1; pt_idx < color_polygon.size(); ++pt_idx) {
            const uint8_t color = color_polygon.colors[pt_idx - 1];
            svg.draw(Line(color_polygon.points[pt_idx - 1], color_polygon.points[pt_idx]), (color < colors.size() ? colors[color] : default_color), stroke_width);
        }

        const uint8_t color = color_polygon.colors.back();
        svg.draw(Line(color_polygon.points.back(), color_polygon.points.front()), (color < colors.size() ? colors[color] : default_color), stroke_width);
    }
}

[[maybe_unused]] static void export_color_polygons_lines_to_svg(const std::string &path, const std::vector<ColorLines> &color_polygons_lines, const ExPolygons &lslices) {
    const std::vector<std::string> colors        = {"blue", "cyan", "red", "orange", "pink", "yellow", "magenta", "purple", "black"};
    const std::string              default_color = "black";
    const coordf_t                 stroke_width  = scaled<coordf_t>(0.05);
    const BoundingBox              bbox          = get_extents(lslices);

    ::Slic3r::SVG svg(path.c_str(), bbox);
    for (const ColorLines &color_polygon_lines : color_polygons_lines) {
        for (const ColorLine &color_line : color_polygon_lines) {
            svg.draw(Line(color_line.a, color_line.b), (color_line.color < colors.size() ? colors[color_line.color] : default_color), stroke_width);
        }
    }
}

[[maybe_unused]] static void export_color_projection_lines_color_ranges_to_svg(const std::string &path, const ColorProjectionExPolygon &color_projection_expolygon, const ExPolygons &lslices) {
    const std::vector<std::string> colors        = {"blue", "cyan", "red", "orange", "pink", "yellow", "magenta", "purple", "black"};
    const std::string              default_color = "black";
    const coordf_t                 stroke_width  = scaled<coordf_t>(0.05);
    const BoundingBox              bbox          = get_extents(lslices);

    ::Slic3r::SVG svg(path.c_str(), bbox);

    const auto draw_color_projection_line = [&](const ColorProjectionLines &color_projection_lines) -> void {
        for (const ColorProjectionLine &color_projection_line : color_projection_lines) {
            svg.draw(Line(color_projection_line.a, color_projection_line.b), default_color, stroke_width);

            for (const ColorProjectionRange &range : color_projection_line.color_projection_ranges) {
                const Vec2d color_projection_line_vec = (color_projection_line.b - color_projection_line.a).cast<double>();
                const Point from_pt                   = (range.from_t * color_projection_line_vec).cast<coord_t>() + color_projection_line.a;
                const Point to_pt                     = (range.to_t * color_projection_line_vec).cast<coord_t>() + color_projection_line.a;

                svg.draw(Line(from_pt, to_pt), (range.color < colors.size() ? colors[range.color] : default_color), stroke_width);
            }
        }
    };

    draw_color_projection_line(color_projection_expolygon.contour);
    for (const ColorProjectionLines &hole_color_projection_lines : color_projection_expolygon.holes) {
        draw_color_projection_line(hole_color_projection_lines);
    }
}

template<typename OutputIterator>
inline OutputIterator douglas_peucker(ColorPoints::const_iterator begin, ColorPoints::const_iterator end, OutputIterator out, const double tolerance, const double max_different_color_length) {
    const int64_t tolerance_sq                  = static_cast<int64_t>(sqr(tolerance));
    const double  max_different_color_length_sq = sqr(max_different_color_length);

    auto point_getter = [](const ColorPoint &color_point) -> Point {
        return color_point.p;
    };

    auto take_floater_predicate = [&tolerance_sq, &max_different_color_length_sq](ColorPoints::const_iterator anchor_it, ColorPoints::const_iterator floater_it, const int64_t max_dist_sq) -> bool {
        // We allow removing points between the anchor and the floater only when the color after the anchor is the same as the color before the floater.
        if (max_dist_sq > tolerance_sq || anchor_it->color_next != floater_it->color_prev)
            return false;

        const uint8_t anchor_color              = anchor_it->color_next;
        double        different_color_length_sq = 0.;
        std::optional<ColorPoint> color_point_prev;
        for (auto cp_it = std::next(anchor_it); cp_it != floater_it; ++cp_it) {
            if (cp_it->color_next == anchor_color) {
                if (!color_point_prev.has_value())
                    continue;

                different_color_length_sq += (cp_it->p - color_point_prev->p).cast<double>().squaredNorm();
                color_point_prev.reset();
            } else if (color_point_prev.has_value()) {
                different_color_length_sq += (cp_it->p - color_point_prev->p).cast<double>().squaredNorm();
                color_point_prev           = *cp_it;
            } else {
                assert(!color_point_prev.has_value());
                different_color_length_sq = 0.;
                color_point_prev          = *cp_it;
            }

            if (different_color_length_sq > max_different_color_length_sq)
                return false;
        }

        return true;
    };

    return douglas_peucker<int64_t>(begin, end, out, take_floater_predicate, point_getter);
}

BoundingBox get_extents(const std::vector<ColoredLines> &colored_polygons) {
    BoundingBox bbox;
    for (const ColoredLines &colored_lines : colored_polygons) {
        for (const ColoredLine &colored_line : colored_lines) {
            bbox.merge(colored_line.line.a);
            bbox.merge(colored_line.line.b);
        }
    }
    return bbox;
}

// Flatten the vector of vectors into a vector.
static inline ColoredLines to_lines(const std::vector<ColoredLines> &c_lines) {
    size_t n_lines = 0;
    for (const auto &c_line : c_lines) {
        n_lines += c_line.size();
    }

    ColoredLines lines;
    lines.reserve(n_lines);
    for (const auto &c_line : c_lines) {
        lines.insert(lines.end(), c_line.begin(), c_line.end());
    }

    return lines;
}

// Determines if the line points from the point between two contour lines is pointing inside polygon or outside.
static inline bool points_inside(const Line &contour_first, const Line &contour_second, const Point &new_point) {
    // Used in points_inside for decision if line leading thought the common point of two lines is pointing inside polygon or outside
    auto three_points_inward_normal = [](const Point &left, const Point &middle, const Point &right) -> Vec2d {
        assert(left != middle);
        assert(middle != right);
        return (perp(Point(middle - left)).cast<double>().normalized() + perp(Point(right - middle)).cast<double>().normalized()).normalized();
    };

    assert(contour_first.b == contour_second.a);
    Vec2d  inward_normal = three_points_inward_normal(contour_first.a, contour_first.b, contour_second.b);
    Vec2d  edge_norm     = (new_point - contour_first.b).cast<double>().normalized();
    double side          = inward_normal.dot(edge_norm);
    //    assert(side != 0.);
    return side > 0.;
}

static size_t non_deleted_edge_count(const VD::vertex_type &vertex) {
    size_t               non_deleted_edge_cnt = 0;
    const VD::edge_type *edge                 = vertex.incident_edge();
    do {
        if (edge->color() != VD_ANNOTATION::DELETED)
            ++non_deleted_edge_cnt;
    } while (edge = edge->prev()->twin(), edge != vertex.incident_edge());

    return non_deleted_edge_cnt;
}

static bool can_vertex_be_deleted(const VD::vertex_type &vertex) {
    if (vertex.color() == VD_ANNOTATION::VERTEX_ON_CONTOUR || vertex.color() == VD_ANNOTATION::DELETED)
        return false;

    return non_deleted_edge_count(vertex) <= 1;
}

static void delete_vertex_deep(const VD::vertex_type &vertex) {
    std::queue<const VD::vertex_type *> vertices_to_delete;
    vertices_to_delete.emplace(&vertex);

    while (!vertices_to_delete.empty()) {
        const VD::vertex_type &vertex_to_delete = *vertices_to_delete.front();
        assert(vertex_to_delete.color() != VD_ANNOTATION::VERTEX_ON_CONTOUR);
        vertices_to_delete.pop();
        vertex_to_delete.color(VD_ANNOTATION::DELETED);

        const VD::edge_type *edge = vertex_to_delete.incident_edge();
        do {
            edge->color(VD_ANNOTATION::DELETED);
            edge->twin()->color(VD_ANNOTATION::DELETED);

            if (edge->is_finite() && can_vertex_be_deleted(*edge->vertex1()))
                vertices_to_delete.emplace(edge->vertex1());
        } while (edge = edge->prev()->twin(), edge != vertex_to_delete.incident_edge());
    }
}

// Removes all edges except one in a Voronoi vertex that has more than one Voronoi edge (for example,
// in concave parts of a polygon). The one edge that is kept is selected arbitrarily.
static void remove_excess_voronoi_edges(const VD::vertex_type &vertex)
{
    if (non_deleted_edge_count(vertex) <= 1) {
        return;
    }

    std::vector<const VD::edge_type *> vertex_edges;
    const VD::edge_type               *edge = vertex.incident_edge();
    do {
        if (edge->color() == VD_ANNOTATION::DELETED) {
            continue;
        }

        vertex_edges.emplace_back(edge);
    } while (edge = edge->prev()->twin(), edge != vertex.incident_edge());

    while (vertex_edges.size() > 1) {
        const VD::edge_type &edge_to_check = *vertex_edges.back();
        edge_to_check.color(VD_ANNOTATION::DELETED);
        edge_to_check.twin()->color(VD_ANNOTATION::DELETED);

        if (const VD::vertex_type &vertex_to_delete = *edge_to_check.vertex1(); can_vertex_be_deleted(vertex_to_delete)) {
            delete_vertex_deep(vertex_to_delete);
        }

        vertex_edges.pop_back();
    }
}

// Returns first non-deleted edge in clockwise.
static const VD::edge_type *get_cw_first_non_deleted_edge(const VD::edge_type *edge_begin)
{
    const VD::edge_type *edge = edge_begin;
    do {
        if (edge->color() == VD_ANNOTATION::DELETED) {
            continue;
        } else {
            assert(edge->color() != VD_ANNOTATION::VERTEX_ON_CONTOUR);
            return edge;
        }
    } while (edge = edge->prev()->twin(), edge != edge_begin);

    return nullptr;
}

// Return convex Polygon for Voronoi cell (Voronoi cell is always convex).
// During processing a Voronoi cell, it marks its edges as deleted.
template<typename CellRange>
static typename std::enable_if<
    std::is_same<CellRange, Geometry::SegmentCellRange<Point>>::value || std::is_same<CellRange, Geometry::PointCellRange<Point>>::value,
    std::optional<Polygon>>::type
cell_range_to_polygon(const CellRange &cell_range)
{
    assert(cell_range.is_valid());
    assert(cell_range.edge_begin->vertex0()->color() == VD_ANNOTATION::VERTEX_ON_CONTOUR);

    if (!cell_range.is_valid() || cell_range.edge_begin->vertex0()->color() != VD_ANNOTATION::VERTEX_ON_CONTOUR) {
        return std::nullopt;
    }

    Polygon cell_polygon;
    cell_polygon.points.emplace_back(Geometry::VoronoiUtils::to_point(cell_range.edge_begin->vertex0()).template cast<coord_t>());

    // We have ensured that each cell_polygon have to start at edge_begin->vertex0() and end at edge_end->vertex1().
    const VD::edge_type *edge = cell_range.edge_begin;
    do {
        if (edge->color() == VD_ANNOTATION::DELETED) {
            continue;
        }

        const VD::vertex_type &next_vertex = *edge->vertex1();
        cell_polygon.points.emplace_back(Geometry::VoronoiUtils::to_point(next_vertex).cast<coord_t>());
        edge->color(VD_ANNOTATION::DELETED);

        if (next_vertex.color() == VD_ANNOTATION::VERTEX_ON_CONTOUR || next_vertex.color() == VD_ANNOTATION::DELETED) {
            break;
        }

        edge = edge->twin();
    } while (edge = edge->twin()->next(), edge != cell_range.edge_begin);

    if (edge->vertex1() != cell_range.edge_end->vertex1()) {
        return std::nullopt;
    }

    if (cell_range.edge_begin->vertex0() == cell_range.edge_end->vertex1()) {
        cell_polygon.points.pop_back();
    }

    return cell_polygon;
}

static std::optional<Polygon> segment_cell_range_to_polygon(const Geometry::SegmentCellRange<Point> &cell_range)
{
    return cell_range_to_polygon(cell_range);
}

static std::optional<Polygon> point_cell_range_to_polygon(const Geometry::PointCellRange<Point> &cell_range)
{
    return cell_range_to_polygon(cell_range);
}

inline double calc_triangle_area(const Point &a_pt, const Point &b_pt, const Point &c_pt)
{
    Vec2d ba = (b_pt - a_pt).cast<double>();
    Vec2d ca = (c_pt - a_pt).cast<double>();
    return Slic3r::cross2(ba, ca) / 2.;
}

static std::pair<Polygon, Polygon> split_convex_polygon_to_two_polygons_with_equal_area(const Polygon &input_polygon)
{
    assert(input_polygon.size() >= 3);
    const double half_area = input_polygon.area() / 2.;

    assert(half_area > 0.);
    if (half_area <= 0.) {
        return {};
    }

    Polygon left_polygon;
    left_polygon.points.emplace_back(input_polygon.points[0]);

    Polygon right_polygon;
    right_polygon.points.emplace_back(input_polygon.points[0]);
    right_polygon.points.emplace_back(input_polygon.points[1]);

    double right_polygon_area = 0.;
    size_t curr_idx           = 2;
    for (; curr_idx < input_polygon.size(); ++curr_idx) {
        const Point &start_pt           = input_polygon.points[0];
        const Point &prev_pt            = input_polygon.points[curr_idx - 1];
        const Point &curr_pt            = input_polygon.points[curr_idx];
        const double curr_triangle_area = calc_triangle_area(start_pt, prev_pt, curr_pt);

        if ((right_polygon_area + curr_triangle_area) <= half_area) {
            right_polygon_area += curr_triangle_area;
            right_polygon.points.emplace_back(curr_pt);
        } else {
            // Split current triangle.
            const double remaining_area  = half_area - right_polygon_area;
            const double split_ratio     = remaining_area / curr_triangle_area;
            const Point  intersection_pt = prev_pt + (split_ratio * (curr_pt - prev_pt).cast<double>()).cast<coord_t>();

            right_polygon.points.emplace_back(intersection_pt);
            left_polygon.points.emplace_back(intersection_pt);
            break;
        }
    }

    for (; curr_idx < input_polygon.size(); ++curr_idx) {
        left_polygon.points.emplace_back(input_polygon.points[curr_idx]);
    }

    assert(left_polygon.size() >= 3 && right_polygon.size() >= 3);
    return {left_polygon, right_polygon};
}

// Returns list of ExPolygons for each extruder + 1 for default unpainted regions.
// It iterates through all nodes on the border between two different colors, and from this point,
// start selection always left most edges for every node to construct CCW polygons.
static std::vector<ExPolygons> extract_colored_segments(const std::vector<ColoredLines> &colored_polygons,
                                                        const size_t                     num_facets_states,
                                                        const size_t                     layer_idx)
{
    const ColoredLines colored_lines = to_lines(colored_polygons);
    const BoundingBox  bbox          = get_extents(colored_polygons);

    auto get_next_contour_line = [&colored_polygons](const ColoredLine &line) -> const ColoredLine & {
        size_t contour_line_size = colored_polygons[line.poly_idx].size();
        size_t contour_next_idx  = (line.local_line_idx + 1) % contour_line_size;
        return colored_polygons[line.poly_idx][contour_next_idx];
    };

    Voronoi::VD vd;
    vd.construct_voronoi(colored_lines.begin(), colored_lines.end());

    // First, mark each Voronoi vertex on the input polygon to prevent it from being deleted later.
    for (const Voronoi::VD::cell_type &cell : vd.cells()) {
        if (cell.is_degenerate() || !cell.contains_segment())
            continue;

        if (const Geometry::SegmentCellRange<Point> cell_range = Geometry::VoronoiUtils::compute_segment_cell_range(cell, colored_lines.begin(), colored_lines.end()); cell_range.is_valid())
            cell_range.edge_begin->vertex0()->color(VD_ANNOTATION::VERTEX_ON_CONTOUR);
    }

    // Second, remove all Voronoi vertices that are outside the bounding box of input polygons.
    // Such Voronoi vertices are definitely not inside of input polygons, so we don't care about them.
    for (const Voronoi::VD::vertex_type &vertex : vd.vertices()) {
        if (vertex.color() == VD_ANNOTATION::DELETED || vertex.color() == VD_ANNOTATION::VERTEX_ON_CONTOUR)
            continue;

        if (!Geometry::VoronoiUtils::is_in_range<coord_t>(vertex) || !bbox.contains(Geometry::VoronoiUtils::to_point(vertex).cast<coord_t>()))
            delete_vertex_deep(vertex);
    }

    // Third, remove all Voronoi edges that are infinite.
    for (const Voronoi::VD::edge_type &edge : vd.edges()) {
        if (edge.color() != VD_ANNOTATION::DELETED && edge.is_infinite()) {
            edge.color(VD_ANNOTATION::DELETED);
            edge.twin()->color(VD_ANNOTATION::DELETED);

            if (edge.vertex0() != nullptr && can_vertex_be_deleted(*edge.vertex0()))
                delete_vertex_deep(*edge.vertex0());

            if (edge.vertex1() != nullptr && can_vertex_be_deleted(*edge.vertex1()))
                delete_vertex_deep(*edge.vertex1());
        }
    }

    // Fourth, remove all edges that point outward from the input polygon.
    for (const Voronoi::VD::cell_type &cell : vd.cells()) {
        if (cell.is_degenerate() || !cell.contains_segment())
            continue;

        if (const Geometry::SegmentCellRange<Point> cell_range = Geometry::VoronoiUtils::compute_segment_cell_range(cell, colored_lines.begin(), colored_lines.end()); cell_range.is_valid()) {
            const ColoredLine &current_line = Geometry::VoronoiUtils::get_source_segment(cell, colored_lines.begin(), colored_lines.end());
            const ColoredLine &next_line = get_next_contour_line(current_line);

            const VD::edge_type *edge = cell_range.edge_begin;
            do {
                if (edge->color() == VD_ANNOTATION::DELETED)
                    continue;

                if (!points_inside(current_line.line, next_line.line, Geometry::VoronoiUtils::to_point(edge->vertex1()).cast<coord_t>())) {
                    edge->color(VD_ANNOTATION::DELETED);
                    edge->twin()->color(VD_ANNOTATION::DELETED);
                    delete_vertex_deep(*edge->vertex1());
                }
            } while (edge = edge->prev()->twin(), edge != cell_range.edge_begin);
        }
    }

    // Fifth, if a Voronoi vertex has more than one Voronoi edge, remove all except one.
    // Edges are removed only for Voronoi vertices that aren't in the place where color changes.
    for (const Voronoi::VD::cell_type &cell : vd.cells()) {
        if (cell.is_degenerate() || !cell.contains_segment()) {
            continue;
        }

        const ColoredLine &curr_line = Geometry::VoronoiUtils::get_source_segment(cell, colored_lines.begin(), colored_lines.end());
        const ColoredLine &next_line = get_next_contour_line(curr_line);
        if (curr_line.color != next_line.color) {
            continue;
        }

        if (const Geometry::SegmentCellRange<Point> cell_range = Geometry::VoronoiUtils::compute_segment_cell_range(cell, colored_lines.begin(), colored_lines.end());
            cell_range.is_valid()) {
            const VD::vertex_type &vertex = *cell_range.edge_begin->vertex0();
            assert(vertex.color() == VD_ANNOTATION::VERTEX_ON_CONTOUR);
            remove_excess_voronoi_edges(vertex);
        }
    }

    if constexpr (MM_SEGMENTATION_DEBUG_GRAPH) {
        export_graph_to_svg(debug_out_path("mm-graph-%d.svg", layer_idx), vd, colored_polygons);
    }

    // Sixth, extract the colored segments from the annotated Voronoi diagram.
    std::vector<ExPolygons> segmented_expolygons_per_extruder(num_facets_states);
    for (const Voronoi::VD::cell_type &cell : vd.cells()) {
        if (cell.is_degenerate() || !cell.contains_segment())
            continue;

        if (const Geometry::SegmentCellRange<Point> cell_range = Geometry::VoronoiUtils::compute_segment_cell_range(cell, colored_lines.begin(), colored_lines.end()); cell_range.is_valid()) {
            if (cell_range.edge_begin->vertex0()->color() != VD_ANNOTATION::VERTEX_ON_CONTOUR)
                continue;

            const ColoredLine           &curr_line            = Geometry::VoronoiUtils::get_source_segment(cell, colored_lines.begin(), colored_lines.end());
            const std::optional<Polygon> segment_cell_polygon = segment_cell_range_to_polygon(cell_range);
            if (!segment_cell_polygon.has_value())
                continue;

            const Voronoi::VD::vertex_type &cell_begin_vertex = *cell_range.edge_begin->vertex0();
            // The cell_range.edge_begin was deleted, so if any edge is starting in the cell_range.edge_begin->vertex0(),
            // then it is an edge from a cell that was generated by a point, and the cell is between two different colors.
            assert(non_deleted_edge_count(cell_begin_vertex) <= 1);
            if (non_deleted_edge_count(cell_begin_vertex) == 1) {
                const VD::edge_type *point_cell_begin_edge = get_cw_first_non_deleted_edge(cell_range.edge_begin);
                const VD::edge_type *point_cell_end_edge   = cell_range.edge_begin->twin();

                assert(point_cell_begin_edge != nullptr && point_cell_end_edge != nullptr);
                if (point_cell_begin_edge == nullptr || point_cell_end_edge == nullptr)
                    continue;

                assert(point_cell_begin_edge->color() != VD_ANNOTATION::DELETED && point_cell_end_edge->color() != VD_ANNOTATION::DELETED);
                assert(point_cell_begin_edge->vertex0() == point_cell_end_edge->vertex1());

                const Geometry::PointCellRange<Point> point_cell_range(Geometry::VoronoiUtils::to_point(point_cell_begin_edge->vertex0()).cast<coord_t>(), point_cell_begin_edge, point_cell_end_edge);
                const std::optional<Polygon>          point_cell_polygon = point_cell_range_to_polygon(point_cell_range);
                if (!point_cell_polygon.has_value())
                    continue;

                const ColoredLine          &next_line           = get_next_contour_line(curr_line);
                std::pair<Polygon, Polygon> point_cell_polygons = split_convex_polygon_to_two_polygons_with_equal_area(*point_cell_polygon);
                segmented_expolygons_per_extruder[curr_line.color].emplace_back(std::move(point_cell_polygons.first));
                segmented_expolygons_per_extruder[next_line.color].emplace_back(std::move(point_cell_polygons.second));
            }

            cell_range.edge_begin->vertex0()->color(VD_ANNOTATION::DELETED);
            segmented_expolygons_per_extruder[curr_line.color].emplace_back(*segment_cell_polygon);
        }
    }

    // Merge all polygons together for each extruder
    for (auto &segmented_expolygons : segmented_expolygons_per_extruder) {
        segmented_expolygons = union_ex(segmented_expolygons);
        segmented_expolygons = Slic3r::closing_ex(segmented_expolygons, static_cast<float>(SCALED_EPSILON));
    }

    return segmented_expolygons_per_extruder;
}

static void cut_segmented_layers(const std::vector<ExPolygons>        &input_expolygons,
                                 std::vector<std::vector<ExPolygons>> &segmented_regions,
                                 const float                           cut_width,
                                 const float                           interlocking_depth,
                                 const std::function<void()>          &throw_on_cancel_callback)
{
    BOOST_LOG_TRIVIAL(debug) << "Print object segmentation - Cutting segmented layers in parallel - Begin";
    const float interlocking_cut_width = interlocking_depth > 0.f ? std::max(cut_width - interlocking_depth, 0.f) : 0.f;
    tbb::parallel_for(tbb::blocked_range<size_t>(0, segmented_regions.size()),[&segmented_regions, &input_expolygons, &cut_width, &interlocking_cut_width, &throw_on_cancel_callback](const tbb::blocked_range<size_t>& range) {
        for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++layer_idx) {
            throw_on_cancel_callback();
            const float  region_cut_width       = (layer_idx % 2 == 0 && interlocking_cut_width > 0.f) ? interlocking_cut_width : cut_width;
            const size_t num_extruders_plus_one = segmented_regions[layer_idx].size();
            if (region_cut_width > 0.f) {
                std::vector<ExPolygons> segmented_regions_cuts(num_extruders_plus_one); // Indexed by extruder_id
                for (size_t extruder_idx = 0; extruder_idx < num_extruders_plus_one; ++extruder_idx)
                    if (const ExPolygons &ex_polygons = segmented_regions[layer_idx][extruder_idx]; !ex_polygons.empty())
                        segmented_regions_cuts[extruder_idx] = diff_ex(ex_polygons, offset_ex(input_expolygons[layer_idx], -region_cut_width));
                segmented_regions[layer_idx] = std::move(segmented_regions_cuts);
            }
        }
    }); // end of parallel_for
    BOOST_LOG_TRIVIAL(debug) << "Print object segmentation - Cutting segmented layers in parallel - End";
}

static bool is_volume_sinking(const indexed_triangle_set &its, const Transform3d &trafo) {
    const Transform3f trafo_f = trafo.cast<float>();
    for (const stl_vertex &vertex : its.vertices) {
        if ((trafo_f * vertex).z() < SINKING_Z_THRESHOLD)
            return true;
    }

    return false;
}

static inline ExPolygons trim_by_top_or_bottom_layer(ExPolygons expolygons_to_trim, const size_t top_or_bottom_layer_idx,  const std::vector<std::vector<Polygons>> &top_or_bottom_raw_by_extruder)
{
    for (const std::vector<Polygons> &top_or_bottom_raw : top_or_bottom_raw_by_extruder) {
        if (top_or_bottom_raw.empty())
            continue;

        if (const Polygons &top_or_bottom = top_or_bottom_raw[top_or_bottom_layer_idx]; !top_or_bottom.empty()) {
            expolygons_to_trim = diff_ex(expolygons_to_trim, top_or_bottom);
        }
    }

    return expolygons_to_trim;
}

// Returns segmentation of top and bottom layers based on painting in segmentation gizmos.
static inline std::vector<std::vector<ExPolygons>> segmentation_top_and_bottom_layers(const PrintObject                                               &print_object,
                                                                                      const std::vector<ExPolygons>                                   &input_expolygons,
                                                                                      const std::function<ModelVolumeFacetsInfo(const ModelVolume &)> &extract_facets_info,
                                                                                      const size_t                                                     num_facets_states,
                                                                                      const std::function<void()>                                     &throw_on_cancel_callback)
{
    BOOST_LOG_TRIVIAL(debug) << "Print object segmentation - Segmentation of top and bottom layers in parallel - Begin";
    const size_t                 num_layers = input_expolygons.size();
    const SpanOfConstPtrs<Layer> layers     = print_object.layers();

    // Maximum number of top / bottom layers accounts for maximum overlap of one thread group into a neighbor thread group.
    int max_top_layers = 0;
    int max_bottom_layers = 0;
    int granularity = 1;
    for (size_t i = 0; i < print_object.num_printing_regions(); ++ i) {
        const PrintRegionConfig &config = print_object.printing_region(i).config();
        max_top_layers    = std::max(max_top_layers, config.top_solid_layers.value);
        max_bottom_layers = std::max(max_bottom_layers, config.bottom_solid_layers.value);
        granularity       = std::max(granularity, std::max(config.top_solid_layers.value, config.bottom_solid_layers.value) - 1);
    }

    // Project upwards pointing painted triangles over top surfaces,
    // project downards pointing painted triangles over bottom surfaces.
    std::vector<std::vector<Polygons>> top_raw(num_facets_states), bottom_raw(num_facets_states);
    std::vector<float> zs = zs_from_layers(layers);
    Transform3d        object_trafo = print_object.trafo_centered();

    if (max_top_layers > 0 || max_bottom_layers > 0) {
        for (const ModelVolume *mv : print_object.model_object()->volumes)
            if (mv->is_model_part()) {
                const Transform3d volume_trafo = object_trafo * mv->get_matrix();
                for (size_t extruder_idx = 0; extruder_idx < num_facets_states; ++extruder_idx) {
                    const indexed_triangle_set painted = extract_facets_info(*mv).facets_annotation.get_facets_strict(*mv, TriangleStateType(extruder_idx));

                    if constexpr (MM_SEGMENTATION_DEBUG_TOP_BOTTOM) {
                        its_write_obj(painted, debug_out_path("mm-painted-patch-%d.obj", extruder_idx).c_str());
                    }

                    if (! painted.indices.empty()) {
                        std::vector<Polygons> top, bottom;
                        if (!zs.empty() && is_volume_sinking(painted, volume_trafo)) {
                            std::vector<float> zs_sinking = {0.f};
                            Slic3r::append(zs_sinking, zs);
                            slice_mesh_slabs(painted, zs_sinking, volume_trafo, max_top_layers > 0 ? &top : nullptr, max_bottom_layers > 0 ? &bottom : nullptr, throw_on_cancel_callback);

                            MeshSlicingParams slicing_params;
                            slicing_params.trafo = volume_trafo;
                            Polygons bottom_slice = slice_mesh(painted, zs[0], slicing_params);

                            top.erase(top.begin());
                            bottom.erase(bottom.begin());

                            bottom[0] = union_(bottom[0], bottom_slice);
                        } else
                            slice_mesh_slabs(painted, zs, volume_trafo, max_top_layers > 0 ? &top : nullptr, max_bottom_layers > 0 ? &bottom : nullptr, throw_on_cancel_callback);
                        auto merge = [](std::vector<Polygons> &&src, std::vector<Polygons> &dst) {
                            auto it_src = find_if(src.begin(), src.end(), [](const Polygons &p){ return ! p.empty(); });
                            if (it_src != src.end()) {
                                if (dst.empty()) {
                                    dst = std::move(src);
                                } else {
                                    assert(src.size() == dst.size());
                                    auto it_dst = dst.begin() + (it_src - src.begin());
                                    for (; it_src != src.end(); ++ it_src, ++ it_dst)
                                        if (! it_src->empty()) {
                                            if (it_dst->empty())
                                                *it_dst = std::move(*it_src);
                                            else
                                                append(*it_dst, std::move(*it_src));
                                        }
                                }
                            }
                        };
                        merge(std::move(top),    top_raw[extruder_idx]);
                        merge(std::move(bottom), bottom_raw[extruder_idx]);
                    }
                }
            }
    }

    auto filter_out_small_polygons = [&num_facets_states, &num_layers](std::vector<std::vector<Polygons>> &raw_surfaces, double min_area) -> void {
        for (size_t extruder_idx = 0; extruder_idx < num_facets_states; ++extruder_idx) {
            if (raw_surfaces[extruder_idx].empty())
                continue;

            for (size_t layer_idx = 0; layer_idx < num_layers; ++layer_idx) {
                if (raw_surfaces[extruder_idx][layer_idx].empty())
                    continue;

                remove_small(raw_surfaces[extruder_idx][layer_idx], min_area);
            }
        }
    };

    // Filter out polygons less than 0.1mm^2, because they are unprintable and causing dimples on outer primers (#7104)
    filter_out_small_polygons(top_raw, Slic3r::sqr(POLYGON_FILTER_MIN_AREA_SCALED));
    filter_out_small_polygons(bottom_raw, Slic3r::sqr(POLYGON_FILTER_MIN_AREA_SCALED));

    // Remove top and bottom surfaces that are covered by the previous or next sliced layer.
    for (size_t extruder_idx = 0; extruder_idx < num_facets_states; ++extruder_idx) {
        for (size_t layer_idx = 0; layer_idx < num_layers; ++layer_idx) {
            const bool has_top_surface    = !top_raw[extruder_idx].empty() && !top_raw[extruder_idx][layer_idx].empty();
            const bool has_bottom_surface = !bottom_raw[extruder_idx].empty() && !bottom_raw[extruder_idx][layer_idx].empty();

            if (has_top_surface && layer_idx < (num_layers - 1)) {
                top_raw[extruder_idx][layer_idx] = diff(top_raw[extruder_idx][layer_idx], input_expolygons[layer_idx + 1]);
            }

            if (has_bottom_surface && layer_idx > 0) {
                bottom_raw[extruder_idx][layer_idx] = diff(bottom_raw[extruder_idx][layer_idx], input_expolygons[layer_idx - 1]);
            }
        }
    }

    if constexpr (MM_SEGMENTATION_DEBUG_TOP_BOTTOM) {
        const std::vector<std::string> colors = {"aqua", "black", "blue", "fuchsia", "gray", "green", "lime", "maroon", "navy", "olive", "purple", "red", "silver", "teal", "yellow"};
        for (size_t layer_id = 0; layer_id < zs.size(); ++layer_id) {
            std::vector<std::pair<Slic3r::ExPolygons, SVG::ExPolygonAttributes>> svg;
            for (size_t extruder_idx = 0; extruder_idx < num_facets_states; ++extruder_idx) {
                if (!top_raw[extruder_idx].empty() && !top_raw[extruder_idx][layer_id].empty()) {
                    if (ExPolygons expoly = union_ex(top_raw[extruder_idx][layer_id]); !expoly.empty()) {
                        const std::string &color = colors[extruder_idx];
                        svg.emplace_back(expoly, SVG::ExPolygonAttributes{format("top%d", extruder_idx), color, color, color});
                    }
                }

                if (!bottom_raw[extruder_idx].empty() && !bottom_raw[extruder_idx][layer_id].empty()) {
                    if (ExPolygons expoly = union_ex(bottom_raw[extruder_idx][layer_id]); !expoly.empty()) {
                        const std::string &color = colors[extruder_idx + 8];
                        svg.emplace_back(expoly, SVG::ExPolygonAttributes{format("bottom%d", extruder_idx), color, color, color});
                    }
                }
            }

            SVG::export_expolygons(debug_out_path("mm-segmentation-top-bottom-%d-%lf.svg", layer_id, zs[layer_id]), svg);
        }
    }

    std::vector<std::vector<ExPolygons>> triangles_by_color_bottom(num_facets_states);
    std::vector<std::vector<ExPolygons>> triangles_by_color_top(num_facets_states);
    triangles_by_color_bottom.assign(num_facets_states, std::vector<ExPolygons>(num_layers * 2));
    triangles_by_color_top.assign(num_facets_states, std::vector<ExPolygons>(num_layers * 2));

    struct LayerColorStat {
        // Number of regions for a queried color.
        int     num_regions             { 0 };
        // Maximum perimeter extrusion width for a queried color.
        float   extrusion_width         { 0.f };
        // Minimum radius of a region to be printable. Used to filter regions by morphological opening.
        float   small_region_threshold  { 0.f };
        // Maximum number of top layers for a queried color.
        int     top_solid_layers        { 0 };
        // Maximum number of bottom layers for a queried color.
        int     bottom_solid_layers     { 0 };
    };
    auto layer_color_stat = [&layers = std::as_const(layers)](const size_t layer_idx, const size_t color_idx) -> LayerColorStat {
        LayerColorStat out;
        const Layer &layer = *layers[layer_idx];
        for (const LayerRegion *region : layer.regions())
            if (const PrintRegionConfig &config = region->region().config();
                // color_idx == 0 means "don't know" extruder aka the underlying extruder.
                // As this region may split existing regions, we collect statistics over all regions for color_idx == 0.
                color_idx == 0 || config.perimeter_extruder == int(color_idx)) {
                const Flow perimeter_flow = region->flow(FlowRole::frPerimeter);
                out.extrusion_width     = std::max<float>(out.extrusion_width, perimeter_flow.width());
                out.top_solid_layers    = std::max<int>(out.top_solid_layers, config.top_solid_layers);
                out.bottom_solid_layers = std::max<int>(out.bottom_solid_layers, config.bottom_solid_layers);
                out.small_region_threshold = config.gap_fill_enabled.value && config.gap_fill_speed.value > 0 ?
                                             // Gap fill enabled. Enable a single line of 1/2 extrusion width.
                                             0.5f * perimeter_flow.width() :
                                             // Gap fill disabled. Enable two lines slightly overlapping.
                                             perimeter_flow.width() + 0.7f * perimeter_flow.spacing();
                out.small_region_threshold = scaled<float>(out.small_region_threshold * 0.5f);
                ++ out.num_regions;
            }
        assert(out.num_regions > 0);
        out.extrusion_width = scaled<float>(out.extrusion_width);
        return out;
    };

    tbb::parallel_for(tbb::blocked_range<size_t>(0, num_layers, granularity), [&granularity, &num_layers, &num_facets_states, &layer_color_stat, &top_raw, &triangles_by_color_top,
                                                                               &throw_on_cancel_callback, &input_expolygons, &bottom_raw, &triangles_by_color_bottom](const tbb::blocked_range<size_t> &range) {
        size_t group_idx   = range.begin() / granularity;
        size_t layer_idx_offset = (group_idx & 1) * num_layers;
        for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
            for (size_t color_idx = 0; color_idx < num_facets_states; ++color_idx) {
                throw_on_cancel_callback();
                LayerColorStat stat = layer_color_stat(layer_idx, color_idx);
                if (std::vector<Polygons> &top = top_raw[color_idx]; !top.empty() && !top[layer_idx].empty()) {
                    if (ExPolygons top_ex = union_ex(top[layer_idx]); !top_ex.empty()) {
                        // Clean up thin projections. They are not printable anyways.
                        if (stat.small_region_threshold > 0)
                            top_ex = opening_ex(top_ex, stat.small_region_threshold);

                        if (!top_ex.empty()) {
                            append(triangles_by_color_top[color_idx][layer_idx + layer_idx_offset], top_ex);
                            float offset = 0.f;
                            ExPolygons layer_slices_trimmed = input_expolygons[layer_idx];
                            for (int last_idx = int(layer_idx) - 1; last_idx >= std::max(int(layer_idx - stat.top_solid_layers), int(0)); --last_idx) {
                                offset -= stat.extrusion_width;
                                layer_slices_trimmed = intersection_ex(layer_slices_trimmed, input_expolygons[last_idx]);
                                ExPolygons last = intersection_ex(top_ex, offset_ex(layer_slices_trimmed, offset));

                                // Trim this propagated top layer by the painted bottom layer.
                                last = trim_by_top_or_bottom_layer(last, size_t(last_idx), bottom_raw);

                                if (stat.small_region_threshold > 0)
                                    last = opening_ex(last, stat.small_region_threshold);

                                if (last.empty())
                                    break;

                                append(triangles_by_color_top[color_idx][last_idx + layer_idx_offset], std::move(last));
                            }
                        }
                    }
                }

                if (std::vector<Polygons> &bottom = bottom_raw[color_idx]; !bottom.empty() && !bottom[layer_idx].empty()) {
                    if (ExPolygons bottom_ex = union_ex(bottom[layer_idx]); !bottom_ex.empty()) {
                        // Clean up thin projections. They are not printable anyways.
                        if (stat.small_region_threshold > 0)
                            bottom_ex = opening_ex(bottom_ex, stat.small_region_threshold);

                        if (!bottom_ex.empty()) {
                            append(triangles_by_color_bottom[color_idx][layer_idx + layer_idx_offset], bottom_ex);
                            float offset = 0.f;
                            ExPolygons layer_slices_trimmed = input_expolygons[layer_idx];
                            for (size_t last_idx = layer_idx + 1; last_idx < std::min(layer_idx + stat.bottom_solid_layers, num_layers); ++last_idx) {
                                offset -= stat.extrusion_width;
                                layer_slices_trimmed = intersection_ex(layer_slices_trimmed, input_expolygons[last_idx]);
                                ExPolygons last = intersection_ex(bottom_ex, offset_ex(layer_slices_trimmed, offset));

                                // Trim this propagated bottom layer by the painted top layer.
                                last = trim_by_top_or_bottom_layer(last, size_t(last_idx), top_raw);

                                if (stat.small_region_threshold > 0)
                                    last = opening_ex(last, stat.small_region_threshold);

                                if (last.empty())
                                    break;

                                append(triangles_by_color_bottom[color_idx][last_idx + layer_idx_offset], std::move(last));
                            }
                        }
                    }
                }
            }
        }
    });

    std::vector<std::vector<ExPolygons>> triangles_by_color_merged(num_facets_states);
    triangles_by_color_merged.assign(num_facets_states, std::vector<ExPolygons>(num_layers));
    tbb::parallel_for(tbb::blocked_range<size_t>(0, num_layers), [&triangles_by_color_merged, &triangles_by_color_bottom, &triangles_by_color_top, &num_layers, &throw_on_cancel_callback](const tbb::blocked_range<size_t> &range) {
        for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
            throw_on_cancel_callback();
            for (size_t color_idx = 0; color_idx < triangles_by_color_merged.size(); ++color_idx) {
                auto &self = triangles_by_color_merged[color_idx][layer_idx];
                append(self, std::move(triangles_by_color_bottom[color_idx][layer_idx]));
                append(self, std::move(triangles_by_color_bottom[color_idx][layer_idx + num_layers]));
                append(self, std::move(triangles_by_color_top[color_idx][layer_idx]));
                append(self, std::move(triangles_by_color_top[color_idx][layer_idx + num_layers]));
                self = union_ex(self);
            }
            // Trim one region by the other if some of the regions overlap.
            for (size_t color_idx = 1; color_idx < triangles_by_color_merged.size(); ++ color_idx)
                triangles_by_color_merged[color_idx][layer_idx] = diff_ex(triangles_by_color_merged[color_idx][layer_idx],
                                                                          triangles_by_color_merged[color_idx - 1][layer_idx]);
        }
    });
    BOOST_LOG_TRIVIAL(debug) << "Print object segmentation - Segmentation of top and bottom layers in parallel - End";

    return triangles_by_color_merged;
}

static std::vector<std::vector<ExPolygons>> merge_segmented_layers(const std::vector<std::vector<ExPolygons>> &segmented_regions,
                                                                   std::vector<std::vector<ExPolygons>>      &&top_and_bottom_layers,
                                                                   const size_t                                num_facets_states,
                                                                   const std::function<void()>                &throw_on_cancel_callback)
{
    const size_t                         num_layers = segmented_regions.size();
    std::vector<std::vector<ExPolygons>> segmented_regions_merged(num_layers);
    segmented_regions_merged.assign(num_layers, std::vector<ExPolygons>(num_facets_states - 1));
    assert(!top_and_bottom_layers.size() || num_facets_states == top_and_bottom_layers.size());

    BOOST_LOG_TRIVIAL(debug) << "Print object segmentation - Merging segmented layers in parallel - Begin";
    tbb::parallel_for(tbb::blocked_range<size_t>(0, num_layers), [&segmented_regions, &top_and_bottom_layers, &segmented_regions_merged, &num_facets_states, &throw_on_cancel_callback](const tbb::blocked_range<size_t> &range) {
        for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++layer_idx) {
            assert(segmented_regions[layer_idx].size() == num_facets_states);
            // Zero is skipped because it is the default color of the volume
            for (size_t extruder_id = 1; extruder_id < num_facets_states; ++extruder_id) {
                throw_on_cancel_callback();
                if (!segmented_regions[layer_idx][extruder_id].empty()) {
                    ExPolygons segmented_regions_trimmed = segmented_regions[layer_idx][extruder_id];
                    if (!top_and_bottom_layers.empty()) {
                        for (const std::vector<ExPolygons> &top_and_bottom_by_extruder : top_and_bottom_layers) {
                            if (!top_and_bottom_by_extruder[layer_idx].empty() && !segmented_regions_trimmed.empty()) {
                                segmented_regions_trimmed = diff_ex(segmented_regions_trimmed, top_and_bottom_by_extruder[layer_idx]);
                            }
                        }
                    }

                    segmented_regions_merged[layer_idx][extruder_id - 1] = std::move(segmented_regions_trimmed);
                }

                if (!top_and_bottom_layers.empty() && !top_and_bottom_layers[extruder_id][layer_idx].empty()) {
                    bool was_top_and_bottom_empty = segmented_regions_merged[layer_idx][extruder_id - 1].empty();
                    append(segmented_regions_merged[layer_idx][extruder_id - 1], top_and_bottom_layers[extruder_id][layer_idx]);

                    // Remove dimples (#7235) appearing after merging side segmentation of the model with tops and bottoms painted layers.
                    if (!was_top_and_bottom_empty)
                        segmented_regions_merged[layer_idx][extruder_id - 1] = offset2_ex(union_ex(segmented_regions_merged[layer_idx][extruder_id - 1]), float(SCALED_EPSILON), -float(SCALED_EPSILON));
                }
            }
        }
    }); // end of parallel_for
    BOOST_LOG_TRIVIAL(debug) << "Print object segmentation - Merging segmented layers in parallel - End";

    return segmented_regions_merged;
}

// Check if all ColoredLines representing several Polygons (ExPolygon) uses the same color.
static bool has_polygons_only_one_color(const std::vector<ColoredLines> &colored_polygons)
{
    assert(!colored_polygons.empty());
    assert(!colored_polygons.front().empty());
    int first_line_color = colored_polygons.front().front().color;
    for (const ColoredLines &colored_polygon : colored_polygons) {
        for (const ColoredLine &colored_line : colored_polygon) {
            if (first_line_color != colored_line.color)
                return false;
        }
    }

    return true;
}

BoundingBox get_extents(const ColorPolygon &c_poly) {
    return c_poly.bounding_box();
}

BoundingBox get_extents(const ColorPolygons &c_polygons) {
    BoundingBox bb;
    if (!c_polygons.empty()) {
        bb = get_extents(c_polygons.front());
        for (size_t i = 1; i < c_polygons.size(); ++i) {
            bb.merge(get_extents(c_polygons[i]));
        }
    }

    return bb;
}

// Filter out small ColorPolygons based on minimum area and by applying polygon offset.
bool filter_out_small_color_polygons(ColorPolygons &color_polygons, const double filter_min_area, const float filter_offset) {
    assert(filter_offset >= 0.);

    bool   modified       = false;
    size_t first_free_idx = 0;

    for (ColorPolygon &color_polygon : color_polygons) {
        if (std::abs(color_polygon.area()) >= filter_min_area && (filter_offset <= 0. || !offset(Polygon(color_polygon.points), filter_offset).empty())) {
            if (const size_t color_polygon_idx = &color_polygon - color_polygons.data(); first_free_idx < color_polygon_idx) {
                std::swap(color_polygon.points, color_polygons[first_free_idx].points);
                std::swap(color_polygon.colors, color_polygons[first_free_idx].colors);
            }

            ++first_free_idx;
        } else {
            modified = true;
        }
    }

    if (first_free_idx < color_polygons.size()) {
        color_polygons.erase(color_polygons.begin() + int(first_free_idx), color_polygons.end());
    }

    return modified;
}

ColorPoints color_polygon_to_color_points(const ColorPolygon &color_polygon) {
    assert(!color_polygon.empty());
    assert(color_polygon.points.size() == color_polygon.colors.size());

    ColorPoints color_points_out;
    color_points_out.reserve(color_polygon.size());

    for (const Point &pt : color_polygon.points) {
        const size_t  pt_idx     = &pt - color_polygon.points.data();
        const uint8_t color_prev = (pt_idx == 0) ? color_polygon.colors.back() : color_polygon.colors[pt_idx - 1];
        const uint8_t color_next = color_polygon.colors[pt_idx];

        color_points_out.emplace_back(pt, color_prev, color_next);
    }

    return color_points_out;
}

std::vector<ColorPoints> color_polygons_to_color_points(const ColorPolygons &color_polygons) {
    std::vector<ColorPoints> color_polygons_points_out;
    color_polygons_points_out.reserve(color_polygons.size());

    for (const ColorPolygon &color_polygon : color_polygons)
        color_polygons_points_out.emplace_back(color_polygon_to_color_points(color_polygon));

    return color_polygons_points_out;
}

std::vector<ColoredLines> color_points_to_colored_lines(const std::vector<ColorPoints> &color_polygons_points) {
    std::vector<ColoredLines> colored_lines_vec_out(color_polygons_points.size());

    for (const ColorPoints &color_polygon_points : color_polygons_points) {
        const size_t  color_polygon_idx = &color_polygon_points - color_polygons_points.data();
        ColoredLines &colored_lines     = colored_lines_vec_out[color_polygon_idx];
        colored_lines.reserve(color_polygon_points.size());

        for (size_t cpt_idx = 0; cpt_idx < color_polygon_points.size() - 1; ++cpt_idx) {
            const ColorPoint &curr_color_point = color_polygon_points[cpt_idx];
            const ColorPoint &next_color_point = color_polygon_points[cpt_idx + 1];
            colored_lines.push_back({Line(curr_color_point.p, next_color_point.p), curr_color_point.color_next, int(color_polygon_idx), int(cpt_idx)});
        }

        colored_lines.push_back({Line(color_polygon_points.back().p, color_polygon_points.front().p), color_polygon_points.back().color_next, int(color_polygon_idx), int(color_polygon_points.size() - 1)});
    }

    return colored_lines_vec_out;
}

ColorLines color_points_to_color_lines(const ColorPoints &color_points) {
    ColorLines color_lines_out;
    color_lines_out.reserve(color_points.size());

    for (size_t cpt_idx = 1; cpt_idx < color_points.size(); ++cpt_idx) {
        const ColorPoint &prev_cpt = color_points[cpt_idx - 1];
        const ColorPoint &curr_cpt = color_points[cpt_idx];
        color_lines_out.emplace_back(prev_cpt.p, curr_cpt.p, prev_cpt.color_next);
    }

    color_lines_out.emplace_back(color_points.back().p, color_points.front().p, color_points.back().color_next);

    return color_lines_out;
}

// Create the flat vector of ColorLine from the vector of ColorLines.
static ColorLines flatten_color_lines(const std::vector<ColorLines> &color_polygons_lines) {
    const size_t total_color_lines_count = std::accumulate(color_polygons_lines.begin(), color_polygons_lines.end(), size_t(0),
                                                           [](const size_t acc, const ColorLines &color_lines) {
                                                               return acc + color_lines.size();
                                                           });

    ColorLines color_lines_out;
    color_lines_out.reserve(total_color_lines_count);
    for (const ColorLines &color_lines : color_polygons_lines) {
        Slic3r::append(color_lines_out, color_lines);
    }

    return color_lines_out;
}

static std::vector<float> get_print_object_layers_zs(const SpanOfConstPtrs<Layer> &layers) {
    std::vector<float> layers_zs;
    layers_zs.reserve(layers.size());

    for (const Layer *layer : layers) {
        layers_zs.emplace_back(static_cast<float>(layer->slice_z));
    }

    return layers_zs;
}

void static filter_color_of_small_segments(ColorPoints &color_polygon_points, const double max_different_color_length) {
    struct ColorSegment
    {
        explicit ColorSegment(size_t color_pt_begin_idx, size_t color_pt_end_idx, uint8_t color, double length)
            : color_pt_begin_idx(color_pt_begin_idx), color_pt_end_idx(color_pt_end_idx), color(color), length(length) {}

        size_t  color_pt_begin_idx = 0;
        size_t  color_pt_end_idx   = 0;
        uint8_t color              = 0;
        double  length             = 0.;
    };

    auto pt_length = [](const ColorPoint &color_pt_a, const ColorPoint &color_pt_b) -> double {
        return (color_pt_b.p.cast<double>() - color_pt_a.p.cast<double>()).norm();
    };

    std::vector<ColorSegment> color_segments;
    color_segments.emplace_back(0, 0, color_polygon_points.front().color_next, 0.);

    for (size_t color_pt_idx = 1; color_pt_idx < color_polygon_points.size(); ++color_pt_idx) {
        const ColorPoint &prev_color_pt = color_polygon_points[color_pt_idx - 1];
        const ColorPoint &curr_color_pt = color_polygon_points[color_pt_idx];

        ColorSegment &last_color_segment = color_segments.back();

        if (last_color_segment.color == curr_color_pt.color_next) {
            last_color_segment.color_pt_end_idx = color_pt_idx;
            last_color_segment.length += pt_length(prev_color_pt, curr_color_pt);
        } else {
            last_color_segment.color_pt_end_idx = color_pt_idx;
            last_color_segment.length += pt_length(prev_color_pt, curr_color_pt);
            color_segments.emplace_back(color_pt_idx, color_pt_idx, curr_color_pt.color_next, 0.);
        }
    }

    ColorSegment &last_color_segment     = color_segments.back();
    last_color_segment.color_pt_end_idx  = 0;
    last_color_segment.length           += pt_length(color_polygon_points.back(), color_polygon_points.front());

    if (color_segments.size() > 2 && color_segments.front().color == last_color_segment.color) {
        color_segments.front().color_pt_begin_idx  = last_color_segment.color_pt_begin_idx;
        color_segments.front().length             += last_color_segment.length;
        color_segments.pop_back();
    }

    auto next_segment_idx = [&color_segments](const size_t curr_segment_idx) -> size_t {
        return curr_segment_idx < (color_segments.size() - 1) ? curr_segment_idx + 1 : 0;
    };

    for (size_t from_segment_idx = 0; from_segment_idx < color_segments.size();) {
        size_t to_segment_idx          = next_segment_idx(from_segment_idx);
        double total_diff_color_length = 0.;

        bool update_color = false;
        while (from_segment_idx != to_segment_idx) {
            if (total_diff_color_length > max_different_color_length) {
                break;
            } else if (color_segments[from_segment_idx].color == color_segments[to_segment_idx].color) {
                update_color = true;
                break;
            }

            total_diff_color_length += color_segments[to_segment_idx].length;
            to_segment_idx           = next_segment_idx(to_segment_idx);
        }

        if (!update_color) {
            ++from_segment_idx;
            continue;
        }

        const uint8_t new_color = color_segments[from_segment_idx].color;
        for (size_t curr_segment_idx = next_segment_idx(from_segment_idx); curr_segment_idx != to_segment_idx; curr_segment_idx = next_segment_idx(curr_segment_idx)) {
            for (size_t pt_idx = color_segments[curr_segment_idx].color_pt_begin_idx; pt_idx != color_segments[curr_segment_idx].color_pt_end_idx; pt_idx = (pt_idx < color_polygon_points.size() - 1) ? pt_idx + 1 : 0) {
                color_polygon_points[pt_idx].color_prev = new_color;
                color_polygon_points[pt_idx].color_next = new_color;
            }

            color_polygon_points[color_segments[curr_segment_idx].color_pt_end_idx].color_prev = new_color;
            color_polygon_points[color_segments[curr_segment_idx].color_pt_end_idx].color_next = new_color;

            color_segments[curr_segment_idx].color = new_color;
        }

        if (from_segment_idx < to_segment_idx) {
            from_segment_idx = to_segment_idx;
        } else {
            // We already processed all segments.
            break;
        }
    }
}

[[maybe_unused]] static bool is_valid_color_polygon_points(const ColorPoints &color_polygon_points) {
    for (size_t pt_idx = 1; pt_idx < color_polygon_points.size(); ++pt_idx) {
        const ColorPoint &prev_color_pt = color_polygon_points[pt_idx - 1];
        const ColorPoint &curr_color_pt = color_polygon_points[pt_idx];

        if (prev_color_pt.color_next != curr_color_pt.color_prev)
            return false;
    }

    if (color_polygon_points.back().color_next != color_polygon_points.front().color_prev)
        return false;

    return true;
}

static ColorProjectionLines create_color_projection_lines(const Polygon &polygon)
{
    ColorProjectionLines color_projection_lines;
    color_projection_lines.reserve(polygon.size());

    for (const Line &line : polygon.lines()) {
        color_projection_lines.emplace_back(line);
    }

    return color_projection_lines;
}

static ColorProjectionExPolygon create_color_projection_expolygon(const ExPolygon &expolygon)
{
    ColorProjectionExPolygon color_projection_expolygon;
    color_projection_expolygon.contour = create_color_projection_lines(expolygon.contour);

    color_projection_expolygon.holes.reserve(expolygon.holes.size());
    for (const Polygon &hole_polygon : expolygon.holes) {
        color_projection_expolygon.holes.emplace_back(create_color_projection_lines(hole_polygon));
    }

    return color_projection_expolygon;
}

static ColorProjectionExPolygons create_color_projection_expolygons(const ExPolygons &expolygons)
{
    ColorProjectionExPolygons color_projection_expolygons;
    color_projection_expolygons.reserve(expolygons.size());

    for (const ExPolygon &expolygon : expolygons) {
        color_projection_expolygons.emplace_back(create_color_projection_expolygon(expolygon));
    }

    return color_projection_expolygons;
}

// Create the flat vector of ColorProjectionLineWrapper where each ColorProjectionLineWrapper
// is pointing into the one ColorProjectionLine in the vector of ColorProjectionExPolygons.
static std::vector<ColorProjectionLineWrapper> create_color_projection_lines_mapping(ColorProjectionExPolygons &color_projection_expolygons)
{
    std::vector<ColorProjectionLineWrapper> color_projection_lines_mapping;
    color_projection_lines_mapping.reserve(lines_count(color_projection_expolygons));

    const auto append_color_projection_lines = [&color_projection_lines_mapping](ColorProjectionLines &color_polygon_projection_lines) {
        for (ColorProjectionLine &color_projection_line : color_polygon_projection_lines) {
            color_projection_lines_mapping.emplace_back(&color_projection_line);
        }
    };

    for (ColorProjectionExPolygon &color_projection_expolygon : color_projection_expolygons) {
        append_color_projection_lines(color_projection_expolygon.contour);
        for (ColorProjectionLines &hole_color_projection_lines : color_projection_expolygon.holes) {
            append_color_projection_lines(hole_color_projection_lines);
        }
    }

    return color_projection_lines_mapping;
}

// Return the color of the first part of the first line of the polygon (after projection).
static uint8_t get_color_of_first_polygon_line(const ColorProjectionLines &color_polygon_projection_lines) {
    assert(!color_polygon_projection_lines.empty());

    if (color_polygon_projection_lines.empty()) {
        return 0;
    } else if (const ColorProjectionLine &first_line = color_polygon_projection_lines.front(); !first_line.color_changes.empty() && first_line.color_changes.front().t == 0.f) {
        return first_line.color_changes.front().color_next;
    }

    auto last_projection_line_it = std::find_if(color_polygon_projection_lines.rbegin(), color_polygon_projection_lines.rend(), [](const ColorProjectionLine &line) {
        return !line.color_changes.empty();
    });

    assert(last_projection_line_it != color_polygon_projection_lines.rend());
    if (last_projection_line_it == color_polygon_projection_lines.rend()) {
        // There is no projected color on this whole polygon.
        return 0;
    } else {
        return last_projection_line_it->color_changes.back().color_next;
    }
}

// Helper function to find the dominant color in a map.
static std::optional<uint8_t> get_dominant_color_from_map(const std::unordered_map<uint8_t, double> &area_per_color)
{
    if (area_per_color.empty())
        return std::nullopt;

    return std::max_element(area_per_color.begin(), area_per_color.end(),
                            [](const std::pair<uint8_t, double> &p1, const std::pair<uint8_t, double> &p2) {
                                return p1.second < p2.second;
                            })->first;
}

// Function to find the dominant color over the beginning of the ColorProjectionLine.
static std::optional<uint8_t> find_dominant_color_from_begin(const ColorProjectionLine &color_line)
{
    assert(!color_line.color_changes.empty());
    const ColorChanges                 &color_changes              = color_line.color_changes;
    const double                        line_length                = (color_line.b - color_line.a).cast<double>().norm();
    const double                        max_snap_distance          = std::min(line_length, 10. * MM_SEGMENTATION_MAX_SNAP_DISTANCE_SCALED);
    bool                                max_snap_distance_exceeded = false;
    std::unordered_map<uint8_t, double> area_per_color;
    for (auto curr_it = std::next(color_changes.cbegin()); curr_it != color_changes.cend(); ++curr_it) {
        auto prev_it = std::prev(curr_it);
        if (const double endpoint_dist = curr_it->t * line_length; endpoint_dist < max_snap_distance) {
            area_per_color[prev_it->color_next] += (curr_it->t - prev_it->t) * line_length;
        } else {
            area_per_color[prev_it->color_next] += max_snap_distance - prev_it->t * line_length;
            max_snap_distance_exceeded           = true;
            break;
        }
    }

    if (!max_snap_distance_exceeded) {
        const ColorChange &last_color_change          = color_changes.back();
        area_per_color[last_color_change.color_next] += max_snap_distance - last_color_change.t * line_length;
    }

    return get_dominant_color_from_map(area_per_color);
}

// Function to find the dominant color over the end of the ColorProjectionLine.
std::optional<uint8_t> find_dominant_color_from_end(const ColorProjectionLine &color_line)
{
    const ColorChanges                 &color_changes     = color_line.color_changes;
    const double                        line_length       = (color_line.b - color_line.a).cast<double>().norm();
    const double                        max_snap_distance = std::min(line_length, 10. * MM_SEGMENTATION_MAX_SNAP_DISTANCE_SCALED);
    double                              prev_t            = 1.;
    std::unordered_map<uint8_t, double> area_per_color;
    for (auto curr_it = color_changes.crbegin(); curr_it != color_changes.crend(); ++curr_it) {
        if (const double endpoint_dist = (1. - curr_it->t) * line_length; endpoint_dist < max_snap_distance) {
            area_per_color[curr_it->color_next] += (prev_t - curr_it->t) * line_length;
        } else {
            area_per_color[curr_it->color_next] += max_snap_distance - (1. - prev_t) * line_length;
            break;
        }

        prev_t = curr_it->t;
    }

    return get_dominant_color_from_map(area_per_color);
}

static void filter_projected_color_points_on_polygon(ColorProjectionLines &color_polygon_projection_lines)
{
    for (ColorProjectionLine &color_line : color_polygon_projection_lines) {
        if (color_line.color_changes.empty())
            continue;

        std::sort(color_line.color_changes.begin(), color_line.color_changes.end());

        // Snap projected points to the first endpoint of the line.
        const double line_length = (color_line.b - color_line.a).cast<double>().norm();

        std::vector<ColorChange *> snap_candidates;
        for (ColorChange &color_change : color_line.color_changes) {
            if (const double endpoint_dist = color_change.t * line_length; endpoint_dist < MM_SEGMENTATION_MAX_SNAP_DISTANCE_SCALED) {
                snap_candidates.emplace_back(&color_change);
            } else {
                break;
            }
        }

        if (snap_candidates.size() == 1) {
            snap_candidates.front()->t = 0.;
        } else if (const std::optional<uint8_t> dominant_color_from_begin = find_dominant_color_from_begin(color_line); snap_candidates.size() > 1 && dominant_color_from_begin.has_value()) {
            ColorChange &first_candidate = *snap_candidates.front();
            first_candidate.t            = 0.;
            for (ColorChange *snap_candidate : snap_candidates) {
                snap_candidate->color_next = *dominant_color_from_begin;
            }
        }

        snap_candidates.clear();

        // Snap projected points to the second endpoint of the line.
        for (auto cr_it = color_line.color_changes.rbegin(); cr_it != color_line.color_changes.rend(); ++cr_it) {
            ColorChange &color_change = *cr_it;
            if (const double endpoint_dist = (1. - color_change.t) * line_length; endpoint_dist < MM_SEGMENTATION_MAX_SNAP_DISTANCE_SCALED) {
                snap_candidates.emplace_back(&color_change);
            } else {
                break;
            }
        }

        if (const std::optional<uint8_t> dominant_color_from_end = find_dominant_color_from_end(color_line); !snap_candidates.empty() && dominant_color_from_end.has_value()) {
            for (ColorChange *snap_candidate : snap_candidates) {
                snap_candidate->color_next = *dominant_color_from_end;
            }

            // Be aware that snap_candidates are sorted in the opposite order to color_line.color_changes.
            color_line.color_changes.back().t = 1.;
        }

        // Remove color ranges that just repeating the same color.
        // We don't care about color_prev, because both color_prev and color_next may not be connected.
        // Also, we will not use color_prev during the final stage of producing ColorPolygons.
        if (color_line.color_changes.size() > 1) {
            ColorChanges color_changes_filtered;
            color_changes_filtered.reserve(color_line.color_changes.size());

            color_changes_filtered.emplace_back(color_line.color_changes.front());
            for (auto cr_it = std::next(color_line.color_changes.begin()); cr_it != color_line.color_changes.end(); ++cr_it) {
                ColorChange &color_change = *cr_it;

                if (color_changes_filtered.back().color_next == color_change.color_next) {
                    continue;
                } else if (const double t_diff = (color_change.t - color_changes_filtered.back().t); t_diff * line_length < MM_SEGMENTATION_MAX_SNAP_DISTANCE_SCALED) {
                    color_changes_filtered.back().color_next = color_change.color_next;
                } else {
                    color_changes_filtered.emplace_back(color_change);
                }
            }

            color_line.color_changes = std::move(color_changes_filtered);
        }
    }
}

static void filter_projected_color_points_on_expolygon(ColorProjectionExPolygon &color_projection_expolygon)
{
    filter_projected_color_points_on_polygon(color_projection_expolygon.contour);
    for (ColorProjectionLines &hole_color_projection_lines : color_projection_expolygon.holes) {
        filter_projected_color_points_on_polygon(hole_color_projection_lines);
    }
}

static double calc_three_color_points_angle(const ColorPoint &prev_pt, const ColorPoint &curr_pt, const ColorPoint &next_pt)
{
    assert(prev_pt.p != curr_pt.p);
    assert(curr_pt.p != next_pt.p);

    const Vec2d ab_vec = (prev_pt.p - curr_pt.p).cast<double>().normalized();
    const Vec2d cb_vec = (next_pt.p - curr_pt.p).cast<double>().normalized();
    return PI - std::acos(std::clamp(ab_vec.dot(cb_vec), -1.0, 1.0));
}

struct SnapAngle
{
    enum class Direction { Forward, Backward };

    size_t    pt_index;
    Direction direction;
};

static std::optional<SnapAngle> find_nearest_snap_angle(const ColorPoints &color_polygon_points, const size_t begin_idx, const double max_distance, const double angle_threshold)
{
    assert(begin_idx < color_polygon_points.size() && color_polygon_points.size() >= 3);

    const auto calc_three_points_angle = [&color_polygon_points](const size_t prev_idx, const size_t curr_idx, const size_t next_idx) -> double {
        return calc_three_color_points_angle(color_polygon_points[prev_idx], color_polygon_points[curr_idx], color_polygon_points[next_idx]);
    };

    const auto next_index = [&color_polygon_points](const size_t curr_idx) -> size_t {
        return curr_idx < (color_polygon_points.size() - 1) ? curr_idx + 1 : 0;
    };

    const auto prev_index = [&color_polygon_points](const size_t curr_idx) -> size_t {
        return curr_idx > 0 ? curr_idx - 1 : color_polygon_points.size() - 1;
    };

    const auto distance_sqr = [&color_polygon_points](const size_t first_idx, const size_t second_idx) -> double {
        return (color_polygon_points[second_idx].p - color_polygon_points[first_idx].p).cast<double>().squaredNorm();
    };

    // Try to find a snap angle in the forward direction.
    const double max_distance_sqr   = Slic3r::sqr(max_distance);
    size_t       prev_idx           = prev_index(begin_idx);
    size_t       curr_idx           = begin_idx;
    size_t       next_idx           = next_index(begin_idx);
    double       total_distance_sqr = 0.;
    do {
        total_distance_sqr += distance_sqr(curr_idx, next_idx);
        if (total_distance_sqr > max_distance_sqr) {
            break;
        }

        prev_idx = curr_idx;
        curr_idx = next_idx;
        next_idx = next_index(next_idx);

        if (color_polygon_points[prev_idx].color_next != color_polygon_points[curr_idx].color_next) {
            break;
        }

        if (const double angle = calc_three_points_angle(prev_idx, curr_idx, next_idx); angle > angle_threshold) {
            return SnapAngle{curr_idx, SnapAngle::Direction::Forward};
        }
    } while (curr_idx != begin_idx);

    // Try to find a snap angle in the backward direction.
    prev_idx           = prev_index(begin_idx);
    curr_idx           = begin_idx;
    next_idx           = next_index(begin_idx);
    total_distance_sqr = 0.;
    do {
        total_distance_sqr += distance_sqr(prev_idx, curr_idx);
        if (total_distance_sqr > max_distance_sqr) {
            break;
        }

        next_idx = curr_idx;
        curr_idx = prev_idx;
        prev_idx = prev_index(prev_idx);

        if (color_polygon_points[prev_idx].color_next != color_polygon_points[curr_idx].color_next) {
            break;
        }

        if (const double angle = calc_three_points_angle(prev_idx, curr_idx, next_idx); angle > angle_threshold) {
            return SnapAngle{curr_idx, SnapAngle::Direction::Backward};
        }
    } while (curr_idx != begin_idx);

    return std::nullopt;
}

static void snap_projected_color_points_to_nearest_angles(std::vector<ColorPoints> &color_polygons_points)
{
    for (ColorPoints &color_polygon_points : color_polygons_points) {
        assert(color_polygon_points.size() >= 3);

        const auto next_index = [&color_polygon_points](const size_t curr_idx) -> size_t {
            return curr_idx < (color_polygon_points.size() - 1) ? curr_idx + 1 : 0;
        };

        const auto prev_index = [&color_polygon_points](const size_t curr_idx) -> size_t {
            return curr_idx > 0 ? curr_idx - 1 : color_polygon_points.size() - 1;
        };

        for (size_t curr_idx = 0; curr_idx < color_polygon_points.size(); ++curr_idx) {
            const ColorPoint &prev_pt = color_polygon_points[prev_index(curr_idx)];
            const ColorPoint &curr_pt = color_polygon_points[curr_idx];
            const ColorPoint &next_pt = color_polygon_points[next_index(curr_idx)];

            // We care only about places in which color changes with angles below the threshold.
            if (prev_pt.color_next == curr_pt.color_next || calc_three_color_points_angle(prev_pt, curr_pt, next_pt) > MM_SEGMENTATION_SNAP_ANGLE_THRESHOLD) {
                continue;
            }

            const std::optional<SnapAngle> snap_angle = find_nearest_snap_angle(color_polygon_points, curr_idx, MM_SEGMENTATION_SNAP_ANGLE_MAX_DISTANCE_SCALED, MM_SEGMENTATION_SNAP_ANGLE_THRESHOLD);
            if (!snap_angle.has_value()) {
                continue;
            }

            if (snap_angle->direction == SnapAngle::Direction::Forward) {
                color_polygon_points[snap_angle->pt_index].color_next = curr_pt.color_next;

                for (size_t modify_idx = curr_idx; modify_idx != snap_angle->pt_index; modify_idx = next_index(modify_idx)) {
                    color_polygon_points[modify_idx].color_next = prev_pt.color_next;
                }
            } else {
                assert(snap_angle->direction == SnapAngle::Direction::Backward);
                color_polygon_points[prev_index(snap_angle->pt_index)].color_next = prev_pt.color_next;

                for (size_t modify_idx = snap_angle->pt_index; modify_idx != curr_idx; modify_idx = next_index(modify_idx)) {
                    color_polygon_points[modify_idx].color_next = curr_pt.color_next;
                }
            }
        }
    }
}

static ColorPoints convert_color_polygon_projection_lines_to_color_points(const ColorProjectionLines &color_polygon_projection_lines)
{
    if (color_polygon_projection_lines.empty())
        return {};

    ColorPoints color_polygon_points;
    color_polygon_points.reserve(color_polygon_projection_lines.size());

    uint8_t prev_color = get_color_of_first_polygon_line(color_polygon_projection_lines);
    uint8_t curr_color = prev_color;
    for (const ColorProjectionLine &color_line : color_polygon_projection_lines) {
        if (color_line.color_changes.empty()) {
            color_polygon_points.emplace_back(color_line.a, prev_color, curr_color);
            prev_color = curr_color;
        } else {
            if (const ColorChange &first_color_change = color_line.color_changes.front(); first_color_change.t != 0.) {
                color_polygon_points.emplace_back(color_line.a, prev_color, curr_color);
                prev_color = curr_color;
            }

            for (const ColorChange &color_change : color_line.color_changes) {
                if (color_change.t != 1.) {
                    const Vec2d color_line_vec    = (color_line.b - color_line.a).cast<double>();
                    const Point color_line_new_pt = (color_change.t * color_line_vec).cast<coord_t>() + color_line.a;

                    color_polygon_points.emplace_back(color_line_new_pt, prev_color, color_change.color_next);
                    curr_color = color_change.color_next;
                    prev_color = curr_color;
                }
            }

            if (const ColorChange &last_color_change = color_line.color_changes.back(); last_color_change.t == 1.) {
                curr_color = last_color_change.color_next;
            }
        }
    }

    ColorPoints color_polygon_points_filtered;
    color_polygon_points_filtered.reserve(color_polygon_points.size());

    douglas_peucker(color_polygon_points.begin(), color_polygon_points.end(), std::back_inserter(color_polygon_points_filtered), INPUT_POLYGONS_FILTER_TOLERANCE_SCALED, POLYGON_COLOR_FILTER_DISTANCE_SCALED);

    if (color_polygon_points_filtered.size() < 3)
        return {};

    filter_color_of_small_segments(color_polygon_points_filtered, POLYGON_COLOR_FILTER_DISTANCE_SCALED);

    return color_polygon_points_filtered;
}

static std::vector<ColorPoints> convert_color_expolygon_projection_lines_to_color_points(const ColorProjectionExPolygon &color_projection_expolygon)
{
    std::vector<ColorPoints> color_expolygon_points;
    color_expolygon_points.reserve(color_projection_expolygon.polygons_count());

    ColorPoints contour_color_points = convert_color_polygon_projection_lines_to_color_points(color_projection_expolygon.contour);
    if (contour_color_points.empty())
        return color_expolygon_points;

    color_expolygon_points.emplace_back(std::move(contour_color_points));
    for (const ColorProjectionLines &hole_color_projection_lines : color_projection_expolygon.holes) {
        ColorPoints hole_color_points = convert_color_polygon_projection_lines_to_color_points(hole_color_projection_lines);
        if (hole_color_points.empty())
            continue;

        color_expolygon_points.emplace_back(std::move(hole_color_points));
    }

    return color_expolygon_points;
}

static std::optional<ColorProjectionRange> project_color_line_on_projection_line(const ColorLine &color_line, const ColorProjectionLine &projection_line, const double max_projection_distance_scaled) {
    const Vec2d projection_line_vec = (projection_line.b - projection_line.a).cast<double>();
    const Vec2d color_line_vec_a    = (color_line.a - projection_line.a).cast<double>();
    const Vec2d color_line_vec_b    = (color_line.b - projection_line.a).cast<double>();

    const double projection_line_length_sqr = projection_line_vec.squaredNorm();
    if (projection_line_length_sqr == 0.)
        return std::nullopt;

    // Project both endpoints of color_line on the projection_line.
    const double t_a_raw = color_line_vec_a.dot(projection_line_vec) / projection_line_length_sqr;
    const double t_b_raw = color_line_vec_b.dot(projection_line_vec) / projection_line_length_sqr;

    const double t_a_clamped = std::clamp(t_a_raw, 0., 1.);
    const double t_b_clamped = std::clamp(t_b_raw, 0., 1.);

    if (t_a_clamped == t_b_clamped)
        return std::nullopt;

    auto distance_to_color_line = [&projection_line_vec, &projection_line, &color_line](const double t_raw, const double t_clamped, const Vec2d &color_line_vec_pt) -> double {
        if (0. <= t_raw && t_raw <= 1.) {
            return (t_clamped * projection_line_vec - color_line_vec_pt).norm();
        } else {
            // T value is outside <0, 1>, so we calculate the distance between the clamped T value and the nearest point on the color_line.
            // That means that we calculate the distance between one of the endpoints of the projection_line and the color_line.
            const Point &projection_line_nearest_pt = (t_raw < 0.) ? projection_line.a : projection_line.b;
            return line_alg::distance_to(color_line.line(), projection_line_nearest_pt);
        }
    };

    // Calculate the distance of both endpoints of color_line to the projection_line.
    const double color_line_a_dist = distance_to_color_line(t_a_raw, t_a_clamped, color_line_vec_a);
    const double color_line_b_dist = distance_to_color_line(t_b_raw, t_b_clamped, color_line_vec_b);

    ColorProjectionRange range = t_a_clamped < t_b_clamped ? ColorProjectionRange{t_a_clamped, color_line_a_dist, t_b_clamped, color_line_b_dist, color_line.color}
                                                           : ColorProjectionRange{t_b_clamped, color_line_b_dist, t_a_clamped, color_line_a_dist, color_line.color};

    if (range.from_distance <= max_projection_distance_scaled && range.to_distance <= max_projection_distance_scaled) {
        // Both endpoints are close enough to the line, so we don't have to do linear interpolation.
        return range;
    } else if (range.from_distance > max_projection_distance_scaled && range.to_distance > max_projection_distance_scaled) {
        // Both endpoints are too distant from the projection_line.
        return std::nullopt;
    }

    // Calculate for which value of T we reach the distance of max_projection_distance_scaled.
    const double t_max = (max_projection_distance_scaled - range.from_distance) / (range.to_distance - range.from_distance) * (range.to_t - range.from_t) + range.from_t;
    if (range.from_distance > max_projection_distance_scaled) {
        range.from_t        = t_max;
        range.from_distance = max_projection_distance_scaled;
    } else {
        range.to_t        = t_max;
        range.to_distance = max_projection_distance_scaled;
    }

    return range;
}

inline void update_color_changes_using_color_projection_ranges(ColorProjectionLine &projection_line) {
    ColorProjectionRanges &ranges = projection_line.color_projection_ranges;
    Slic3r::sort_remove_duplicates(ranges);

    // First, calculate event points in which the nearest color could change.
    std::vector<double> event_points;
    for (const ColorProjectionRange &range : ranges) {
        event_points.emplace_back(range.from_t);
        event_points.emplace_back(range.to_t);
    }

    auto make_linef = [](const ColorProjectionRange &range) -> Linef {
        return {Vec2d(range.from_t, range.from_distance), Vec2d(range.to_t, range.to_distance)};
    };

    for (auto curr_range_it = ranges.begin(); curr_range_it != ranges.end(); ++curr_range_it) {
        for (auto next_range_it = std::next(curr_range_it); next_range_it != ranges.end(); ++next_range_it) {
            if (curr_range_it->to_t == next_range_it->from_t) {
                continue;
            } else if (!curr_range_it->contains(next_range_it->from_t)) {
                // Ranges are sorted based on the from_t, so when the next->from_t isn't inside the current range, we can skip all other succeeding ranges.
                break;
            }

            Vec2d intersect_pt;
            if (line_alg::intersection(make_linef(*curr_range_it), make_linef(*next_range_it), &intersect_pt)) {
                event_points.emplace_back(intersect_pt.x());
            }
        }
    }

    Slic3r::sort_remove_duplicates(event_points);

    for (size_t event_idx = 1; event_idx < event_points.size(); ++event_idx) {
        const double range_start = event_points[event_idx - 1];
        const double range_end   = event_points[event_idx];

        double              min_area_value = std::numeric_limits<double>::max();
        ColorPolygon::Color min_area_color = 0;
        for (const ColorProjectionRange &range : ranges) {
            if (!range.contains(range_start) || !range.contains(range_end))
                continue;

            // Minimize the area of the trapezoid defined by range length and distance in its endpoints.
            const double range_area = range.distance_at(range_start) + range.distance_at(range_end);
            if (range_area < min_area_value) {
                min_area_value = range_area;
                min_area_color = range.color;
            }
        }

        if (min_area_value != std::numeric_limits<double>::max()) {
            projection_line.color_changes.emplace_back(range_start, min_area_color);
        }
    }
}

static void update_color_changes_using_color_projection_ranges(ColorProjectionLines &polygon_projection_lines)
{
    for (ColorProjectionLine &projection_line : polygon_projection_lines) {
        update_color_changes_using_color_projection_ranges(projection_line);
    }
}

static void update_color_changes_using_color_projection_ranges(ColorProjectionExPolygon &color_projection_expolygon)
{
    update_color_changes_using_color_projection_ranges(color_projection_expolygon.contour);
    for (ColorProjectionLines &hole_color_projection_lines : color_projection_expolygon.holes) {
        update_color_changes_using_color_projection_ranges(hole_color_projection_lines);
    }
}

static std::vector<ColorPolygons> slice_model_volume_with_color(const ModelVolume                                              &model_volume,
                                                               const std::function<ModelVolumeFacetsInfo(const ModelVolume &)> &extract_facets_info,
                                                               const std::vector<float>                                        &layer_zs,
                                                               const PrintObject                                               &print_object,
                                                               const size_t                                                     num_facets_states)
{
    const ModelVolumeFacetsInfo facets_info = extract_facets_info(model_volume);

    const auto extract_mesh_with_color = [&model_volume, &facets_info]() -> indexed_triangle_set_with_color {
        if (const int volume_extruder_id = model_volume.extruder_id(); facets_info.replace_default_extruder && !facets_info.is_painted && volume_extruder_id >= 0) {
            const TriangleMesh &mesh = model_volume.mesh();
            return {mesh.its.indices, mesh.its.vertices, std::vector<uint8_t>(mesh.its.indices.size(), uint8_t(volume_extruder_id))};
        }

        return facets_info.facets_annotation.get_all_facets_strict_with_colors(model_volume);
    };

    const indexed_triangle_set_with_color mesh_with_color = extract_mesh_with_color();
    const Transform3d                     trafo           = print_object.trafo_centered() * model_volume.get_matrix();
    const MeshSlicingParams               slicing_params{trafo};

    std::vector<ColorPolygons> color_polygons_per_layer = slice_mesh(mesh_with_color, layer_zs, slicing_params);

    // Replace default painted color (TriangleStateType::NONE) with volume extruder.
    if (const int volume_extruder_id = model_volume.extruder_id(); facets_info.replace_default_extruder && facets_info.is_painted && volume_extruder_id > 0) {
        for (ColorPolygons &color_polygons : color_polygons_per_layer) {
            for (ColorPolygon &color_polygon : color_polygons) {
                std::replace(color_polygon.colors.begin(), color_polygon.colors.end(), static_cast<uint8_t>(TriangleStateType::NONE), static_cast<uint8_t>(volume_extruder_id));
            }
        }
    }

    // Replace any non-existing painted color with the default (TriangleStateType::NONE).
    for (ColorPolygons &color_polygons : color_polygons_per_layer) {
        for (ColorPolygon &color_polygon : color_polygons) {
            std::replace_if(color_polygon.colors.begin(), color_polygon.colors.end(),
                            [&num_facets_states](const uint8_t color) { return color >= num_facets_states; },
                            static_cast<uint8_t>(TriangleStateType::NONE));
        }
    }

    return color_polygons_per_layer;
}

// For each ColorProjectionLine, find the nearest ColorLines and project them on the ColorProjectionLine.
static void project_color_projection_lines_on_color_lines(ColorProjectionLines                           &color_projection_lines, const AABBTreeLines::LinesDistancer<ColorLine> &color_lines_distancer)
{
    for (ColorProjectionLine &projection_line : color_projection_lines) {
        std::vector<size_t> nearest_color_line_indices;
        Slic3r::append(nearest_color_line_indices, color_lines_distancer.all_lines_in_radius(projection_line.a, MM_SEGMENTATION_MAX_PROJECTION_DISTANCE_SCALED));
        Slic3r::append(nearest_color_line_indices, color_lines_distancer.all_lines_in_radius(projection_line.b, MM_SEGMENTATION_MAX_PROJECTION_DISTANCE_SCALED));
        Slic3r::sort_remove_duplicates(nearest_color_line_indices);

        for (size_t nearest_color_line_idx : nearest_color_line_indices) {
            const ColorLine                    &color_line = color_lines_distancer.get_line(nearest_color_line_idx);
            std::optional<ColorProjectionRange> projection = project_color_line_on_projection_line(color_line, projection_line, MM_SEGMENTATION_MAX_PROJECTION_DISTANCE_SCALED);
            if (projection.has_value()) {
                projection_line.color_projection_ranges.emplace_back(*projection);
            }
        }
    }
}

// For each ColorProjectionLine, find the nearest ColorLines and project them on the ColorProjectionLine.
static void project_color_projection_expolygon_on_color_lines(ColorProjectionExPolygon &color_projection_expolygon, const AABBTreeLines::LinesDistancer<ColorLine> &color_lines_distancer)
{
    project_color_projection_lines_on_color_lines(color_projection_expolygon.contour, color_lines_distancer);
    for (ColorProjectionLines &hole_color_projection_lines : color_projection_expolygon.holes) {
        project_color_projection_lines_on_color_lines(hole_color_projection_lines, color_lines_distancer);
    }
}

// For each ColorProjectionLine, find the nearest ColorLines and project them on the ColorProjectionLine.
static void project_color_projection_expolygons_on_color_lines(ColorProjectionExPolygons &color_projection_expolygons, const AABBTreeLines::LinesDistancer<ColorLine> &color_lines_distancer)
{
    for (ColorProjectionExPolygon &color_projection_expolygon : color_projection_expolygons) {
        project_color_projection_expolygon_on_color_lines(color_projection_expolygon, color_lines_distancer);
    }
}

// For each ColorLine, find the nearest ColorProjectionLines and project the ColorLine on each ColorProjectionLine.
static void project_color_lines_on_color_projection_lines(std::vector<ColorLines> &color_polygons_lines, const AABBTreeLines::LinesDistancer<ColorProjectionLineWrapper> &color_projection_lines_distancer)
{
    for (const ColorLines &color_polygon : color_polygons_lines) {
        for (const ColorLine &color_line : color_polygon) {
            std::vector<size_t> nearest_projection_line_indices;
            Slic3r::append(nearest_projection_line_indices, color_projection_lines_distancer.all_lines_in_radius(color_line.a, MM_SEGMENTATION_MAX_PROJECTION_DISTANCE_SCALED));
            Slic3r::append(nearest_projection_line_indices, color_projection_lines_distancer.all_lines_in_radius(color_line.b, MM_SEGMENTATION_MAX_PROJECTION_DISTANCE_SCALED));
            Slic3r::sort_remove_duplicates(nearest_projection_line_indices);

            for (size_t nearest_projection_line_idx : nearest_projection_line_indices) {
                ColorProjectionLine                 &color_projection_line = *color_projection_lines_distancer.get_line(nearest_projection_line_idx).color_projection_line;
                std::optional<ColorProjectionRange> projection              = project_color_line_on_projection_line(color_line, color_projection_line, MM_SEGMENTATION_MAX_PROJECTION_DISTANCE_SCALED);
                if (projection.has_value()) {
                    color_projection_line.color_projection_ranges.emplace_back(*projection);
                }
            }
        }
    }
}

std::vector<std::vector<ExPolygons>> segmentation_by_painting(const PrintObject                                               &print_object,
                                                              const std::function<ModelVolumeFacetsInfo(const ModelVolume &)> &extract_facets_info,
                                                              const size_t                                                     num_facets_states,
                                                              const float                                                      segmentation_max_width,
                                                              const float                                                      segmentation_interlocking_depth,
                                                              const bool                                                       segmentation_interlocking_beam,
                                                              const IncludeTopAndBottomLayers                                  include_top_and_bottom_layers,
                                                              const std::function<void()>                                     &throw_on_cancel_callback)
{
    const size_t                                   num_layers    = print_object.layers().size();
    const SpanOfConstPtrs<Layer>                   layers        = print_object.layers();

    std::vector<ExPolygons>                input_expolygons(num_layers);
    std::vector<ColorProjectionExPolygons> input_expolygons_projection_lines_layers(num_layers);
    std::vector<std::vector<ColorLines>>   color_polygons_lines_layers(num_layers);

    // Merge all regions and remove small holes
    BOOST_LOG_TRIVIAL(debug) << "Print object segmentation - Slices preprocessing in parallel - Begin";
    tbb::parallel_for(tbb::blocked_range<size_t>(0, num_layers), [&layers, &input_expolygons, &input_expolygons_projection_lines_layers, &throw_on_cancel_callback](const tbb::blocked_range<size_t> &range) {
        for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++layer_idx) {
            throw_on_cancel_callback();

            ExPolygons ex_polygons;
            for (LayerRegion *region : layers[layer_idx]->regions()) {
                for (const Surface &surface : region->slices()) {
                    Slic3r::append(ex_polygons, offset_ex(surface.expolygon, float(10 * SCALED_EPSILON)));
                }
            }

            // All expolygons are expanded by SCALED_EPSILON, merged, and then shrunk again by SCALED_EPSILON
            // to ensure that very close polygons will be merged.
            ex_polygons = union_ex(ex_polygons);
            // Remove all expolygons and holes with an area less than 0.1mm^2
            remove_small_and_small_holes(ex_polygons, Slic3r::sqr(POLYGON_FILTER_MIN_AREA_SCALED));
            // Occasionally, some input polygons contained self-intersections that caused problems with Voronoi diagrams
            // and consequently with the extraction of colored segments by function extract_colored_segments.
            // Calling simplify_polygons removes these self-intersections.
            // Also, occasionally input polygons contained several points very close together (distance between points is 1 or so).
            // Such close points sometimes caused that the Voronoi diagram has self-intersecting edges around these vertices.
            // This consequently leads to issues with the extraction of colored segments by function extract_colored_segments.
            // Calling expolygons_simplify fixed these issues.
            input_expolygons[layer_idx]                         = remove_duplicates(expolygons_simplify(offset_ex(ex_polygons, -10.f * float(SCALED_EPSILON)), 5 * SCALED_EPSILON), scaled<coord_t>(0.01), PI / 6);
            input_expolygons_projection_lines_layers[layer_idx] = create_color_projection_expolygons(input_expolygons[layer_idx]);

            if constexpr (MM_SEGMENTATION_DEBUG_INPUT) {
                export_processed_input_expolygons_to_svg(debug_out_path("mm-input-%d.svg", layer_idx), layers[layer_idx]->regions(), input_expolygons[layer_idx]);
            }
        }
    }); // end of parallel_for
    BOOST_LOG_TRIVIAL(debug) << "Print object segmentation - Slices preprocessing in parallel - End";

    BOOST_LOG_TRIVIAL(debug) << "Print object segmentation - Slicing painted triangles - Begin";
    const std::vector<float> layer_zs = get_print_object_layers_zs(layers);
    for (const ModelVolume *mv : print_object.model_object()->volumes) {
        std::vector<ColorPolygons> color_polygons_per_layer = slice_model_volume_with_color(*mv, extract_facets_info, layer_zs, print_object, num_facets_states);

        tbb::parallel_for(tbb::blocked_range<size_t>(0, num_layers), [&color_polygons_per_layer, &color_polygons_lines_layers, &throw_on_cancel_callback](const tbb::blocked_range<size_t> &range) {
            for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++layer_idx) {
                throw_on_cancel_callback();

                ColorPolygons &raw_color_polygons = color_polygons_per_layer[layer_idx];
                filter_out_small_color_polygons(raw_color_polygons, POLYGON_FILTER_MIN_AREA_SCALED, POLYGON_FILTER_MIN_OFFSET_SCALED);

                if (raw_color_polygons.empty())
                    continue;

                // Convert ColorPolygons into the vector of ColorPoints to perform several filtrations that are performed on points.
                color_polygons_lines_layers[layer_idx].reserve(color_polygons_lines_layers[layer_idx].size() + raw_color_polygons.size());
                for (const ColorPoints &color_polygon_points : color_polygons_to_color_points(raw_color_polygons)) {
                    ColorPoints color_polygon_points_filtered;
                    color_polygon_points_filtered.reserve(color_polygon_points.size());

                    douglas_peucker(color_polygon_points.begin(), color_polygon_points.end(), std::back_inserter(color_polygon_points_filtered), POLYGON_COLOR_FILTER_TOLERANCE_SCALED, POLYGON_COLOR_FILTER_DISTANCE_SCALED);

                    if (color_polygon_points_filtered.size() < 3)
                        continue;

                    filter_color_of_small_segments(color_polygon_points_filtered, POLYGON_COLOR_FILTER_DISTANCE_SCALED);
                    assert(is_valid_color_polygon_points(color_polygon_points_filtered));

                    color_polygons_lines_layers[layer_idx].emplace_back(color_points_to_color_lines(color_polygon_points_filtered));
                }
            }
        }); // end of parallel_for
    }
    BOOST_LOG_TRIVIAL(debug) << "Print object segmentation - Slicing painted triangles - End";

    if constexpr (MM_SEGMENTATION_DEBUG_FILTERED_COLOR_LINES) {
        for (size_t layer_idx = 0; layer_idx < print_object.layers().size(); ++layer_idx) {
            export_color_polygons_lines_to_svg(debug_out_path("mm-filtered-color-line-%d.svg", layer_idx), color_polygons_lines_layers[layer_idx], input_expolygons[layer_idx]);
        }
    }

    // Project sliced ColorPolygons on sliced layers (input_expolygons).
    BOOST_LOG_TRIVIAL(debug) << "Print object segmentation - Projection of painted triangles - Begin";
    tbb::parallel_for(tbb::blocked_range<size_t>(0, num_layers), [&color_polygons_lines_layers, &input_expolygons_projection_lines_layers, &throw_on_cancel_callback](const tbb::blocked_range<size_t> &range) {
        for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++layer_idx) {
            throw_on_cancel_callback();

            // For each ColorLine, find the nearest ColorProjectionLines and project the ColorLine on each ColorProjectionLine.
            const AABBTreeLines::LinesDistancer<ColorProjectionLineWrapper> color_projection_lines_distancer{create_color_projection_lines_mapping(input_expolygons_projection_lines_layers[layer_idx])};
            project_color_lines_on_color_projection_lines(color_polygons_lines_layers[layer_idx], color_projection_lines_distancer);

            // For each ColorProjectionLine, find the nearest ColorLines and project them on the ColorProjectionLine.
            const AABBTreeLines::LinesDistancer<ColorLine> color_lines_distancer{flatten_color_lines(color_polygons_lines_layers[layer_idx])};
            project_color_projection_expolygons_on_color_lines(input_expolygons_projection_lines_layers[layer_idx], color_lines_distancer);
        }
    }); // end of parallel_for
    BOOST_LOG_TRIVIAL(debug) << "MM segmentation - Projection of painted triangles - End";

    std::vector<std::vector<ExPolygons>>  segmented_regions(num_layers);
    segmented_regions.assign(num_layers, std::vector<ExPolygons>(num_facets_states));

    // Be aware that after the projection of the ColorPolygons and its postprocessing isn't
    // ensured that consistency of the color_prev. So, only color_next can be used.
    BOOST_LOG_TRIVIAL(debug) << "Print object segmentation - Layers segmentation in parallel - Begin";
    tbb::parallel_for(tbb::blocked_range<size_t>(0, num_layers), [&input_expolygons_projection_lines_layers, &segmented_regions, &input_expolygons, &num_facets_states, &throw_on_cancel_callback](const tbb::blocked_range<size_t> &range) {
        for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++layer_idx) {
            throw_on_cancel_callback();

            ColorProjectionExPolygons &input_expolygons_projection_lines = input_expolygons_projection_lines_layers[layer_idx];
            for (ColorProjectionExPolygon &input_expolygon_projection_lines : input_expolygons_projection_lines) {
                const size_t expolygon_idx = &input_expolygon_projection_lines - input_expolygons_projection_lines.data();

                if constexpr (MM_SEGMENTATION_DEBUG_COLOR_RANGES) {
                    export_color_projection_lines_color_ranges_to_svg(debug_out_path("mm-color-ranges-%d-%d.svg", layer_idx, expolygon_idx), input_expolygon_projection_lines, input_expolygons[layer_idx]);
                }

                update_color_changes_using_color_projection_ranges(input_expolygon_projection_lines);
                filter_projected_color_points_on_expolygon(input_expolygon_projection_lines);

                std::vector<ColorPoints> color_polygons_points = convert_color_expolygon_projection_lines_to_color_points(input_expolygon_projection_lines);
                if (color_polygons_points.empty())
                    continue;

                snap_projected_color_points_to_nearest_angles(color_polygons_points);

                if constexpr (MM_SEGMENTATION_DEBUG_COLORIZED_POLYGONS) {
                    export_color_polygons_points_to_svg(debug_out_path("mm-projected-color_polygon-%d-%d.svg", layer_idx, expolygon_idx), color_polygons_points, input_expolygons[layer_idx]);
                }

                const std::vector<ColoredLines> colored_polygons = color_points_to_colored_lines(color_polygons_points);
                assert(!colored_polygons.empty());
                if (has_polygons_only_one_color(colored_polygons)) {
                    // When the whole ExPolygon is painted using the same color, it is not needed to construct a Voronoi diagram for the segmentation of this ExPolygon.
                    assert(!colored_polygons.front().empty());
                    segmented_regions[layer_idx][size_t(colored_polygons.front().front().color)].emplace_back(input_expolygons[layer_idx][expolygon_idx]);
                } else {
                    std::vector<ExPolygons> colored_segments_by_states = extract_colored_segments(colored_polygons, num_facets_states, layer_idx);
                    for (size_t state_idx = 0; state_idx < num_facets_states; ++state_idx) {
                        if (colored_segments_by_states[state_idx].empty())
                            continue;

                        Slic3r::append(segmented_regions[layer_idx][state_idx], std::move(colored_segments_by_states[state_idx]));
                    }
                }
            }

            if constexpr (MM_SEGMENTATION_DEBUG_REGIONS) {
                export_regions_to_svg(debug_out_path("mm-regions-non-merged-%d.svg", layer_idx), segmented_regions[layer_idx], input_expolygons[layer_idx]);
            }
        }
    }); // end of parallel_for
    BOOST_LOG_TRIVIAL(debug) << "Print object segmentation - Layers segmentation in parallel - End";
    throw_on_cancel_callback();

    // The first index is extruder number (includes default extruder), and the second one is layer number
    std::vector<std::vector<ExPolygons>> top_and_bottom_layers;
    if (include_top_and_bottom_layers == IncludeTopAndBottomLayers::Yes) {
        top_and_bottom_layers = segmentation_top_and_bottom_layers(print_object, input_expolygons, extract_facets_info, num_facets_states, throw_on_cancel_callback);
        throw_on_cancel_callback();
    }

    if ((segmentation_max_width > 0.f || segmentation_interlocking_depth > 0.f) && !segmentation_interlocking_beam) {
        cut_segmented_layers(input_expolygons, segmented_regions, scaled<float>(segmentation_max_width), scaled<float>(segmentation_interlocking_depth), throw_on_cancel_callback);
        throw_on_cancel_callback();
    }

    std::vector<std::vector<ExPolygons>> segmented_regions_merged = merge_segmented_layers(segmented_regions, std::move(top_and_bottom_layers), num_facets_states, throw_on_cancel_callback);
    throw_on_cancel_callback();

    if constexpr (MM_SEGMENTATION_DEBUG_REGIONS) {
        for (size_t layer_idx = 0; layer_idx < print_object.layers().size(); ++layer_idx) {
            export_regions_to_svg(debug_out_path("mm-regions-merged-%d.svg", layer_idx), segmented_regions_merged[layer_idx], input_expolygons[layer_idx]);
        }
    }

    return segmented_regions_merged;
}

// Returns multi-material segmentation based on painting in multi-material segmentation gizmo
std::vector<std::vector<ExPolygons>> multi_material_segmentation_by_painting(const PrintObject &print_object, const std::function<void()> &throw_on_cancel_callback) {
    const size_t num_facets_states  = print_object.print()->config().nozzle_diameter.size() + 1;
    const float  max_width          = float(print_object.config().mmu_segmented_region_max_width.value);
    const float  interlocking_depth = float(print_object.config().mmu_segmented_region_interlocking_depth.value);
    const bool   interlocking_beam  = print_object.config().interlocking_beam.value;

    const auto extract_facets_info = [](const ModelVolume &mv) -> ModelVolumeFacetsInfo {
        return {mv.mm_segmentation_facets, mv.is_mm_painted(), false};
    };

    return segmentation_by_painting(print_object, extract_facets_info, num_facets_states, max_width, interlocking_depth, interlocking_beam, IncludeTopAndBottomLayers::Yes, throw_on_cancel_callback);
}

// Returns fuzzy skin segmentation based on painting in fuzzy skin segmentation gizmo
std::vector<std::vector<ExPolygons>> fuzzy_skin_segmentation_by_painting(const PrintObject &print_object, const std::function<void()> &throw_on_cancel_callback) {
    const size_t num_facets_states = 2; // Unpainted facets and facets painted with fuzzy skin.

    const auto extract_facets_info = [](const ModelVolume &mv) -> ModelVolumeFacetsInfo {
        return {mv.fuzzy_skin_facets, mv.is_fuzzy_skin_painted(), false};
    };

    // Because we apply fuzzy skin just on external perimeters, we limit the depth of fuzzy skin
    // by the maximal extrusion width of external perimeters.
    float max_external_perimeter_width = 0.;
    for (size_t region_idx = 0; region_idx < print_object.num_printing_regions(); ++region_idx) {
        const PrintRegion &region = print_object.printing_region(region_idx);
        max_external_perimeter_width = std::max<float>(max_external_perimeter_width, region.flow(print_object, frExternalPerimeter, print_object.config().layer_height).width());
    }

    return segmentation_by_painting(print_object, extract_facets_info, num_facets_states, max_external_perimeter_width, 0.f, false, IncludeTopAndBottomLayers::No, throw_on_cancel_callback);
}


} // namespace Slic3r