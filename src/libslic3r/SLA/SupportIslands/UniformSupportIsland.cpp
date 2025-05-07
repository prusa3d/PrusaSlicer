#include "UniformSupportIsland.hpp"

#include <cmath>
#include <optional>
#include <vector>
#include <cassert>
#include <memory>

#include <boost/log/trivial.hpp>

#include <libslic3r/ClipperUtils.hpp> // allign
#include <libslic3r/KDTreeIndirect.hpp> // closest point
#include <libslic3r/Geometry.hpp>
#include "libslic3r/Geometry/Voronoi.hpp"
#include <libslic3r/Geometry/VoronoiOffset.hpp>
#include <libslic3r/Geometry/VoronoiVisualUtils.hpp>
#include <libslic3r/Point.hpp>
#include <libslic3r/SVG.hpp>
#include <libslic3r/SLA/SupportPointGenerator.hpp>
#include <libslic3r/ExPolygonsIndex.hpp>
#include <libslic3r/IntersectionPoints.hpp>
#include <libslic3r/Exception.hpp>

#include "VoronoiGraph.hpp"
#include "Parabola.hpp"
#include "IStackFunction.hpp"
#include "EvaluateNeighbor.hpp"
#include "ParabolaUtils.hpp"
#include "VoronoiGraphUtils.hpp"
#include "VectorUtils.hpp"
#include "LineUtils.hpp"
#include "PointUtils.hpp"

#include "VoronoiDiagramCGAL.hpp" // aligning of points

// comment definition of NDEBUG to enable assert()
//#define NDEBUG
//#define SLA_SAMPLE_ISLAND_UTILS_STORE_FIELD_TO_SVG_PATH "C:/data/temp/fields/island_<<COUNTER>>.svg"
//#define SLA_SAMPLE_ISLAND_UTILS_STORE_PENINSULA_FIELD_TO_SVG_PATH "C:/data/temp/fields/peninsula_<<COUNTER>>.svg"
//#define SLA_SAMPLE_ISLAND_UTILS_STORE_ALIGNED_TO_SVG_PATH "C:/data/temp/align/island_<<COUNTER>>_aligned.svg"
//#define SLA_SAMPLE_ISLAND_UTILS_STORE_ALIGN_ONCE_TO_SVG_PATH "C:/data/temp/align_once/iter_<<COUNTER>>.svg"
//#define SLA_SAMPLE_ISLAND_UTILS_DEBUG_CELL_DISTANCE_PATH "C:/data/temp/island_cell.svg"
//#define SLA_SAMPLE_ISLAND_UTILS_DEBUG_PARTS_PATH "C:/data/temp/parts/part<<COUNTER>>.svg"

namespace {
using namespace Slic3r;
using namespace Slic3r::sla;

/// <summary>
/// Replace first occurence of string
/// TODO: Generalize and Move into string utils
/// </summary>
/// <param name="s"></param>
/// <param name="toReplace"></param>
/// <param name="replaceWith"></param>
/// <returns></returns>
std::string replace_first(
    std::string s,
    const std::string& toReplace,
    const std::string& replaceWith
) {
    std::size_t pos = s.find(toReplace);
    if (pos == std::string::npos) return s;
    s.replace(pos, toReplace.length(), replaceWith);
    return s;
}

/// <summary>
/// IMPROVE: use Slic3r::BoundingBox
/// Search for reference to an Expolygon with biggest contour
/// </summary>
/// <param name="expolygons">Input</param>
/// <returns>reference into expolygons</returns>
const ExPolygon &get_expolygon_with_biggest_contour(const ExPolygons &expolygons) {
    assert(!expolygons.empty());
    const ExPolygon *biggest = &expolygons.front();
    for (size_t index = 1; index < expolygons.size(); ++index) {
        const ExPolygon *current = &expolygons[index];
        if (biggest->contour.size() < current->contour.size())
            biggest = current;
    }
    return *biggest;
}

/// <summary>
/// When radius of all points is smaller than max radius set output center and return true
/// </summary>
/// <param name="points"></param>
/// <param name="max_radius"></param>
/// <param name="output_center"></param>
/// <returns>True when Bounding box of points is smaller than max radius</returns>
bool get_center(const Points &points, coord_t max_radius, Point& output_center){
    if (points.size()<=2)
        return false;
    auto it = points.begin();
    Point min = *it;
    Point max = *it;
    for (++it; it != points.end(); ++it) {
        if (min.x() > it->x()) {
            min.x() = it->x();
            if (max.x() - min.x() > max_radius)
                return false;
        } else if(max.x() < it->x()) {
            max.x() = it->x();
            if (max.x() - min.x() > max_radius)
                return false;
        }
        if (min.y() > it->y()) {
            min.y() = it->y();
            if (max.y() - min.y() > max_radius)
                return false;
        } else if (max.y() < it->y()) {
            max.y() = it->y();
            if (max.y() - min.y() > max_radius)
                return false;
        }
    }

    // prevent overflow of point range, no care about 1 size
    output_center = min/2 + max/2;
    return true;
}

/// <summary>
/// Decrease level of detail
/// </summary>
/// <param name="island">Polygon to reduce count of points</param>
/// <param name="config">Define progressivness of reduction</param>
/// <returns>Simplified island</returns>
ExPolygon get_simplified(const ExPolygon &island, const SampleConfig &config) {
    //// closing similar to FDM arachne do before voronoi inspiration in make_expolygons inside TriangleMeshSlicer
    //float closing_radius = scale_(0.0499f);
    //float offset_out = closing_radius;
    //float offset_in = -closing_radius;
    //ExPolygons closed_expolygons = offset2_ex({island}, offset_out, offset_in); // mitter
    //ExPolygon closed_expolygon = get_expolygon_with_biggest_contour(closed_expolygons);
    //// "Close" operation still create neighbor pixel for sharp triangle tip - cause VD issues

    ExPolygons simplified_expolygons = island.simplify(config.simplification_tolerance);
    if (simplified_expolygons.empty())
        return island;
        
    ExPolygon biggest = get_expolygon_with_biggest_contour(simplified_expolygons);

    // NOTE: Order of polygon is different for Windows and Linux 
    // to unify behavior one have to sort holes
    std::sort(biggest.holes.begin(), biggest.holes.end(), 
        // first sort by size of polygons than by coordinates of points
        [](const Polygon &polygon1, const Polygon &polygon2) {
            if (polygon1.size() > polygon2.size())
                return true;
            if (polygon1.size() < polygon2.size())
                return false;
            // NOTE: polygon1.size() == polygon2.size()
            for (size_t point_index = 0; point_index < polygon1.size(); ++point_index) {
                const Point &p1 = polygon1[point_index];
                const Point &p2 = polygon2[point_index];
                if (p1.x() > p2.x())
                    return true;
                if (p1.x() < p2.x())
                    return false;
                // NOTE: p1.x() == p2.x()
                if (p1.y() > p2.y())
                    return true;
                if (p1.y() < p2.y())
                    return false;
                // NOTE: p1 == p2 check next point
            }
            return true;
        });
        
    return biggest;
}

/// <summary>
/// Transform support point to slicer points
/// </summary>
Points to_points(const SupportIslandPoints &support_points){
    Points result;
    result.reserve(support_points.size());
    std::transform(support_points.begin(), support_points.end(), std::back_inserter(result), 
        [](const std::unique_ptr<SupportIslandPoint> &p) { return p->point; });
    return result;
}

#ifdef OPTION_TO_STORE_ISLAND
SVG draw_island(const std::string &path, const ExPolygon &island, const ExPolygon &simplified_island) {
    SVG svg(path, BoundingBox{island.contour.points});
    svg.draw_original(island);
    svg.draw(island, "lightgray");
    svg.draw(simplified_island, "gray");
    return svg;
}
SVG draw_island_graph(const std::string &path, const ExPolygon &island, 
    const ExPolygon &simplified_island, const VoronoiGraph& skeleton,
    const VoronoiGraph::ExPath& longest_path, const Lines& lines, const SampleConfig &config) {
    SVG svg = draw_island(path, island, simplified_island);
    VoronoiGraphUtils::draw(svg, skeleton, lines, config, true /*print Pointer address*/);
    coord_t width = config.head_radius / 10;
    VoronoiGraphUtils::draw(svg, longest_path.nodes, width, "orange");
    return svg;
}
#endif // OPTION_TO_STORE_ISLAND

/// <summary>
/// keep same distances between support points
/// call once align
/// </summary>
/// <param name="samples">In/Out support points to be alligned(min 3 points)</param>
/// <param name="island">Area for sampling, border for position of samples</param>
/// <param name="config"> Sampling configuration
/// Maximal distance between neighbor points +
/// Term criteria for align: Minimal sample move and Maximal count of iteration</param>
void align_samples(SupportIslandPoints &samples, const ExPolygon &island, const SampleConfig &config);

void draw(SVG &svg, const SupportIslandPoints &supportIslandPoints, coord_t radius, bool write_type = true);

/// <summary>
/// Create unique static support point
/// </summary>
/// <param name="position">Define position on VD</param>
/// <param name="type">Type of support point</param>
/// <returns>new created support point</returns>
SupportIslandPointPtr create_no_move_point(
    const VoronoiGraph::Position &position,
    SupportIslandPoint::Type      type)
{
    Point point = VoronoiGraphUtils::create_edge_point(position);
    return std::make_unique<SupportIslandNoMovePoint>(point, type);
}

/// <summary>
/// Find point lay on path with distance from first point on path
/// </summary>
/// <param name="path">Neighbor connected Nodes</param>
/// <param name="distance">Distance to final point</param>
/// <returns>Position on VG with distance to first node when exists.
/// When distance is out of path return null optional</returns>
std::optional<VoronoiGraph::Position> create_position_on_path(
    const VoronoiGraph::Nodes &path,
    double                     distance)
{
    const VoronoiGraph::Node *prev_node       = nullptr;
    double                    actual_distance = 0.;
    for (const VoronoiGraph::Node *node : path) {
        if (prev_node == nullptr) { // first call
            prev_node = node;
            continue;
        }
        const VoronoiGraph::Node::Neighbor *neighbor =
            VoronoiGraphUtils::get_neighbor(prev_node, node);
        actual_distance += neighbor->length();
        if (actual_distance >= distance) {
            // over half point is on
            double behind_position = actual_distance - distance;
            double ratio           = 1. - behind_position / neighbor->length();
            return VoronoiGraph::Position(neighbor, ratio);
        }
        prev_node = node;
    }

    // distance must be inside path
    // this means bad input params
    assert(false);
    return {}; // unreachable
}

/// <summary>
/// Find first point lay on sequence of node
/// where widht are equal second params OR
/// distance from first node is exactly max distance
/// Depends which occure first
/// </summary>
/// <param name="path">Sequence of nodes, should be longer than max distance</param>
/// <param name="lines">Source lines for VG --> params for parabola.</param>
/// <param name="width">Width of island(2x distance to outline)</param>
/// <param name="max_distance">Maximal distance from first node on path.
/// At end is set to actual distance from first node.</param>
/// <returns>Position when exists</returns>
std::optional<VoronoiGraph::Position> create_position_on_path(
    const VoronoiGraph::Nodes &path, const Lines &lines, coord_t width, coord_t &max_distance)
{
    const VoronoiGraph::Node *prev_node = nullptr;
    coord_t  actual_distance = 0;
    for (const VoronoiGraph::Node *node : path) {
        if (prev_node == nullptr) { // first call
            prev_node = node;
            continue;
        }
        const VoronoiGraph::Node::Neighbor *neighbor =
            VoronoiGraphUtils::get_neighbor(prev_node, node);

        if (width <= neighbor->max_width()) {
            VoronoiGraph::Position position = VoronoiGraphUtils::get_position_with_width(neighbor, width, lines);
            // set max distance to actual distance
            coord_t rest_distance = position.calc_distance();
            coord_t distance      = actual_distance + rest_distance;
            if (max_distance > distance) {
                max_distance = distance;
                return position;
            }
        }

        actual_distance += static_cast<coord_t>(neighbor->length());
        if (actual_distance >= max_distance) {
            // over half point is on
            coord_t behind_position = actual_distance - max_distance;
            double ratio = 1. - behind_position / neighbor->length();
            return VoronoiGraph::Position(neighbor, ratio);
        }
        prev_node = node;
    }

    // distance must be inside path
    // this means bad input params
    assert(false);
    return {}; // unreachable
}

/// <summary>
/// Find point lay in center of path
/// Distance from this point to front of path
/// is same as distance to back of path
/// </summary>
/// <param name="path">Queue of neighbor nodes.(must be neighbor)</param>
/// <param name="type">Type of result island point</param>
/// <returns>Point laying on voronoi diagram</returns>
SupportIslandPointPtr create_middle_path_point(
    const VoronoiGraph::Path &path, SupportIslandPoint::Type type)
{
    auto position_opt = create_position_on_path(path.nodes, path.length / 2);
    if (!position_opt.has_value()) return nullptr;
    return create_no_move_point(*position_opt, type);
}

#ifndef NDEBUG
bool is_points_in_distance(const Point & p,
                           const Points &points,
                           double                max_distance)
{
    return std::all_of(points.begin(), points.end(), 
        [p, max_distance](const Point &point) {
        double d = (p - point).cast<double>().norm();
        return d <= max_distance;
    });
}
#endif // NDEBUG

void move_duplicit_positions(SupportIslandPoints &supports, const Points &prev_position) {
    // remove duplicit points when exist
    Points aligned = to_points(supports);
    std::vector<size_t> sorted(aligned.size());
    std::iota(sorted.begin(), sorted.end(), 0);
    auto cmp_index = [&aligned](size_t a_index, size_t b_index) {
        // sort by x and than by y
        const Point &a = aligned[a_index];
        const Point &b = aligned[b_index];
        return a.x() < b.x() || (a.x() == b.x() && a.y() < b.y());
    };
    std::sort(sorted.begin(), sorted.end(), cmp_index);

    auto get_duplicit_index = [](const std::vector<size_t> &sorted, const Points& aligned) {
        const Point *prev_p = &aligned[sorted.front()];
        for (size_t i = 1; i < sorted.size(); ++i){
            if (const Point &p = aligned[sorted[i]]; *prev_p == p) {
                return sorted[i];
            } else {
                prev_p = &p;
            }
        }
        return sorted.size();
    };

    do {
        size_t duplicit_index = get_duplicit_index(sorted, aligned);
        if (duplicit_index >= sorted.size())
            return; // without duplicit points

        // divide last move to half
        Point new_pos = prev_position[duplicit_index] / 2 + aligned[duplicit_index] / 2;
        coord_t move_distance = supports[duplicit_index]->move(new_pos);
        assert(move_distance > 0); // It must move
        aligned[duplicit_index] = supports[duplicit_index]->point; // update aligned position
        // IMPROVE: Resort duplicit index use std::rotate 
        std::sort(sorted.begin(), sorted.end(), cmp_index);
    } while (true); // end when no duplicit index 
}

/// <summary>
/// once align
/// </summary>
/// <param name="supports">In/Out support points to be alligned(min 3 points)</param>
/// <param name="island">Area for sampling, border for position of samples</param>
/// <param name="config"> Sampling configuration
/// Maximal distance between neighbor points +
/// Term criteria for align: Minimal sample move and Maximal count of iteration</param>
/// <returns>Maximal distance of move during aligning.</returns>
coord_t align_once(
    SupportIslandPoints &supports, 
    const ExPolygon &island, 
    const SampleConfig &config) 
{  
    // IMPROVE: Do not calculate voronoi diagram out of island(only triangulate island)
    // https://stackoverflow.com/questions/23823345/how-to-construct-a-voronoi-diagram-inside-a-polygon 
    // IMPROVE1: add accessor to point coordinate do not copy points
    // IMPROVE2: add filter for create cell polygon only for moveable samples
    Points points = to_points(supports);
    coord_t max_distance = std::max(std::max(
        config.thin_max_distance, 
        config.thick_inner_max_distance),
        config.thick_outline_max_distance); 
    Polygons cell_polygons = create_voronoi_cells_cgal(points, max_distance);
    
#ifdef SLA_SAMPLE_ISLAND_UTILS_STORE_ALIGN_ONCE_TO_SVG_PATH
    std::string color_of_island = "#FF8080";             // LightRed. Should not be visible - cell color should overlap
    std::string color_point_cell = "lightgray";          // bigger than island but NOT self overlap
    std::string color_island_cell_intersection = "gray"; // Should full overlap island !!
    std::string color_old_point = "lightblue";           // Center of island cell intersection
    std::string color_wanted_point = "darkblue";         // Center of island cell intersection
    std::string color_new_point = "blue";                // Center of island cell intersection
    std::string color_static_point = "black";
    BoundingBox bbox(island.contour.points);
    static int counter = 0;
    Slic3r::SVG svg(replace_first(SLA_SAMPLE_ISLAND_UTILS_STORE_ALIGN_ONCE_TO_SVG_PATH, 
        "<<COUNTER>>", std::to_string(counter++)).c_str(), bbox);
    svg.draw(island, color_of_island);
#endif // SLA_SAMPLE_ISLAND_UTILS_STORE_ALIGN_ONCE_TO_SVG_PATH

    // Maximal move during align each loop of align it should decrease
    coord_t max_move = 0;
    for (size_t i = 0; i < supports.size(); i++) {
        const Polygon &cell_polygon = cell_polygons[i];
        SupportIslandPointPtr &support = supports[i];

#ifdef SLA_SAMPLE_ISLAND_UTILS_STORE_ALIGN_ONCE_TO_SVG_PATH
        if (!support->can_move()) { // draww freezed support points
            svg.draw(support->point, color_static_point, config.head_radius);
            svg.draw_text(support->point + Point(config.head_radius, 0),
                SupportIslandPoint::to_string(support->type).c_str(), color_static_point.c_str());
        }
#endif // SLA_SAMPLE_ISLAND_UTILS_STORE_ALIGN_ONCE_TO_SVG_PATH
        if (!support->can_move())
            continue;
        
        // polygon must be at least triangle
        assert(cell_polygon.points.size() >= 3);
        if (cell_polygon.points.size() < 3)
            continue; // do not align point with invalid cell

        // IMPROVE: add intersection polygon with expolygon
        Polygons intersections = intersection(cell_polygon, island);
        const Polygon *island_cell = nullptr;
        if (intersections.size() == 1) {
            island_cell = &intersections.front();
            // intersection island and cell made by suppot point
            // must generate polygon containing initial source for voronoi cell
            // otherwise it is invalid voronoi diagram
            assert(island_cell->contains(support->point));
        } else {
            for (const Polygon &intersection : intersections) {
                if (intersection.contains(support->point)) {
                    island_cell = &intersection;
                    break;
                }
            }
            // intersection island and cell made by suppot point 
            // must generate polygon containing initial source for voronoi cell
            // otherwise it is invalid voronoi diagram
            assert(island_cell != nullptr);
            if (island_cell == nullptr)
                continue;
        }

        // new aligned position for sample
        Point island_cell_center = island_cell->centroid();

#ifdef SLA_SAMPLE_ISLAND_UTILS_DEBUG_CELL_DISTANCE_PATH
        {SVG cell_svg(SLA_SAMPLE_ISLAND_UTILS_DEBUG_CELL_DISTANCE_PATH, island_cell->points);
        cell_svg.draw(island, "lightgreen");
        cell_svg.draw(cell_polygon, "lightgray");
        cell_svg.draw(points, "darkgray", config.head_radius);
        cell_svg.draw(*island_cell, "gray");
        cell_svg.draw(sample->point, "green", config.head_radius);
        cell_svg.draw(island_cell_center, "black", config.head_radius);}
#endif //SLA_SAMPLE_ISLAND_UTILS_DEBUG_CELL_DISTANCE_PATH
        // Check that still points do not have bigger distance from each other
        assert(is_points_in_distance(island_cell_center, island_cell->points, 
            std::max(std::max(config.thick_inner_max_distance, config.thick_outline_max_distance), config.thin_max_distance)));

#ifdef SLA_SAMPLE_ISLAND_UTILS_STORE_ALIGN_ONCE_TO_SVG_PATH
        svg.draw(cell_polygon, color_point_cell);
        svg.draw(*island_cell, color_island_cell_intersection);
        svg.draw(Line(support->point, island_cell_center), color_wanted_point, config.head_radius / 5);
        svg.draw(support->point, color_old_point, config.head_radius);
        svg.draw(island_cell_center, color_wanted_point, config.head_radius); // wanted position
#endif // SLA_SAMPLE_ISLAND_UTILS_STORE_ALIGN_ONCE_TO_SVG_PATH

        // say samples to use its restriction to change posion close to center
        coord_t act_move = support->move(island_cell_center);
        if (max_move < act_move)
            max_move = act_move;  

#ifdef SLA_SAMPLE_ISLAND_UTILS_STORE_ALIGN_ONCE_TO_SVG_PATH
        svg.draw(support->point, color_new_point, config.head_radius);
        svg.draw_text(support->point + Point(config.head_radius, 0),
            SupportIslandPoint::to_string(support->type).c_str(), color_new_point.c_str()
        );
#endif // SLA_SAMPLE_ISLAND_UTILS_STORE_ALIGN_ONCE_TO_SVG_PATH
    }

    move_duplicit_positions(supports, points);
    return max_move;
}

void align_samples(SupportIslandPoints &samples, const ExPolygon &island, const SampleConfig & config)
{
    if (samples.size() == 1)
        return; // Do not align one support

    // Can't create voronoi for duplicit points
    // Fix previous algo to not produce duplicit points
    assert(!has_duplicate_points(to_points(samples)));

    bool exist_moveable = false;
    for (const auto &sample : samples) {
        if (sample->can_move()) {
            exist_moveable = true;
            break;
        }
    }
    if (!exist_moveable) 
        return; // no support to align

    size_t count_iteration = config.count_iteration; // copy
    coord_t max_move        = 0;
    while (--count_iteration > 1) {
        max_move = align_once(samples, island, config);        
        if (max_move < config.minimal_move) break;
    }

#ifdef SLA_SAMPLE_ISLAND_UTILS_STORE_ALIGNED_TO_SVG_PATH
    static int  counter = 0;
    SVG svg(replace_first(SLA_SAMPLE_ISLAND_UTILS_STORE_ALIGNED_TO_SVG_PATH, 
        "<<COUNTER>>", std::to_string(counter++)).c_str(),BoundingBox(island.contour.points));
    svg.draw(island);
    draw(svg, samples, config.head_radius);
    svg.Close();
    std::cout << "Align use " << config.count_iteration - count_iteration
            << " iteration and finish with precision " << unscale(max_move,0)[0] <<
            " mm" << std::endl;
#endif // SLA_SAMPLE_ISLAND_UTILS_STORE_ALIGNED_TO_SVG_PATH
    
}

void align_samples_with_permanent(
    SupportIslandPoints &samples, const ExPolygon &island, const Points& permanent, const SampleConfig &config)
{
    assert(!permanent.empty());
    if (permanent.empty())
        return align_samples(samples, island, config);
    
    // detect whether add adding support points 
    size_t tolerance = 1 + size_t(permanent.size() * 0.1); // 1 + 10% of permanent points
    bool extend_permanent = samples.size() > (permanent.size() + tolerance);
    if (!extend_permanent) // use only permanent support points
        return samples.clear();

    // find closest samples to permanent support points
    Points points;
    points.reserve(samples.size());
    for (const SupportIslandPointPtr &p : samples)
        points.push_back(p->point);
    auto point_accessor = [&points](size_t idx, size_t dim) -> coord_t & {
        return points[idx][dim]; };
    KDTreeIndirect<2, coord_t, decltype(point_accessor)> tree(point_accessor, samples.size());
    for (size_t i = 0; i < permanent.size(); ++i) {
        std::array<size_t, 5> closests = find_closest_points<5>(tree, permanent[i]);        
        bool found_closest = false;
        for (size_t idx : closests) {
            if (idx >= samples.size())
                continue; // closest function return also size_t::max()
            SupportIslandPointPtr &sample = samples[idx];
            if (sample->type == SupportIslandPoint::Type::permanent)
                continue; // already used
            sample->type = SupportIslandPoint::Type::permanent;
            found_closest = true;
            break;
        }
        if (!found_closest) { // backup solution when closest 5 fails, took first non permanent
            for (const auto &sample : samples)
                if (sample->type != SupportIslandPoint::Type::permanent) {
                    sample->type = SupportIslandPoint::Type::permanent;
                    break;
                }
        }
    }

    // remove samples marked as permanent
    samples.erase(std::remove_if(samples.begin(), samples.end(), [](const SupportIslandPointPtr &sample) {
        return sample->type == SupportIslandPoint::Type::permanent; }), samples.end());

    // add permanent into samples
    for (const Point&p: permanent)
        samples.push_back(
            std::make_unique<SupportIslandNoMovePoint>(p, SupportIslandPoint::Type::permanent));
    
    align_samples(samples, island, config);

    // remove permanent samples inserted for aligning
    samples.erase(std::remove_if(samples.begin(), samples.end(), [](const SupportIslandPointPtr &sample) {
        return sample->type == SupportIslandPoint::Type::permanent; }), samples.end());
}

/// <summary>
/// Separation of thin and thick part of island
/// </summary>
    
using VD = Slic3r::Geometry::VoronoiDiagram;
using Position = VoronoiGraph::Position;
using Positions = std::vector<Position>;
using Neighbor = VoronoiGraph::Node::Neighbor;

/// <summary>
/// Define narrow part of island along voronoi skeleton
/// </summary>
struct ThinPart
{
    // Ceneter of longest path inside island part
    // longest path is choosen from:
    //      shortest connection path between ends
    //   OR farest path to node from end
    //   OR farest path between nodes(only when ends are empty)
    Position center;

