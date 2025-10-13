#include "VoronoiGraphUtils.hpp"

#include <cmath>
#include <set>
#include <libslic3r/Geometry/VoronoiOffset.hpp>
#include "IStackFunction.hpp"
#include "EvaluateNeighbor.hpp"
#include "ParabolaUtils.hpp"
#include "LineUtils.hpp"
#include "PointUtils.hpp"
#include "PolygonUtils.hpp"

#include <libslic3r/Geometry/VoronoiVisualUtils.hpp>

// comment definition of NDEBUG to enable assert()
//#define NDEBUG
#include <cassert>

//#define SLA_SVG_VISUALIZATION_CELL_2_POLYGON

using namespace Slic3r::sla;

coord_t VoronoiGraphUtils::to_coord(const VD::coordinate_type &coord)
{
    static const VD::coordinate_type min_val =
        static_cast<VD::coordinate_type>(std::numeric_limits<coord_t>::min());
    static const VD::coordinate_type max_val =
        static_cast<VD::coordinate_type>(std::numeric_limits<coord_t>::max());
    if (coord > max_val) return std::numeric_limits<coord_t>::max();
    if (coord < min_val) return std::numeric_limits<coord_t>::min();
    return static_cast<coord_t>(std::round(coord));
}

Slic3r::Point VoronoiGraphUtils::to_point(const VD::vertex_type *vertex)
{
    return Point(to_coord(vertex->x()), to_coord(vertex->y()));
}

VoronoiGraphUtils::VD::point_type VoronoiGraphUtils::to_point(const Point &point)
{
    return VD::point_type(point.x(), point.y());
}

Slic3r::Vec2d VoronoiGraphUtils::to_point_d(const VD::vertex_type *vertex)
{
    return Vec2d(vertex->x(), vertex->y());
}

VoronoiGraphUtils::VD::segment_type VoronoiGraphUtils::to_segment(const Line &line)
{
    return VD::segment_type(to_point(line.a), to_point(line.b));
}

Slic3r::Point VoronoiGraphUtils::to_direction(const VD::edge_type *edge)
{
    return to_direction_d(edge).cast<coord_t>();
}

Slic3r::Vec2d VoronoiGraphUtils::to_direction_d(const VD::edge_type *edge) 
{
    const VD::vertex_type *v0 = edge->vertex0();
    const VD::vertex_type *v1 = edge->vertex1();
    return Vec2d(v1->x() - v0->x(), v1->y() - v0->y());
}

bool VoronoiGraphUtils::is_coord_in_limits(const VD::coordinate_type &coord,
                                           const coord_t &            source,
                                           double max_distance)
{
    VD::coordinate_type min_val = source - max_distance;
    VD::coordinate_type max_val = source + max_distance;
    if (coord > max_val) return false;
    if (coord < min_val) return false;
    return true;
}

bool VoronoiGraphUtils::is_point_in_limits(const VD::vertex_type *vertex,
                                           const Point &          source,
                                           double max_distance)
{
    if (vertex == nullptr) return false;
    return is_coord_in_limits(vertex->x(), source.x(), max_distance) &&
           is_coord_in_limits(vertex->y(), source.y(), max_distance);
}

// create line segment between (in the middle) points. With size depend on their distance
Slic3r::Line VoronoiGraphUtils::create_line_between_source_points(
    const Point &point1, const Point &point2, double maximal_distance)
{
    Point middle = (point1 + point2) / 2;
    Point diff = point1 - point2;
    double distance_2 = diff.x() * static_cast<double>(diff.x()) +
                        diff.y() * static_cast<double>(diff.y());
    double half_distance = sqrt(distance_2) / 2.;
    double half_distance_2 = distance_2 / 4;
    double size = sqrt(maximal_distance * maximal_distance - half_distance_2);
    // normalized direction to side multiplied by size/2
    double scale = size / half_distance / 2;
    Point  side_dir(-diff.y() * scale, diff.x() * scale);
    return Line(middle - side_dir, middle + side_dir);
}

std::optional<Slic3r::Line> VoronoiGraphUtils::to_line(
    const VD::edge_type &edge, const Points &points, double maximal_distance)
{
    assert(edge.is_linear());
    assert(edge.is_primary());
    const Point &p1 = retrieve_point(points, *edge.cell());
    const Point &p2 = retrieve_point(points, *edge.twin()->cell());
    const VD::vertex_type *v0 = edge.vertex0();
    const VD::vertex_type *v1 = edge.vertex1();

    bool  use_v1 = false; // v0 == nullptr or out of limits
    bool  use_double_precision = false;
    bool  use_both = false;
    if (edge.is_finite()) {
        bool is_v0_in_limit = is_point_in_limits(v0, p1, maximal_distance);
        bool is_v1_in_limit = is_point_in_limits(v1, p1, maximal_distance);
        if (!is_v0_in_limit) { 
            use_v1 = true;
            if (!is_v1_in_limit) {
                use_double_precision = true;
                use_both             = true;
            }
        } else if (is_v1_in_limit) {
            // normal full edge line segment
            return Line(to_point(v0), to_point(v1));
        }
    } else if (v0 == nullptr) {
        if (v1 == nullptr) 
        {// both vertex are nullptr, create edge between points
            return create_line_between_source_points(p1, p2, maximal_distance);
        }
        if (!is_point_in_limits(v1, p1, maximal_distance)) 
            use_double_precision = true;
        use_v1 = true;
    } else if (!is_point_in_limits(v0, p1, maximal_distance)) {
        use_double_precision = true;
        if (v1 != nullptr)
            use_v1 = true; // v1 is in
    }

    Point direction  = (use_v1) ? 
        Point(p2.y() - p1.y(), p1.x() - p2.x()) :
        Point(p1.y() - p2.y(), p2.x() - p1.x());
    const VD::vertex_type* edge_vertex = (use_v1) ? v1 : v0;
    // koeficient for crop line
    if (!use_double_precision) {
        Point ray_point = to_point(edge_vertex);
        Line ray(ray_point, ray_point + direction);
        return LineUtils::crop_half_ray(ray, p1, maximal_distance);
    }
    std::optional<Linef> segment;
    if (use_both) { 
        Linef edge_segment(to_point_d(v0), to_point_d(v1));
        segment = LineUtils::crop_line(edge_segment, p1, maximal_distance);
    } else {
        // Vertex can't be used as start point because data type limitation
        // Explanation for shortening line is in Test::bad_vertex
        Vec2d middle    = (p1.cast<double>() + p2.cast<double>()) / 2.;
        Vec2d vertex = to_point_d(edge_vertex);
        Vec2d vertex_direction = (vertex - middle);
        Vec2d vertex_dir_abs(fabs(vertex_direction.x()), fabs(vertex_direction.y()));
        double divider = (vertex_dir_abs.x() > vertex_dir_abs.y()) ?
                                      vertex_dir_abs.x() / maximal_distance :
                                      vertex_dir_abs.y() / maximal_distance;
        Vec2d  vertex_dir_short = vertex_direction / divider;
        Vec2d  start_point      = middle + vertex_dir_short;
        Linef  line_short(start_point, start_point + direction.cast<double>());
        segment = LineUtils::crop_half_ray(line_short, p1, maximal_distance);
    }
    if (!segment.has_value()) return {};
    return Line(segment->a.cast<coord_t>(), segment->b.cast<coord_t>());
}


