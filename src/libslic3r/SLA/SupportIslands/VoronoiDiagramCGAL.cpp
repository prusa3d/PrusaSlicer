#include "VoronoiDiagramCGAL.hpp"

#include <iostream>

// includes for defining the Voronoi diagram adaptor
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Delaunay_triangulation_2.h>
#include <CGAL/Voronoi_diagram_2.h>
#include <CGAL/Delaunay_triangulation_adaptation_traits_2.h>
#include <CGAL/Delaunay_triangulation_adaptation_policies_2.h>

#include "libslic3r/Geometry.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/SLA/SupportIslands/LineUtils.hpp"
#include "libslic3r/SLA/SupportIslands/VoronoiGraphUtils.hpp"

using namespace Slic3r;
using namespace Slic3r::sla;

// typedefs for defining the adaptor
using K = CGAL::Exact_predicates_inexact_constructions_kernel;
using DT = CGAL::Delaunay_triangulation_2<K>;
using AT = CGAL::Delaunay_triangulation_adaptation_traits_2<DT>;
using AP = CGAL::Delaunay_triangulation_caching_degeneracy_removal_policy_2<DT>;
using VD = CGAL::Voronoi_diagram_2<DT, AT, AP>;

// typedef for the result type of the point location
using Site_2 = AT::Site_2;
using Point_2 = AT::Point_2;
using Locate_result = VD::Locate_result;
using Vertex_handle = VD::Vertex_handle;
using Face_handle = VD::Face_handle;
using Halfedge_handle = VD::Halfedge_handle;
using Ccb_halfedge_circulator = VD::Ccb_halfedge_circulator;

namespace{
// conversion from double to coor_t
Slic3r::Point to_point(const Site_2 &s) { return Slic3r::Point(s.x(), s.y()); }

/// <summary>
/// Create line segment lay between given points with distance limited by maximal_distance
/// </summary>
/// <param name="point1"></param>
/// <param name="point2"></param>
/// <param name="maximal_distance">limits for result segment</param>
/// <returns>line perpendicular to line between point1 and point2</returns>
Slic3r::Line create_line_between_points(
    const Point &point1, const Point &point2, double maximal_distance
) {
    Point middle = point1/2 + point2/2;
    Point diff = point1 - point2; // direction from point2 to point1
    coord_t manhatn_distance = abs(diff.x()) + abs(diff.y());
    // alligned points should not be too close
    assert(manhatn_distance >= 1);
    // it is not neccesary to know exact distance 
    // One need to know minimal distance between points.
    // worst case is diagonal: sqrt(2*(0.5 * manhatn_distance)^2) = 
    double min_distance = manhatn_distance * .7; // 1 / sqrt(2)
    double scale = maximal_distance / min_distance;
    Point side_dir(-diff.y() * scale, diff.x() * scale);
    return Line(middle - side_dir, middle + side_dir);
}

/// <summary>
/// Crop line which is no too far away(compare to maximal_distance) from v1(or v2)
/// </summary>
/// <param name="a"></param>
/// <param name="b"></param>
/// <param name="v1"></param>
/// <param name="v2"></param>
/// <param name="maximal_distance"></param>
/// <returns></returns>
std::optional<Slic3r::Line> crop_line(
    const Point_2 &a, const Point_2 &b, 
    const Point &v1, const Point &v2, double maximal_distance
) {
    Point diff = v1 - v2;
    Point dir(-diff.y(), diff.x());
    coord_t abs_x = abs(dir.x());
    coord_t abs_y = abs(dir.y());
    Point middle = v1 / 2 + v2 / 2;

    double a_t, b_t; // [units is size of dir]
    if (abs_x > abs_y) {
        a_t = (a.x() - middle.x()) / (double) dir.x();
        b_t = (b.x() - middle.x()) / (double) dir.x();
    } else {
        a_t = (a.y() - middle.y()) / (double) dir.y();
        b_t = (b.y() - middle.y()) / (double) dir.y();
    }

    // calculate approx distance from middle point to detect too far from middle
    coord_t manhatn_distance = abs_x + abs_y;
    // alligned points should not be too close
    assert(manhatn_distance >= 1);
    double min_distance = manhatn_distance * .7;
    double a_dist = a_t * min_distance;
    double b_dist = b_t * min_distance;

    double scale = maximal_distance / min_distance;
    Line l(to_point(a), to_point(b));
    if (a_dist > maximal_distance) {
        if (b_dist > maximal_distance)
            return {}; // out of range
        l.a = middle + (scale * dir.cast<double>()).cast<coord_t>();
    } else if (a_dist < -maximal_distance) {
        if (b_dist < -maximal_distance)
            return {}; // out of range
        l.a = middle - (scale * dir.cast<double>()).cast<coord_t>();
    }
    if (b_dist > maximal_distance) {
        l.b = middle + (scale * dir.cast<double>()).cast<coord_t>();
    } else if (b_dist < -maximal_distance)
        l.b = middle - (scale * dir.cast<double>()).cast<coord_t>();
    return l;
}

/// <summary>
/// Crop ray to line which is no too far away(compare to maximal_distance) from v1(or v2)
/// </summary>
/// <param name="p">ray start</param>
/// <param name="v1"></param>
/// <param name="v2"></param>
/// <param name="maximal_distance">Limits for line</param>
/// <returns></returns>
std::optional<Slic3r::Line> crop_ray(const Point_2 &ray_point, const Point &v1, const Point &v2, double maximal_distance) {
    assert(maximal_distance > 0);
    Point diff = v2 - v1;
    Point ray_dir(-diff.y(), diff.x());

    // bounds are similar as for line between points
    Point middle = v1/2 + v2/2;
    coord_t abs_x = abs(ray_dir.x());
    coord_t abs_y = abs(ray_dir.y());
    coord_t manhatn_dist = abs_x + abs_y; // maximal distance

    // alligned points should not be too close
    assert(manhatn_dist >= 1);
        
    double min_distance = manhatn_dist * .7;
    assert(min_distance > 0);
    
    // count of dir from ray point to middle
    double middle_t = (abs_x > abs_y) ?
        // use x coor
        (middle.x() - ray_point.x()) / (double) ray_dir.x() :
        // use y coor
        (middle.y() - ray_point.y()) / (double) ray_dir.y();
    
    // minimal distance from ray point to middle point
    double min_middle_dist = middle_t * min_distance;
    if (min_middle_dist < -maximal_distance)
        // ray start out of area of interest
        return {};

    double scale = maximal_distance / min_distance;
    Point side_dir = (ray_dir.cast<double>() * scale).cast<coord_t>();
    return Line(min_middle_dist > maximal_distance? 
            (middle - side_dir) : to_point(ray_point), 
        middle + side_dir);
}

std::optional<Slic3r::Line> to_line(
    const Halfedge_handle &edge,
    double maximal_distance
) {
    // validation slow down a lot, Never appear during algorithm tunning
    assert(edge->is_valid());
    //if (!edge->is_valid()) return {}; 

    if (edge->has_source()) {
        // source point of edge
        if (edge->has_target()) { // Line segment
            assert(edge->is_segment());
            return crop_line(
                edge->source()->point(),
                edge->target()->point(),
                to_point(edge->up()->point()),
                to_point(edge->down()->point()),
                maximal_distance);
        } else { // ray from source
            assert(edge->is_ray());
            return crop_ray(
                edge->source()->point(), 
                to_point(edge->up()->point()),
                to_point(edge->down()->point()),
                maximal_distance);
        }
    } else if (edge->has_target()) { // ray from target
        assert(edge->is_ray());
        return crop_ray(
            edge->target()->point(), 
            to_point(edge->down()->point()), 
            to_point(edge->up()->point()),
            maximal_distance);
    } 
    // infinite line between points
    assert(edge->is_bisector());
    return create_line_between_points(
        to_point(edge->up()->point()), 
        to_point(edge->down()->point()),
        maximal_distance
    );
}

} // namespace