    // Transition from tiny to thick part
    // sorted by address of neighbor
    Positions ends;
};
using ThinParts = std::vector<ThinPart>;

/// <summary>
/// Define wide(fat) part of island along voronoi skeleton
/// </summary>
struct ThickPart
{
    // neighbor from thick part (twin of end with smallest source line index)
    // edge from thin to thick, start.node is inside of thick part
    const Neighbor* start;

    // Transition from thick to thin part
    // sorted by address of neighbor
    Positions ends;
};
using ThickParts = std::vector<ThickPart>;

/// <summary>
/// Generate support points for thin part of island
/// </summary>
/// <param name="part">One thin part of island</param>
/// <param name="results">[OUTPUT]Set of support points</param>
/// <param name="config">Define density of support points</param>
void create_supports_for_thin_part(
    const ThinPart &part, SupportIslandPoints &results, const SampleConfig &config
) {
    struct SupportIn
    {
        // want to create support in
        coord_t support_in; // [nano meters]
        // Neighbor to continue is not sampled yet
        const Neighbor *neighbor;
    };
    using SupportIns = std::vector<SupportIn>;

    coord_t support_distance = config.thin_max_distance;
    coord_t half_support_distance = support_distance / 2;

    // Current neighbor
    SupportIn curr{half_support_distance + part.center.calc_distance(), part.center.neighbor};
    const Neighbor *twin_start = VoronoiGraphUtils::get_twin(*curr.neighbor);
    coord_t twin_support_in = static_cast<coord_t>(twin_start->length()) - curr.support_in +
        support_distance;

    // Process queue
    SupportIns process;
    process.push_back(SupportIn{twin_support_in, twin_start});
    bool is_first_neighbor = true; // help to skip checking first neighbor exist in process

    // Loop over thin part of island to create support points on the voronoi skeleton.
    while (curr.neighbor != nullptr || !process.empty()) {
        if (curr.neighbor == nullptr) { // need to pop next one from process
            curr = process.back();      // copy
            process.pop_back();
        }

        auto part_end_it = std::lower_bound(part.ends.begin(), part.ends.end(), curr.neighbor,
            [](const Position &end, const Neighbor *n) { return end.neighbor < n; });
        bool is_end_neighbor = part_end_it != part.ends.end() &&
            curr.neighbor == part_end_it->neighbor;

        // add support on current neighbor
        coord_t edge_length = (is_end_neighbor) ? part_end_it->calc_distance() :
                                                  static_cast<coord_t>(curr.neighbor->length());
        while (edge_length >= curr.support_in) {
            double ratio = curr.support_in / curr.neighbor->length();
            VoronoiGraph::Position position(curr.neighbor, ratio);
            results.push_back(std::make_unique<SupportCenterIslandPoint>(
                position, &config, SupportIslandPoint::Type::thin_part_change));
            curr.support_in += support_distance;
        }
        curr.support_in -= edge_length;

        if (is_end_neighbor) {
            // on the current neighbor lay part end(transition into neighbor Thick part)
            if (curr.support_in < half_support_distance)
                results.push_back(std::make_unique<SupportCenterIslandPoint>(
                    *part_end_it, &config, SupportIslandPoint::Type::thin_part));
            curr.neighbor = nullptr;
            continue;
        }

        // Voronoi has zero width only on contour of island
        // IMPROVE: Add supports for edges, but not for
        //   * sharp corner
        //   * already near supported (How to decide which one to support?)
        // if (curr.neighbor->min_width() == 0) create_edge_support();
        // OLD function name was create_sample_center_end()

        // detect loop on island part
        const Neighbor *twin = VoronoiGraphUtils::get_twin(*curr.neighbor);
        if (!is_first_neighbor) { // not first neighbor
            if (auto process_it = std::find_if(process.begin(), process.end(),
                [twin](const SupportIn &p) { return p.neighbor == twin; });
                process_it != process.end()) { // self loop detected
                if (curr.support_in < half_support_distance) {
                    Position position{curr.neighbor, 1.}; // fine tune position by alignment
                    results.push_back(std::make_unique<SupportCenterIslandPoint>(
                        position, &config, SupportIslandPoint::Type::thin_part_loop));
                }
                process.erase(process_it);
                curr.neighbor = nullptr;
                continue;
            }
        } else {
            is_first_neighbor = false;
        }

        // next neighbor is short cut to not push back and pop new_starts
        const Neighbor *next_neighbor = nullptr;
        for (const Neighbor &node_neighbor : curr.neighbor->node->neighbors) {
            // Check wheather node is not previous one
            if (twin == &node_neighbor)
                continue;
            if (next_neighbor == nullptr) {
                next_neighbor = &node_neighbor;
                continue;
            }
            process.push_back(SupportIn{curr.support_in, &node_neighbor});
        }
        // NOTE: next_neighbor is null when no next neighbor
        curr.neighbor = next_neighbor;
    }
}

// Data type object represents one island change from wide to tiny part
// It is stored inside map under source line index
// Help to create field from thick part of island
struct WideTinyChange{
    // new coordinate for line.b point
    Point new_b;
    // new coordinate for next line.a point
    Point next_new_a;
    // index to lines
    size_t next_line_index;