Slic3r::Polygon VoronoiGraphUtils::to_polygon(const Lines &lines,
                                              const Point &center,
                                              double       maximal_distance,
                                              double       minimal_distance,
                                              size_t       count_points)
{
    assert(minimal_distance > 0.);
    assert(maximal_distance > minimal_distance);
    assert(count_points >= 3);
    if (lines.empty())
        return PolygonUtils::create_regular(count_points, maximal_distance, center);

    Points points;
    points.reserve(std::max(lines.size(), count_points));
    const Line *prev_line = &lines.back();
    double max_angle = 2 * M_PI / count_points;
    for (const Line &line : lines) {
        const Point &p1   = prev_line->b;
        const Point &p2   = line.a;
        prev_line         = &line;
        Point diff = p1-p2;
        if (abs(diff.x()) < minimal_distance &&
            abs(diff.y()) < minimal_distance) {
            Point avg   = (p1 + p2) / 2;
            points.push_back(avg);
            continue;
        } 
        Point  v1 = p1 - center;
        Point  v2 = p2 - center;
        double a1 = std::atan2(v1.y(), v1.x());
        double a2 = std::atan2(v2.y(), v2.x());

        double diff_angle = a2 - a1;
        if(diff_angle < 0.) diff_angle += 2 * M_PI;
        if(diff_angle > 2 * M_PI) diff_angle -= 2 * M_PI;

        size_t count_segment = std::floor(fabs(diff_angle) / max_angle) + 1;
        double increase_angle = diff_angle / count_segment;
        points.push_back(p1);
        for (size_t i = 1; i < count_segment; i++) {
            double angle = a1 + i*increase_angle;
            double x = cos(angle) * maximal_distance + center.x();
            assert(x < std::numeric_limits<coord_t>::max());
            assert(x > std::numeric_limits<coord_t>::min());
            double y = sin(angle) * maximal_distance + center.y();
            assert(y < std::numeric_limits<coord_t>::max());
            assert(y > std::numeric_limits<coord_t>::min());
            points.emplace_back(x,y);
        }
        points.push_back(p2);
    }
    Polygon polygon(points);
    if (!polygon.contains(center)) { 
        draw(polygon, lines, center); 
    }
    assert(polygon.is_valid());
    assert(polygon.contains(center));
    assert(PolygonUtils::is_not_self_intersect(polygon, center));
    return polygon;
}
    
Slic3r::Polygon VoronoiGraphUtils::to_polygon(const VD::cell_type & cell,
                                              const Slic3r::Points &points,
                                              double maximal_distance)
{
    Lines lines;
    Point center = points[cell.source_index()];
    // Convenient way to iterate edges around Voronoi cell.
    const VD::edge_type *edge = cell.incident_edge();
    do {
        assert(edge->is_linear());
        if (!edge->is_primary()) continue;
        std::optional<Line> line = to_line(*edge, points, maximal_distance);
        if (!line.has_value()) continue;
        Geometry::Orientation orientation = Geometry::orient(center, line->a, line->b);
        // Can be rich on circle over source point edge
        if (orientation == Geometry::Orientation::ORIENTATION_COLINEAR)
            continue;
        if (orientation == Geometry::Orientation::ORIENTATION_CW)
            std::swap(line->a, line->b);
        lines.push_back(*line);
    } while ((edge = edge->next()) && edge != cell.incident_edge());
    assert(!lines.empty());
    if (lines.size() > 1)
        LineUtils::sort_CCW(lines, center);
    // preccission to decide when not connect neighbor points
    double          min_distance = maximal_distance / 1000.; 
    size_t          count_point  = 6; // count added points
    Slic3r::Polygon polygon = to_polygon(lines, center, maximal_distance, min_distance, count_point);
#ifdef SLA_SVG_VISUALIZATION_CELL_2_POLYGON
    {
        std::cout << "cell " << cell.source_index() << " has " << lines.size()  << "edges" << std::endl;
        BoundingBox bbox(center - Point(maximal_distance, maximal_distance),
                         center + Point(maximal_distance, maximal_distance));
        static int  counter  = 0;
        std::string filename = "polygon" + std::to_string(counter++) + ".svg";
        SVG svg(filename.c_str(), bbox);
        svg.draw(center, "lightgreen", maximal_distance);
        svg.draw(polygon, "lightblue");
        int index = 0;
        for (auto &line : lines) {
            svg.draw(line);
            svg.draw_text(line.a, ("A"+std::to_string(++index)).c_str(), "green");
            svg.draw_text(line.b, ("B" + std::to_string(index)).c_str(), "blue");
        }
        svg.draw(center, "red", maximal_distance / 100);
    }
#endif /* SLA_SVG_VISUALIZATION_CELL_2_POLYGON */
    return polygon;
}

VoronoiGraph::Node *VoronoiGraphUtils::getNode(VoronoiGraph &         graph,
                                               const VD::vertex_type *vertex,
                                               const VD::edge_type *  edge,
                                               const Lines &          lines)
{
    std::map<const VD::vertex_type *, VoronoiGraph::Node> &data = graph.data;
    auto mapItem = data.find(vertex);
    // return when exists
    if (mapItem != data.end()) return &mapItem->second;

    // is new vertex (first edge to this vertex)
    // calculate distance to islad border + fill item0
    const VD::cell_type *cell = edge->cell();
    // const VD::cell_type *  cell2     = edge.twin()->cell();
    const Line &line = lines[cell->source_index()];
    // const Line &           line1     = lines[cell2->source_index()];
    Point  point = to_point(vertex);
    double distance = line.distance_to(point);

    auto [iterator, success] = data.emplace(vertex, VoronoiGraph::Node(vertex, distance));

    assert(success);
    if (!success) return nullptr;

    return &iterator->second;
}

Slic3r::Point VoronoiGraphUtils::retrieve_point(const Lines &        lines,
                                                const VD::cell_type &cell)
{
    using namespace boost::polygon;
    assert(cell.source_category() == SOURCE_CATEGORY_SEGMENT_START_POINT ||
           cell.source_category() == SOURCE_CATEGORY_SEGMENT_END_POINT);
    return (cell.source_category() == SOURCE_CATEGORY_SEGMENT_START_POINT) ?
               lines[cell.source_index()].a :
               lines[cell.source_index()].b;
}

const Slic3r::Point &VoronoiGraphUtils::retrieve_point(
    const Points &points, const VD::cell_type &cell)
{
    assert(cell.contains_point());
    assert(cell.source_category() == boost::polygon::SOURCE_CATEGORY_SINGLE_POINT);
    return points[cell.source_index()];
}

Slic3r::Point VoronoiGraphUtils::get_parabola_point(
    const VD::edge_type &parabola, const Slic3r::Lines &lines)
{
    using namespace boost::polygon;
    assert(parabola.is_curved());
    const VD::cell_type& cell = (parabola.cell()->contains_point())?
                                *parabola.cell() : *parabola.twin()->cell();
    assert(cell.contains_point());
    assert(cell.source_category() == SOURCE_CATEGORY_SEGMENT_START_POINT ||
           cell.source_category() == SOURCE_CATEGORY_SEGMENT_END_POINT);
    return (cell.source_category() == SOURCE_CATEGORY_SEGMENT_START_POINT) ?
           lines[cell.source_index()].a :
           lines[cell.source_index()].b;
}

