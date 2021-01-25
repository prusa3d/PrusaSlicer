#include "BoundingBox.hpp"
#include "ExPolygon.hpp"

#include "Exception.hpp"
#include "MedialAxis.hpp"
#include "Geometry.hpp"
#include "Polygon.hpp"
#include "Line.hpp"
#include "ClipperUtils.hpp"
#include "SVG.hpp"
#include "polypartition.h"
#include "poly2tri/poly2tri.h"
#include <algorithm>
#include <cassert>
#include <list>

namespace Slic3r {

ExPolygon::operator Points() const
{
    Points points;
    Polygons pp = *this;
    for (Polygons::const_iterator poly = pp.begin(); poly != pp.end(); ++poly) {
        for (Points::const_iterator point = poly->points.begin(); point != poly->points.end(); ++point)
            points.push_back(*point);
    }
    return points;
}

ExPolygon::operator Polygons() const
{
    return to_polygons(*this);
}

ExPolygon::operator Polylines() const
{
    return to_polylines(*this);
}

void ExPolygon::scale(double factor)
{
    contour.scale(factor);
    for (Polygon &hole : holes)
        hole.scale(factor);
}

void ExPolygon::translate(const Point &p)
{
    contour.translate(p);
    for (Polygon &hole : holes)
        hole.translate(p);
}

void ExPolygon::rotate(double angle)
{
    contour.rotate(angle);
    for (Polygon &hole : holes)
        hole.rotate(angle);
}

void ExPolygon::rotate(double angle, const Point &center)
{
    contour.rotate(angle, center);
    for (Polygon &hole : holes)
        hole.rotate(angle, center);
}

double ExPolygon::area() const
{
    double a = this->contour.area();
    for (const Polygon &hole : holes)
        a -= - hole.area();  // holes have negative area
    return a;
}

bool ExPolygon::is_valid() const
{
    if (!this->contour.is_valid() || !this->contour.is_counter_clockwise()) return false;
    for (Polygons::const_iterator it = this->holes.begin(); it != this->holes.end(); ++it) {
        if (!(*it).is_valid() || (*it).is_counter_clockwise()) return false;
    }
    return true;
}

bool ExPolygon::contains(const Line &line) const
{
    return this->contains(Polyline(line.a, line.b));
}

bool ExPolygon::contains(const Polyline &polyline) const
{
    return diff_pl((Polylines)polyline, *this).empty();
}

bool ExPolygon::contains(const Polylines &polylines) const
{
    #if 0
    BoundingBox bbox = get_extents(polylines);
    bbox.merge(get_extents(*this));
    SVG svg(debug_out_path("ExPolygon_contains.svg"), bbox);
    svg.draw(*this);
    svg.draw_outline(*this);
    svg.draw(polylines, "blue");
    #endif
    Polylines pl_out = diff_pl(polylines, *this);
    #if 0
    svg.draw(pl_out, "red");
    #endif
    return pl_out.empty();
}

bool ExPolygon::contains(const Point &point) const
{
    if (!this->contour.contains(point)) return false;
    for (Polygons::const_iterator it = this->holes.begin(); it != this->holes.end(); ++it) {
        if (it->contains(point)) return false;
    }
    return true;
}

// inclusive version of contains() that also checks whether point is on boundaries
bool ExPolygon::contains_b(const Point &point) const
{
    return this->contains(point) || this->has_boundary_point(point);
}

bool
ExPolygon::has_boundary_point(const Point &point) const
{
    if (this->contour.has_boundary_point(point)) return true;
    for (Polygons::const_iterator h = this->holes.begin(); h != this->holes.end(); ++h) {
        if (h->has_boundary_point(point)) return true;
    }
    return false;
}

bool ExPolygon::overlaps(const ExPolygon &other) const
{
    #if 0
    BoundingBox bbox = get_extents(other);
    bbox.merge(get_extents(*this));
    static int iRun = 0;
    SVG svg(debug_out_path("ExPolygon_overlaps-%d.svg", iRun ++), bbox);
    svg.draw(*this);
    svg.draw_outline(*this);
    svg.draw_outline(other, "blue");
    #endif
    Polylines pl_out = intersection_pl((Polylines)other, *this);
    #if 0
    svg.draw(pl_out, "red");
    #endif
    if (! pl_out.empty())
        return true; 
    //FIXME ExPolygon::overlaps() shall be commutative, it is not!
    return ! other.contour.points.empty() && this->contains_b(other.contour.points.front());
}

void
ExPolygon::simplify_p(double tolerance, Polygons* polygons) const
{
    Polygons pp = this->simplify_p(tolerance);
    polygons->insert(polygons->end(), pp.begin(), pp.end());
}

Polygons
ExPolygon::simplify_p(double tolerance) const
{
    Polygons pp;
    pp.reserve(this->holes.size() + 1);
    // contour
    {
        Polygon p = this->contour;
        p.points.push_back(p.points.front());
        p.points = MultiPoint::_douglas_peucker(p.points, tolerance);
        p.points.pop_back();
        pp.emplace_back(std::move(p));
    }
    // holes
    for (Polygon p : this->holes) {
        p.points.push_back(p.points.front());
        p.points = MultiPoint::_douglas_peucker(p.points, tolerance);
        p.points.pop_back();
        pp.emplace_back(std::move(p));
    }
    return simplify_polygons(pp);
}

ExPolygons
ExPolygon::simplify(double tolerance) const
{
    return union_ex(this->simplify_p(tolerance));
}

void
ExPolygon::simplify(double tolerance, ExPolygons* expolygons) const
{
    append(*expolygons, this->simplify(tolerance));
}

/// remove point that are at SCALED_EPSILON * 2 distance.
//simplier than simplify
void
ExPolygon::remove_point_too_near(const coord_t tolerance) {
    const double tolerance_sq = tolerance * (double)tolerance;
    size_t id = 1;
    while (id < this->contour.points.size() - 1) {
        coord_t newdist = (coord_t)std::min(this->contour.points[id].distance_to_square(this->contour.points[id - 1])
            , this->contour.points[id].distance_to_square(this->contour.points[id + 1]));
        if (newdist < tolerance_sq) {
            this->contour.points.erase(this->contour.points.begin() + id);
            newdist = (coord_t)this->contour.points[id].distance_to_square(this->contour.points[id - 1]);
        }
        //go to next one
        //if you removed a point, it check if the next one isn't too near from the previous one.
        // if not, it byepass it.
        if (newdist > tolerance_sq) {
            ++id;
        }
    }
    if (this->contour.points.front().distance_to_square(this->contour.points.back()) < tolerance_sq) {
        this->contour.points.erase(this->contour.points.end() -1);
    }
}

void
ExPolygon::medial_axis(double max_width, double min_width, Polylines* polylines) const
{
    ThickPolylines tp;
    MedialAxis{ *this, coord_t(max_width), coord_t(min_width), coord_t(max_width / 2.0) }.build(tp);
    polylines->insert(polylines->end(), tp.begin(), tp.end());
}

/*
void ExPolygon::get_trapezoids(Polygons* polygons) const
{
    ExPolygons expp;
    expp.push_back(*this);
    boost::polygon::get_trapezoids(*polygons, expp);
}

void ExPolygon::get_trapezoids(Polygons* polygons, double angle) const
{
    ExPolygon clone = *this;
    clone.rotate(PI/2 - angle, Point(0,0));
    clone.get_trapezoids(polygons);
    for (Polygons::iterator polygon = polygons->begin(); polygon != polygons->end(); ++polygon)
        polygon->rotate(-(PI/2 - angle), Point(0,0));
}
*/

// This algorithm may return more trapezoids than necessary
// (i.e. it may break a single trapezoid in several because
// other parts of the object have x coordinates in the middle)
void ExPolygon::get_trapezoids2(Polygons* polygons) const
{
    // get all points of this ExPolygon
    Points pp = *this;
    
    // build our bounding box
    BoundingBox bb(pp);
    
    // get all x coordinates
    std::vector<coord_t> xx;
    xx.reserve(pp.size());
    for (Points::const_iterator p = pp.begin(); p != pp.end(); ++p)
        xx.push_back(p->x());
    std::sort(xx.begin(), xx.end());
    
    // find trapezoids by looping from first to next-to-last coordinate
    for (std::vector<coord_t>::const_iterator x = xx.begin(); x != xx.end()-1; ++x) {
        coord_t next_x = *(x + 1);
        if (*x != next_x)
            // intersect with rectangle
            // append results to return value
            polygons_append(*polygons, intersection({ { { *x, bb.min.y() }, { next_x, bb.min.y() }, { next_x, bb.max.y() }, { *x, bb.max.y() } } }, to_polygons(*this)));
    }
}

void ExPolygon::get_trapezoids2(Polygons* polygons, double angle) const
{
    ExPolygon clone = *this;
    clone.rotate(PI / 2 - angle, Point(0, 0));
    clone.get_trapezoids2(polygons);
    for (Polygons::iterator polygon = polygons->begin(); polygon != polygons->end(); ++polygon)
        polygon->rotate(-(PI / 2 - angle), Point(0, 0));
}

void
ExPolygon::get_trapezoids3_half(Polygons* polygons, float spacing) const {

    // get all points of this ExPolygon
    Points pp = *this;

    if (pp.empty()) return;

    // build our bounding box
    BoundingBox bb(pp);

    // get all x coordinates
    coord_t min_x = pp[0].x(), max_x = pp[0].x();
    std::vector<coord_t> xx;
    for (Points::const_iterator p = pp.begin(); p != pp.end(); ++p) {
        if (min_x > p->x()) min_x = p->x();
        if (max_x < p->x()) max_x = p->x();
    }
    for (coord_t x = min_x; x < max_x - (coord_t)(spacing / 2); x += (coord_t)spacing) {
        xx.push_back(x);
    }
    xx.push_back(max_x);
    //std::sort(xx.begin(), xx.end());

    // find trapezoids by looping from first to next-to-last coordinate
    for (std::vector<coord_t>::const_iterator x = xx.begin(); x != xx.end() - 1; ++x) {
        coord_t next_x = *(x + 1);
        if (*x == next_x) continue;

        // build rectangle
        Polygon poly;
        poly.points.resize(4);
        poly[0].x() = *x + (coord_t)spacing / 4;
        poly[0].y() = bb.min(1);
        poly[1].x() = next_x - (coord_t)spacing / 4;
        poly[1].y() = bb.min(1);
        poly[2].x() = next_x - (coord_t)spacing / 4;
        poly[2].y() = bb.max(1);
        poly[3].x() = *x + (coord_t)spacing / 4;
        poly[3].y() = bb.max(1);

        // intersect with this expolygon
        // append results to return value
        polygons_append(*polygons, intersection(Polygons{ poly }, to_polygons(*this)));
    }
}

// While this triangulates successfully, it's NOT a constrained triangulation
// as it will create more vertices on the boundaries than the ones supplied.
void ExPolygon::triangulate(Polygons* polygons) const
{
    // first make trapezoids
    Polygons trapezoids;
    this->get_trapezoids2(&trapezoids);
    
    // then triangulate each trapezoid
    for (Polygons::iterator polygon = trapezoids.begin(); polygon != trapezoids.end(); ++polygon)
        polygon->triangulate_convex(polygons);
}

/*
void ExPolygon::triangulate_pp(Polygons* polygons) const
{
    // convert polygons
    std::list<TPPLPoly> input;
    
    ExPolygons expp = union_ex(simplify_polygons(to_polygons(*this), true));
    
    for (ExPolygons::const_iterator ex = expp.begin(); ex != expp.end(); ++ex) {
        // contour
        {
            TPPLPoly p;
            p.Init(int(ex->contour.points.size()));
            //printf("%zu\n0\n", ex->contour.points.size());
            for (const Point &point : ex->contour.points) {
                size_t i = &point - &ex->contour.points.front();
                p[i].x = point(0);
                p[i].y = point(1);
                //printf("%ld %ld\n", point->x(), point->y());
            }
            p.SetHole(false);
            input.push_back(p);
        }
    
        // holes
        for (Polygons::const_iterator hole = ex->holes.begin(); hole != ex->holes.end(); ++hole) {
            TPPLPoly p;
            p.Init(hole->points.size());
            //printf("%zu\n1\n", hole->points.size());
            for (const Point &point : hole->points) {
                size_t i = &point - &hole->points.front();
                p[i].x = point(0);
                p[i].y = point(1);
                //printf("%ld %ld\n", point->x(), point->y());
            }
            p.SetHole(true);
            input.push_back(p);
        }
    }
    
    // perform triangulation
    std::list<TPPLPoly> output;
    int res = TPPLPartition().Triangulate_MONO(&input, &output);
    if (res != 1)
        throw Slic3r::RuntimeError("Triangulation failed");
    
    // convert output polygons
    for (std::list<TPPLPoly>::iterator poly = output.begin(); poly != output.end(); ++poly) {
        long num_points = poly->GetNumPoints();
        Polygon p;
        p.points.resize(num_points);
        for (long i = 0; i < num_points; ++i) {
            p.points[i](0) = coord_t((*poly)[i].x);
            p.points[i](1) = coord_t((*poly)[i].y);
        }
        polygons->push_back(p);
    }
}
*/

std::list<TPPLPoly> expoly_to_polypartition_input(const ExPolygon &ex)
{
	std::list<TPPLPoly> input;
	// contour
	{
		input.emplace_back();
		TPPLPoly &p = input.back();
		p.Init(int(ex.contour.points.size()));
		for (const Point &point : ex.contour.points) {
			size_t i = &point - &ex.contour.points.front();
			p[i].x = point.x();
			p[i].y = point.y();
		}
		p.SetHole(false);
	}
	// holes
	for (const Polygon &hole : ex.holes) {
		input.emplace_back();
		TPPLPoly &p = input.back();
		p.Init(hole.points.size());
		for (const Point &point : hole.points) {
			size_t i = &point - &hole.points.front();
			p[i].x = point(0);
			p[i].y = point(1);
		}
		p.SetHole(true);
	}
	return input;
}

std::list<TPPLPoly> expoly_to_polypartition_input(const ExPolygons &expps)
{
    std::list<TPPLPoly> input;
	for (const ExPolygon &ex : expps) {
        // contour
        {
            input.emplace_back();
            TPPLPoly &p = input.back();
            p.Init(int(ex.contour.points.size()));
            for (const Point &point : ex.contour.points) {
                size_t i = &point - &ex.contour.points.front();
                p[i].x = point(0);
                p[i].y = point(1);
            }
            p.SetHole(false);
        }
        // holes
        for (const Polygon &hole : ex.holes) {
            input.emplace_back();
            TPPLPoly &p = input.back();
            p.Init(hole.points.size());
            for (const Point &point : hole.points) {
                size_t i = &point - &hole.points.front();
                p[i].x = point(0);
                p[i].y = point(1);
            }
            p.SetHole(true);
        }
    }
    return input;
}

std::vector<Point> polypartition_output_to_triangles(const std::list<TPPLPoly> &output)
{
    size_t num_triangles = 0;
    for (const TPPLPoly &poly : output)
        if (poly.GetNumPoints() >= 3)
            num_triangles += (size_t)poly.GetNumPoints() - 2;
    std::vector<Point> triangles;
    triangles.reserve(triangles.size() + num_triangles * 3);
    for (const TPPLPoly &poly : output) {
        long num_points = poly.GetNumPoints();
        if (num_points >= 3) {
            const TPPLPoint *pt0 = &poly[0];
            const TPPLPoint *pt1 = nullptr;
            const TPPLPoint *pt2 = &poly[1];
            for (long i = 2; i < num_points; ++ i) {
                pt1 = pt2;
                pt2 = &poly[i];
                triangles.emplace_back(coord_t(pt0->x), coord_t(pt0->y));
                triangles.emplace_back(coord_t(pt1->x), coord_t(pt1->y));
                triangles.emplace_back(coord_t(pt2->x), coord_t(pt2->y));
            }
        }
    }
    return triangles;
}

void ExPolygon::triangulate_pp(Points *triangles) const
{
    ExPolygons expp = union_ex(simplify_polygons(to_polygons(*this), true));
    std::list<TPPLPoly> input = expoly_to_polypartition_input(expp);
    // perform triangulation
    std::list<TPPLPoly> output;
    int res = TPPLPartition().Triangulate_MONO(&input, &output);
// int TPPLPartition::Triangulate_EC(TPPLPolyList *inpolys, TPPLPolyList *triangles) {
    if (res != 1)
        throw Slic3r::RuntimeError("Triangulation failed");
    *triangles = polypartition_output_to_triangles(output);
}

// Uses the Poly2tri library maintained by Jan Niklas Hasse @jhasse // https://github.com/jhasse/poly2tri
// See https://github.com/jhasse/poly2tri/blob/master/README.md for the limitations of the library!
// No duplicate points are allowed, no very close points, holes must not touch outer contour etc.
void ExPolygon::triangulate_p2t(Polygons* polygons) const
{
    ExPolygons expp = simplify_polygons_ex(*this, true);
    
    for (ExPolygons::const_iterator ex = expp.begin(); ex != expp.end(); ++ex) {
        // TODO: prevent duplicate points

        // contour
        std::vector<p2t::Point*> ContourPoints;
        for (const Point &pt : ex->contour.points)
            // We should delete each p2t::Point object
            ContourPoints.push_back(new p2t::Point(double(pt.x()), double(pt.y())));
        p2t::CDT cdt(ContourPoints);

        // holes
        for (Polygons::const_iterator hole = ex->holes.begin(); hole != ex->holes.end(); ++hole) {
            std::vector<p2t::Point*> points;
            for (const Point &pt : hole->points)
                // will be destructed in SweepContext::~SweepContext
                points.push_back(new p2t::Point(double(pt.x()), double(pt.y())));
            cdt.AddHole(points);
        }
        
        // perform triangulation
        try {
        cdt.Triangulate();
        std::vector<p2t::Triangle*> triangles = cdt.GetTriangles();
        
        for (std::vector<p2t::Triangle*>::const_iterator triangle = triangles.begin(); triangle != triangles.end(); ++triangle) {
            Polygon p;
            for (int i = 0; i <= 2; ++i) {
                p2t::Point* point = (*triangle)->GetPoint(i);
                p.points.push_back(Point(point->x, point->y));
            }
            polygons->push_back(p);
        }
        } catch (const Slic3r::RuntimeError & /* err */) {
            assert(false);
            // just ignore, don't triangulate
        }

        for (p2t::Point *ptr : ContourPoints)
            delete ptr;
    }
}

Lines ExPolygon::lines() const
{
    Lines lines = this->contour.lines();
    for (Polygons::const_iterator h = this->holes.begin(); h != this->holes.end(); ++h) {
        Lines hole_lines = h->lines();
        lines.insert(lines.end(), hole_lines.begin(), hole_lines.end());
    }
    return lines;
}

BoundingBox get_extents(const ExPolygon &expolygon)
{
    return get_extents(expolygon.contour);
}

BoundingBox get_extents(const ExPolygons &expolygons)
{
    BoundingBox bbox;
    if (! expolygons.empty()) {
        for (size_t i = 0; i < expolygons.size(); ++ i)
			if (! expolygons[i].contour.points.empty())
				bbox.merge(get_extents(expolygons[i]));
    }
    return bbox;
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
            bbox.merge(get_extents_rotated(expolygons[i].contour, angle));
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