    WideTinyChange(Point new_b, Point next_new_a, size_t next_line_index)
        : new_b(new_b)
        , next_new_a(next_new_a)
        , next_line_index(next_line_index)
    {}

    // is used only when multi wide tiny change are on same Line
    struct SortFromAToB
    {
        LineUtils::SortFromAToB compare;
        SortFromAToB(const Line &line) : compare(line) {}            
        bool operator()(const WideTinyChange &left,
                        const WideTinyChange &right)
        {
            return compare.compare(left.new_b, right.new_b);
        }
    };
};
using WideTinyChanges = std::vector<WideTinyChange>;

/// <summary>
/// Collect all source line indices from Voronoi Graph part
/// </summary>
/// <param name="input">input.node lay inside of part</param>
/// <param name="ends">Limits of part, should be accesibly only from one side</param>
/// <returns>Source line indices of island part</returns>
std::vector<size_t> get_line_indices(const Neighbor* input, const Positions& ends) {
    std::vector<size_t> indices;
    // Process queue
    std::vector<const Neighbor *> process;
    const Neighbor *current = input;
    // Loop over thin part of island to create support points on the voronoi skeleton.
    while (current != nullptr || !process.empty()) {
        if (current == nullptr) {       // need to pop next one from process
            current = process.back();   // copy
            process.pop_back();
        }
        
        const VD::edge_type *edge = current->edge;
        indices.push_back(edge->cell()->source_index());
        indices.push_back(edge->twin()->cell()->source_index());

        // Is current neighbor one of ends?
        if(auto end_it = std::lower_bound(ends.begin(), ends.end(), current,
            [](const Position &end, const Neighbor *n) { return end.neighbor < n; });            
            end_it != ends.end() && current == end_it->neighbor){
            current = nullptr;
            continue;
        }

        // Exist current neighbor in process queue
        const Neighbor *twin = VoronoiGraphUtils::get_twin(*current);        
        if (auto process_it = std::find_if(process.begin(), process.end(), 
            [&twin](const Neighbor *n) { return n == twin; });            
            process_it != process.end()) {
            process.erase(process_it);
            current = nullptr;
            continue;
        }

        // search for next neighbor
        const std::vector<Neighbor> &node_neighbors = current->node->neighbors;
        current = nullptr; 
        for (const Neighbor &node_neighbor : node_neighbors) {
            // Check wheather node is not previous one
            if (twin == &node_neighbor) continue;
            if (current == nullptr) {
                current = &node_neighbor;
                continue;
            }
            process.push_back(&node_neighbor);
        }
    }
    return indices;
}

/// <summary>
/// Fix expolygon with hole bigger than contour
/// NOTE: when change contour and index it is neccesary also fix source indices
/// </summary>
/// <param name="shape">[In/Out] expolygon</param>
/// <param name="ids">[OUT] source indices of island contour line creating field</param>
/// <returns>True when contour is changed</returns>
bool set_biggest_hole_as_contour(ExPolygon &shape, std::vector<size_t> &ids) {
    Point contour_size = BoundingBox(shape.contour.points).size();
    Polygons &holes = shape.holes;
    size_t contour_index = holes.size();
    for (size_t hole_index = 0; hole_index < holes.size(); ++hole_index) {
        Point hole_size = BoundingBox(holes[hole_index].points).size();
        if (hole_size.x() < contour_size.x()) // X size should be enough
            continue;                         // size is smaller it is really hole
        contour_size = hole_size;
        contour_index = hole_index;
    }
    if (contour_index == holes.size())
        return false; // contour is set correctly

    // some hole is bigger than contour and become contour

    // swap source indices
    size_t contour_count = shape.contour.size();
    size_t hole_index_offset = contour_count;
    for (size_t i = 0; i < contour_index; i++)
        hole_index_offset += shape.holes[i].size();
    size_t hole_index_end = hole_index_offset + shape.holes[contour_index].size();    

    // swap contour with hole
    Polygon tmp = holes[contour_index]; // copy
    std::swap(tmp, shape.contour);
    holes[contour_index] = std::move(tmp);

    // Temp copy of the old hole(newly contour) indices
    std::vector<size_t> contour_indices(ids.begin() + hole_index_offset, 
                                        ids.begin() + hole_index_end); // copy
    ids.erase(ids.begin() + hole_index_offset, // remove copied contour
              ids.begin() + hole_index_end);    
    ids.insert(ids.begin() + hole_index_offset, // insert old contour(newly hole) 
               ids.begin(), ids.begin() + contour_count);
    ids.erase(ids.begin(), ids.begin() + contour_count); // remove old contour
    ids.insert(ids.begin(), contour_indices.begin(), contour_indices.end());
    return true;
}

/// <summary>
/// DTO represents Wide parts of island to sample
/// extend polygon with information about source lines
/// </summary>
struct Field {
    // inner part of field, offseted border(island outline) by minimal_distance_from_outline
    ExPolygons inner;