Slic3r::Line VoronoiGraphUtils::get_parabola_line(
    const VD::edge_type &parabola, const Slic3r::Lines &lines)
{
    assert(parabola.is_curved());
    const VD::cell_type& cell = (parabola.cell()->contains_segment())?
                                *parabola.cell() : *parabola.twin()->cell();
    assert(cell.contains_segment());
    return lines[cell.source_index()];
}

Parabola VoronoiGraphUtils::get_parabola(
    const VD::edge_type &edge, const Lines &lines)
{
    Point    point = get_parabola_point(edge, lines);
    Line     line  = get_parabola_line(edge, lines);
    return Parabola(line, point);
}

double VoronoiGraphUtils::calculate_length_of_parabola(
    const VD::edge_type &                               edge,
    const Lines &                                       lines)
{
    Point v0 = to_point(edge.vertex0());
    Point v1 = to_point(edge.vertex1());
    ParabolaSegment parabola(get_parabola(edge, lines), v0, v1);
    return ParabolaUtils::length(parabola);
}

double VoronoiGraphUtils::calculate_length(
    const VD::edge_type &edge, const Lines &lines)
{
    if (edge.is_linear()) {
        const VD::vertex_type* v0 = edge.vertex0();
        const VD::vertex_type* v1 = edge.vertex1();
        double diffX = v0->x() - v1->x();
        double diffY = v0->y() - v1->y();
        return sqrt(diffX * diffX + diffY * diffY);
    }    
    assert(edge.is_curved());
    return calculate_length_of_parabola(edge, lines);
}

double VoronoiGraphUtils::calculate_max_width(
    const VD::edge_type &edge, const Lines &lines)
{
    auto get_squared_distance = [&](const VD::vertex_type *vertex,
                                    const Point &point) -> double {
        Point point_v = to_point(vertex);
        Vec2d vector  = (point - point_v).cast<double>();
        return vector.x() * vector.x() + vector.y() * vector.y();
    };
    auto max_width = [&](const Point& point)->double{
        return 2. *
               sqrt(std::max(get_squared_distance(edge.vertex0(), point),
                             get_squared_distance(edge.vertex1(), point)));
    };

    if (edge.is_linear()) {
        // edge line could be initialized by 2 points
        if (edge.cell()->contains_point()) {
            const Line &source_line = lines[edge.cell()->source_index()];
            Point source_point;
            if (edge.cell()->source_category() ==
                boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT)
                source_point = source_line.a;
            else {
                assert(edge.cell()->source_category() ==
                    boost::polygon::SOURCE_CATEGORY_SEGMENT_END_POINT);
                source_point = source_line.b;
            }
            return max_width(source_point);
        }
        assert(edge.cell()->contains_segment());
        assert(!edge.twin()->cell()->contains_point());
        assert(edge.twin()->cell()->contains_segment());

        const Line &line = lines[edge.cell()->source_index()];

        Point  v0        = to_point(edge.vertex0());
        Point  v1        = to_point(edge.vertex1());
        double distance0 = line.perp_distance_to(v0);
        double distance1 = line.perp_distance_to(v1);
        return 2 * std::max(distance0, distance1);
    }
    assert(edge.is_curved());
    Parabola parabola = get_parabola(edge, lines);
    // distance to point and line is same
    // vector from edge vertex to parabola focus point
    return max_width(parabola.focus);
}

std::pair<coord_t, coord_t> VoronoiGraphUtils::calculate_width(
    const VD::edge_type &edge, const Lines &lines)
{
    if (edge.is_linear()) 
        return calculate_width_for_line(edge, lines);
    return calculate_width_for_parabola(edge, lines);
}

std::pair<coord_t, coord_t> VoronoiGraphUtils::calculate_width_for_line(
    const VD::edge_type &line_edge, const Lines &lines)
{
    assert(line_edge.is_linear());
    // edge line could be initialized by 2 points
    if (line_edge.cell()->contains_point()) {
        const Line &source_line = lines[line_edge.cell()->source_index()];
        Point       source_point;
        if (line_edge.cell()->source_category() ==
            boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT)
            source_point = source_line.a;
        else {
            assert(line_edge.cell()->source_category() ==
                   boost::polygon::SOURCE_CATEGORY_SEGMENT_END_POINT);
            source_point = source_line.b;
        }
        return min_max_width(line_edge, source_point);
    }
    assert(line_edge.cell()->contains_segment());
    assert(!line_edge.twin()->cell()->contains_point());
    assert(line_edge.twin()->cell()->contains_segment());
    const Line &                line = lines[line_edge.cell()->source_index()];
    Point                       v0   = to_point(line_edge.vertex0());
    Point                       v1   = to_point(line_edge.vertex1());
    double                      distance0 = line.perp_distance_to(v0);
    double                      distance1 = line.perp_distance_to(v1);
    std::pair<coord_t, coord_t> min_max(2 * static_cast<coord_t>(distance0),
                                        2 * static_cast<coord_t>(distance1));
    if (min_max.first > min_max.second)
        std::swap(min_max.first, min_max.second);
    return min_max;
}

std::pair<coord_t, coord_t> VoronoiGraphUtils::calculate_width_for_parabola(
    const VD::edge_type &parabola_edge, const Lines &lines)
{
    assert(parabola_edge.is_curved());
    // distance to point and line on parabola is same
    Parabola                    parabola = get_parabola(parabola_edge, lines);
    Point                       v0       = to_point(parabola_edge.vertex0());
    Point                       v1       = to_point(parabola_edge.vertex1());
    ParabolaSegment             parabola_segment(parabola, v0, v1);
    std::pair<coord_t, coord_t> min_max = min_max_width(parabola_edge, parabola.focus);        
    if (ParabolaUtils::is_over_zero(parabola_segment)) {
        min_max.first = parabola.directrix.perp_distance_to(parabola.focus);
    }
    return min_max;
}

std::pair<coord_t, coord_t> VoronoiGraphUtils::min_max_width(
    const VD::edge_type &edge, const Point &point)
{
    auto distance = [](const VD::vertex_type *vertex,
                       const Point &          point) -> coord_t {
        Vec2d  point_d  = point.cast<double>();
        Vec2d  diff     = point_d - to_point_d(vertex);
        double distance = diff.norm();
        return static_cast<coord_t>(std::round(distance));
    };
    std::pair<coord_t, coord_t> result(2 * distance(edge.vertex0(), point),
                                       2 * distance(edge.vertex1(), point));
    if (result.first > result.second) std::swap(result.first, result.second);
    return result;
};