Polygons Slic3r::sla::create_voronoi_cells_cgal(const Points &points, coord_t max_distance) {
    assert(points.size() > 1);

    // Different way to fill points into VD
    // delaunary triangulation
    std::vector<DT::Point> dt_points;
    dt_points.reserve(points.size());
    for (const Point &p : points)
        dt_points.emplace_back(p.x(), p.y());
    DT dt(dt_points.begin(), dt_points.end());
    assert(dt.is_valid());
    VD vd(dt);
    assert(vd.is_valid());

    // voronoi diagram seems that points face order is same as inserted points
    //VD vd;
    //for (const Point& p: points) {
    //    Site_2 t(p.x(), p.y());        
    //    vd.insert(t);
    //}

    Polygons cells(points.size());
    size_t fit_index = 0;
    // Loop over the faces of the Voronoi diagram in order of given points
    for (VD::Face_iterator fit = vd.faces_begin(); fit != vd.faces_end(); ++fit, ++fit_index) {
        // get source point index
        // TODO: do it without search !!!
        Point_2 source_point = fit->dual()->point();
        Slic3r::Point source_pt(source_point.x(), source_point.y());
        auto it = std::find(points.begin(), points.end(), source_pt);
        assert(it != points.end());
        if (it == points.end())
            continue;
        size_t index = it - points.begin();
        assert(source_pt.x() == points[index].x());
        assert(source_pt.y() == points[index].y());

        // origin of voronoi face 
        const Point& origin = points[index];
        Lines lines;
        // collect croped lines of field
        Ccb_halfedge_circulator ec_start = fit->ccb();
        Ccb_halfedge_circulator ec = ec_start;
        do {
            assert(ec->is_valid());
            std::optional<Slic3r::Line> line_opt = to_line(ec, max_distance);
            if (!line_opt.has_value())
                continue;
            Line &line = *line_opt;
            Geometry::Orientation orientation = Geometry::orient(origin, line.a, line.b);
            // Can be rich on circle over source point edge
            if (orientation == Geometry::Orientation::ORIENTATION_COLINEAR) continue;
            if (orientation == Geometry::Orientation::ORIENTATION_CW) std::swap(line.a, line.b);
            lines.push_back(std::move(line));
        } while (++ec != ec_start);
        assert(!lines.empty());
        if (lines.size() > 1)
            LineUtils::sort_CCW(lines, origin);


        //Slic3r::SVG
        //    svg("C:/data/temp/CGAL_VD_face.svg",
        //        {origin - Point(max_distance, max_distance),
        //         origin + Point(max_distance, max_distance)});
        //svg.draw(lines, "green");
        //do {
        //    if (ec->has_source())
        //        svg.draw(to_point(ec->source()->point()));
        //    if (ec->has_target())
        //        svg.draw(to_point(ec->target()->point()));
        //    if (ec->is_segment())
        //        svg.draw(Line(to_point(ec->source()->point()), to_point(ec->target()->point())));
        //} while (++ec != ec_start);
        //svg.draw(origin, "red");
        //svg.Close();

        // preccission to decide when not connect neighbor points
        double min_distance = max_distance / 1000.;
        size_t count_point = 6; // count added points
        // cell for current processed face
        cells[index] =
            VoronoiGraphUtils::to_polygon(lines, origin, max_distance, min_distance, count_point);
    }
    // Point_2 p;
    // Locate_result lr = vd.locate(p); // Could locate face of VD - potentionaly could iterate input points
    return cells;
}