    // Flag for each line from inner, whether this line needs to be supported
    // Converted from needs of border lines
    // same size as to_lines(inner).size()
    std::vector<bool> is_inner_outline;
};

/// <summary>
/// Create field
/// Offset island shape to inner part and transfer is_outline flags onto inner lines
/// </summary>
/// <param name="island">source field</param>
/// <param name="offset_delta">distance from outline</param>
/// <param name="is_outline">When True than island line should be supported
/// So this information must be propagated to inner line
/// NOTE: same size as to_lines(island).size()</param>
/// <returns>Field</returns>
Field create_field(const Slic3r::ExPolygon &island, float offset_delta, const std::vector<bool>& is_outline)
{
    ExPolygons inner = offset_ex(island, -offset_delta, ClipperLib::jtSquare);
    if (inner.empty()) return {}; // no place for support point

    // TODO: Connect indexes for convert during creation of offset
    // !! this implementation was fast for develop BUT NOT for running !!
    // Use offset with Z coordinate and then connect by Z coordinate
    const double angle_tolerace = 1e-4;
    const double distance_tolerance = 20.;
    Lines island_lines = to_lines(island);
    Lines inner_lines = to_lines(inner);
    size_t inner_line_index = 0; // continue where prev seach stop
    // Convert index map from island index to inner index
    size_t invalid_conversion = island_lines.size();
    std::vector<size_t> inner_2_island(inner_lines.size(), invalid_conversion);
    for (size_t island_line_index = 0; island_line_index < island_lines.size(); ++island_line_index) {
        const Line &island_line = island_lines[island_line_index];
        Vec2d dir1 = LineUtils::direction(island_line).cast<double>();
        dir1.normalize();
        size_t majorit_axis = (fabs(dir1.x()) > fabs(dir1.y())) ? 0 : 1;
        coord_t start1 = island_line.a[majorit_axis];
        coord_t end1 = island_line.b[majorit_axis];
        if (start1 > end1) std::swap(start1, end1);

        size_t stop_inner_index = inner_line_index;
        do {
            ++inner_line_index;
            if (inner_line_index == inner_lines.size())
                inner_line_index = 0;            
            const Line &inner_line = inner_lines[inner_line_index];

            // check that line overlap its interval
            coord_t start2 = inner_line.a[majorit_axis];
            coord_t end2 = inner_line.b[majorit_axis];
            if (start2 > end2) std::swap(start2, end2);
            if (start1 > end2 || start2 > end1) continue; // not overlaped intervals

            Vec2d dir2 = LineUtils::direction(inner_line).cast<double>();
            dir2.normalize();
            double  angle = acos(dir1.dot(dir2));             
            if (fabs(angle) > angle_tolerace) continue; // not similar direction           

            // Improve: use only one side of offest !!
            Point offset_middle = LineUtils::middle(inner_line);
            double distance = island_line.perp_signed_distance_to(offset_middle);
            if (fabs(distance - offset_delta) > distance_tolerance)
                continue; // only parallel line with big distance

            // found first inner line
            inner_2_island[inner_line_index] = island_line_index;

            // There could be also liar but we ignor that fact and accept first one
            break;
        } while (inner_line_index != stop_inner_index);
    }

    // Create outline flags for inner lines
    enum class Outline { // extend bool with unknown value
        yes,
        no,
        unknown};
    std::vector<Outline> inner_outline(inner_2_island.size(), Outline::unknown);
    for (size_t inner_index = 0; inner_index < inner_2_island.size(); ++inner_index) {
        size_t border_index = inner_2_island[inner_index];
        if (border_index == invalid_conversion)
            continue; // inner_outline[inner_index] = Outline::unknown
        inner_outline[inner_index] = is_outline[border_index]? Outline::yes : Outline::no;
    }

    // limit unknown state
    ExPolygonsIndices border_indices(ExPolygons{island});

    size_t inner_offset = 0; // offset of current inner polygon inside of the lambda
    auto remove_unknown = [&inner_offset, &inner_outline, &inner_2_island, &border_indices, invalid_conversion]
    (size_t polygon_size) {
        ScopeGuard offset_increase([&inner_offset, polygon_size] 
            { inner_offset += polygon_size; }); // increase offset for next polygon

        // collect sequence of unknown
        size_t first_yes = 0;
        while (first_yes < polygon_size && 
            inner_outline[first_yes + inner_offset] != Outline::yes)
            ++first_yes;

        // polygon do not contain outline for sampling
        if (first_yes == polygon_size) {
            for (size_t i = 0; i < polygon_size; ++i)
                inner_outline[i + inner_offset] = Outline::no;
            return;
        }
        auto loop_increment = [polygon_size](size_t &i) { // loop incrementation of index
            if (++i == polygon_size) i = 0; };
        auto set_to = [&inner_outline, inner_offset, loop_increment]
        (size_t from, size_t to, Outline value) { 
            for (size_t i = from; i != to; loop_increment(i)) {
                inner_outline[i + inner_offset] = value;
            }
        };

        bool is_prev_outline = true;
        int32_t first_polygon = border_indices.cvt(inner_2_island[first_yes + inner_offset]).polygon_index;
        int32_t prev_polygon = first_polygon; 
        size_t start_unknown = polygon_size; // invalid value, current index is not Outline::unknown
        size_t i = first_yes;
        loop_increment(i); // one after first_yes

        // resolve sequence of unknown outline from start_unknown to end_unknown(polygon indices)
        // 
        auto resolve_unknown = [&start_unknown, &is_prev_outline, &prev_polygon, &set_to]
        (size_t end_unknown, bool is_current_outline, int32_t border_polygon_index) {            
            Outline value = (is_current_outline && // is current(after unknown) outline
                                is_prev_outline && // was (before unknown) outline
                            border_polygon_index == prev_polygon // is same border polygon
                            )? Outline::yes : Outline::no;
            set_to(start_unknown, end_unknown, value); // change sequence of unknown to value
        };
        for (; i != first_yes; loop_increment(i)) {
            size_t inner_index = i + inner_offset;
            Outline outline = inner_outline[inner_index];
            if (outline == Outline::unknown){
                if (start_unknown == polygon_size)
                    start_unknown = i;
                continue;
            }
            size_t border_line_index = inner_2_island[inner_index];
            int32_t border_polygon_index = (border_line_index == invalid_conversion) ? -1 :
                border_indices.cvt(static_cast<int32_t>(border_line_index)).polygon_index;
            bool is_current_outline = outline == Outline::yes;
            if (start_unknown != polygon_size) {
                resolve_unknown(i, is_current_outline, border_polygon_index); 
                start_unknown = polygon_size;
            }
            prev_polygon = border_polygon_index;
            is_prev_outline = is_current_outline;
        }
        if (start_unknown != polygon_size) // last unknown sequence
            resolve_unknown(i, true, first_polygon);
    };
    for (const ExPolygon &inner_expoly: inner) {
        remove_unknown(inner_expoly.contour.size());
        for (const Polygon& hole: inner_expoly.holes)
            remove_unknown(hole.size());        
    }
    assert(inner_offset == inner_lines.size());
    assert(std::none_of(inner_outline.begin(), inner_outline.end(), [](const Outline &o) {
        return o == Outline::unknown; }));
    std::vector<bool> is_inner_outline(inner_2_island.size(), false);
    for (const Outline &o : inner_outline)
        if (o == Outline::yes)
            is_inner_outline[&o - &inner_outline.front()] = true;    
    return Field{inner, is_inner_outline};
}

#if defined(SLA_SAMPLE_ISLAND_UTILS_STORE_FIELD_TO_SVG_PATH) || defined(SLA_SAMPLE_ISLAND_UTILS_STORE_PENINSULA_FIELD_TO_SVG_PATH)
void draw(SVG &svg, const Field &field, const ExPolygon& border, bool draw_border_line_indexes = false, bool draw_field_source_indexes = true) {
    const char *field_color = "red";
    const char *border_line_color = "blue";
    const char *inner_line_color = "lightgreen";
    const char *inner_line_outline_color = "darkgreen";
    svg.draw(border, field_color);
    Lines border_lines = to_lines(border);
    LineUtils::draw(svg, border_lines, border_line_color, 0., draw_border_line_indexes);
    if (field.inner.empty())
        return;
    // draw inner
    Lines inner_lines = to_lines(field.inner);
    LineUtils::draw(svg, inner_lines, inner_line_color, 0., draw_border_line_indexes);
    if (draw_field_source_indexes)
        for (auto &line : inner_lines) {
            size_t index = &line - &inner_lines.front();
            Point middle_point = LineUtils::middle(line);
            std::string text = std::to_string(index);
            const char *color = inner_line_color;
            if (field.is_inner_outline[&line - &inner_lines.front()]) {
                LineUtils::draw(svg, line, inner_line_outline_color);
                color = inner_line_outline_color;
            }
            svg.draw_text(middle_point, text.c_str(), color);
        }
}
#endif // SLA_SAMPLE_ISLAND_UTILS_STORE_FIELD_TO_SVG_PATH || SLA_SAMPLE_ISLAND_UTILS_STORE_PENINSULA_FIELD_TO_SVG_PATH

std::map<size_t, WideTinyChanges> create_wide_tiny_changes(const Positions& part_ends, const Lines &lines) {
    std::map<size_t, WideTinyChanges> wide_tiny_changes;
    // part_ends are already oriented
    for (const Position &position : part_ends) {
        Point p1, p2;
        std::tie(p2, p1) = VoronoiGraphUtils::point_on_lines(position, lines);
        const VD::edge_type *edge = position.neighbor->edge;
        size_t i1 = edge->twin()->cell()->source_index();
        size_t i2 = edge->cell()->source_index();
        
        // add sorted change from wide to tiny
        // stored uder line index or line shorten in point b
        WideTinyChange change(p1, p2, i2);
        auto item = wide_tiny_changes.find(i1);
        if (item == wide_tiny_changes.end()) {
            wide_tiny_changes[i1] = {change};
        } else {
            WideTinyChange::SortFromAToB pred(lines[i1]);
            VectorUtils::insert_sorted(item->second, change, pred);
        }
    }
    return wide_tiny_changes;
}

// IMPROVE do not use pointers on node but pointers on Neighbor
Field create_thick_field(const ThickPart& part, const Lines &lines, const SampleConfig &config)
{    
    // store shortening of outline segments
    //   line index, vector<next line index + 2x shortening points>
    std::map<size_t, WideTinyChanges> wide_tiny_changes = create_wide_tiny_changes(part.ends, lines);
    
    // connection of line on island
    std::map<size_t, size_t> b_connection = LineUtils::create_line_connection_over_b(lines);

    std::vector<size_t> source_indices;
    auto inser_point_b = [&lines, &b_connection, &source_indices]
    (size_t &index, Points &points, std::set<size_t> &done)
    {
        const Line &line = lines[index];
        points.push_back(line.b);
        const auto &connection_item = b_connection.find(index);
        assert(connection_item != b_connection.end());
        done.insert(index);
        index = connection_item->second;
        source_indices.push_back(index);
    };

    size_t source_index_for_change = lines.size();

    /// <summary>
    /// Insert change into 
    /// NOTE: separate functionality to be able force break from second loop
    /// </summary>
    /// <param name="lines">island(ExPolygon) converted to lines</param>
    /// <param name="index"></param> ...
    /// <returns>False when change lead to close loop(into first change) otherwise True</returns>
    auto insert_changes = [&wide_tiny_changes, &lines, &source_indices, source_index_for_change]
    (size_t &index, Points &points, std::set<size_t> &done, size_t input_index)->bool {
        auto change_item = wide_tiny_changes.find(index);
        while (change_item != wide_tiny_changes.end()) {
            const WideTinyChanges &changes = change_item->second;
            assert(!changes.empty());
            size_t change_index = 0;
            if (!points.empty()) { // Not first point, could lead to termination
                LineUtils::SortFromAToB pred(lines[index]);
                bool no_change = false;
                while (pred.compare(changes[change_index].new_b, points.back())) {
                    ++change_index;
                    if (change_index >= changes.size()) {
                        no_change = true;
                        break;
                    }
                }
                if (no_change) break;

                // Field ends with change into first index
                if (change_item->first == input_index &&
                    change_index == 0) {
                    return false;
                }
            }
            const WideTinyChange &change = changes[change_index];
            // prevent double points
            if (points.empty() ||
                !PointUtils::is_equal(points.back(), change.new_b)) {
                points.push_back(change.new_b);
                source_indices.push_back(source_index_for_change);
            } else {
                source_indices.back() = source_index_for_change;
            }
            // prevent double points
            if (!PointUtils::is_equal(lines[change.next_line_index].b,
                                      change.next_new_a)) {
                points.push_back(change.next_new_a);
                source_indices.push_back(change.next_line_index);
            }
            done.insert(index);

            auto is_before_first_change = [&wide_tiny_changes, input_index, &lines]
                (const Point& point_on_input_line) {
                // is current change into first index line lay before first change?
                auto input_change_item = wide_tiny_changes.find(input_index);
                if(input_change_item == wide_tiny_changes.end())
                    return true;

                const WideTinyChanges &changes = input_change_item->second;
                LineUtils::SortFromAToB pred(lines[input_index]);
                for (const WideTinyChange &change : changes) {
                    if (pred.compare(change.new_b, point_on_input_line))
                        // Exist input change before 
                        return false;
                }
                // It is before first index
                return true;
            };

            // change into first index - loop is finished by change
            if (index != input_index && 
                input_index == change.next_line_index && 
                is_before_first_change(change.next_new_a)) {
                return false;
            }

            index = change.next_line_index;
            change_item = wide_tiny_changes.find(index);
        }
        return true;
    };
    
    // all source line indices belongs to thick part of island
    std::vector<size_t> field_line_indices = get_line_indices(part.start, part.ends);  

    // Collect outer points of field
    Points points;
    points.reserve(field_line_indices.size());
    std::vector<size_t> outline_indexes;
    outline_indexes.reserve(field_line_indices.size());
    size_t input_index1 = part.start->edge->cell()->source_index();
    size_t input_index2 = part.start->edge->twin()->cell()->source_index();
    size_t input_index  = std::min(input_index1, input_index2); // Why select min index?
    size_t outline_index = input_index;
    // Done indexes is used to detect holes in field
    std::set<size_t> done_indices; // IMPROVE: use vector(size of lines count) with bools
#ifdef SLA_SAMPLE_ISLAND_UTILS_STORE_FIELD_TO_SVG_PATH    
    static int counter = 0;
    std::string field_to_svg_path = replace_first(
        SLA_SAMPLE_ISLAND_UTILS_STORE_FIELD_TO_SVG_PATH, "<<COUNTER>>", std::to_string(counter++));
    {
        SVG svg(field_to_svg_path.c_str(), LineUtils::create_bounding_box(lines));
        LineUtils::draw(svg, lines, "black", 0., /*indices*/ true);
        for(const auto& change_it: wide_tiny_changes)
            for (const auto& change: change_it.second){
                Line bisector(change.new_b, change.next_new_a);
                LineUtils::draw(svg, bisector, "red");
                std::string text = "from " + std::to_string(change_it.first) 
                    + " to " + std::to_string(change.next_line_index);
                svg.draw_text(bisector.a/2 + bisector.b/2, text.c_str(), "orange");
            }
    } // flush svg file
#endif // SLA_SAMPLE_ISLAND_UTILS_STORE_FIELD_TO_SVG_PATH
    do {
        if (!insert_changes(outline_index, points, done_indices, input_index))
            break;        
        inser_point_b(outline_index, points, done_indices);

        if (points.size() > (lines.size() + 2*part.ends.size())){
            // protection against endless loop
            assert(false);
            return {};
        }
    } while (outline_index != input_index);

    assert(points.size() >= 3);
    if (points.size() < 3)
        return {}; // invalid field

    ExPolygon border{Polygon{points}};
    // finding holes(another closed polygon)
    if (done_indices.size() < field_line_indices.size()) {
        for (const size_t &index : field_line_indices) {
            if(done_indices.find(index) != done_indices.end()) continue;
            // new  hole
            Points hole_points;
            size_t hole_index = index;
            do {
                inser_point_b(hole_index, hole_points, done_indices);
            } while (hole_index != index);
            border.holes.emplace_back(hole_points);
        }
        // Set largest polygon as contour
        set_biggest_hole_as_contour(border, source_indices);
    }
    std::vector<bool> is_border_outline;
    is_border_outline.reserve(source_indices.size());
    for (size_t source_index : source_indices)
        is_border_outline.push_back(source_index != source_index_for_change);    
    float delta = static_cast<float>(config.minimal_distance_from_outline);
    Field field = create_field(border, delta, is_border_outline);
#ifdef SLA_SAMPLE_ISLAND_UTILS_STORE_FIELD_TO_SVG_PATH
    {
        const char *source_line_color = "black";
        bool draw_source_line_indexes = true;
        bool draw_border_line_indexes = false;
        bool draw_field_source_indexes = true;
        SVG svg(field_to_svg_path.c_str(),LineUtils::create_bounding_box(lines));
        LineUtils::draw(svg, lines, source_line_color, 0., draw_source_line_indexes);
        draw(svg, field, border, draw_border_line_indexes, draw_field_source_indexes);
    }
#endif //SLA_SAMPLE_ISLAND_UTILS_STORE_FIELD_TO_SVG_PATH
    assert(!field.inner.empty());
    return field;
}

/// <summary>
/// Uniform sample expolygon area by points inside Equilateral triangle center
/// </summary>
/// <param name="expoly">Input area to sample.(scaled)</param>
/// <param name="triangle_side">Distance between samples.</param>
/// <returns>Uniform samples(scaled)</returns>
Slic3r::Points sample_expolygon(const ExPolygon &expoly, coord_t triangle_side){
    const Points &points = expoly.contour.points;
    assert(!points.empty());
    // get y range
    coord_t min_y = points.front().y();
    coord_t max_y = min_y;
    for (const Point &point : points) {
        if (min_y > point.y())
            min_y = point.y();
        else if (max_y < point.y())
            max_y = point.y();
    }
    coord_t half_triangle_side = triangle_side / 2;
    static const float coef2 = sqrt(3.) / 2.;
    coord_t triangle_height = static_cast<coord_t>(std::round(triangle_side * coef2));

    // IMPROVE: use line end y
    Lines lines = to_lines(expoly);
    // remove lines paralel with axe x
    lines.erase(std::remove_if(lines.begin(), lines.end(),
                               [](const Line &l) {
                                   return l.a.y() == l.b.y();
                               }), lines.end());

    // change line direction from top to bottom
    for (Line &line : lines)
        if (line.a.y() > line.b.y()) std::swap(line.a, line.b);
    
    // sort by a.y()
    std::sort(lines.begin(), lines.end(),
              [](const Line &l1, const Line &l2) -> bool {
                  return l1.a.y() < l2.a.y();
              });
    // IMPROVE: guess size and reserve points
    Points result;
    size_t start_index = 0;
    bool is_odd = false;
    for (coord_t y = min_y + triangle_height / 2; y < max_y; y += triangle_height) {
        is_odd = !is_odd;
        std::vector<coord_t> intersections;
        bool increase_start_index = true;
        for (auto line = std::begin(lines)+start_index; line != std::end(lines); ++line) {
            const Point &b = line->b;
            if (b.y() <= y) {
                // removing lines is slow, start index is faster
                // line = lines.erase(line); 
                if (increase_start_index) ++start_index;
                continue;
            }
            increase_start_index = false;
            const Point &a = line->a;
            if (a.y() >= y) break;
            float   y_range      = static_cast<float>(b.y() - a.y());
            float   x_range      = static_cast<float>(b.x() - a.x());
            float   ratio        = (y - a.y()) / y_range;
            coord_t intersection = a.x() +
                                   static_cast<coord_t>(x_range * ratio);
            intersections.push_back(intersection);
        }
        assert(intersections.size() % 2 == 0);
        std::sort(intersections.begin(), intersections.end());
        for (size_t index = 0; index + 1 < intersections.size(); index += 2) {
            coord_t start_x = intersections[index];
            coord_t end_x   = intersections[index + 1];
            if (is_odd) start_x += half_triangle_side;
            coord_t div = start_x / triangle_side;
            if (start_x > 0) div += 1;
            coord_t x = div * triangle_side;
            if (is_odd) x -= half_triangle_side;
            while (x < end_x) {
                result.emplace_back(x, y);
                x += triangle_side;
            }
        }
    }
    return result;
}

/// <summary>
/// Same as sample_expolygon but offseted by centroid and rotate by farrest point from centroid
/// </summary>
Slic3r::Points sample_expolygons_with_centering(const ExPolygons &expolys, coord_t triangle_side) {
    Points result;
    for (const ExPolygon &expoly : expolys) {
        assert(!expoly.contour.empty());
        if (expoly.contour.size() < 3)
            continue;
        // to unify sampling of rotated expolygon offset and rotate pattern by centroid and farrest point
        Point center = expoly.contour.centroid();
        Point extrem = expoly.contour.front(); // the farest point from center
        // NOTE: ignore case with multiple same distance points
        double extrem_distance_sq = -1.;
        for (const Point &point : expoly.contour.points) {
            Point from_center = point - center;
            double distance_sq = from_center.cast<double>().squaredNorm();
            if (extrem_distance_sq < distance_sq) {
                extrem_distance_sq = distance_sq;
                extrem = point;
            }
        }
        double angle = atan2(extrem.y() - center.y(), extrem.x() - center.x());
        ExPolygon expoly_tr = expoly; // copy
        expoly_tr.rotate(angle, center);
        Points samples = sample_expolygon(expoly_tr, triangle_side);
        for (Point &sample : samples) 
            sample.rotate(-angle, center);
        append(result, samples);        
    }
    return result;
}

/// <summary>
/// create support points on border of field
/// </summary>
/// <param name="field">Input field</param>
/// <param name="config">Parameters for sampling.</param>
/// <returns>support for outline</returns>
SupportIslandPoints sample_outline(const Field &field, const SampleConfig &config){
    coord_t max_align_distance = config.max_align_distance;
    coord_t sample_distance = config.thick_outline_max_distance;
    SupportIslandPoints result;

    using RestrictionPtr = std::shared_ptr<SupportOutlineIslandPoint::Restriction>;
    auto add_sample = [&result, sample_distance]
        (size_t index, const RestrictionPtr& restriction, coord_t &last_support) {
        const double &line_length_double = restriction->lengths[index];
        coord_t line_length = static_cast<coord_t>(std::round(line_length_double));        
        while (last_support + line_length > sample_distance){
            float ratio = static_cast<float>((sample_distance - last_support) / line_length_double);
            SupportOutlineIslandPoint::Position position(index, ratio);
            result.emplace_back(std::make_unique<SupportOutlineIslandPoint>(
                position, restriction, SupportIslandPoint::Type::thick_part_outline));
            last_support -= sample_distance;
        }
        last_support += line_length;
    };
    auto add_circle_sample = [max_align_distance, sample_distance, &add_sample]
        (const Polygon &polygon) {
        // IMPROVE: find interesting points to start sampling
        Lines lines = to_lines(polygon);
        std::vector<double> lengths;
        lengths.reserve(lines.size());
        double sum_lengths = 0;
        for (const Line &line : lines) {
            double length = line.length();
            sum_lengths += length;
            lengths.push_back(length);
        }

        using Restriction = SupportOutlineIslandPoint::RestrictionCircleSequence;
        auto restriction  = std::make_shared<Restriction>(lines, lengths, max_align_distance);
        coord_t last_support = std::min(static_cast<coord_t>(sum_lengths), sample_distance) / 2;
        for (size_t index = 0; index < lines.size(); ++index)
            add_sample(index, restriction, last_support);
    };

    // sample line sequence
    auto add_lines_samples = [&add_sample, max_align_distance, sample_distance]
        (const Lines &inner_lines, size_t first_index, size_t last_index) {
        if (first_index >= inner_lines.size() || 
            last_index >= inner_lines.size()) {
            // Invalid state caused by bad pairing of inner lines with outline contour
            // Observed on field created from peninsula (not separated tiny parts) 
            // and different way to create the change for connection to land.
            assert(false);
            return;
        }

        ++last_index; // index after last item
        Lines lines;
        // is over start ?
        if (first_index > last_index) {
            size_t count = last_index + (inner_lines.size() - first_index);
            lines.reserve(count);
            std::copy(inner_lines.begin() + first_index,
                        inner_lines.end(),
                        std::back_inserter(lines));
            std::copy(inner_lines.begin(),
                      inner_lines.begin() + last_index,
                        std::back_inserter(lines));
        } else {
            size_t count = last_index - first_index;
            lines.reserve(count); 
            std::copy(inner_lines.begin() + first_index,
                      inner_lines.begin() + last_index,
                      std::back_inserter(lines));
        }

        // IMPROVE: find interesting points to start sampling
        std::vector<double> lengths;
        lengths.reserve(lines.size());
        double sum_lengths = 0;
        for (const Line &line : lines) { 
            double length = line.length();
            sum_lengths += length;
            lengths.push_back(length);
        }
        
        using Restriction = SupportOutlineIslandPoint::RestrictionLineSequence;
        auto restriction = std::make_shared<Restriction>(lines, lengths, max_align_distance);
        
        // CHECK: Is correct to has always one support on outline sequence? 
        // or no sample small sequence at all?
        coord_t last_support = std::min(static_cast<coord_t>(sum_lengths), sample_distance) / 2;
        for (size_t index = 0; index < lines.size(); ++index) { 
            add_sample(index, restriction, last_support);
        }
    };
      
    auto sample_polygon = [&add_circle_sample, &add_lines_samples, 
        &is_outline = field.is_inner_outline]
        (const Polygon &inner_polygon, size_t inner_offset) {
        // weird inner shape to sample, 
        // investigate field.border offseting
        assert(inner_polygon.size() >= 3);
        if (inner_polygon.size() < 3)
            return; // no shape to sample
        
        // contain polygon tiny wide change?
        size_t first_change_index = inner_polygon.size();
        for (size_t polygon_index = 0; polygon_index < inner_polygon.size(); ++polygon_index)
            if (!is_outline[polygon_index + inner_offset]) {
                // found change from wide to tiny part
                first_change_index = polygon_index;
                break;
            }

        // is polygon without change
        if (first_change_index == inner_polygon.size())
            return add_circle_sample(inner_polygon);
        
        // exist change create line sequences
        // initialize with non valid values
        size_t inner_invalid = inner_polygon.size();
        // first and last index to inner lines
        size_t inner_first = inner_invalid; // 
        size_t inner_last  = inner_invalid;
        size_t stop_index  = first_change_index;
        if (stop_index == 0) // when check inner_index contain index after last item
            stop_index = inner_polygon.size();

        size_t inner_index = first_change_index;
        do { // search for first outline index after change
            ++inner_index;
            if (inner_index == inner_polygon.size()) {
                inner_index = 0;
                // Detect that whole polygon is not peninsula outline(coast)
                if (first_change_index == 0)
                    return; // Polygon do not contain edge to support.
            }
        } while (!is_outline[inner_index + inner_offset]);

        const Lines inner_lines = to_lines(inner_polygon);
        for (;inner_index != stop_index; ++inner_index) {
            if (inner_index == inner_lines.size())
                inner_index = 0;

            // not all inner lines has corresponding field line
            // same has more than one field line
            if (!is_outline[inner_index + inner_offset]) { // non outline part
                if (inner_first == inner_invalid) continue;
                // create Restriction object
                add_lines_samples(inner_lines, inner_first, inner_last);
                inner_first = inner_invalid;
                inner_last  = inner_invalid;
                continue;
            }
            
            inner_last = inner_index;
            // initialize first index
            if (inner_first == inner_invalid) inner_first = inner_last;
        }
        if (inner_first != inner_invalid)
            add_lines_samples(inner_lines, inner_first, inner_last);
    };

    // No inner space to sample
    if (field.inner.empty() || field.inner.front().contour.size() < 3)
        return result;

    // Sample inner outlines
    size_t index_offset = 0;
    for (const ExPolygon & inner: field.inner) {
        sample_polygon(inner.contour, index_offset);
        index_offset += inner.contour.size();
        for (const Polygon &hole: inner.holes) {
            sample_polygon(hole, index_offset);
            index_offset += hole.size();
        }
    }
    return result;
}

/// <summary>
/// Create field from thick part of island
/// Add support points on field contour(uniform step)
/// Add support points into inner part of field (grind)
/// </summary>
/// <param name="part">Define thick part of VG</param>
/// <param name="results">OUTPUT support points</param>
/// <param name="lines">Island contour(with holes)</param>
/// <param name="config">Define support density (by grid size and contour step)</param>
void create_supports_for_thick_part(const ThickPart &part, SupportIslandPoints &results, 
    const Lines &lines, const SampleConfig &config) {
    // Create field for thick part of island
    Field field = create_thick_field(part, lines, config);
    if (field.inner.empty())
        return; // no inner part
    SupportIslandPoints outline_support = sample_outline(field, config);
    results.insert(results.end(), 
        std::move_iterator(outline_support.begin()),
        std::move_iterator(outline_support.end()));
    // Inner must survive after sample field for aligning supports(move along outline)
    auto inner = std::make_shared<ExPolygons>(field.inner);    
    Points inner_points = sample_expolygons_with_centering(*inner, config.thick_inner_max_distance);    
    std::transform(inner_points.begin(), inner_points.end(), std::back_inserter(results), 
        [&](const Point &point) { 
            return std::make_unique<SupportIslandInnerPoint>(
                           point, inner, SupportIslandPoint::Type::thick_part_inner);
        });
}

// Search for interfaces
// 1. thin to min_wide
// 2. min_wide to max_center
// 3. max_center to Thick
enum class IslandPartType { thin, middle, thick };

// transition into neighbor part
struct IslandPartChange {
    // position on the way out of island part
    // Position::Neighbor::node is target(twin neighbor has source)
    // Position::ration define position on connection between nodes
    Position position; 
    size_t part_index;
};
using IslandPartChanges = std::vector<IslandPartChange>;

/// <summary>
/// Part of island with interfaces defined by positions
/// </summary>
struct IslandPart {
    // type of island part { thin | middle | thick }
    IslandPartType type; 