VoronoiGraph VoronoiGraphUtils::create_skeleton(const VD &vd, const Lines &lines)
{
    // vd should be annotated.
    // assert(Voronoi::debug::verify_inside_outside_annotations(vd));

    VoronoiGraph skeleton;
    for (const VD::edge_type &edge : vd.edges()) {
        if (
            // Ignore secondary and unbounded edges, they shall never be part
            // of the skeleton.
            edge.is_secondary() || edge.is_infinite() ||
            // Skip the twin edge of an edge, that has already been processed.
            &edge > edge.twin() ||
            // Ignore outer edges.
            (Voronoi::edge_category(edge) !=
                 Voronoi::EdgeCategory::PointsInside &&
             Voronoi::edge_category(edge.twin()) !=
                 Voronoi::EdgeCategory::PointsInside))
            continue;

        const VD::vertex_type * v0        = edge.vertex0();
        const VD::vertex_type * v1        = edge.vertex1();
        Voronoi::VertexCategory category0 = Voronoi::vertex_category(*v0);
        Voronoi::VertexCategory category1 = Voronoi::vertex_category(*v1);
        if (category0 == Voronoi::VertexCategory::Outside ||
            category1 == Voronoi::VertexCategory::Outside)
            continue;
        // only debug check annotation
        if (category0 == Voronoi::VertexCategory::Unknown ||
            category1 == Voronoi::VertexCategory::Unknown)
            return {}; // vd must be annotated

        double length = calculate_length(edge, lines);
        coord_t             min_width, max_width;
        std::tie(min_width, max_width) = calculate_width(edge, lines);
        auto neighbor_size = std::make_shared<VoronoiGraph::Node::Neighbor::Size>(
            length, min_width, max_width);

        VoronoiGraph::Node *node0 = getNode(skeleton, v0, &edge, lines);
        VoronoiGraph::Node *node1 = getNode(skeleton, v1, &edge, lines);
        // add extended Edge to graph, both side
        node0->neighbors.emplace_back(&edge, node1, neighbor_size);
        node1->neighbors.emplace_back(edge.twin(), node0, std::move(neighbor_size));
    }
    return skeleton;
}

const VoronoiGraph::Node::Neighbor *VoronoiGraphUtils::get_neighbor(
    const VoronoiGraph::Node *from, const VoronoiGraph::Node *to)
{
    for (const VoronoiGraph::Node::Neighbor &neighbor : from->neighbors)
        if (neighbor.node == to) return &neighbor;
    return nullptr;
}

double VoronoiGraphUtils::get_neighbor_distance(const VoronoiGraph::Node *from,
                                                const VoronoiGraph::Node *to)
{
    const VoronoiGraph::Node::Neighbor *neighbor = get_neighbor(from, to);
    assert(neighbor != nullptr);
    return neighbor->length();
}

VoronoiGraph::Path VoronoiGraphUtils::find_longest_path_on_circle(
    const VoronoiGraph::Circle &                 circle,
    const VoronoiGraph::ExPath::SideBranchesMap &side_branches)
{
    double half_circle_length = circle.length / 2.;
    double distance_on_circle = 0;

    bool                      is_longest_revers_direction = false;
    const VoronoiGraph::Node *longest_circle_node         = nullptr;
    const VoronoiGraph::Path *longest_circle_branch       = nullptr;
    double                    longest_branch_length       = 0;

    bool is_short_revers_direction = false;
    // find longest side branch
    const VoronoiGraph::Node *prev_circle_node = nullptr;
    for (const VoronoiGraph::Node *circle_node : circle.nodes) {
        if (prev_circle_node != nullptr)
            distance_on_circle += get_neighbor_distance(circle_node,
                                                        prev_circle_node);
        prev_circle_node = circle_node;

        auto side_branches_item = side_branches.find(circle_node);
        if (side_branches_item != side_branches.end()) {
            // side_branches should be sorted by length
            if (distance_on_circle > half_circle_length)
                is_short_revers_direction = true;
            const auto &longest_node_branch = side_branches_item->second.top();
            double      circle_branch_length = longest_node_branch.length +
                                          ((is_short_revers_direction) ?
                                               (circle.length -
                                                distance_on_circle) :
                                               distance_on_circle);
            if (longest_branch_length < circle_branch_length) {
                longest_branch_length       = circle_branch_length;
                is_longest_revers_direction = is_short_revers_direction;
                longest_circle_node         = circle_node;
                longest_circle_branch       = &longest_node_branch;
            }
        }
    }
    assert(longest_circle_node !=
           nullptr); // only circle with no side branches
    assert(longest_circle_branch != nullptr);
    // almost same - double preccission
    // distance_on_circle += get_neighbor_distance(circle.path.back(),
    // circle.path.front()); assert(distance_on_circle == circle.length);

    // circlePath
    auto circle_iterator = std::find(circle.nodes.begin(), circle.nodes.end(),
                                     longest_circle_node);
    VoronoiGraph::Nodes circle_path;
    if (is_longest_revers_direction) {
        circle_path = VoronoiGraph::Nodes(circle_iterator, circle.nodes.end());
        std::reverse(circle_path.begin(), circle_path.end());
    } else {
        if (longest_circle_node != circle.nodes.front())
            circle_path = VoronoiGraph::Nodes(circle.nodes.begin() + 1,
                                              circle_iterator + 1);
    }
    // append longest side branch
    circle_path.insert(circle_path.end(),
                       longest_circle_branch->nodes.begin(),
                       longest_circle_branch->nodes.end());
    return {circle_path, longest_branch_length};
}

VoronoiGraph::Path VoronoiGraphUtils::find_longest_path_on_circles(
    const VoronoiGraph::Node &  input_node,
    size_t                      finished_circle_index,
    const VoronoiGraph::ExPath &ex_path)
{
    const std::vector<VoronoiGraph::Circle> &circles = ex_path.circles;
    const auto &circle                = circles[finished_circle_index];
    auto        connected_circle_item = ex_path.connected_circle.find(
        finished_circle_index);
    // is only one circle
    if (connected_circle_item == ex_path.connected_circle.end()) {
        // find longest path over circle and store it into next_path
        return find_longest_path_on_circle(circle, ex_path.side_branches);
    }

    // multi circle
    // find longest path over circles
    const std::set<size_t> &connected_circles = connected_circle_item->second;

    // collect all circle ndoes
    std::set<const VoronoiGraph::Node *> nodes;
    nodes.insert(circle.nodes.begin(), circle.nodes.end());
    for (size_t circle_index : connected_circles) {
        const auto &circle = circles[circle_index];
        nodes.insert(circle.nodes.begin(), circle.nodes.end());
    }

    // nodes are path throw circles
    // length is sum path throw circles PLUS length of longest side_branch
    VoronoiGraph::Path longest_path;

    // wide search by shortest distance for path over circle's node
    // !! Do NOT use recursion, may cause stack overflow
    std::set<const VoronoiGraph::Node *> done; // all ready checked
    // on top is shortest path
    std::priority_queue<VoronoiGraph::Path, std::vector<VoronoiGraph::Path>,
                        VoronoiGraph::Path::OrderLengthFromShortest>
                       search_queue;
    VoronoiGraph::Path start_path({&input_node}, 0.);
    search_queue.emplace(start_path);
    while (!search_queue.empty()) {
        // shortest path from input_node
        VoronoiGraph::Path path(std::move(search_queue.top()));
        search_queue.pop();
        const VoronoiGraph::Node &node = *path.nodes.back();
        if (done.find(&node) != done.end()) { // already checked
            continue;
        }
        done.insert(&node);
        for (const VoronoiGraph::Node::Neighbor &neighbor : node.neighbors) {
            if (nodes.find(neighbor.node) == nodes.end())
                continue; // out of circles
            if (done.find(neighbor.node) != done.end()) continue;
            VoronoiGraph::Path neighbor_path = path; // make copy
            neighbor_path.append(neighbor.node, neighbor.length());
            search_queue.push(neighbor_path);

            auto branches_item = ex_path.side_branches.find(neighbor.node);
            // exist side from this neighbor node ?
            if (branches_item == ex_path.side_branches.end()) continue;
            const VoronoiGraph::Path &longest_branch = branches_item->second
                                                           .top();
            double length = longest_branch.length + neighbor_path.length;
            if (longest_path.length < length) {
                longest_path.length = length;
                longest_path.nodes  = neighbor_path.nodes; // copy path
            }
        }
    }

    // create result path
    assert(!longest_path.nodes.empty());
    longest_path.nodes.erase(longest_path.nodes.begin()); // remove input_node
    assert(!longest_path.nodes.empty());
    auto branches_item = ex_path.side_branches.find(longest_path.nodes.back());
    if (branches_item == ex_path.side_branches.end()) {
        // longest path ends on circle
        return longest_path;
    }
    const VoronoiGraph::Path &longest_branch = branches_item->second.top();
    longest_path.nodes.insert(longest_path.nodes.end(),
                              longest_branch.nodes.begin(),
                              longest_branch.nodes.end());
    return longest_path;
}

