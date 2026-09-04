#include <ankerl/unordered_dense.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <cstring>

#include "Slic3r/Biz/Algorithms/ExPolygon.hpp"
#include "Slic3r/Biz/Algorithms/DouglasPeucker.hpp"
#include "Slic3r/Biz/Algorithms/Polygon.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "ExPolygon.hpp"
#include "Geometry/MedialAxis.hpp"
#include "Polygon.hpp"
#include "Line.hpp"
#include "ClipperUtils.hpp"
#include "libslic3r/MultiPoint.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/libslic3r.h"

using namespace Slic3r::Biz;

namespace Slic3r {

namespace BB = Biz::Algorithms::BoundingBox;

bool on_boundary(const ExPolygon &expolygon, const Point &point, double eps)
{
    if (::Slic3r::polygon_on_boundary(expolygon.contour, point, eps))
        return true;
    for (const Polygon &hole : expolygon.holes)
        if (::Slic3r::polygon_on_boundary(hole, point, eps))
            return true;
    return false;
}

// Projection of a point onto the polygon.
Point point_projection(const ExPolygon &expolygon, const Point &point)
{
    if (expolygon.holes.empty()) {
        return ::Slic3r::point_projection(expolygon.contour, point);
    } else {
        double dist_min2 = std::numeric_limits<double>::max();
        Point  closest_pt_min;
        for (size_t i = 0; i < expolygon.num_contours(); ++ i) {
            Point closest_pt = ::Slic3r::point_projection(expolygon.contour_or_hole(i), point);
            double d2 = (closest_pt - point).cast<double>().squaredNorm();
            if (d2 < dist_min2) {
                dist_min2      = d2;
                closest_pt_min = closest_pt;
            }
        }
        return closest_pt_min;
    }
}

void medial_axis(const ExPolygon& expolygon, const double min_width, const double max_width, ThickPolylines* polylines)
{
    // init helper object
    Slic3r::Geometry::MedialAxis ma(min_width, max_width, expolygon);
    
    // compute the Voronoi diagram and extract medial axis polylines
    ThickPolylines pp;
    ma.build(&pp);
    
    /*
    Biz::Algorithms::SVG::SVG svg("medial_axis.svg");
    svg.draw(*this);
    svg.draw(pp);
    svg.Close();
    */
    
    /* Find the maximum width returned; we're going to use this for validating and 
       filtering the output segments. */
    double max_w = 0;
    for (ThickPolylines::const_iterator it = pp.begin(); it != pp.end(); ++it)
        max_w = fmaxf(max_w, *std::max_element(it->width.begin(), it->width.end()));
    
    /* Loop through all returned polylines in order to extend their endpoints to the 
       expolygon boundaries */
    bool removed = false;
    for (size_t i = 0; i < pp.size(); ++i) {
        ThickPolyline& polyline = pp[i];
        
        // extend initial and final segments of each polyline if they're actual endpoints
        /* We assign new endpoints to temporary variables because in case of a single-line
           polyline, after we extend the start point it will be caught by the intersection()
           call, so we keep the inner point until we perform the second intersection() as well */
        Point new_front = polyline.points.front();
        Point new_back  = polyline.points.back();
        if (polyline.endpoints.first && !Slic3r::on_boundary(expolygon, new_front, SCALED_EPSILON)) {
            Vec2d p1 = polyline.points.front().cast<double>();
            Vec2d p2 = polyline.points[1].cast<double>();
            // prevent the line from touching on the other side, otherwise intersection() might return that solution
            if (polyline.points.size() == 2)
                p2 = (p1 + p2) * 0.5;
            // Extend the start of the segment.
            p1 -= (p2 - p1).normalized() * max_width;
            if (const std::optional<Point> intersection_pt = Algorithms::Polygon::intersection(expolygon.contour, Line(p1.cast<coord_t>(), p2.cast<coord_t>())); intersection_pt.has_value()) {
                new_front = intersection_pt.value();
            }
        }
        if (polyline.endpoints.second && !Slic3r::on_boundary(expolygon, new_back, SCALED_EPSILON)) {
            Vec2d p1 = (polyline.points.end() - 2)->cast<double>();
            Vec2d p2 = polyline.points.back().cast<double>();
            // prevent the line from touching on the other side, otherwise intersection() might return that solution
            if (polyline.points.size() == 2)
                p1 = (p1 + p2) * 0.5;
            // Extend the start of the segment.
            p2 += (p2 - p1).normalized() * max_width;
            if (const std::optional<Point> intersection_pt = Algorithms::Polygon::intersection(expolygon.contour, Line(p1.cast<coord_t>(), p2.cast<coord_t>())); intersection_pt.has_value()) {
                new_back = intersection_pt.value();
            }
        }
        polyline.points.front() = new_front;
        polyline.points.back()  = new_back;
        
        /*  remove too short polylines
            (we can't do this check before endpoints extension and clipping because we don't
            know how long will the endpoints be extended since it depends on polygon thickness
            which is variable - extension will be <= max_width/2 on each side)  */
        if ((polyline.endpoints.first || polyline.endpoints.second)
            && polyline.length() < max_w*2) {
            pp.erase(pp.begin() + i);
            --i;
            removed = true;
            continue;
        }
    }
    
    /*  If we removed any short polylines we now try to connect consecutive polylines
        in order to allow loop detection. Note that this algorithm is greedier than 
        MedialAxis::process_edge_neighbors() as it will connect random pairs of 
        polylines even when more than two start from the same point. This has no 
        drawbacks since we optimize later using nearest-neighbor which would do the 
        same, but should we use a more sophisticated optimization algorithm we should
        not connect polylines when more than two meet.  */
    if (removed) {
        for (size_t i = 0; i < pp.size(); ++i) {
            ThickPolyline& polyline = pp[i];
            if (polyline.endpoints.first && polyline.endpoints.second) continue; // optimization
            
            // find another polyline starting here
            for (size_t j = i+1; j < pp.size(); ++j) {
                ThickPolyline& other = pp[j];
                if (polyline.last_point() == other.last_point()) {
                    other.reverse();
                } else if (polyline.first_point() == other.last_point()) {
                    polyline.reverse();
                    other.reverse();
                } else if (polyline.first_point() == other.first_point()) {
                    polyline.reverse();
                } else if (polyline.last_point() != other.first_point()) {
                    continue;
                }
                
                polyline.points.insert(polyline.points.end(), other.points.begin() + 1, other.points.end());
                polyline.width.insert(polyline.width.end(), other.width.begin(), other.width.end());
                polyline.endpoints.second = other.endpoints.second;
                assert(polyline.width.size() == polyline.points.size()*2 - 2);
                
                pp.erase(pp.begin() + j);
                j = i;  // restart search from i+1
            }
        }
    }
    
    polylines->insert(polylines->end(), pp.begin(), pp.end());
}

void medial_axis(const ExPolygon& expolygon, const double min_width, const double max_width, Polylines* polylines)
{
    ThickPolylines tp;
    Slic3r::medial_axis(expolygon, min_width, max_width, &tp);
    polylines->reserve(polylines->size() + tp.size());
    for (auto &pl : tp)
        polylines->emplace_back(pl.points);
}

Polylines medial_axis(const ExPolygon& expolygon, const double min_width, const double max_width)
{
    Polylines out;
    Slic3r::medial_axis(expolygon, min_width, max_width, &out);
    return out;
}

// Do expolygons match? If they match, they must have the same topology,
// however their contours may be rotated.
bool expolygons_match(const ExPolygon &l, const ExPolygon &r)
{
    if (l.holes.size() != r.holes.size() || ! polygons_match(l.contour, r.contour))
        return false;
    for (size_t hole_idx = 0; hole_idx < l.holes.size(); ++ hole_idx)
        if (! polygons_match(l.holes[hole_idx], r.holes[hole_idx]))
            return false;
    return true;
}

BoundingBox get_extents(const ExPolygon &expolygon)
{
    const auto bb{Algorithms::ExPolygon::get_extents(expolygon)};
    BoundingBox result{bb.min, bb.max};
    result.defined = bb.defined;
    return result;
}

BoundingBox get_extents(const ExPolygons &expolygons)
{
    const auto bb{Algorithms::ExPolygon::get_extents(expolygons)};
    BoundingBox result{bb.min, bb.max};
    result.defined = bb.defined;
    return result;
}

BoundingBox get_extents_rotated(const ExPolygon &expolygon, double angle)
{
    return get_extents_rotated(expolygon.contour, angle);
}

BoundingBox get_extents_rotated(const ExPolygons &expolygons, double angle)
{
    BoundingBox bbox;
    if (! expolygons.empty()) {
        bbox = get_extents_rotated(expolygons.front().contour, angle);
        for (size_t i = 1; i < expolygons.size(); ++ i)
            bbox = BB::merge(bbox, get_extents_rotated(expolygons[i].contour, angle));
    }
    return bbox;
}

extern std::vector<BoundingBox> get_extents_vector(const ExPolygons &polygons)
{
    std::vector<BoundingBox> out;
    out.reserve(polygons.size());
    for (ExPolygons::const_iterator it = polygons.begin(); it != polygons.end(); ++ it)
        out.push_back(get_extents(*it));
    return out;
}

bool remove_sticks(ExPolygon &poly)
{
    return remove_sticks(poly.contour) || remove_sticks(poly.holes);
}

void keep_largest_contour_only(ExPolygons &polygons)
{
	if (polygons.size() > 1) {
	    double     max_area = 0.;
	    ExPolygon* max_area_polygon = nullptr;
	    for (ExPolygon& p : polygons) {
	        double a = p.contour.area();
	        if (a > max_area) {
	            max_area         = a;
	            max_area_polygon = &p;
	        }
	    }
	    assert(max_area_polygon != nullptr);
	    ExPolygon p(std::move(*max_area_polygon));
	    polygons.clear();
	    polygons.emplace_back(std::move(p));
	}
}

} // namespace Slic3r