    // Positions and index of island part change
    IslandPartChanges changes;

    // sum of all lengths inside of part
    // IMPROVE1: Separate calculation localy into function merge_middle_parts_into_biggest_neighbor
    // IMPROVE2: better will be length of longest path
    // Used as rule to connect(merge) middle part of island to its biggest neighbour
    // NOTE: No solution for island with 2 biggest neighbors with same sum_lengths. 
    coord_t sum_lengths = 0;
};
using IslandParts = std::vector<IslandPart>;

/// <summary>
/// Data for process island parts' separation
/// </summary>
struct ProcessItem {
    // previously processed island node
    const VoronoiGraph::Node *prev_node = nullptr;

    // current island node to investigate neighbors
    const VoronoiGraph::Node *node = nullptr;

    // index of island part stored in island_parts
    // NOTE: Can't use reference because of vector reallocation
    size_t i = std::numeric_limits<size_t>::max();
};
using ProcessItems = std::vector<ProcessItem>;

/// <summary>
/// Add new island part 
/// </summary>
/// <param name="island_parts">Already existing island parts</param>
/// <param name="part_index">Source part index</param>
/// <param name="to_type">Type for new added part</param>
/// <param name="neighbor">Edge where appear change from one state to another</param>
/// <param name="limit">min or max(thick_min_width, thin_max_width)</param>
/// <param name="lines">Island border</param>
/// <param name="config">Minimal Island part length</param>
/// <returns>index of new part inside island_parts</returns>
size_t add_part(
    IslandParts &island_parts,
    size_t part_index,
    IslandPartType to_type,
    const Neighbor *neighbor,
    coord_t limit,
    const Lines &lines,
    const SampleConfig &config
) {
    Position position = VoronoiGraphUtils::get_position_with_width(neighbor, limit, lines);

    // Do not create part, when it is too close to island contour
    if (VoronoiGraphUtils::ends_in_distanace(position, config.min_part_length))        
        return part_index; // too close to border to add part, nothing to add

    size_t new_part_index = island_parts.size();
    const Neighbor *twin = VoronoiGraphUtils::get_twin(*neighbor);
    Position twin_position(twin, 1. - position.ratio);
    
    if (new_part_index == 1 &&
        VoronoiGraphUtils::ends_in_distanace(twin_position, config.min_part_length)) { 
        // Exist only initial island
        // NOTE: First island part is from start shorter than SampleConfig::min_part_length
        // Which is different to rest of island.
        assert(island_parts.size() == 1);
        assert(island_parts.front().changes.empty());
        // First island is too close to border to create new island part
        // First island is initialy set set thin, 
        // but correct type is same as type in short length distance from start
        island_parts.front().type = to_type;
        return part_index;
    }

    island_parts[part_index].changes.push_back({position, new_part_index});
    // NOTE: ignore multiple position on same neighbor
    island_parts[part_index].sum_lengths += position.calc_distance();
    
    coord_t sum_lengths = twin_position.calc_distance();
    IslandPartChanges changes{IslandPartChange{twin_position, part_index}};
    island_parts.push_back({to_type, changes, sum_lengths});
    return new_part_index;
}

/// <summary>
/// Detect interface between thin, middle and thick part of island
/// </summary>
/// <param name="island_parts">Already existing parts</param>
/// <param name="item_i">current part index</param>
/// <param name="neighbor">current neigbor to investigate</param>
/// <param name="lines">Island contour</param>
/// <param name="config">Configuration of hysterezis</param>
/// <returns>Next part index</returns>
size_t detect_interface(IslandParts &island_parts, size_t part_index, const Neighbor *neighbor, const Lines &lines, const SampleConfig &config) {
    // Range for of hysterezis between thin and thick part of island
    coord_t min = config.thick_min_width;
    coord_t max = config.thin_max_width;

    size_t next_part_index = part_index;
    switch (island_parts[part_index].type) {
    case IslandPartType::thin:
        // Near contour is type permanent no matter of width
        // assert(neighbor->min_width() <= min); 
        if (neighbor->max_width() < min) break; // still thin part
        next_part_index = add_part(island_parts, part_index, IslandPartType::middle, neighbor, min, lines, config);
        if (neighbor->max_width() < max) return next_part_index; // no thick part          
        return add_part(island_parts, next_part_index, IslandPartType::thick, neighbor, max, lines, config);
    case IslandPartType::middle:
        // assert(neighbor->min_width() >= min || neighbor->max_width() <= max);
        if (neighbor->min_width() < min) {
            return add_part(island_parts, part_index, IslandPartType::thin, neighbor, min, lines, config);
        } else if (neighbor->max_width() > max) {
            return add_part(island_parts, part_index, IslandPartType::thick, neighbor, max, lines, config);
        }
        break;// still middle part
    case IslandPartType::thick:
        //assert(neighbor->max_width() >= max);        
        if (neighbor->max_width() > max) break; // still thick part
        next_part_index = add_part(island_parts, part_index, IslandPartType::middle, neighbor, max, lines, config);
        if (neighbor->min_width() > min) return next_part_index; // no thin part
        return add_part(island_parts, next_part_index, IslandPartType::thin, neighbor, min, lines, config);        
    default: assert(false); // unknown part type
    }

    // without new interface between island parts
    island_parts[part_index].sum_lengths += static_cast<coord_t>(neighbor->length());
    return part_index; 
}

/// <summary>
/// Merge two island parts defined by index
/// NOTE: Do not sum IslandPart::sum_lengths on purpose to be independent on the merging order
/// </summary>
/// <param name="island_parts">All parts</param>
/// <param name="index">Merge into</param>
/// <param name="remove_index">Merge from</param>
void merge_island_parts(IslandParts &island_parts, size_t index, size_t remove_index){
    // It is better to remove bigger index, not neccessary
    assert(index < remove_index);
    // merge part interfaces
    IslandPartChanges &changes = island_parts[index].changes;
    IslandPartChanges &remove_changes = island_parts[remove_index].changes;

    // remove changes back to merged part
    auto remove_changes_end = std::remove_if(remove_changes.begin(), remove_changes.end(), 
        [i=index](const IslandPartChange &change) { return change.part_index == i; });

    // remove changes into removed part
    changes.erase(std::remove_if(changes.begin(), changes.end(), 
        [i=remove_index](const IslandPartChange &change) { return change.part_index == i; }),
        changes.end());

    // move changes from remove part to merged part
    changes.insert(changes.end(), 
        std::move_iterator(remove_changes.begin()),
        std::move_iterator(remove_changes_end));

    // remove island part
    island_parts.erase(island_parts.begin() + remove_index);

    // fix indices inside island part changes
    for (IslandPart &island_part : island_parts) {
        for (IslandPartChange &change : island_part.changes) {
            if (change.part_index == remove_index)
                change.part_index = index;
            else if (change.part_index > remove_index)
                --change.part_index;
        }
    }
}

/// <summary>
/// When apper loop back to already processed part of island graph this function merge island parts
/// </summary>
/// <param name="island_parts">All island parts</param>
/// <param name="item">To fix index</param>
/// <param name="index">Index into island parts to merge</param>
/// <param name="remove_index">Index into island parts to merge</param>
/// <param name="process">Queue of future processing</param>
void merge_parts_and_fix_process(IslandParts &island_parts,
    ProcessItem &item, size_t index, size_t remove_index, ProcessItems &process, const Neighbor* neighbor) {
    if (remove_index == index) return; // nothing to merge, loop connect to itself

    // Merged parts should be the same state, it is essential for alhorithm
    // Only first island part changes its type, but only before first change
    IslandPart &part = island_parts[index];
    IslandPart &part_ = island_parts[remove_index];
    if (part.type != part_.type) {
        part.changes.push_back(IslandPartChange{Position(neighbor, 0.), remove_index});
        const Neighbor *twin = VoronoiGraphUtils::get_twin(*neighbor);
        part_.changes.push_back(IslandPartChange{Position(twin, 1.), index});
        return;
    }

    // remove part with bigger index
    if (remove_index < index) 
        std::swap(remove_index, index);

    island_parts[index].sum_lengths += island_parts[remove_index].sum_lengths;
    merge_island_parts(island_parts, index, remove_index);    

    // fix indices in process queue
    for (ProcessItem &p : process)
        if (p.i == remove_index)
            p.i = index; // swap to new index
        else if (p.i > remove_index)
            --p.i; // decrease index

    // fix index for current item
    if (item.i > remove_index)
        --item.i; // decrease index
}

void merge_middle_parts_into_biggest_neighbor(IslandParts& island_parts) {
    // Connect parts till there is no middle parts
    for (size_t index = 0; index < island_parts.size(); ++index) {
        const IslandPart &island_part = island_parts[index];
        if (island_part.type != IslandPartType::middle) continue; // only middle parts
        // there must be change into middle part island always start as thin part
        assert(!island_part.changes.empty());
        if (island_part.changes.empty()) continue; // weird situation
        // find biggest neighbor island part
        auto max_change = std::max_element(island_part.changes.begin(), island_part.changes.end(),
            [&island_parts](const IslandPartChange &a, const IslandPartChange &b) {
                return island_parts[a.part_index].sum_lengths <
                    island_parts[b.part_index].sum_lengths;});

        // set island type by merged one (Thin OR Thick)
        island_parts[index].type = island_parts[max_change->part_index].type;

        size_t merged_index = index;
        size_t remove_index = max_change->part_index;
        if (merged_index > remove_index)
            std::swap(merged_index, remove_index);

        // NOTE: be carefull, function remove island part inside island_parts
        merge_island_parts(island_parts, merged_index, remove_index);
        --index; // on current index could be different island part
    }
}

void merge_same_neighbor_type_parts(IslandParts &island_parts) {
    // connect neighbor parts with same type
    for (size_t island_part_index = 0; island_part_index < island_parts.size(); ++island_part_index) {
        while (true) {
            const IslandPart &island_part = island_parts[island_part_index];
            assert(island_part.type != IslandPartType::middle); // only thin or thick parts        
            const IslandPartChanges &changes = island_part.changes;
            auto change_it = std::find_if(changes.begin(), changes.end(), 
                [&island_parts, type = island_part.type](const IslandPartChange &change) {
                    assert(change.part_index < island_parts.size());
                    return island_parts[change.part_index].type == type;});
            if (change_it == changes.end()) break; // no more changes
            merge_island_parts(island_parts, island_part_index, change_it->part_index);
        }
    }
}

/// <summary>
/// Find shortest distances between changes (combination of changes)
/// and choose the longest distance or farest node distance from changes
/// </summary>
/// <param name="changes">transition into different part island</param>
/// <param name="center">[optional]Center of longest path</param>
/// <returns>Length of island part defined as longest distance on graph inside part</returns>
coord_t get_longest_distance(const IslandPartChanges& changes, Position* center = nullptr) {
    const Neighbor *front_twin = VoronoiGraphUtils::get_twin(*changes.front().position.neighbor);
    if (changes.size() == 2 && front_twin == changes.back().position.neighbor) {
        // Special case when part lay only on one neighbor
        if (center != nullptr) {
            *center = changes.front().position;// copy
            center->ratio = (center->ratio + changes.back().position.ratio)/2;
        }
        return static_cast<coord_t>(changes.front().position.neighbor->length() *
            (1 - changes.front().position.ratio - changes.back().position.ratio));
    }

    struct ShortestDistance{
        coord_t distance;
        size_t prev_node_distance_index;
    };    
    using ShortestDistances = std::vector<ShortestDistance>;
    // for each island part node find distance to changes
    struct NodeDistance {
        // island part node
        const VoronoiGraph::Node *node;
        // shortest distance to node from change
        ShortestDistances shortest_distances; // size == changes.size()
    };
    using NodeDistances = std::vector<NodeDistance>;
    NodeDistances node_distances;

    const coord_t no_distance = std::numeric_limits<coord_t>::max();
    const size_t no_index = std::numeric_limits<size_t>::max();
    size_t count = changes.size();
    for (const IslandPartChange &change : changes) {
        const VoronoiGraph::Node *node = VoronoiGraphUtils::get_twin(*change.position.neighbor)->node;
        size_t change_index = &change - &changes.front();
        coord_t distance = change.position.calc_distance();
        if (auto node_distance_it = std::find_if(node_distances.begin(), node_distances.end(), 
            [&node](const NodeDistance &node_distance) { return node_distance.node == node;});
            node_distance_it != node_distances.end()) { // multiple changes has same nearest node
            ShortestDistance &shortest_distance = node_distance_it->shortest_distances[change_index];
            assert(shortest_distance.distance == no_distance);
            assert(shortest_distance.prev_node_distance_index == no_index);
            shortest_distance.distance = distance;
            continue; // Do not add twice into node_distances
        }
        ShortestDistances shortest_distances(count, ShortestDistance{no_distance, no_index});
        shortest_distances[change_index].distance = distance;
        node_distances.push_back(NodeDistance{node, std::move(shortest_distances)});
    }

    // use sorted changes for faster check of neighbors
    IslandPartChanges sorted_changes = changes; // copy
    std::sort(sorted_changes.begin(), sorted_changes.end(),
        [](const IslandPartChange &a, const IslandPartChange &b) {
            return a.position.neighbor < b.position.neighbor;
        });
    auto exist_part_change_for_neighbor = [&sorted_changes](const Neighbor *neighbor) {
        auto it = std::lower_bound(sorted_changes.begin(), sorted_changes.end(), neighbor,
            [](const IslandPartChange &change, const Neighbor *neighbor_) { 
                return change.position.neighbor < neighbor_; });
        if (it == sorted_changes.end()) return false;
        return it->position.neighbor == neighbor;
    };
    
    // Queue of island nodes to propagate shortest distance into their neigbors
    // contain indices into node_distances
    std::vector<size_t> process;
    for (size_t i = 1; i < node_distances.size(); i++) process.push_back(i); // zero index is start
    size_t next_distance_index = 0; // zero index is start
    size_t current_node_distance_index = -1;
    const Neighbor *prev_neighbor = front_twin;
    // propagate distances into neighbors
    while (true /* next_distance_index < node_distances.size()*/) {
        current_node_distance_index = next_distance_index;
        next_distance_index = -1; // set to no value ... index > node_distances.size()
        for (const Neighbor &neighbor : node_distances[current_node_distance_index].node->neighbors) {
            if (&neighbor == prev_neighbor) continue;
            if (exist_part_change_for_neighbor(&neighbor))
                continue; // change is search graph limit

            // IMPROVE: use binary search
            auto node_distance_it = std::find_if(node_distances.begin(), node_distances.end(),
                [node = neighbor.node](const NodeDistance& d) {
                    return d.node == node;} );
            if (node_distance_it == node_distances.end()) {
                // create new node distance
                ShortestDistances new_shortest_distances =
                    node_distances[current_node_distance_index].shortest_distances; // copy
                for (ShortestDistance &d : new_shortest_distances)
                    if (d.distance != no_distance) {
                        d.distance += static_cast<coord_t>(neighbor.length());
                        d.prev_node_distance_index = current_node_distance_index;
                    }
                if (next_distance_index < node_distances.size())
                    process.push_back(next_distance_index); // store for next processing
                next_distance_index = node_distances.size();
                prev_neighbor = VoronoiGraphUtils::get_twin(neighbor);
                // extend node distances (NOTE: invalidate addresing into node_distances)
                node_distances.push_back(NodeDistance{neighbor.node, new_shortest_distances});
                continue;
            }

            bool exist_distance_change = false;
            // update distances
            for (size_t i = 0; i < count; ++i) {
                const ShortestDistance &d = node_distances[current_node_distance_index]
                                                .shortest_distances[i];
                if (d.distance == no_distance) continue;
                coord_t new_distance = d.distance + static_cast<coord_t>(neighbor.length());                
                if (ShortestDistance &current_distance = node_distance_it->shortest_distances[i];
                    current_distance.distance > new_distance) {
                    current_distance.distance = new_distance;
                    current_distance.prev_node_distance_index = current_node_distance_index;
                    exist_distance_change = true;
                }
            }
            if (!exist_distance_change)
                continue; // no change in distances

            size_t item_index = node_distance_it - node_distances.begin();
            // process store unique indices into node_distances
            if(std::find(process.begin(), process.end(), item_index) != process.end())
                continue; // already in process

            if (next_distance_index < node_distances.size())
                process.push_back(next_distance_index); // store for next processing
            next_distance_index = item_index;
            prev_neighbor = VoronoiGraphUtils::get_twin(neighbor);
        }

        if (next_distance_index >= node_distances.size()){
            if (process.empty())
                break; // no more nodes to process
            next_distance_index = process.back();
            process.pop_back();
            prev_neighbor = nullptr; // do not know previous neighbor
            continue;
        }
    }

    // find farest distance node from changes
    coord_t farest_from_change = 0;
    size_t change_index = 0;
    const NodeDistance *farest_distnace = &node_distances.front();
    for (const NodeDistance &node_distance : node_distances)
        for (const ShortestDistance& d : node_distance.shortest_distances)
            if (farest_from_change < d.distance) {
                farest_from_change = d.distance;
                change_index = &d - &node_distance.shortest_distances.front();
                farest_distnace = &node_distance;
            }    
    
    // farest distance between changes
    // till node distances do not change order than index of change is index of closest node of change
    size_t source_change = count;
    for (size_t i = 0; i < (count-1); ++i) {
        const NodeDistance &node_distance = node_distances[i];
        const ShortestDistance &distance_to_change = node_distance.shortest_distances[i];
        for (size_t j = i+1; j < count; ++j) {
            coord_t distance = node_distance.shortest_distances[j].distance + distance_to_change.distance;
            if (farest_from_change < distance) {
                // this change is farest from other changes
                farest_from_change = distance;
                change_index = j;
                source_change = i;
                farest_distnace = &node_distance;
            }
        }
    }

    // center is not needed so return only farest distance
    if (center == nullptr)
        return farest_from_change;

    // Next lines are for calculation of center for longest path
    coord_t half_distance = farest_from_change / 2;

    // check if center is on change neighbor
    auto is_ceneter_on_change_neighbor = [&changes, center, half_distance](size_t change_index) {
        if (change_index >= changes.size())
            return false;
        const Position &position = changes[change_index].position;
        if (position.calc_distance() < half_distance)
            return false;
        // center lay on neighbour with change
        center->neighbor = position.neighbor;
        center->ratio = position.ratio - half_distance / position.neighbor->length();
        return true;
    };
    if (is_ceneter_on_change_neighbor(source_change) ||
        is_ceneter_on_change_neighbor(change_index))
        return farest_from_change;

    const NodeDistance *prev_node_distance = farest_distnace;
    const NodeDistance *node_distance = nullptr; 
    // iterate over longest path to find center(half distance)
    while (prev_node_distance->shortest_distances[change_index].distance >= half_distance) {
        node_distance = prev_node_distance;
        size_t prev_index = node_distance->shortest_distances[change_index].prev_node_distance_index;
        // case with center on change neighbor is already handled, so prev_index should be valid
        assert(prev_index != no_index && prev_index<node_distances.size());
        prev_node_distance = &node_distances[prev_index];   
    }

    // case with center on change neighbor should be already handled
    assert(node_distance != nullptr);
    if (node_distance == nullptr)
        // weird situation - hack to not crash on SPE-2714
        throw Slic3r::RuntimeError("SLA support point generator has failed."
            "\n\nThe generator was unable to sample an island. You may try to work around the problem "
            "by changing the orientation of the model slightly.\n\nWe are sorry for the inconvenience.");

    //if (node_distance == nullptr)
    //    return farest_from_change;
    assert(node_distance->shortest_distances[change_index].distance >= half_distance);
    assert(prev_node_distance->shortest_distances[change_index].distance <= half_distance);
    coord_t to_half_distance = half_distance - node_distance->shortest_distances[change_index].distance;
    // find neighbor between node_distance and prev_node_distance
    for (const Neighbor &n : node_distance->node->neighbors) {
        if (n.node != prev_node_distance->node) 
            continue;
        center->neighbor = &n;
        center->ratio = to_half_distance / n.length();
        return farest_from_change;
    }

    // weird situation when center is not found
    assert(false);
    return farest_from_change;
}

/// <summary>
/// Remove island part with index
/// Merge all neighbors of deleted part together and create merged part on lowest index of merged parts
/// </summary>
/// <param name="island_parts">All existing island parts with type only thin OR thick</param>
/// <param name="index">index of island part to remove</param>
/// <returns>modified part index and all removed part indices</returns>
std::pair<size_t, std::vector<size_t>> merge_negihbor(IslandParts &island_parts, size_t index) {
    // merge all neighbors into one part
    std::vector<size_t> remove_indices;
    const IslandPartChanges &changes = island_parts[index].changes;
    // all neighbor should be the same type which is different to current one.
    assert(std::find_if(changes.begin(), changes.end(), [&island_parts, type = island_parts[index].type]
        (const IslandPartChange &c) { return island_parts[c.part_index].type == type; }) == changes.end());
    remove_indices.reserve(changes.size());

    // collect changes from neighbors for result part + indices of neighbor parts
    IslandPartChanges modified_changes; 
    for (const IslandPartChange &change : changes) {        
        if (auto it = std::lower_bound(remove_indices.begin(), remove_indices.end(), change.part_index);
            it != remove_indices.end() && *it == change.part_index)
            continue; // part already added

        VectorUtils::insert_sorted(remove_indices, change.part_index, std::less{});
        // iterate neighbor changes and collect only changes to other neighbors
        for (const IslandPartChange &n_change : island_parts[change.part_index].changes) {
            if (n_change.part_index == index)
                continue; // skip back to removed part

            // Till it is made only on thick+thin parts and neighbor are different type
            // It should never appear
            assert(std::find_if(changes.begin(), changes.end(), [i = n_change.part_index]
                (const IslandPartChange &change){ return change.part_index == i;}) == changes.end());
            //if(std::find_if(changes.begin(), changes.end(), [polygon_index = n_change.part_index]
            //(const IslandPartChange &change){ return change.part_index == polygon_index;}) != changes.end())
            //    continue; // skip removed part changes
            modified_changes.push_back(n_change);
        }
    }

    // Set modified index to smallest from index or remove_indices(are sorted)
    size_t modified_index = index;
    if (remove_indices.front() < index) {
        std::swap(remove_indices.front(), modified_index);
        std::sort(remove_indices.begin(), remove_indices.end());
    }

    // Set result part after merge
    IslandPart& merged_part = island_parts[modified_index];    
    merged_part.type = island_parts[changes.front().part_index].type; // type of neighbor
    merged_part.changes = modified_changes;
    merged_part.sum_lengths = 0; // invalid value after merge
    
    // remove parts from island parts, from high index to low
    for (auto it = remove_indices.rbegin(); it < remove_indices.rend(); ++it)
        island_parts.erase(island_parts.begin() + *it);

    // For all parts and their changes fix indices
    for (IslandPart &island_part : island_parts)
        for (IslandPartChange &change : island_part.changes){
            auto it = std::lower_bound(remove_indices.begin(), remove_indices.end(), change.part_index);
            if (it != remove_indices.end() && *it == change.part_index) { // index from removed indices set to modified index
                change.part_index = modified_index; // Set neighbors neighbors to point on modified_index
            } else { // index bigger than some of removed index decrease by the amount of smaller removed indices
                change.part_index -= it - remove_indices.begin();
            }
        }

    return std::make_pair(modified_index, remove_indices);
}

/// <summary>
/// Calculate all distances between changes(combination of changes)
/// Choose the longest distance between change for each island part(part_length).
/// Merge island parts in order from shortest path_length
/// Till path_length is smaller than config::min_part_length
/// </summary>
/// <param name="island_parts">Only thin or thick parts</param>
/// <param name="min_part_length">Minimal length of part to not be merged into neighbors</param>
void merge_short_parts(IslandParts &island_parts, coord_t min_part_length) {    
    // should be called only for multiple island parts, at least 2
    assert(island_parts.size() > 1);
    if (island_parts.size() <= 1) return; // nothing to merge

    // only thin OR thick parts
    assert(std::find_if(island_parts.begin(), island_parts.end(), [](const IslandPart &i)
        {return i.type != IslandPartType::thin && i.type != IslandPartType::thick; }) == island_parts.end());

    // same size as island_parts
    std::vector<coord_t> part_lengths;
    part_lengths.reserve(island_parts.size());
    for (const IslandPart& island_part: island_parts)
        part_lengths.push_back(get_longest_distance(island_part.changes));

    // Merge island parts in order from shortest length
    while(true){
        // find smallest part
        size_t smallest_part_index = std::min_element(part_lengths.begin(), part_lengths.end()) - part_lengths.begin();
        if (part_lengths[smallest_part_index] >= min_part_length)
            break; // all parts are long enough
        
        auto [index, remove_indices] = merge_negihbor(island_parts, smallest_part_index);
        if (island_parts.size() == 1)
            return; // only longest part left

        // update part lengths
        part_lengths[index] = get_longest_distance(island_parts[index].changes);
        for (auto remove_index_it = remove_indices.rbegin();
             remove_index_it != remove_indices.rend(); 
            ++remove_index_it)
            // remove lengths for removed parts
            part_lengths.erase(part_lengths.begin() + *remove_index_it);
    }    
}

ThinPart create_only_thin_part(const VoronoiGraph::ExPath &path) {
    std::optional<Position> path_center_opt = create_position_on_path(path.nodes, path.length / 2);
    assert(path_center_opt.has_value());
    return ThinPart{*path_center_opt, /*ends*/ {}};
}

const VoronoiGraph::Node::Neighbor *get_smallest_source_index(const Positions& positions){ 
    // do not call with empty positions
    assert(!positions.empty());
    if (positions.size() == 1)
        return positions.front().neighbor;

    const VoronoiGraph::Node::Neighbor *smallest = nullptr;
    size_t smallest_index = std::numeric_limits<size_t>::max();
    for (const Position &position : positions) {
        const VD::edge_type *e = position.neighbor->edge;
        size_t min_index = std::min(
            e->cell()->source_index(), 
            e->twin()->cell()->source_index());
        if (smallest_index > min_index) {
            smallest_index = min_index;
            smallest = position.neighbor;
        }
    }
    return smallest;
}

std::pair<ThinParts, ThickParts> convert_island_parts_to_thin_thick(
    const IslandParts& island_parts, const VoronoiGraph::ExPath &path)
{
    // always must be at least one island part
    assert(!island_parts.empty());
    // when exist only one change there can't be any changes
    assert(island_parts.size() != 1 || island_parts.front().changes.empty());
    // convert island parts into result
    if (island_parts.size() == 1)
        return island_parts.front().type == IslandPartType::thin ?
            std::make_pair(ThinParts{create_only_thin_part(path)}, ThickParts{}) :
            std::make_pair(ThinParts{}, ThickParts{
                ThickPart{&path.nodes.front()->neighbors.front()}});

    std::pair<ThinParts, ThickParts> result;
    ThinParts& thin_parts = result.first;
    ThickParts& thick_parts = result.second;
    for (const IslandPart& i:island_parts) {
        // Only one island item is solved earlier, soo each part has to have changes
        assert(!i.changes.empty());
        Positions ends;
        ends.reserve(i.changes.size());
        std::transform(i.changes.begin(), i.changes.end(), std::back_inserter(ends),
            [](const IslandPartChange &change) { return change.position; });
        std::sort(ends.begin(), ends.end(),
            [](const Position &a, const Position &b) { return a.neighbor < b.neighbor; });
        if (i.type == IslandPartType::thin) {
            // Calculate center of longest distance, discard distance
            Position center;
            get_longest_distance(i.changes, &center);
            thin_parts.push_back(ThinPart{center, std::move(ends)});
        } else {
            assert(i.type == IslandPartType::thick);
            const Neighbor *start = VoronoiGraphUtils::get_twin(*get_smallest_source_index(ends));
            // NOTE: VD could contain different order of edges each run.
            // To unify behavior as a start index is selected edge with smallest index of source line
            thick_parts.push_back(ThickPart {start, std::move(ends)});
        }
    }
    return result;
}

#ifdef SLA_SAMPLE_ISLAND_UTILS_DEBUG_PARTS_PATH
void draw(const IslandParts &parts, const ProcessItems &queue, const ProcessItem& current, const Lines& lines) {

    static int counter = 0;
    std::string svg_path = replace_first(SLA_SAMPLE_ISLAND_UTILS_DEBUG_PARTS_PATH, "<<COUNTER>>", std::to_string(counter++));
    SVG svg(svg_path.c_str(), LineUtils::create_bounding_box(lines));
    LineUtils::draw(svg, lines, "black", 0.);

    const char *thin_color = "blue";
    const char *thick_color = "green";
    const char *middle_color = "orange";
    coord_t edge_width = 3e4;

    using Neighbor = VoronoiGraph::Node::Neighbor;    
    std::vector<const Neighbor *> queue_done;
    auto get_neighbor = [](const VoronoiGraph::Node *from, const VoronoiGraph::Node* to)->const Neighbor* {
        if (from == nullptr || to == nullptr)
            return nullptr;
        for (const Neighbor &n : from->neighbors)
            if (n.node == to)
                return &n;
        return nullptr;
    };
    for (const ProcessItem& it : queue) {
        const Neighbor *n = get_neighbor(it.prev_node, it.node);
        queue_done.push_back(n);
        queue_done.push_back(VoronoiGraphUtils::get_twin(*n));
    }
    
    if (const Neighbor *n = get_neighbor(current.prev_node, current.node);
        n != nullptr) {
        queue_done.push_back(n);
        queue_done.push_back(VoronoiGraphUtils::get_twin(*n));
    }
    for (const IslandPart &part : parts){
        for (const IslandPartChange &change : part.changes) { // add changes
            const Neighbor *n = change.position.neighbor;
            queue_done.push_back(n);
            queue_done.push_back(VoronoiGraphUtils::get_twin(*n));
        }
    }
    auto neighbor_sort = [](const Neighbor *a, const Neighbor *b) {return a < b;};
    std::sort(queue_done.begin(), queue_done.end(), neighbor_sort);
    queue_done.erase(std::unique(queue_done.begin(), queue_done.end()), queue_done.end());    

    for (const IslandPart &part: parts){
        size_t index = &part - &parts.front();
        Positions ends;
        const char *color = 
                part.type == IslandPartType::thin ? thin_color :
                part.type == IslandPartType::thick ? thick_color : middle_color;
        for (const IslandPartChange &change: part.changes){
            if (change.part_index > index)
                continue;

            Point p = VoronoiGraphUtils::create_edge_point(change.position);
            const auto edge = change.position.neighbor->edge;
            const VD::vertex_type *v0 = edge->vertex0();
            const VD::vertex_type *v1 = edge->vertex1();
            Vec2d dir(v1->x() - v0->x(), v1->y() - v0->y());
            dir.normalize();
            dir.x() *= 0.6 / SCALING_FACTOR;
            dir.y() *= 1 / SCALING_FACTOR;
            Point dir_ = dir.cast<coord_t>();

            auto type = parts[change.part_index].type;
            const char *neighbor_color = 
                type == IslandPartType::thin ? thin_color :
                type == IslandPartType::thick ? thick_color : middle_color;
            svg.draw(p.cast<coord_t>(), "gray");
            VoronoiGraphUtils::draw(svg, *edge, lines, "gray", edge_width);
            Point letter_offset(-.75/SCALING_FACTOR, -.7/SCALING_FACTOR);
            svg.draw_text(letter_offset + p + dir_, std::to_string(change.part_index).c_str(), neighbor_color);
            svg.draw_text(letter_offset + p - dir_, std::to_string(index).c_str(), color);
            ends.push_back(change.position);
        }

        if (part.changes.empty()) {
            assert(parts.size() == 1);
            continue; // only first part till first change
        }

        const Neighbor *start = VoronoiGraphUtils::get_twin(*part.changes.front().position.neighbor);
        auto is_start = [start](const IslandPartChange &change) {
            return change.position.neighbor == start; 
        };
        if (std::find_if(part.changes.begin(), part.changes.end(), is_start) != part.changes.end())
            continue; // start is also end;  

        std::vector<const Neighbor *> done = queue_done; // copy queue        
        std::function<void(const Neighbor *)> draw_neighbor; // recursive function for draw
        draw_neighbor = [&draw_neighbor, &done, &svg, &lines, color, neighbor_sort, edge_width, index]
        (const Neighbor *neighbor) {
            VectorUtils::insert_sorted(done, neighbor, neighbor_sort);
            const Neighbor *twin = VoronoiGraphUtils::get_twin(*neighbor);
            VectorUtils::insert_sorted(done, twin, neighbor_sort);
            for (const Neighbor &n: neighbor->node->neighbors){
                if (&n == twin)
                    continue;

                if (auto it = std::lower_bound(done.begin(), done.end(), &n, neighbor_sort);
                    it != done.end() && *it == &n)
                    continue; // already done

                VoronoiGraphUtils::draw(svg, *n.edge, lines, color, edge_width);
                auto v0 = n.edge->vertex0();
                auto v1 = n.edge->vertex1();
                Point p(v0->x() / 2 + v1->x() / 2, v0->y() / 2 + v1->y() / 2);
                svg.draw_text(p, std::to_string(index).c_str(), color, 1);
                draw_neighbor(&n);//recursive call
            }
        };
        draw_neighbor(start);
    }

    for (const ProcessItem &item : queue) {
        Point from = VoronoiGraphUtils::to_point(item.prev_node->vertex);
        Point to = VoronoiGraphUtils::to_point(item.node->vertex);
        svg.draw(Line(from, to), "darkgray");
        svg.draw(to, "darkgray");
        svg.draw_text(from / 2 + to / 2, std::to_string(item.i).c_str(), "darkgray");
    }

    if (current.prev_node == nullptr){
        if (current.node == nullptr)
            return;
        Point p = VoronoiGraphUtils::to_point(current.node->vertex);
        svg.draw(p, "red");
        svg.draw_text(p, std::to_string(current.i).c_str(), "red");
    } else {
        Point from = VoronoiGraphUtils::to_point(current.prev_node->vertex);
        Point to = VoronoiGraphUtils::to_point(current.node->vertex);
        svg.draw(Line(from, to), "red");
        svg.draw(to, "red");
        svg.draw_text(from / 2 + to / 2, std::to_string(current.i).c_str(), "red");
    }
}
#endif // SLA_SAMPLE_ISLAND_UTILS_DEBUG_PARTS_PATH

#ifndef NDEBUG
bool exist_twin_change_in_part(const IslandParts &parts){
    auto sort_by_neighbor = [](const IslandPartChange &c1, const IslandPartChange &c2) {
        return c1.position.neighbor < c2.position.neighbor;
    };
    auto is_same_neighbor = [](const IslandPartChange &c1, const IslandPartChange &c2) {
        return c1.position.neighbor == c2.position.neighbor;
    };
    for(const IslandPart &part: parts){
        IslandPartChanges changes = part.changes; // copy
        std::sort(changes.begin(), changes.end(), sort_by_neighbor);
        if (std::unique(changes.begin(), changes.end(), is_same_neighbor) != changes.end())
            return true;
    }
    return false;
}
#endif // DEBUG

/// <summary>
/// Separate thin(narrow) and thick(wide) part of island
/// </summary>
/// <param name="path">Longest path over island</param>
/// <param name="lines">Island border</param>
/// <param name="config">Define border between thin and thick part 
/// and minimal length of separable part</param>
/// <returns>Thin and thick parts</returns>
std::pair<ThinParts, ThickParts> separate_thin_thick(
    const VoronoiGraph::ExPath &path, const Lines &lines, const SampleConfig &config
) {
    // Check input
    assert(!path.nodes.empty());
    assert(lines.size() >= 3); // at least triangle

    // Start dividing on some border of island
    const VoronoiGraph::Node *start_node = path.nodes.front();

    // CHECK that front of path is outline node
    assert(start_node->neighbors.size() == 1);
    // first neighbor must be from outline node
    assert(start_node->neighbors.front().min_width() == 0); 
        
    IslandParts island_parts{IslandPart{IslandPartType::thin, /*changes*/{}, /*sum_lengths*/0}};
    ProcessItem item = {/*prev_node*/ nullptr, start_node, 0}; // current processing item
    ProcessItems process; // queue of nodes to process     
    do { // iterate over all nodes in graph and collect interfaces into island_parts
//#ifdef SLA_SAMPLE_ISLAND_UTILS_DEBUG_PARTS_PATH
//        draw(island_parts, process, item, lines);
//#endif // SLA_SAMPLE_ISLAND_UTILS_DEBUG_PARTS_PATH
        ProcessItem next_item = {nullptr, nullptr, std::numeric_limits<size_t>::max()};
        for (const Neighbor &neighbor: item.node->neighbors) {
            if (neighbor.node == item.prev_node) continue; // already done
            if (next_item.node != nullptr) { // already prepared item is stored into queue
                process.push_back(next_item);
                next_item.node = nullptr;
            }

            // exist loop back?
            auto is_oposit_item = [prev_node = item.node, node = neighbor.node](const ProcessItem &p) {
                return p.node == prev_node && p.prev_node == node;};
            if (auto process_it = std::find_if(process.begin(), process.end(), is_oposit_item);                                
                process_it != process.end()) {
                // solve loop back
                merge_parts_and_fix_process(island_parts, item, item.i, process_it->i, process, &neighbor);
                // branch is already processed
                process.erase(process_it);
                continue;
            }

            size_t next_part_index = detect_interface(island_parts, item.i, &neighbor, lines, config);
            next_item = ProcessItem{item.node, neighbor.node, next_part_index};
        }
        // Select next node to process        
        if (next_item.node != nullptr) {
            item = next_item; // copy
        } else {
            if (process.empty()) 
                break; // no more nodes to process            
            item = process.back(); // copy
            process.pop_back();            
        }
    } while (item.node != nullptr); // loop should end by break with empty process
#ifdef SLA_SAMPLE_ISLAND_UTILS_DEBUG_PARTS_PATH
    draw(island_parts, process, item, lines);
#endif // SLA_SAMPLE_ISLAND_UTILS_DEBUG_PARTS_PATH

    merge_middle_parts_into_biggest_neighbor(island_parts);
    if (island_parts.size() != 1)
        merge_same_neighbor_type_parts(island_parts);
    if (island_parts.size() != 1)
        merge_short_parts(island_parts, config.min_part_length);
    assert(!exist_twin_change_in_part(island_parts));
#ifdef SLA_SAMPLE_ISLAND_UTILS_DEBUG_PARTS_PATH
    draw(island_parts, {}, {}, lines);
#endif // SLA_SAMPLE_ISLAND_UTILS_DEBUG_PARTS_PATH
    return convert_island_parts_to_thin_thick(island_parts, path);
}

/// <summary>
/// create points on both ends of path with side distance from border
/// </summary>
/// <param name="path">Longest path over island.</param>
/// <param name="lines">Source lines for VG --> outline of island.</param>
/// <param name="width">Wanted width on position</param>
/// <param name="max_side_distance">Maximal distance from side</param>
/// <returns>2x Static Support point(lay os sides of path)</returns>
SupportIslandPoints create_side_points(
    const VoronoiGraph::ExPath &path, const Lines& lines, const SampleConfig &config,
    SupportIslandPoint::Type type = SupportIslandPoint::Type::two_points)
{
    coord_t max_distance_by_length = static_cast<coord_t>(path.length * config.max_length_ratio_for_two_support_points);
    coord_t max_distance = std::min(config.maximal_distance_from_outline, max_distance_by_length);

    VoronoiGraph::Nodes reverse_path = path.nodes; // copy
    std::reverse(reverse_path.begin(), reverse_path.end());

    coord_t width = 2 * config.head_radius;
    coord_t side_distance1 = max_distance; // copy
    coord_t side_distance2 = max_distance; // copy
    auto pos1 = create_position_on_path(path.nodes, lines, width, side_distance1);
    auto pos2 = create_position_on_path(reverse_path, lines, width, side_distance2);
    assert(pos1.has_value());
    assert(pos2.has_value());
    SupportIslandPoints result;
    result.reserve(2);
    result.push_back(create_no_move_point(*pos1, type));
    result.push_back(create_no_move_point(*pos2, type));
    return result;
}

void draw(SVG &svg, const SupportIslandPoints &supportIslandPoints, coord_t radius, bool write_type) {
    const char *color = nullptr;
    for (const auto &p : supportIslandPoints) {
        switch (p->type) {
        case SupportIslandPoint::Type::thin_part:
        case SupportIslandPoint::Type::thin_part_change:
        case SupportIslandPoint::Type::thin_part_loop: color = "lightred"; break;
        case SupportIslandPoint::Type::thick_part_outline: color = "lightblue"; break;
        case SupportIslandPoint::Type::thick_part_inner: color = "lightgreen"; break;
        case SupportIslandPoint::Type::one_bb_center_point: color = "red"; break;
        case SupportIslandPoint::Type::one_center_point:
        case SupportIslandPoint::Type::two_points:
        case SupportIslandPoint::Type::two_points_backup:
        default: color = "black";
        }
        svg.draw(p->point, color, radius);
        if (write_type && p->type != SupportIslandPoint::Type::undefined) {
            auto type_name = SupportIslandPoint::to_string(p->type);
            Point start = p->point + Point(radius, 0);
            svg.draw_text(start, std::string(type_name).c_str(), color, 8);
        }
    }
}
} // namespace