std::optional<VoronoiGraph::Circle> VoronoiGraphUtils::create_circle(
    const VoronoiGraph::Path &          path,
    const VoronoiGraph::Node::Neighbor &neighbor)
{
    VoronoiGraph::Nodes passed_nodes = path.nodes;
    // detection of circle
    // not neccesary to check last one in path
    auto        end_find  = passed_nodes.end() - 1;
    const auto &path_item = std::find(passed_nodes.begin(), end_find,
                                      neighbor.node);
    if (path_item == end_find) return {}; // circle not detected
    // separate Circle:
    VoronoiGraph::Nodes circle_path(path_item, passed_nodes.end());
    // !!! Real circle lenght is calculated on detection of end circle
    // now circle_length contain also lenght of path before circle
    double circle_length = path.length + neighbor.length();
    // solve of branch length will be at begin of cirlce
    return VoronoiGraph::Circle(std::move(circle_path), circle_length);
};

void VoronoiGraphUtils::merge_connected_circle(
    VoronoiGraph::ExPath::ConnectedCircles &dst,
    VoronoiGraph::ExPath::ConnectedCircles &src,
    size_t                                  dst_circle_count)
{
    std::set<size_t> done;
    for (const auto &item : src) {
        size_t dst_index = dst_circle_count + item.first;
        if (done.find(dst_index) != done.end()) continue;
        done.insert(dst_index);

        std::set<size_t> connected_circle;
        for (const size_t &src_index : item.second)
            connected_circle.insert(dst_circle_count + src_index);

        auto &dst_set = dst[dst_index];
        dst_set.merge(connected_circle);

        // write same information into connected circles
        connected_circle = dst_set; // copy
        connected_circle.insert(dst_index);
        for (size_t prev_connection_idx : dst_set) {
            done.insert(prev_connection_idx);
            for (size_t connected_circle_idx : connected_circle) {
                if (connected_circle_idx == prev_connection_idx) continue;
                dst[prev_connection_idx].insert(connected_circle_idx);
            }
        }
    }
}

void VoronoiGraphUtils::append_neighbor_branch(VoronoiGraph::ExPath &dst,
                                               VoronoiGraph::ExPath &src)
{
    // move side branches
    if (!src.side_branches.empty())
        dst.side_branches
            .insert(std::make_move_iterator(src.side_branches.begin()),
                    std::make_move_iterator(src.side_branches.end()));

    // move circles
    if (!src.circles.empty()) {
        // copy connected circles indexes
        if (!src.connected_circle.empty()) {
            merge_connected_circle(dst.connected_circle, src.connected_circle,
                                   dst.circles.size());
        }
        dst.circles.insert(dst.circles.end(),
                           std::make_move_iterator(src.circles.begin()),
                           std::make_move_iterator(src.circles.end()));
    }
}

void VoronoiGraphUtils::reshape_longest_path(VoronoiGraph::ExPath &path)
{
    assert(path.nodes.size() >= 1);

    double                    actual_length = 0.;
    const VoronoiGraph::Node *prev_node     = nullptr;
    VoronoiGraph::Nodes       origin_path   = path.nodes; // make copy
    // index to path
    size_t path_index = 0;
    for (const VoronoiGraph::Node *node : origin_path) {
        if (prev_node != nullptr) {
            ++path_index;
            actual_length += get_neighbor_distance(prev_node, node);
        }
        prev_node = node;
        // increase actual length

        auto side_branches_item = path.side_branches.find(node);
        if (side_branches_item == path.side_branches.end())
            continue; // no side branches
        VoronoiGraph::ExPath::SideBranches &branches = side_branches_item
                                                           ->second;
        if (actual_length >= branches.top().length)
            continue; // no longer branch

        auto               end_path = path.nodes.begin() + path_index;
        VoronoiGraph::Path side_branch({path.nodes.begin(), end_path},
                                       actual_length);
        std::reverse(side_branch.nodes.begin(), side_branch.nodes.end());
        VoronoiGraph::Path new_main_branch(std::move(branches.top()));
        branches.pop();
        std::reverse(new_main_branch.nodes.begin(),
                     new_main_branch.nodes.end());
        // add old main path store into side branches - may be it is not neccessary
        branches.push(std::move(side_branch));

        // swap side branch with main branch
        path.nodes.erase(path.nodes.begin(), end_path);
        path.nodes.insert(path.nodes.begin(), new_main_branch.nodes.begin(),
                          new_main_branch.nodes.end());

        path.length += new_main_branch.length;
        path.length -= actual_length;
        path_index    = new_main_branch.nodes.size();
        actual_length = new_main_branch.length;
    }
}

VoronoiGraph::ExPath VoronoiGraphUtils::create_longest_path(
    const VoronoiGraph::Node *start_node)
{
    VoronoiGraph::ExPath longest_path;
    CallStack            call_stack;
    call_stack.emplace(
        std::make_unique<EvaluateNeighbor>(longest_path, start_node));

    // depth search for longest path in graph
    while (!call_stack.empty()) {
        std::unique_ptr<IStackFunction> stack_function = std::move(
            call_stack.top());
        call_stack.pop();
        stack_function->process(call_stack);
        // stack function deleted
    }

    reshape_longest_path(longest_path);
    // after reshape it shoud be longest path for whole Voronoi Graph
    return longest_path;
}

const VoronoiGraph::Node::Neighbor *VoronoiGraphUtils::get_twin(const VoronoiGraph::Node::Neighbor& neighbor)
{
    auto twin_edge = neighbor.edge->twin();
    for (const VoronoiGraph::Node::Neighbor &twin_neighbor : neighbor.node->neighbors) {
        if (twin_neighbor.edge == twin_edge) return &twin_neighbor;
    }
    assert(false);
    return nullptr;
}

const VoronoiGraph::Node *VoronoiGraphUtils::get_twin_node(const VoronoiGraph::Node::Neighbor &neighbor)
{
    return get_twin(neighbor)->node;
}

bool VoronoiGraphUtils::is_opposit_direction(const VD::edge_type *edge, const Line &line)
{
    Point dir_line = LineUtils::direction(line);
    Point dir_edge = VoronoiGraphUtils::to_direction(edge);
    return !PointUtils::is_same_direction(dir_line, dir_edge);
}

Slic3r::Point VoronoiGraphUtils::create_edge_point(
    const VoronoiGraph::Position &position)
{
    return create_edge_point(position.neighbor->edge, position.ratio);
}
    
Slic3r::Point VoronoiGraphUtils::create_edge_point(const VD::edge_type *edge,
                                                   double               ratio)
{
    const VD::vertex_type *v0 = edge->vertex0();
    const VD::vertex_type *v1 = edge->vertex1();
    if (ratio <= std::numeric_limits<double>::epsilon())
        return Point(v0->x(), v0->y());
    if (ratio >= 1. - std::numeric_limits<double>::epsilon())
        return Point(v1->x(), v1->y());

    if (edge->is_linear()) {
        Point dir(v1->x() - v0->x(), v1->y() - v0->y());
        // normalize
        dir *= ratio;
        return Point(v0->x() + dir.x(), v0->y() + dir.y());
    }

    assert(edge->is_curved());
    // TODO: distance on curve

    // approx by line
    Point dir(v1->x() - v0->x(), v1->y() - v0->y());
    dir *= ratio;
    return Point(v0->x() + dir.x(), v0->y() + dir.y());
}

// NOTE: Heuristic is bad -> Width is not linear on edge e.g. VD of hexagon
// Solution: Edge has to know width changes.
VoronoiGraph::Position VoronoiGraphUtils::get_position_with_width(
    const VoronoiGraph::Node::Neighbor *neighbor, coord_t width, const Slic3r::Lines &lines)
{
    VoronoiGraph::Position result(neighbor, 0.);
    const VD::edge_type *edge = neighbor->edge;
    if (edge->is_curved()) { 
        // Every point on curve has same distance from outline
        // !!! NOT TRUE !!!
        // Only same distance from point and line !!!
        // TODO: Fix it
        return result; 
    }
    assert(edge->is_finite());
    Slic3r::Line edge_line(to_point(edge->vertex0()), to_point(edge->vertex1()));
    const Slic3r::Line &source_line = lines[edge->cell()->source_index()];
    if (LineUtils::is_parallel(edge_line, source_line)) {
        // Every point on parallel lines has same distance
        return result; 
    }

    double half_width = width / 2.;

    double a_dist = source_line.perp_distance_to(edge_line.a);
    double b_dist = source_line.perp_distance_to(edge_line.b);

    // check if half_width is in range from a_dist to b_dist
    if (a_dist > b_dist) { 
        if (b_dist >= half_width) {
            // vertex1 is closer to width
            result.ratio = 1.;
            return result;
        } else if (a_dist <= half_width) {
            // vertex0 is closer to width
            return result;
        }
    } else {
        // a_dist < b_dist
        if (a_dist >= half_width) { 
            // vertex0 is closer to width
            return result; 
        } else if (b_dist <= half_width) {
            // vertex1 is closer to width
            result.ratio = 1.;
            return result;
        }
    }
    result.ratio = fabs((a_dist - half_width) / (a_dist - b_dist));
    return result;
}

std::pair<Slic3r::Point, Slic3r::Point> VoronoiGraphUtils::point_on_lines(
    const VoronoiGraph::Position &position, const Lines &lines)
{
    const VD::edge_type *edge = position.neighbor->edge;

    // TODO: solve point on parabola
    //assert(edge->is_linear());

    Point edge_point = create_edge_point(position);
    auto point_on_line = [&](const VD::edge_type *edge) -> Point {
        assert(edge->is_finite());
        const VD::cell_type *cell = edge->cell();
        size_t line_index = cell->source_index();
        const Line &line = lines[line_index];
        using namespace boost::polygon;
        if (cell->source_category() == SOURCE_CATEGORY_SEGMENT_START_POINT) {
            return line.a;
        }
        if (cell->source_category() == SOURCE_CATEGORY_SEGMENT_END_POINT) {
            return line.b;
        }
        
        Point dir = LineUtils::direction(line);
        Line intersecting_line(edge_point, edge_point + PointUtils::perp(dir));
        std::optional<Vec2d> intersection = LineUtils::intersection(line, intersecting_line);
        assert(intersection.has_value());
        Point result = intersection->cast<coord_t>();
        // result MUST lay on the line, accuracy of float intersection could move point out of line
        coord_t tolerance = 5; // for sure it is 5 but found case which need tolerance(1) - SPE-2709
        if (abs(result.x() - line.a.x()) < tolerance && 
            abs(result.y() - line.a.y()) < tolerance)
            return line.a; // almost point a
        if (abs(result.x() - line.b.x()) < tolerance && 
            abs(result.y() - line.b.y()) < tolerance)
            return line.b; // almost point b
        return result;
    };
    
    return {point_on_line(edge), point_on_line(edge->twin())};
}

namespace{
using namespace Slic3r;
using VD = Slic3r::Geometry::VoronoiDiagram;
double get_distance_sq(const VD::edge_type &edge, const Point &point, double &edge_ratio) {
    // TODO: find closest point on curve edge
    // if (edge.is_linear()) {

    // get line foot point, inspired Geometry::foot_pt
    Vec2d v0 = VoronoiGraphUtils::to_point_d(edge.vertex0());
    Vec2d v = point.cast<double>() - v0;
    Vec2d v1 = VoronoiGraphUtils::to_point_d(edge.vertex1());
    Vec2d edge_dir = v1 - v0;
    double l2 = edge_dir.squaredNorm();
    edge_ratio = v.dot(edge_dir) / l2;
    // IMPROVE: not neccesary to calculate point if (edge_ratio > 1 || edge_ratio < 0)
    Point edge_point;
    if (edge_ratio > 1.)
        edge_point = v1.cast<coord_t>();
    else if (edge_ratio < 0.)
        edge_point = v0.cast<coord_t>();
    else { // foot point
        edge_point = (v0 + edge_dir * edge_ratio).cast<coord_t>();
    }
    return (point - edge_point).cast<double>().squaredNorm();
}
}

VoronoiGraph::Position VoronoiGraphUtils::align(
    const VoronoiGraph::Position &position, const Point &to, double max_distance)
{
    // for each neighbor in max distance try align edge
    struct NodeDistance
    {
        const VoronoiGraph::Node *node;
        double                    distance; // distance to search for closest point
        NodeDistance(const VoronoiGraph::Node *node, double distance)
            : node(node), distance(distance)
        {}
    };
    std::queue<NodeDistance> process;
    const VoronoiGraph::Node::Neighbor* neighbor = position.neighbor;
    double from_distance = neighbor->length() * position.ratio;
    if (from_distance < max_distance) {
        const VoronoiGraph::Node *from_node = VoronoiGraphUtils::get_twin_node(*neighbor);
        process.emplace(from_node, from_distance);
    }
    double to_distance = neighbor->length() * (1 - position.ratio);
    if (to_distance < max_distance) {
        const VoronoiGraph::Node *to_node = neighbor->node;
        process.emplace(to_node, to_distance);        
    }
    if (process.empty()) { 
        const VoronoiGraph::Node *node = (position.ratio < 0.5) ?
            VoronoiGraphUtils::get_twin_node(*neighbor) : neighbor->node;
        process.emplace(node, max_distance);
    }

    double closest_distance_sq = std::numeric_limits<double>::max();
    VoronoiGraph::Position closest;

    std::set<const VoronoiGraph::Node *> done;
    while (!process.empty()) { 
        NodeDistance nd = process.front(); // copy
        process.pop();
        if (done.find(nd.node) != done.end()) continue;
        done.insert(nd.node);
        for (const auto &neighbor : nd.node->neighbors) {
            if (done.find(neighbor.node) != done.end()) continue;
            double ratio;
            double distance_sq = get_distance_sq(*neighbor.edge, to, ratio);
            if (closest_distance_sq > distance_sq) { 
                closest_distance_sq = distance_sq;
                closest = VoronoiGraph::Position(&neighbor, ratio);
            }
            double from_start = nd.distance + neighbor.length();
            if (from_start < max_distance)
                process.emplace(neighbor.node, from_start);
        }
    }
    return closest;
}