//////////////////////////////
/// uniform support island ///
//////////////////////////////
namespace Slic3r::sla {
SupportIslandPoints uniform_support_island(
    const ExPolygon &island, const Points& permanent, const SampleConfig &config){
    ExPolygon simplified_island = get_simplified(island, config);
#ifdef OPTION_TO_STORE_ISLAND
    std::string path;
    if (!config.path.empty()) {
        static int counter = 0;
        path = replace_first(config.path, "<<order>>", std::to_string(++counter));
        draw_island(path, island, simplified_island);
        // need to save svg in case of infinite loop so no store SVG into variable
    }
#endif // OPTION_TO_STORE_ISLAND

    // 0) When island is smaller than minimal-head diameter,
    // it will be supported whole by support poin in center  
    if (Point center; get_center(simplified_island.contour.points, config.head_radius, center)) {
        SupportIslandPoints supports;
        supports.push_back(std::make_unique<SupportIslandNoMovePoint>(
            center, SupportIslandInnerPoint::Type::one_bb_center_point));
#ifdef OPTION_TO_STORE_ISLAND
        if (!path.empty()){ // add center support point into image
            SVG svg = draw_island(path, island, simplified_island);
            svg.draw_text(Point{0, 0}, "one center support point", "black");
            draw(svg, supports, config.head_radius);
        }
#endif // OPTION_TO_STORE_ISLAND
        return supports;
    }

    Geometry::VoronoiDiagram vd;
    Lines lines = to_lines(simplified_island);
    vd.construct_voronoi(lines.begin(), lines.end());
    assert(vd.get_issue_type() == Geometry::VoronoiDiagram::IssueType::NO_ISSUE_DETECTED);
    if (vd.get_issue_type() != Geometry::VoronoiDiagram::IssueType::NO_ISSUE_DETECTED) {
        // error state suppport island by one point
        Point center = BoundingBox{island.contour.points}.center();
        SupportIslandPoints supports;
        supports.push_back(std::make_unique<SupportIslandNoMovePoint>(
            center, SupportIslandInnerPoint::Type::bad_shape_for_vd));
#ifdef OPTION_TO_STORE_ISLAND
        if (!path.empty()) { // add center support point into image
            SVG svg = draw_island(path, island, simplified_island);
            svg.draw_text(Point{0, 0}, "Can't create Voronoi Diagram for the shape", "red");
            draw(svg, supports, config.head_radius);
        }
#endif // OPTION_TO_STORE_ISLAND
        return supports;
    }
    Voronoi::annotate_inside_outside(vd, lines);
    VoronoiGraph skeleton = VoronoiGraphUtils::create_skeleton(vd, lines);
    VoronoiGraph::ExPath longest_path;

    const VoronoiGraph::Node *start_node = VoronoiGraphUtils::getFirstContourNode(skeleton);
    // every island has to have a point on contour
    assert(start_node != nullptr);
    longest_path = VoronoiGraphUtils::create_longest_path(start_node);

#ifdef OPTION_TO_STORE_ISLAND // add voronoi diagram with longest path into image
    if (!path.empty()) draw_island_graph(path, island, simplified_island, skeleton, longest_path, lines, config);
#endif // OPTION_TO_STORE_ISLAND

    // 1) One support point
    if (longest_path.length < config.max_length_for_one_support_point) {
        // create only one point in center
        SupportIslandPoints supports;
        supports.push_back(create_middle_path_point(
            longest_path, SupportIslandPoint::Type::one_center_point));
#ifdef OPTION_TO_STORE_ISLAND
        if (!path.empty()){
            SVG svg = draw_island(path, island, simplified_island);
            draw(svg, supports, config.head_radius);
        }
#endif // OPTION_TO_STORE_ISLAND
        return supports;
    }

    // 2) Two support points have to stretch island even if haed is not fully under island.
    if (VoronoiGraphUtils::get_max_width(longest_path) < config.thin_max_width &&
        longest_path.length < config.max_length_for_two_support_points) {        
        SupportIslandPoints supports = create_side_points(longest_path, lines, config);        
#ifdef OPTION_TO_STORE_ISLAND
        if (!path.empty()){
            SVG svg = draw_island(path, island, simplified_island);
            draw(svg, supports, config.head_radius);
        }
#endif // OPTION_TO_STORE_ISLAND
        return supports;
    }

    // TODO: 3) Triangle aligned support points
    // eval outline and find three point create almost equilateral triangle to stretch island

    // 4) Divide island on Thin & Thick part and support by parts
    SupportIslandPoints supports;
    auto [thin, thick] = separate_thin_thick(longest_path, lines, config);
    assert(!thin.empty() || !thick.empty());
    for (const ThinPart &part : thin) create_supports_for_thin_part(part, supports, config);
    for (const ThickPart &part : thick) create_supports_for_thick_part(part, supports, lines, config);

    // At least 2 support points are neccessary after thin/thick sampling heuristic
    if (supports.size() <= 2){
        SupportIslandInnerPoint::Type type = SupportIslandInnerPoint::Type::two_points_backup;
        SupportIslandPoints two_supports = create_side_points(longest_path, lines, config, type);
#ifdef OPTION_TO_STORE_ISLAND
        if (!path.empty()) {
            SVG svg = draw_island(path, island, simplified_island);
            draw(svg, two_supports, config.head_radius);
        }
#endif // OPTION_TO_STORE_ISLAND
        return two_supports;
    }

#ifdef OPTION_TO_STORE_ISLAND
    Points supports_before_align = ::to_points(supports);
    if (!path.empty()) {
        SVG svg = draw_island_graph(path, island, simplified_island, skeleton, longest_path, lines, config);
        draw(svg, supports, config.head_radius);
    }
#endif // OPTION_TO_STORE_ISLAND

    // allign samples
    if (permanent.empty())
        align_samples(supports, island, config);
    else 
        align_samples_with_permanent(supports, island, permanent, config);

#ifdef OPTION_TO_STORE_ISLAND
    if (!path.empty()) {
        SVG svg = draw_island(path, island, simplified_island);
        coord_t width = config.head_radius / 5;
        VoronoiGraphUtils::draw(svg, longest_path.nodes, width, "darkorange");
        VoronoiGraphUtils::draw(svg, skeleton, lines, config, false /*print Pointer address*/);
        
        Lines align_moves;
        align_moves.reserve(supports.size());
        for (size_t i = 0; i < supports.size(); ++i)
            align_moves.push_back(Line(supports[i]->point, supports_before_align[i]));
        svg.draw(align_moves, "lightgray", width);
        draw(svg, supports, config.head_radius);
    }
#endif // OPTION_TO_STORE_ISLAND
    return supports;
}

// Follow implementation "create_supports_for_thick_part("
SupportIslandPoints uniform_support_peninsula(
    const Peninsula &peninsula, const Points& permanent, const SampleConfig &config){
    // create_peninsula_field
    float delta = static_cast<float>(config.minimal_distance_from_outline);
    Field field = create_field(peninsula.unsuported_area, delta, peninsula.is_outline);
    assert(!field.inner.empty());
    if (field.inner.empty())
        return {}; // no inner part

#ifdef SLA_SAMPLE_ISLAND_UTILS_STORE_PENINSULA_FIELD_TO_SVG_PATH
    {
        Lines lines = to_lines(peninsula.unsuported_area);
        const char *source_line_color = "black";
        bool draw_source_line_indexes = true;
        bool draw_border_line_indexes = false;
        bool draw_field_source_indexes = true;
        static int counter = 0;
        SVG svg(replace_first(SLA_SAMPLE_ISLAND_UTILS_STORE_PENINSULA_FIELD_TO_SVG_PATH,
            "<<COUNTER>>", std::to_string(counter++)).c_str(),
                LineUtils::create_bounding_box(lines));
        LineUtils::draw(svg, lines, source_line_color, 0., draw_source_line_indexes);
        draw(svg, field, peninsula.unsuported_area, draw_border_line_indexes, draw_field_source_indexes);
    }
#endif // SLA_SAMPLE_ISLAND_UTILS_STORE_PENINSULA_FIELD_TO_SVG_PATH

    SupportIslandPoints results = sample_outline(field, config);
    // Inner must survive after sample field for aligning supports(move along outline)
    auto inner = std::make_shared<ExPolygons>(field.inner);    
    Points inner_points = sample_expolygons_with_centering(*inner, config.thick_inner_max_distance);    
    std::transform(inner_points.begin(), inner_points.end(), std::back_inserter(results), 
        [&inner](const Point &point) { return std::make_unique<SupportIslandInnerPoint>(
                                      point, inner, SupportIslandPoint::Type::thick_part_inner);});
    
    // allign samples
    if (permanent.empty())
        align_samples(results, peninsula.unsuported_area, config);
    else
        align_samples_with_permanent(results, peninsula.unsuported_area, permanent, config);
    return results;
}

bool is_uniform_support_island_visualization_disabled() {
#ifndef NDEBUG
    return false;
#endif
#ifdef SLA_SAMPLE_ISLAND_UTILS_STORE_FIELD_TO_SVG_PATH
    return false;
#endif
#ifdef SLA_SAMPLE_ISLAND_UTILS_STORE_PENINSULA_FIELD_TO_SVG_PATH
    return false;
#endif
#ifdef SLA_SAMPLE_ISLAND_UTILS_STORE_ALIGN_ONCE_TO_SVG_PATH
    return false;
#endif
#ifdef SLA_SAMPLE_ISLAND_UTILS_STORE_ALIGNED_TO_SVG_PATH
    return false;
#endif
#ifdef SLA_SAMPLE_ISLAND_UTILS_DEBUG_PARTS_PATH
    return false;
#endif    
    return true;
}

} // namespace Slic3r::sla