const VoronoiGraph::Node *VoronoiGraphUtils::getFirstContourNode(
    const VoronoiGraph &graph)
{
    for (const auto &[key, value] : graph.data) {
        const VD::vertex_type & vertex   = *key;
        Voronoi::VertexCategory category = Voronoi::vertex_category(vertex);
        if (category == Voronoi::VertexCategory::OnContour) {
            return &value;
        }
    }
    return nullptr;
}

coord_t VoronoiGraphUtils::get_max_width(const VoronoiGraph::Nodes &path)
{
    coord_t max = 0;
    const VoronoiGraph::Node *prev_node = nullptr;
    for (const VoronoiGraph::Node *node : path) { 
        if (prev_node == nullptr) {
            prev_node = node;
            continue;
        }
        const VoronoiGraph::Node::Neighbor *neighbor = get_neighbor(prev_node, node);
        if (max < neighbor->max_width())  max = neighbor->max_width();        
        prev_node = node;
    }
    return max;
}

coord_t VoronoiGraphUtils::get_max_width(
    const VoronoiGraph::ExPath &longest_path)
{
    coord_t max = get_max_width(longest_path.nodes);
    for (const auto &side_branches_item : longest_path.side_branches) {
        const VoronoiGraph::Node *prev_node = side_branches_item.first;
        VoronoiGraph::ExPath::SideBranches side_branches = side_branches_item.second; // !!! copy
        while (!side_branches.empty()) {
            const VoronoiGraph::Path &side_path = side_branches.top();
            const VoronoiGraph::Node::Neighbor *first_neighbor =
                get_neighbor(prev_node, side_path.nodes.front());
            coord_t max_side_branch = std::max(
                get_max_width(side_path.nodes), first_neighbor->max_width());
            if (max < max_side_branch) max = max_side_branch;
            side_branches.pop();
        }
    }

    for (const VoronoiGraph::Circle &circle : longest_path.circles) {
        const VoronoiGraph::Node::Neighbor *first_neighbor =
            get_neighbor(circle.nodes.front(), circle.nodes.back());
        double max_circle = std::max(
            first_neighbor->max_width(), get_max_width(circle.nodes));
        if (max < max_circle) max = max_circle;
    }

    return max;
}

// !!! is slower than go along path
coord_t VoronoiGraphUtils::get_max_width(const VoronoiGraph::Node *node)
{
    coord_t max = 0;
    std::set<const VoronoiGraph::Node *> done;
    std::queue<const VoronoiGraph::Node *> process;
    process.push(node);
    while (!process.empty()) {
        const VoronoiGraph::Node *actual_node = process.front();
        process.pop();
        if (done.find(actual_node) != done.end()) continue;
        for (const VoronoiGraph::Node::Neighbor& neighbor: actual_node->neighbors) {
            if (done.find(neighbor.node) != done.end()) continue;
            process.push(neighbor.node);
            if (max < neighbor.max_width()) max = neighbor.max_width();
        }
        done.insert(actual_node);
    }
    return max;
}

bool VoronoiGraphUtils::ends_in_distanace(const VoronoiGraph::Position &position, coord_t max_distance) {
    const VoronoiGraph::Node *node = position.neighbor->node;
    coord_t rest_distance = max_distance - position.calc_rest_distance();
    if (rest_distance < 0)
        return false;

    // speed up - end of gpraph is no need investigate further
    if (node->neighbors.size() == 1)
        return true;

    // Already processed nodes
    std::set<const VoronoiGraph::Node *> done;
    done.insert(get_twin_node(*position.neighbor));

    struct Next{
        const VoronoiGraph::Node *node;
        coord_t rest_distance;
    };
    // sorted by distance from position from biggest
    std::vector<Next> process_queue;
    do {
        done.insert(node);
        for (const VoronoiGraph::Node::Neighbor &neighbor: node->neighbors){
            const VoronoiGraph::Node *neighbor_node = neighbor.node;
            // Check whether node is already done
            // Nodes are processed from closer to position 
            // soo done neighbor have to has bigger rest_distance
            if (done.find(neighbor_node) != done.end())
                // node is already explore
                continue;

            coord_t neighbor_rest = rest_distance - static_cast<coord_t>(neighbor.length());
            if (neighbor_rest < 0)
                // exist node far than max distance
                return false;

            // speed up - end of gpraph is no need to add to the process queue
            if (neighbor_node->neighbors.size() == 1)
                continue;

            // check whether exist in queue this node with farer path and fix it
            auto it = std::find_if(process_queue.begin(), process_queue.end(), 
                [neighbor_node](const Next &n) { return n.node == neighbor_node;});
            if (it == process_queue.end()){
                process_queue.emplace_back(Next{neighbor_node, neighbor_rest});
            } else if (it->rest_distance < neighbor_rest) {
                // found shorter path to node
                it->rest_distance = neighbor_rest;
            }
        }

        if (process_queue.empty())
            return true;

        // find biggest rest distance -> closest to input position
        auto next = std::max_element(process_queue.begin(), process_queue.end(), 
            [](const Next& n1, const Next& n2){
                return n1.rest_distance < n2.rest_distance;
            });
        rest_distance = next->rest_distance;
        node = next->node;
        process_queue.erase(next); // process queue pop
    } while (true);    
}

void VoronoiGraphUtils::for_neighbor_at_distance(
    const VoronoiGraph::Position &position,
    coord_t                       max_distance,
    std::function<void(const VoronoiGraph::Node::Neighbor &, coord_t)> fnc)
{
    coord_t                   act_distance = position.calc_distance();
    const VoronoiGraph::Node *act_node     = position.neighbor->node;
    const VoronoiGraph::Node *twin_node = get_twin_node(*position.neighbor);
    std::set<const VoronoiGraph::Node *> done;
    done.insert(twin_node);
    done.insert(act_node);
    std::queue<std::pair<const VoronoiGraph::Node *, coord_t>> process;
    coord_t distance = position.calc_rest_distance();
    if (distance < max_distance) process.push({twin_node, distance});

    while (true) {
        const VoronoiGraph::Node *next_node     = nullptr;
        coord_t                   next_distance = 0;
        for (const auto &neighbor : act_node->neighbors) {
            if (done.find(neighbor.node) != done.end())
                continue; // already checked
            done.insert(neighbor.node);

            fnc(neighbor, act_distance);

            coord_t length   = static_cast<coord_t>(neighbor.length());
            coord_t distance = act_distance + length;
            if (distance >= max_distance) continue;
            if (next_node == nullptr) {
                next_node     = neighbor.node;
                next_distance = distance;
            } else {
                process.push({neighbor.node, distance});
            }
        }
        if (next_node != nullptr) { // exist next node
            act_node     = next_node;
            act_distance = next_distance;
        } else if (!process.empty()) { // exist next process
            act_node     = process.front().first;
            act_distance = process.front().second;
            process.pop();
        } else { // no next node neither process
            break;
        }
    }
}

double VoronoiGraphUtils::outline_angle(const VoronoiGraph::Node::Neighbor &neighbor, const Lines& lines)
{
    assert(neighbor.edge->is_linear());
    assert(neighbor.min_width() == 0);
    const VD::cell_type *c1 = neighbor.edge->cell();
    const VD::cell_type *c2 = neighbor.edge->twin()->cell();

    const Line &l1 = lines[c1->source_index()];
    const Line &l2 = lines[c2->source_index()];

    Vec2d d1 = LineUtils::direction(l1).cast<double>();
    Vec2d d2 = LineUtils::direction(l2).cast<double>();

    double dot = d1.dot(-d2);
    return std::acos(dot/d1.norm() / d2.norm());
}

void VoronoiGraphUtils::draw(SVG &               svg,
                             const VoronoiGraph &graph,
                             const Lines &       lines,
                             const SampleConfig &config,
                             bool                pointer_caption)
{
    coord_t width = config.head_radius / 10;
    LineUtils::draw(svg, lines, "black", width, false);

    auto print_address = [&](const Point& p, const char* prefix, void * addr, const char* color){
        if (pointer_caption) {
            std::stringstream ss;
            ss << prefix << std::hex << reinterpret_cast<intptr_t>(addr);
            std::string s = ss.str();
            svg.draw_text(p, s.c_str(), color, 1);
        }
    };

    std::vector<const char *> skeleton_colors{
        "yellow", // thin (min+max belowe thin)
        "yellowgreen", // on way to thin (max is above thin)
        "limegreen", // between (inside histerezis)
        "forestgreen", // on way to thick (min is belove thick)
        "darkgreen", // thick (min+max above thick)
        "brown" // cross histereze
    };
    std::vector<std::string> color_description{
        "thin (min+max belowe thin " + std::to_string(config.thick_min_width) + ")",
        "on way to thin (max is above thin)",
        "between (inside histerezis)",
        "on way to thick (min is belove thick)",
        "thick (min+max above thick " + std::to_string(config.thin_max_width) + ")",
        "cross histereze"
    };
    Point legend_position = lines.front().a;
    for (const Line& l: lines){
        if (legend_position.x() < l.a.x())
            legend_position.x() = l.a.x();
        if (legend_position.y() < l.a.y())
            legend_position.y() = l.a.y();
    }
    for (size_t i = 0; i < 6; i++) {        
        Point p(legend_position.x(), legend_position.y() - static_cast<coord_t>(i * 1.3 / SCALING_FACTOR));
        std::string text = color_description[i] + " (" + skeleton_colors[i] + ")"; 
        svg.draw_text(p, text.c_str(), skeleton_colors[i], 8);
    }

    auto get_color = [&skeleton_colors, 
        min_limit = config.thick_min_width, 
        max_limit = config.thin_max_width]
    (const VoronoiGraph::Node::Neighbor &n) {
        assert(n.min_width() <= n.max_width());
        coord_t min_width = n.min_width();
        coord_t max_width = n.max_width();
        if (min_width >= max_limit){
            return skeleton_colors[4];
        } else if (max_width <= min_limit){
            return skeleton_colors[0];
        } else if (min_width >= min_limit &&
                   max_width <= max_limit){
            return skeleton_colors[2];
        } else if (min_width <= min_limit &&
                   max_width >= max_limit){
            return skeleton_colors[5];
        } else if (min_width <= min_limit){
            return skeleton_colors[1];
        } else if (max_width >= max_limit) {
            return skeleton_colors[3];
        }
        assert(false);
        return "gray";        
    };
        
    for (const auto &[key, value] : graph.data) {
        Point p(key->x(), key->y());
        svg.draw(p, "lightgray", width);
        print_address(p, "vertex ptr ",(void*)key, "lightgray");
        for (const auto &n : value.neighbors) {
            Point from = to_point(n.edge->vertex0());
            Point to   = to_point(n.edge->vertex1());
            bool  is_second = n.edge->vertex0() > n.edge->vertex1();
            Point center    = (from + to) / 2;
            Point p = center + ((is_second) ? Point(0., -2e5) : Point(0., 2e5));
            print_address(p, "neighbor ptr ", (void *) &n, "gray");
            if (is_second) continue;
            const char *color = get_color(n);
            if (pointer_caption) {
                std::string width_str = "width min=" + std::to_string(n.min_width()) +
                                        " max=" + std::to_string(n.max_width());
                svg.draw_text(center, width_str.c_str(), color, 3);
            }
            draw(svg, *n.edge, lines, color, 2*width);
        }
    }
}

void VoronoiGraphUtils::draw(SVG &                svg,
                             const VD::edge_type &edge,
                             const Lines &        lines,
                             const char *         color,
                             coord_t              width)
{
    Point from = to_point(edge.vertex0());
    Point to   = to_point(edge.vertex1());
    if (edge.is_curved()) { 
        Parabola p = get_parabola(edge, lines);
        ParabolaSegment ps(p, from, to);
        ParabolaUtils::draw(svg, ps, color, width);
        return;
    }
    svg.draw(Line(from, to), color, width);
}


void VoronoiGraphUtils::draw(SVG &                      svg,
                             const VoronoiGraph::Nodes &path,
                             coord_t                    width,
                             const char *               color,
                             bool                       finish,
                            bool caption)
{
    const VoronoiGraph::Node *prev_node = (finish) ? path.back() : nullptr;
    int                       index     = 0;
    for (auto &node : path) {
        ++index;
        if (prev_node == nullptr) {
            prev_node = node;
            continue;
        }

        Point from = to_point(prev_node->vertex);
        Point to   = to_point(node->vertex);
        svg.draw(Line(from, to), color, width);
        if (caption) {
            svg.draw_text(from, std::to_string(index - 1).c_str(), color, 6);
            svg.draw_text(to, std::to_string(index).c_str(), color, 6);
        }
        prev_node = node;
    }
}

void VoronoiGraphUtils::draw(SVG &                       svg,
                             const VoronoiGraph::ExPath &path,
                             coord_t                     width)
{
    const char *circlePathColor   = "green";
    const char *sideBranchesColor = "blue";
    const char *mainPathColor     = "red";

    for (auto &circle : path.circles) {
        draw(svg, circle.nodes, width, circlePathColor, true);
        Point center(0, 0);
        for (auto p : circle.nodes) {
            center += to_point(p->vertex);
        }
        center.x() /= circle.nodes.size();
        center.y() /= circle.nodes.size();

        svg.draw_text(center,
                      ("C" + std::to_string(&circle - &path.circles.front()))
                          .c_str(),
                      circlePathColor);
    }

    for (const auto &branches : path.side_branches) {
        auto tmp = branches.second; // copy
        while (!tmp.empty()) {
            const auto &branch = tmp.top();
            auto        path   = branch.nodes;
            path.insert(path.begin(), branches.first);
            draw(svg, path, width, sideBranchesColor);
            tmp.pop();
        }
    }

    draw(svg, path.nodes, width, mainPathColor);
}

void VoronoiGraphUtils::draw(const Polygon &polygon,
                             const Lines &  lines,
                             const Point &  center)
{
    SVG  svg("Bad_polygon.svg", {polygon.points});
    svg.draw(polygon, "orange");
    LineUtils::draw(svg, lines, "red", 0., true, true);
    svg.draw(center);
}