#include "MedialAxis.hpp"
#include "BoundingBox.hpp"
#include "ExPolygon.hpp"
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
    int count_error = 0;
    
void
MedialAxis::build(Polylines &polylines)
{
    ThickPolylines tp;
    this->build(tp);
    polylines.insert(polylines.end(), tp.begin(), tp.end());
}

void
MedialAxis::polyline_from_voronoi(const Lines& voronoi_edges, ThickPolylines* polylines)
{
    this->lines = voronoi_edges;
    construct_voronoi(lines.begin(), lines.end(), &this->vd);

    typedef const VD::edge_type   edge_t;
    
    // DEBUG: dump all Voronoi edges
    /*{
        for (VD::const_edge_iterator edge = this->vd.edges().begin(); edge != this->vd.edges().end(); ++edge) {
            if (edge->is_infinite()) continue;
            const edge_t* edgeptr = &*edge;
            ThickPolyline polyline;
            polyline.points.push_back(Point( edge->vertex0()->x(), edge->vertex0()->y() ));
            polyline.points.push_back(Point( edge->vertex1()->x(), edge->vertex1()->y() ));
            polyline.width.push_back(this->thickness[edgeptr].first);
            polyline.width.push_back(this->thickness[edgeptr].second);
            polylines->push_back(polyline);
        }
        return;
    }*/
    
    
    
    // collect valid edges (i.e. prune those not belonging to MAT)
    // note: this keeps twins, so it inserts twice the number of the valid edges
    this->valid_edges.clear();
    {
        std::set<const edge_t*> seen_edges;
        for (VD::const_edge_iterator edge = this->vd.edges().begin(); edge != this->vd.edges().end(); ++edge) {
            // if we only process segments representing closed loops, none if the
            // infinite edges (if any) would be part of our MAT anyway
            if (edge->is_secondary() || edge->is_infinite()) continue;
        
            // don't re-validate twins
            if (seen_edges.find(&*edge) != seen_edges.end()) continue;  // TODO: is this needed?
            seen_edges.insert(&*edge);
            seen_edges.insert(edge->twin());
            
            if (!this->validate_edge(&*edge)) continue;
            this->valid_edges.insert(&*edge);
            this->valid_edges.insert(edge->twin());
        }
    }
    this->edges = this->valid_edges;
    
    // iterate through the valid edges to build polylines
    while (!this->edges.empty()) {
        const edge_t* edge = *this->edges.begin();
        if (this->thickness[edge].first > this->max_width*1.001) {
            //std::cerr << "Error, edge.first has a thickness of " << unscaled(this->thickness[edge].first) << " > " << unscaled(this->max_width) << "\n";
            //(void)this->edges.erase(edge);
            //(void)this->edges.erase(edge->twin());
            //continue;
        }
        if (this->thickness[edge].second > this->max_width*1.001) {
            //std::cerr << "Error, edge.second has a thickness of " << unscaled(this->thickness[edge].second) << " > " << unscaled(this->max_width) << "\n";
            //(void)this->edges.erase(edge);
            //(void)this->edges.erase(edge->twin());
            //continue;
        }
        
        // start a polyline
        ThickPolyline polyline;
        polyline.points.push_back(Point( edge->vertex0()->x(), edge->vertex0()->y() ));
        polyline.points.push_back(Point( edge->vertex1()->x(), edge->vertex1()->y() ));
        polyline.width.push_back(this->thickness[edge].first);
        polyline.width.push_back(this->thickness[edge].second);
        
        // remove this edge and its twin from the available edges
        (void)this->edges.erase(edge);
        (void)this->edges.erase(edge->twin());
        
        // get next points
        this->process_edge_neighbors(edge, &polyline);
        
        // get previous points
        {
            ThickPolyline rpolyline;
            this->process_edge_neighbors(edge->twin(), &rpolyline);
            polyline.points.insert(polyline.points.begin(), rpolyline.points.rbegin(), rpolyline.points.rend());
            polyline.width.insert(polyline.width.begin(), rpolyline.width.rbegin(), rpolyline.width.rend());
            polyline.endpoints.first = rpolyline.endpoints.second;
        }
        
        assert(polyline.width.size() == polyline.points.size());
        
        // if loop, set endpoints to false
        // prevent loop endpoints from being extended
        if (polyline.first_point().coincides_with(polyline.last_point())) {
            polyline.endpoints.first = false;
            polyline.endpoints.second = false;
        }
        
        // append polyline to result
        polylines->push_back(polyline);
    }

    #ifdef SLIC3R_DEBUG
    {
        static int iRun = 0;
        dump_voronoi_to_svg(this->lines, this->vd, polylines, debug_out_path("MedialAxis-%d.svg", iRun ++).c_str());
        printf("Thick lines: ");
        for (ThickPolylines::const_iterator it = polylines->begin(); it != polylines->end(); ++ it) {
            ThickLines lines = it->thicklines();
            for (ThickLines::const_iterator it2 = lines.begin(); it2 != lines.end(); ++ it2) {
                printf("%f,%f ", it2->a_width, it2->b_width);
            }
        }
        printf("\n");
    }
    #endif /* SLIC3R_DEBUG */
}

void
MedialAxis::process_edge_neighbors(const VD::edge_type* edge, ThickPolyline* polyline)
{
    while (true) {
        // Since rot_next() works on the edge starting point but we want
        // to find neighbors on the ending point, we just swap edge with
        // its twin.
        const VD::edge_type* twin = edge->twin();
    
        // count neighbors for this edge
        std::vector<const VD::edge_type*> neighbors;
        for (const VD::edge_type* neighbor = twin->rot_next(); neighbor != twin;
            neighbor = neighbor->rot_next()) {
            if (this->valid_edges.count(neighbor) > 0) neighbors.push_back(neighbor);
        }
    
        // if we have a single neighbor then we can continue recursively
        if (neighbors.size() == 1) {
            const VD::edge_type* neighbor = neighbors.front();
            
            // break if this is a closed loop
            if (this->edges.count(neighbor) == 0) return;
            
            Point new_point(neighbor->vertex1()->x(), neighbor->vertex1()->y());
            polyline->points.push_back(new_point);
            polyline->width.push_back(this->thickness[neighbor].second);
            
            (void)this->edges.erase(neighbor);
            (void)this->edges.erase(neighbor->twin());
            edge = neighbor;
        } else if (neighbors.size() == 0) {
            polyline->endpoints.second = true;
            return;
        } else {
            // T-shaped or star-shaped joint
            return;
        }
    }
}

bool
MedialAxis::validate_edge(const VD::edge_type* edge)
{
    // prevent overflows and detect almost-infinite edges
    if (std::abs(edge->vertex0()->x()) > double(CLIPPER_MAX_COORD_UNSCALED) || 
        std::abs(edge->vertex0()->y()) > double(CLIPPER_MAX_COORD_UNSCALED) || 
        std::abs(edge->vertex1()->x()) > double(CLIPPER_MAX_COORD_UNSCALED) ||
        std::abs(edge->vertex1()->y()) > double(CLIPPER_MAX_COORD_UNSCALED))
        return false;

    // construct the line representing this edge of the Voronoi diagram
    const Line line(
        Point( edge->vertex0()->x(), edge->vertex0()->y() ),
        Point( edge->vertex1()->x(), edge->vertex1()->y() )
    );
    
    // discard edge if it lies outside the supplied shape
    // this could maybe be optimized (checking inclusion of the endpoints
    // might give false positives as they might belong to the contour itself)
    if (line.a.coincides_with(line.b)) {
        // in this case, contains(line) returns a false positive
        if (!this->expolygon.contains(line.a)) return false;
    } else {
        //test if  (!expolygon.contains(line))
        Polylines external_bits = diff_pl(Polylines{ Polyline{ line.a, line.b } }, expolygon);
        if (!external_bits.empty()){
            //check if the bits that are not inside are under epsilon length
            coordf_t max_length = 0;
            for (Polyline &poly : external_bits){
                max_length = std::max(max_length, poly.length());
            }
            if (max_length > SCALED_EPSILON)
                return false;
        }
    }
    
    // retrieve the original line segments which generated the edge we're checking
    const VD::cell_type* cell_l = edge->cell();
    const VD::cell_type* cell_r = edge->twin()->cell();
    const Line &segment_l = this->retrieve_segment(cell_l);
    const Line &segment_r = this->retrieve_segment(cell_r);
    
    
    //SVG svg("edge.svg");
    //svg.draw(this->expolygon.expolygon);
    //svg.draw(line);
    //svg.draw(segment_l, "red");
    //svg.draw(segment_r, "blue");
    //svg.Close();
    //
    
    /*  Calculate thickness of the cross-section at both the endpoints of this edge.
        Our Voronoi edge is part of a CCW sequence going around its Voronoi cell 
        located on the left side. (segment_l).
        This edge's twin goes around segment_r. Thus, segment_r is 
        oriented in the same direction as our main edge, and segment_l is oriented
        in the same direction as our twin edge.
        We used to only consider the (half-)distances to segment_r, and that works
        whenever segment_l and segment_r are almost specular and facing. However, 
        at curves they are staggered and they only face for a very little length
        (our very short edge represents such visibility).
        Both w0 and w1 can be calculated either towards cell_l or cell_r with equal
        results by Voronoi definition.
        When cell_l or cell_r don't refer to the segment but only to an endpoint, we
        calculate the distance to that endpoint instead.  */
    
    coordf_t w0 = cell_r->contains_segment()
        ? line.a.distance_to(segment_r)*2
        : line.a.distance_to(this->retrieve_endpoint(cell_r))*2;
    
    coordf_t w1 = cell_l->contains_segment()
        ? line.b.distance_to(segment_l)*2
        : line.b.distance_to(this->retrieve_endpoint(cell_l))*2;
    
    //don't remove the line that goes to the intersection of the contour
    // we use them to create nicer thin wall lines
    //if (cell_l->contains_segment() && cell_r->contains_segment()) {
    //    // calculate the relative angle between the two boundary segments
    //    double angle = fabs(segment_r.orientation() - segment_l.orientation());
    //    if (angle > PI) angle = 2*PI - angle;
    //    assert(angle >= 0 && angle <= PI);
    //    
    //    // fabs(angle) ranges from 0 (collinear, same direction) to PI (collinear, opposite direction)
    //    // we're interested only in segments close to the second case (facing segments)
    //    // so we allow some tolerance.
    //    // this filter ensures that we're dealing with a narrow/oriented area (longer than thick)
    //    // we don't run it on edges not generated by two segments (thus generated by one segment
    //    // and the endpoint of another segment), since their orientation would not be meaningful
    //    if (PI - angle > PI/8) {
    //        // angle is not narrow enough
    //        
    //        // only apply this filter to segments that are not too short otherwise their 
    //        // angle could possibly be not meaningful
    //        if (w0 < SCALED_EPSILON || w1 < SCALED_EPSILON || line.length() >= this->min_width)
    //            return false;
    //    }
    //} else {
    //    if (w0 < SCALED_EPSILON || w1 < SCALED_EPSILON)
    //        return false;
    //}

    // don't do that before we try to fusion them
    //if (w0 < this->min_width && w1 < this->min_width)
    //    return false;
    //

    //shouldn't occur if perimeter_generator is well made. *1.05 for a little wiggle room
    if (w0 > this->max_width*1.05 && w1 > this->max_width*1.05)
        return false;
    
    this->thickness[edge]         = std::make_pair(w0, w1);
    this->thickness[edge->twin()] = std::make_pair(w1, w0);
    
    return true;
}

const Line&
MedialAxis::retrieve_segment(const VD::cell_type* cell) const
{
    return lines[cell->source_index()];
}

const Point&
MedialAxis::retrieve_endpoint(const VD::cell_type* cell) const
{
    const Line& line = this->retrieve_segment(cell);
    if (cell->source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT) {
        return line.a;
    } else {
        return line.b;
    }
}


/// remove point that are at SCALED_EPSILON * 2 distance.
void
remove_point_too_near(ThickPolyline* to_reduce)
{
    const coord_t smallest = (coord_t)SCALED_EPSILON * 2;
    size_t id = 1;
    while (id < to_reduce->points.size() - 1) {
        coord_t newdist = (coord_t)std::min(to_reduce->points[id].distance_to(to_reduce->points[id - 1])
            , to_reduce->points[id].distance_to(to_reduce->points[id + 1]));
        if (newdist < smallest) {
            to_reduce->points.erase(to_reduce->points.begin() + id);
            to_reduce->width.erase(to_reduce->width.begin() + id);
            newdist = (coord_t)to_reduce->points[id].distance_to(to_reduce->points[id - 1]);
            //if you removed a point, it check if the next one isn't too near from the previous one.
            // if not, it bypass it.
            if (newdist > smallest) {
                ++id;
            }
        }
        //go to next one
        else ++id;
    }
}

/// add points  from pattern to to_modify at the same % of the length
/// so not add if an other point is present at the correct position
void
add_point_same_percent(ThickPolyline* pattern, ThickPolyline* to_modify)
{
    const double to_modify_length = to_modify->length();
    const double percent_epsilon = SCALED_EPSILON / to_modify_length;
    const double pattern_length = pattern->length();

    double percent_length = 0;
    for (size_t idx_point = 1; idx_point < pattern->points.size() - 1; ++idx_point) {
        percent_length += pattern->points[idx_point-1].distance_to(pattern->points[idx_point]) / pattern_length;
        //find position 
        size_t idx_other = 1;
        double percent_length_other_before = 0;
        double percent_length_other = 0;
        while (idx_other < to_modify->points.size()) {
            percent_length_other_before = percent_length_other;
            percent_length_other += to_modify->points[idx_other-1].distance_to(to_modify->points[idx_other])
                / to_modify_length;
            if (percent_length_other > percent_length - percent_epsilon) {
                //if higher (we have gone over it)
                break;
            }
            ++idx_other;
        }
        if (percent_length_other > percent_length + percent_epsilon) {
            //insert a new point before the position
            double percent_dist = (percent_length - percent_length_other_before) / (percent_length_other - percent_length_other_before);
            coordf_t new_width = to_modify->width[idx_other - 1] * (1 - percent_dist);
            new_width += to_modify->width[idx_other] * (percent_dist);
            to_modify->width.insert(to_modify->width.begin() + idx_other, new_width);
            to_modify->points.insert(
                to_modify->points.begin() + idx_other,
                to_modify->points[idx_other - 1].interpolate(percent_dist, to_modify->points[idx_other]));
        }
    }
}

/// find the nearest angle in the contour (or 2 nearest if it's difficult to choose) 
/// return 1 for an angle of 90° and 0 for an angle of 0° or 180°
/// find the nearest angle in the contour (or 2 nearest if it's difficult to choose) 
/// return 1 for an angle of 90° and 0 for an angle of 0° or 180°
double
get_coeff_from_angle_countour(Point &point, const ExPolygon &contour, coord_t min_dist_between_point) {
    double nearest_dist = point.distance_to(contour.contour.points.front());
    Point point_nearest = contour.contour.points.front();
    size_t id_nearest = 0;
    double near_dist = nearest_dist;
    Point point_near = point_nearest;
    size_t id_near = 0;
    for (size_t id_point = 1; id_point < contour.contour.points.size(); ++id_point) {
        if (nearest_dist > point.distance_to(contour.contour.points[id_point])) {
            //update point_near
            id_near = id_nearest;
            point_near = point_nearest;
            near_dist = nearest_dist;
            //update nearest
            nearest_dist = point.distance_to(contour.contour.points[id_point]);
            point_nearest = contour.contour.points[id_point];
            id_nearest = id_point;
        }
    }
    double angle = 0;
    size_t id_before = id_nearest == 0 ? contour.contour.points.size() - 1 : id_nearest - 1;
    Point point_before = id_nearest == 0 ? contour.contour.points.back() : contour.contour.points[id_nearest - 1];
    //Search one point far enough to be relevant
    while (point_nearest.distance_to(point_before) < min_dist_between_point) {
        point_before = id_before == 0 ? contour.contour.points.back() : contour.contour.points[id_before - 1];
        id_before = id_before == 0 ? contour.contour.points.size() - 1 : id_before - 1;
        //don't loop
        if (id_before == id_nearest) {
            id_before = id_nearest == 0 ? contour.contour.points.size() - 1 : id_nearest - 1;
            point_before = id_nearest == 0 ? contour.contour.points.back() : contour.contour.points[id_nearest - 1];
            break;
        }
    }
    size_t id_after = id_nearest == contour.contour.points.size() - 1 ? 0 : id_nearest + 1;
    Point point_after = id_nearest == contour.contour.points.size() - 1 ? contour.contour.points.front() : contour.contour.points[id_nearest + 1];
    //Search one point far enough to be relevant
    while (point_nearest.distance_to(point_after) < min_dist_between_point) {
        point_after = id_after == contour.contour.points.size() - 1 ? contour.contour.points.front() : contour.contour.points[id_after + 1];
        id_after = id_after == contour.contour.points.size() - 1 ? 0 : id_after + 1;
        //don't loop
        if (id_after == id_nearest) {
            id_after = id_nearest == contour.contour.points.size() - 1 ? 0 : id_nearest + 1;
            point_after = id_nearest == contour.contour.points.size() - 1 ? contour.contour.points.front() : contour.contour.points[id_nearest + 1];
            break;
        }
    }
    //compute angle
    angle = point_nearest.ccw_angle(point_before, point_after);
    if (angle >= PI) angle = 2 * PI - angle;  // smaller angle
    //compute the diff from 90°
    angle = abs(angle - PI / 2);
    if (point_near.coincides_with(point_nearest) && std::max(nearest_dist, near_dist) + SCALED_EPSILON < point_nearest.distance_to(point_near)) {
        //not only nearest
        Point point_before = id_near == 0 ? contour.contour.points.back() : contour.contour.points[id_near - 1];
        Point point_after = id_near == contour.contour.points.size() - 1 ? contour.contour.points.front() : contour.contour.points[id_near + 1];
        double angle2 = std::min(point_nearest.ccw_angle(point_before, point_after), point_nearest.ccw_angle(point_after, point_before));
        angle2 = abs(angle - PI / 2);
        angle = (angle + angle2) / 2;
    }

    return 1 - (angle / (PI / 2));
}

double
dot(Line l1, Line l2)
{
    Vec2d v_1(l1.b.x() - l1.a.x(), l1.b.y() - l1.a.y());
    v_1.normalize();
    Vec2d v_2(l2.b.x() - l2.a.x(), l2.b.y() - l2.a.y());
    v_2.normalize();
    return v_1.x()*v_2.x() + v_1.y()*v_2.y();
}

void
MedialAxis::fusion_curve(ThickPolylines &pp)
{
    //fusion Y with only 1 '0' value => the "0" branch "pull" the cross-point
    bool changes = false;
    for (size_t i = 0; i < pp.size(); ++i) {
        ThickPolyline& polyline = pp[i];
        // only consider 2-point polyline with endpoint
        //if (polyline.points.size() != 2) continue; // too restrictive.
        if (polyline.endpoints.first) polyline.reverse();
        else if (!polyline.endpoints.second) continue;
        if (polyline.width.back() > EPSILON) continue;

        //check my length is small
        coord_t length = (coord_t)polyline.length();
        if (length > max_width) continue;

        size_t closest_point_idx = this->expolygon.contour.closest_point_index(polyline.points.back());

        //check the 0-width point is on the contour.
        if (closest_point_idx == (size_t)-1) continue;

        size_t prev_idx = closest_point_idx == 0 ? this->expolygon.contour.points.size() - 1 : closest_point_idx - 1;
        size_t next_idx = closest_point_idx == this->expolygon.contour.points.size() - 1 ? 0 : closest_point_idx + 1;
        double mindot = 1;
        mindot = std::min(mindot, abs(dot(Line(polyline.points[polyline.points.size() - 1], polyline.points[polyline.points.size() - 2]),
            (Line(this->expolygon.contour.points[closest_point_idx], this->expolygon.contour.points[prev_idx])))));
        mindot = std::min(mindot, abs(dot(Line(polyline.points[polyline.points.size() - 1], polyline.points[polyline.points.size() - 2]),
            (Line(this->expolygon.contour.points[closest_point_idx], this->expolygon.contour.points[next_idx])))));

        //compute angle
        double coeff_contour_angle = this->expolygon.contour.points[closest_point_idx].ccw_angle(this->expolygon.contour.points[prev_idx], this->expolygon.contour.points[next_idx]);
        if (coeff_contour_angle >= PI) coeff_contour_angle = 2 * PI - coeff_contour_angle;  // smaller angle
        //compute the diff from 90°
        coeff_contour_angle = abs(coeff_contour_angle - PI / 2);


        // look if other end is a cross point with almost 90° angle
        double sum_dot = 0;
        double min_dot = 0;
        // look if other end is a cross point with multiple other branch
        std::vector<size_t> crosspoint;
        for (size_t j = 0; j < pp.size(); ++j) {
            if (j == i) continue;
            ThickPolyline& other = pp[j];
            if (polyline.first_point().coincides_with(other.last_point())) {
                other.reverse();
                crosspoint.push_back(j);
                double dot_temp = dot(Line(polyline.points[0], polyline.points[1]), (Line(other.points[0], other.points[1])));
                min_dot = std::min(min_dot, abs(dot_temp));
                sum_dot += dot_temp;
            } else if (polyline.first_point().coincides_with(other.first_point())) {
                crosspoint.push_back(j);
                double dot_temp = dot(Line(polyline.points[0], polyline.points[1]), (Line(other.points[0], other.points[1])));
                min_dot = std::min(min_dot, abs(dot_temp));
                sum_dot += dot_temp;
            }
        }
        sum_dot = abs(sum_dot);

        //only consider very shallow angle for contour
        if (mindot > 0.15 &&
            (1 - (coeff_contour_angle / (PI / 2))) > 0.2) continue;

        //check if it's a line that we can pull
        if (crosspoint.size() != 2) continue;
        if (sum_dot > 0.2) continue;
        if (min_dot > 0.5) continue;
        //don't remove useful bits. TODO: use the mindot to know by how much to multiply (1 when 90°, 1.42 when 45+, 1 when 0°)
        if (polyline.length() > polyline.width.front()*1.42) continue;

        //don't pull, it distords the line if there are too many points.
        //// pull it a bit, depends on my size, the dot?, and the coeff at my 0-end (~14% for a square, almost 0 for a gentle curve)
        //coord_t length_pull = polyline.length();
        //length_pull *= 0.144 * get_coeff_from_angle_countour(polyline.points.back(), this->expolygon, std::min(min_width, polyline.length() / 2));

        ////compute dir
        //Vectorf pull_direction(polyline.points[1].x() - polyline.points[0].x(), polyline.points[1].y() - polyline.points[0].y());
        //pull_direction = normalize(pull_direction);
        //pull_direction.x() *= length_pull;
        //pull_direction.y() *= length_pull;

        ////pull the points
        //Point &p1 = pp[crosspoint[0]].points[0];
        //p1.x() = p1.x() + (coord_t)pull_direction.x();
        //p1.y() = p1.y() + (coord_t)pull_direction.y();

        //Point &p2 = pp[crosspoint[1]].points[0];
        //p2.x() = p2.x() + (coord_t)pull_direction.x();
        //p2.y() = p2.y() + (coord_t)pull_direction.y();

        //delete the now unused polyline
        pp.erase(pp.begin() + i);
        --i;
        changes = true;
    }
    if (changes) {
        concatThickPolylines(pp);
        ///reorder, in case of change
        std::sort(pp.begin(), pp.end(), [](const ThickPolyline & a, const ThickPolyline & b) { return a.length() < b.length(); });
        //have to redo it to remove multi-branch bits.
        fusion_curve(pp);
    }
}

void
MedialAxis::remove_bits(ThickPolylines &pp)
{

    //remove small bits that stick out of the path
    bool changes = false;
    for (size_t i = 0; i < pp.size(); ++i) {
        ThickPolyline& polyline = pp[i];
        // only consider polyline with 0-end
        if (polyline.endpoints.first) polyline.reverse();
        else if (!polyline.endpoints.second) continue;
        if (polyline.width.back() > 0) continue;

        //check my length is small
        coord_t length = (coord_t)polyline.length();
        if (length > max_width*1.5) {
            continue;
        }

        // look if other end is a cross point with multiple other branch
        std::vector<size_t> crosspoint;
        for (size_t j = 0; j < pp.size(); ++j) {
            if (j == i) continue;
            ThickPolyline& other = pp[j];
            if (polyline.first_point().coincides_with(other.last_point())) {
                other.reverse();
                crosspoint.push_back(j);
            } else if (polyline.first_point().coincides_with(other.first_point())) {
                crosspoint.push_back(j);
            }
        }
        if (crosspoint.size() < 2) continue;

        //check if is smaller or the other ones are not endpoits
        int nb_better_than_me = 0;
        for (int i = 0; i < crosspoint.size(); i++) {
            if (!pp[crosspoint[0]].endpoints.second || length <= pp[crosspoint[0]].length()) 
                nb_better_than_me++;
        }
        if (nb_better_than_me < 2) continue;

        //check if the length of the polyline is small vs width of the other lines
        double max_width = 0;
        for (int i = 0; i < crosspoint.size(); i++) {
            max_width = std::max(max_width, pp[crosspoint[i]].width[0]);
        }
        if (length > max_width + min_width)
            continue;

        //delete the now unused polyline
        pp.erase(pp.begin() + i);
        --i;
        changes = true;
    }
    if (changes) {
        concatThickPolylines(pp);
        ///reorder, in case of change
        std::sort(pp.begin(), pp.end(), [](const ThickPolyline & a, const ThickPolyline & b) { return a.length() < b.length(); });
    }
}

void
MedialAxis::fusion_corners(ThickPolylines &pp)
{

    //fusion Y with only 1 '0' value => the "0" branch "pull" the cross-point
    bool changes = false;
    for (size_t i = 0; i < pp.size(); ++i) {
        ThickPolyline& polyline = pp[i];
        // only consider polyline with 0-end
        //if (polyline.points.size() != 2) continue; // maybe we should have something to merge X-point to 2-point if it's near enough.
        if (polyline.endpoints.first) polyline.reverse();
        else if (!polyline.endpoints.second) continue;
        if (polyline.width.back() > 0) continue;

        //check my length is small
        coord_t length = (coord_t)polyline.length();
        if (length > max_width) continue;

        // look if other end is a cross point with multiple other branch
        std::vector<size_t> crosspoint;
        for (size_t j = 0; j < pp.size(); ++j) {
            if (j == i) continue;
            ThickPolyline& other = pp[j];
            if (polyline.first_point().coincides_with(other.last_point())) {
                other.reverse();
                crosspoint.push_back(j);
            } else if (polyline.first_point().coincides_with(other.first_point())) {
                crosspoint.push_back(j);
            }
        }
        //check if it's a line that we can pull
        if (crosspoint.size() != 2) continue;

        // check if i am at the external side of a curve
        double angle1 = polyline.points[0].ccw_angle(polyline.points[1], pp[crosspoint[0]].points[1]);
        if (angle1 >= PI) angle1 = 2 * PI - angle1;  // smaller angle
        double angle2 = polyline.points[0].ccw_angle(polyline.points[1], pp[crosspoint[1]].points[1]);
        if (angle2 >= PI) angle2 = 2 * PI - angle2;  // smaller angle
        if (angle1 + angle2 < PI) continue;

        //check if is smaller or the other ones are not endpoits
        if (pp[crosspoint[0]].endpoints.second && length > pp[crosspoint[0]].length()) continue;
        if (pp[crosspoint[1]].endpoints.second && length > pp[crosspoint[1]].length()) continue;

        //FIXME: also pull (a bit less) points that are near to this one.
        // if true, pull it a bit, depends on my size, the dot?, and the coeff at my 0-end (~14% for a square, almost 0 for a gentle curve)
        coord_t length_pull = (coord_t)polyline.length();
        length_pull *= (coord_t)( 0.144 * get_coeff_from_angle_countour(
            polyline.points.back(), 
            this->expolygon, 
            std::min(min_width, (coord_t)(polyline.length() / 2))));

        //compute dir
        Vec2d pull_direction(polyline.points[1].x() - polyline.points[0].x(), polyline.points[1].y() - polyline.points[0].y());
        pull_direction.normalize();
        pull_direction.x() *= length_pull;
        pull_direction.y() *= length_pull;

        //pull the points
        Point &p1 = pp[crosspoint[0]].points[0];
        p1.x() = p1.x() + (coord_t)pull_direction.x();
        p1.y() = p1.y() + (coord_t)pull_direction.y();

        Point &p2 = pp[crosspoint[1]].points[0];
        p2.x() = p2.x() + (coord_t)pull_direction.x();
        p2.y() = p2.y() + (coord_t)pull_direction.y();

        //delete the now unused polyline
        pp.erase(pp.begin() + i);
        --i;
        changes = true;
    }
    if (changes) {
        concatThickPolylines(pp);
        ///reorder, in case of change
        std::sort(pp.begin(), pp.end(), [](const ThickPolyline & a, const ThickPolyline & b) { return a.length() < b.length(); });
    }
}

void
MedialAxis::extends_line_both_side(ThickPolylines& pp) {
    const ExPolygons anchors = offset2_ex(to_polygons(diff_ex(*this->bounds, this->expolygon)), double(-SCALED_RESOLUTION), double(SCALED_RESOLUTION));
    for (size_t i = 0; i < pp.size(); ++i) {
        ThickPolyline& polyline = pp[i];
        this->extends_line(polyline, anchors, this->min_width);
        if (!polyline.points.empty()) {
            polyline.reverse();
            this->extends_line(polyline, anchors, this->min_width);
        }
        if (polyline.points.empty()) {
            pp.erase(pp.begin() + i);
            --i;
        }
    }
}

void
MedialAxis::extends_line(ThickPolyline& polyline, const ExPolygons& anchors, const coord_t join_width)
{
    // extend initial and final segments of each polyline if they're actual endpoints
    // We assign new endpoints to temporary variables because in case of a single-line
    // polyline, after we extend the start point it will be caught by the intersection()
    // call, so we keep the inner point until we perform the second intersection() as well
    if (polyline.endpoints.second && !bounds->has_boundary_point(polyline.points.back())) {
        size_t first_idx = polyline.points.size() - 2;
        Line line(*(polyline.points.begin() + first_idx), polyline.points.back());
        while (line.length() < SCALED_RESOLUTION && first_idx>0) {
            first_idx--;
            line.a = *(polyline.points.begin() + first_idx);
        }
        // prevent the line from touching on the other side, otherwise intersection() might return that solution
        if (polyline.points.size() == 2 && this->expolygon.contains(line.midpoint())) line.a = line.midpoint();

        line.extend_end((double)max_width);
        Point new_back;
        if (this->expolygon.contour.has_boundary_point(polyline.points.back())) {
            new_back = polyline.points.back();
        } else {
            bool finded = this->expolygon.contour.first_intersection(line, &new_back);
            //verify also for holes.
            Point new_back_temp;
            for (Polygon hole : this->expolygon.holes) {
                if (hole.first_intersection(line, &new_back_temp)) {
                    if (!finded || line.a.distance_to(new_back_temp) < line.a.distance_to(new_back)) {
                        finded = true;
                        new_back = new_back_temp;
                    }
                }
            }
            // safety check if no intersection
            if (!finded) {
                if (!this->expolygon.contains(line.b)) {
                    //it's outside!!!
                    //if (!this->expolygon.contains(line.a)) {
                    //    std::cout << "Error, a line is formed that start outside a polygon, end outside of it and don't cross it!\n";
                    //} else {
                    //    std::cout << "Error, a line is formed that start in a polygon, end outside of it and don't cross it!\n";
                    //}

                    //{
                    //    std::stringstream stri;
                    //    stri << "Error_" << (count_error++) << ".svg";
                    //    SVG svg(stri.str());
                    //    svg.draw(anchors);
                    //    svg.draw(this->expolygon);
                    //    svg.draw(line);
                    //    svg.draw(polyline);
                    //    svg.Close();
                    //}
                    //it's not possible to print that
                    polyline.points.clear();
                    polyline.width.clear();
                    return;
                }
                new_back = line.b;
            }
            polyline.points.push_back(new_back);
            polyline.width.push_back(polyline.width.back());
        }
        Point new_bound;
        bool finded = bounds->contour.first_intersection(line, &new_bound);
        //verify also for holes.
        Point new_bound_temp;
        for (Polygon hole : bounds->holes) {
            if (hole.first_intersection(line, &new_bound_temp)) {
                if (!finded || line.a.distance_to(new_bound_temp) < line.a.distance_to(new_bound)) {
                    finded = true;
                    new_bound = new_bound_temp;
                }
            }
        }
        // safety check if no intersection
        if (!finded) {
            if (line.b.coincides_with_epsilon(polyline.points.back()))
                return;
            //check if we don't over-shoot inside us
            bool is_in_anchor = false;
            for (const ExPolygon& a : anchors) {
                if (a.contains(line.b)) {
                    is_in_anchor = true;
                    break;
                }
            }
            if (!is_in_anchor) return;
            new_bound = line.b;
        }
       /* if (new_bound.coincides_with_epsilon(new_back)) {
            return;
        }*/
        // find anchor
        Point best_anchor;
        double shortest_dist = (double)max_width;
        for (const ExPolygon& a : anchors) {
            Point p_maybe_inside = a.contour.centroid();
            double test_dist = new_bound.distance_to(p_maybe_inside) + new_back.distance_to(p_maybe_inside);
            //if (test_dist < max_width / 2 && (test_dist < shortest_dist || shortest_dist < 0)) {
            double angle_test = new_back.ccw_angle(p_maybe_inside, line.a);
            if (angle_test > PI) angle_test = 2 * PI - angle_test;
            if (test_dist < max_width && test_dist<shortest_dist && abs(angle_test) > PI / 2) {
                shortest_dist = test_dist;
                best_anchor = p_maybe_inside;
            }
        }
        if (best_anchor.x() != 0 && best_anchor.y() != 0) {
            Point p_obj = best_anchor + new_bound;
            p_obj.x() /= 2;
            p_obj.y() /= 2;
            Line l2 = Line(new_back, p_obj);
            l2.extend_end((double)max_width);
            (void)bounds->contour.first_intersection(l2, &new_bound);
        }
        if (new_bound.coincides_with_epsilon(new_back))
            return;
        polyline.points.push_back(new_bound);
        //polyline.width.push_back(join_width);
        //it thickens the line a bit too early, imo
        polyline.width.push_back(polyline.width.back());
    }
}

void
MedialAxis::main_fusion(ThickPolylines& pp)
{
    //int idf = 0;

    bool changes = true;
    std::map<Point, double> coeff_angle_cache;
    while (changes) {
        concatThickPolylines(pp);
        //reoder pp by length (ascending) It's really important to do that to avoid building the line from the width insteand of the length
        std::sort(pp.begin(), pp.end(), [](const ThickPolyline & a, const ThickPolyline & b) {
            bool ahas0 = a.width.front() == 0 || a.width.back() == 0;
            bool bhas0 = b.width.front() == 0 || b.width.back() == 0;
            if (ahas0 && !bhas0) return true;
            if (!ahas0 && bhas0) return false;
            return a.length() < b.length();
        });
        changes = false;
        for (size_t i = 0; i < pp.size(); ++i) {
            ThickPolyline& polyline = pp[i];

            //simple check to see if i can be fusionned
            if (!polyline.endpoints.first && !polyline.endpoints.second) continue;


            ThickPolyline* best_candidate = nullptr;
            float best_dot = -1;
            size_t best_idx = 0;
            double dot_poly_branch = 0;
            double dot_candidate_branch = 0;

            bool find_main_branch = false;
            size_t biggest_main_branch_id = 0;
            coord_t biggest_main_branch_length = 0;

            // find another polyline starting here
            for (size_t j = i + 1; j < pp.size(); ++j) {
                ThickPolyline& other = pp[j];
                if (polyline.last_point().coincides_with(other.last_point())) {
                    polyline.reverse();
                    other.reverse();
                } else if (polyline.first_point().coincides_with(other.last_point())) {
                    other.reverse();
                } else if (polyline.first_point().coincides_with(other.first_point())) {
                } else if (polyline.last_point().coincides_with(other.first_point())) {
                    polyline.reverse();
                } else {
                    continue;
                }
                //std::cout << " try : " << i << ":" << j << " : " << 
                //    (polyline.points.size() < 2 && other.points.size() < 2) <<
                //    (!polyline.endpoints.second || !other.endpoints.second) <<
                //    ((polyline.points.back().distance_to(other.points.back())
                //    + (polyline.width.back() + other.width.back()) / 4)
                //    > max_width*1.05) <<
                //    (abs(polyline.length() - other.length()) > max_width) << "\n";

                //// mergeable tests
                if (polyline.points.size() < 2 && other.points.size() < 2) continue;
                if (!polyline.endpoints.second || !other.endpoints.second) continue;
                // test if the new width will not be too big if a fusion occur
                //note that this isn't the real calcul. It's just to avoid merging lines too far apart.
                if (
                    ((polyline.points.back().distance_to(other.points.back())
                    + (polyline.width.back() + other.width.back()) / 4)
                > max_width*1.05))
                    continue;
                // test if the lines are not too different in length.
                if (abs(polyline.length() - other.length()) > max_width) continue;


                //test if we don't merge with something too different and without any relevance.
                double coeffSizePolyI = 1;
                if (polyline.width.back() == 0) {
                    coeffSizePolyI = 0.1 + 0.9*get_coeff_from_angle_countour(polyline.points.back(), this->expolygon, std::min(min_width, (coord_t)(polyline.length() / 2)));
                }
                double coeffSizeOtherJ = 1;
                if (other.width.back() == 0) {
                    coeffSizeOtherJ = 0.1 + 0.9*get_coeff_from_angle_countour(other.points.back(), this->expolygon, std::min(min_width, (coord_t)(polyline.length() / 2)));
                }
                //std::cout << " try2 : " << i << ":" << j << " : "
                //    << (abs(polyline.length()*coeffSizePolyI - other.length()*coeffSizeOtherJ) > max_width / 2)
                //    << (abs(polyline.length()*coeffSizePolyI - other.length()*coeffSizeOtherJ) > max_width)
                //    << "\n";
                if (abs(polyline.length()*coeffSizePolyI - other.length()*coeffSizeOtherJ) > max_width / 2) continue;


                //compute angle to see if it's better than previous ones (straighter = better).
                //we need to add how strait we are from our main.
                float test_dot = (float)(dot(polyline.lines().front(), other.lines().front()));

                // Get the branch/line in wich we may merge, if possible
                // with that, we can decide what is important, and how we can merge that.
                // angle_poly - angle_candi =90° => one is useless
                // both angle are equal => both are useful with same strength
                // ex: Y => | both are useful to crete a nice line
                // ex2: TTTTT => -----  these 90° useless lines should be discarded
                find_main_branch = false;
                biggest_main_branch_id = 0;
                biggest_main_branch_length = 0;
                for (size_t k = 0; k < pp.size(); ++k) {
                    //std::cout << "try to find main : " << k << " ? " << i << " " << j << " ";
                    if (k == i || k == j) continue;
                    ThickPolyline& main = pp[k];
                    if (polyline.first_point().coincides_with(main.last_point())) {
                        main.reverse();
                        if (!main.endpoints.second)
                            find_main_branch = true;
                        else if (biggest_main_branch_length < main.length()) {
                            biggest_main_branch_id = k;
                            biggest_main_branch_length = (coord_t)main.length();
                        }
                    } else if (polyline.first_point().coincides_with(main.first_point())) {
                        if (!main.endpoints.second)
                            find_main_branch = true;
                        else if (biggest_main_branch_length < main.length()) {
                            biggest_main_branch_id = k;
                            biggest_main_branch_length = (coord_t)main.length();
                        }
                    }
                    if (find_main_branch) {
                        //use this variable to store the good index and break to compute it
                        biggest_main_branch_id = k;
                        break;
                    }
                }
                double dot_poly_branch_test = 0.707;
                double dot_candidate_branch_test = 0.707;
                if (!find_main_branch && biggest_main_branch_length == 0) {
                    // nothing -> it's impossible!
                    dot_poly_branch_test = 0.707;
                    dot_candidate_branch_test = 0.707;
                    //std::cout << "no main branch... impossible!!\n";
                } else if (!find_main_branch && (
                    (pp[biggest_main_branch_id].length() < polyline.length() && (polyline.width.back() != 0 || pp[biggest_main_branch_id].width.back() ==0)) 
                    || (pp[biggest_main_branch_id].length() < other.length() && (other.width.back() != 0 || pp[biggest_main_branch_id].width.back() == 0)))) {
                    //the main branch should have no endpoint or be bigger!
                    //here, it have an endpoint, and is not the biggest -> bad!
                    //std::cout << "he main branch should have no endpoint or be bigger! here, it have an endpoint, and is not the biggest -> bad!\n";
                    continue;
                } else {
                    //compute the dot (biggest_main_branch_id)
                    dot_poly_branch_test = -dot(Line(polyline.points[0], polyline.points[1]), Line(pp[biggest_main_branch_id].points[0], pp[biggest_main_branch_id].points[1]));
                    dot_candidate_branch_test = -dot(Line(other.points[0], other.points[1]), Line(pp[biggest_main_branch_id].points[0], pp[biggest_main_branch_id].points[1]));
                    if (dot_poly_branch_test < 0) dot_poly_branch_test = 0;
                    if (dot_candidate_branch_test < 0) dot_candidate_branch_test = 0;
                    if (pp[biggest_main_branch_id].width.back()>0)
                        test_dot += 2 * (float)dot_poly_branch;
                    //std::cout << "compute dot "<< dot_poly_branch_test<<" & "<< dot_candidate_branch_test <<"\n";
                }
                //test if it's useful to merge or not
                //ie, don't merge  'T' but ok for 'Y', merge only lines of not disproportionate different length (ratio max: 4) (or they are both with 0-width end)
                if (dot_poly_branch_test < 0.1 || dot_candidate_branch_test < 0.1 ||
                    (
                        ((polyline.length()>other.length() ? polyline.length() / other.length() : other.length() / polyline.length()) > 4) 
                        && !(polyline.width.back() == 0 && other.width.back()==0)
                        )) {
                    //std::cout << "not useful to merge\n";
                    continue;
                }
                if (test_dot > best_dot) {
                    best_candidate = &other;
                    best_idx = j;
                    best_dot = test_dot;
                    dot_poly_branch = dot_poly_branch_test;
                    dot_candidate_branch = dot_candidate_branch_test;
                    //{
                    //    std::cout << "going to merge: b1=" << i << ", b2=" << best_idx << ", main=" << biggest_main_branch_id << "\n";
                    //    std::cout << "b1=" << polyline.points.front().x() << " : " << polyline.points.front().y() << " => " << polyline.points.back().x() << " : " << polyline.points.back().y() << "\n";
                    //    std::cout << "b2=" << other.points.front().x() << " : " << other.points.front().y() << " => " << other.points.back().x() << " : " << other.points.back().y() << "\n";
                    //    std::cout << "main=" << pp[biggest_main_branch_id].points.front().x() << " : " << pp[biggest_main_branch_id].points.front().y() << " => " << pp[biggest_main_branch_id].points.back().x() << " : " << pp[biggest_main_branch_id].points.back().y() << "\n";
                    //}
                }
            }
            if (best_candidate != nullptr) {
                //idf++;
                //std::cout << " == fusion " << id <<" : "<< idf << " == with "<< i <<" & "<<best_idx<<"\n";
                // delete very near points
                remove_point_too_near(&polyline);
                remove_point_too_near(best_candidate);

                // add point at the same pos than the other line to have a nicer fusion
                add_point_same_percent(&polyline, best_candidate);
                add_point_same_percent(best_candidate, &polyline);

                //get the angle of the nearest points of the contour to see : _| (good) \_ (average) __(bad)
                //sqrt because the result are nicer this way: don't over-penalize /_ angles
                //TODO: try if we can achieve a better result if we use a different algo if the angle is <90°
                const double coeff_angle_poly = (coeff_angle_cache.find(polyline.points.back()) != coeff_angle_cache.end())
                    ? coeff_angle_cache[polyline.points.back()]
                    : (get_coeff_from_angle_countour(polyline.points.back(), this->expolygon, std::min(min_width, (coord_t)(polyline.length() / 2))));
                const double coeff_angle_candi = (coeff_angle_cache.find(best_candidate->points.back()) != coeff_angle_cache.end())
                    ? coeff_angle_cache[best_candidate->points.back()]
                    : (get_coeff_from_angle_countour(best_candidate->points.back(), this->expolygon, std::min(min_width, (coord_t)(best_candidate->length() / 2))));

                //this will encourage to follow the curve, a little, because it's shorter near the center
                //without that, it tends to go to the outter rim.
                //std::cout << " std::max(polyline.length(), best_candidate->length())=" << std::max(polyline.length(), best_candidate->length())
                //    << ", polyline.length()=" << polyline.length()
                //    << ", best_candidate->length()=" << best_candidate->length()
                //    << ", polyline.length() / max=" << (polyline.length() / std::max(polyline.length(), best_candidate->length()))
                //    << ", best_candidate->length() / max=" << (best_candidate->length() / std::max(polyline.length(), best_candidate->length()))
                //    << "\n";
                double weight_poly = 2 - (polyline.length() / std::max(polyline.length(), best_candidate->length()));
                double weight_candi = 2 - (best_candidate->length() / std::max(polyline.length(), best_candidate->length()));
                weight_poly *= coeff_angle_poly;
                weight_candi *= coeff_angle_candi;
                const double coeff_poly = (dot_poly_branch * weight_poly) / (dot_poly_branch * weight_poly + dot_candidate_branch * weight_candi);
                const double coeff_candi = 1.0 - coeff_poly;
                //std::cout << "coeff_angle_poly=" << coeff_angle_poly
                //    << ", coeff_angle_candi=" << coeff_angle_candi
                //    << ", weight_poly=" << (2 - (polyline.length() / std::max(polyline.length(), best_candidate->length())))
                //    << ", weight_candi=" << (2 - (best_candidate->length() / std::max(polyline.length(), best_candidate->length())))
                //    << ", sumpoly=" << weight_poly
                //    << ", sumcandi=" << weight_candi
                //    << ", dot_poly_branch=" << dot_poly_branch
                //    << ", dot_candidate_branch=" << dot_candidate_branch
                //    << ", coeff_poly=" << coeff_poly
                //    << ", coeff_candi=" << coeff_candi
                //    << "\n";
                //iterate the points
                // as voronoi should create symetric thing, we can iterate synchonously
                size_t idx_point = 1;
                while (idx_point < std::min(polyline.points.size(), best_candidate->points.size())) {
                    //fusion
                    polyline.points[idx_point].x() = (coord_t)( polyline.points[idx_point].x() * coeff_poly + best_candidate->points[idx_point].x() * coeff_candi);
                    polyline.points[idx_point].y() = (coord_t)(polyline.points[idx_point].y() * coeff_poly + best_candidate->points[idx_point].y() * coeff_candi);

                    // The width decrease with distance from the centerline.
                    // This formula is what works the best, even if it's not perfect (created empirically).  0->3% error on a gap fill on some tests.
                    //If someone find  an other formula based on the properties of the voronoi algorithm used here, and it works better, please use it.
                    //or maybe just use the distance to nearest edge in bounds...
                    double value_from_current_width = 0.5*polyline.width[idx_point] * dot_poly_branch / std::max(dot_poly_branch, dot_candidate_branch);
                    value_from_current_width += 0.5*best_candidate->width[idx_point] * dot_candidate_branch / std::max(dot_poly_branch, dot_candidate_branch);
                    double value_from_dist = 2 * polyline.points[idx_point].distance_to(best_candidate->points[idx_point]);
                    value_from_dist *= sqrt(std::min(dot_poly_branch, dot_candidate_branch) / std::max(dot_poly_branch, dot_candidate_branch));
                    polyline.width[idx_point] = value_from_current_width + value_from_dist;
                    //std::cout << "width:" << polyline.width[idx_point] << " = " << value_from_current_width << " + " << value_from_dist 
                    //    << " (<" << max_width << " && " << (bounds.contour.closest_point(polyline.points[idx_point])->distance_to(polyline.points[idx_point]) * 2.1)<<")\n";
                    //failsafes
                    if (polyline.width[idx_point] > max_width)
                        polyline.width[idx_point] = max_width;
                    //failsafe: try to not go out of the radius of the section, take the width of the merging point for that. (and with some offset)
                    coord_t main_branch_width = pp[biggest_main_branch_id].width.front();
                    coord_t main_branch_dist = pp[biggest_main_branch_id].points.front().distance_to(polyline.points[idx_point]);
                    coord_t max_width_from_main = std::sqrt(main_branch_width*main_branch_width + main_branch_dist*main_branch_dist);
                    if (find_main_branch && polyline.width[idx_point] > max_width_from_main)
                        polyline.width[idx_point] = max_width_from_main;
                    if (find_main_branch && polyline.width[idx_point] > pp[biggest_main_branch_id].width.front() * 1.1)
                        polyline.width[idx_point] = pp[biggest_main_branch_id].width.front() * 1.1;
                    //std::cout << "main fusion, max dist : " << max_width_from_main << "\n";

                    ++idx_point;
                }
                if (idx_point < best_candidate->points.size()) {
                    if (idx_point + 1 < best_candidate->points.size()) {
                        //create a new polyline
                        pp.emplace_back();
                        pp.back().endpoints.first = true;
                        pp.back().endpoints.second = best_candidate->endpoints.second;
                        for (size_t idx_point_new_line = idx_point; idx_point_new_line < best_candidate->points.size(); ++idx_point_new_line) {
                            pp.back().points.push_back(best_candidate->points[idx_point_new_line]);
                            pp.back().width.push_back(best_candidate->width[idx_point_new_line]);
                        }
                    } else {
                        //Add last point
                        polyline.points.push_back(best_candidate->points[idx_point]);
                        polyline.width.push_back(best_candidate->width[idx_point]);
                        //select if an end occur
                        polyline.endpoints.second &= best_candidate->endpoints.second;
                    }

                } else {
                    //select if an end occur
                    polyline.endpoints.second &= best_candidate->endpoints.second;
                }

                //remove points that are the same or too close each other, ie simplify
                for (size_t idx_point = 1; idx_point < polyline.points.size(); ++idx_point) {
                    if (polyline.points[idx_point - 1].distance_to(polyline.points[idx_point]) < SCALED_EPSILON) {
                        if (idx_point < polyline.points.size() - 1) {
                            polyline.points.erase(polyline.points.begin() + idx_point);
                            polyline.width.erase(polyline.width.begin() + idx_point);
                        } else {
                            polyline.points.erase(polyline.points.begin() + idx_point - 1);
                            polyline.width.erase(polyline.width.begin() + idx_point - 1);
                        }
                        --idx_point;
                    }
                }
                //remove points that are outside of the geometry
                for (size_t idx_point = 0; idx_point < polyline.points.size(); ++idx_point) {
                    if (!bounds->contains_b(polyline.points[idx_point])) {
                        polyline.points.erase(polyline.points.begin() + idx_point);
                        polyline.width.erase(polyline.width.begin() + idx_point);
                        --idx_point;
                    }
                }

                if (polyline.points.size() < 2) {
                    //remove self
                    pp.erase(pp.begin() + i);
                    --i;
                    --best_idx;
                } else {
                    //update cache
                    coeff_angle_cache[polyline.points.back()] = coeff_angle_poly * coeff_poly + coeff_angle_candi * coeff_candi;
                }

                pp.erase(pp.begin() + best_idx);
                //{
                //    std::stringstream stri;
                //    stri << "medial_axis_2.0_aft_fus_" << id << "_" << idf << ".svg";
                //    SVG svg(stri.str());
                //    svg.draw(bounds);
                //    svg.draw(this->expolygon);
                //    svg.draw(pp);
                //    svg.Close();
                //}
                changes = true;
                break;
            }
        }
    }
}

void
MedialAxis::remove_too_thin_extrusion(ThickPolylines& pp)
{
    // remove too thin extrusion at start & end of polylines
    bool changes = false;
    for (size_t i = 0; i < pp.size(); ++i) {
        ThickPolyline& polyline = pp[i];
        // remove bits with too small extrusion
        while (polyline.points.size() > 1 && polyline.width.front() < this->min_width && polyline.endpoints.first) {
            //try to split if possible
            if (polyline.width[1] > min_width) {
                double percent_can_keep = (min_width - polyline.width[0]) / (polyline.width[1] - polyline.width[0]);
                if (polyline.points.front().distance_to(polyline.points[1]) * (1 - percent_can_keep) > SCALED_RESOLUTION) {
                    //Can split => move the first point and assign a new weight.
                    //the update of endpoints wil be performed in concatThickPolylines
                    polyline.points.front() = polyline.points.front().interpolate(percent_can_keep, polyline.points[1]);
                    polyline.width.front() = min_width;
                } else {
                    /// almost 0-length, Remove
                    polyline.points.erase(polyline.points.begin());
                    polyline.width.erase(polyline.width.begin());
                }
                changes = true;
                break;
            }
            polyline.points.erase(polyline.points.begin());
            polyline.width.erase(polyline.width.begin());
            changes = true;
        }
        while (polyline.points.size() > 1 && polyline.width.back() < this->min_width && polyline.endpoints.second) {
            //try to split if possible
            if (polyline.width[polyline.points.size() - 2] > min_width) {
                double percent_can_keep = (min_width - polyline.width.back()) / (polyline.width[polyline.points.size() - 2] - polyline.width.back());
                if (polyline.points.back().distance_to(polyline.points[polyline.points.size() - 2]) * (1 - percent_can_keep) > SCALED_RESOLUTION) {
                    //Can split => move the first point and assign a new weight.
                    //the update of endpoints wil be performed in concatThickPolylines
                    polyline.points.back() = polyline.points.back().interpolate(percent_can_keep, polyline.points[polyline.points.size() - 2]);
                    polyline.width.back() = min_width;
                } else {
                    /// almost 0-length, Remove
                    polyline.points.erase(polyline.points.end() - 1);
                    polyline.width.erase(polyline.width.end() - 1);
                }
                changes = true;
                break;
            }
            polyline.points.erase(polyline.points.end() - 1);
            polyline.width.erase(polyline.width.end() - 1);
            changes = true;
        }
        //remove points and bits that comes from a "main line"
        if (polyline.points.size() < 2 || (changes && polyline.length() < max_width && polyline.points.size() ==2)) {
            //remove self if too small
            pp.erase(pp.begin() + i);
            --i;
        }
    }
    if (changes) concatThickPolylines(pp);
}

void
MedialAxis::concatenate_polylines_with_crossing(ThickPolylines& pp)
{

    // concatenate, but even where multiple thickpolyline join, to create nice long strait polylines
    /*  If we removed any short polylines we now try to connect consecutive polylines
    in order to allow loop detection. Note that this algorithm is greedier than
    MedialAxis::process_edge_neighbors() as it will connect random pairs of
    polylines even when more than two start from the same point. This has no
    drawbacks since we optimize later using nearest-neighbor which would do the
    same, but should we use a more sophisticated optimization algorithm we should
    not connect polylines when more than two meet.
    Optimisation of the old algorithm : now we select the most "strait line" choice
    when we merge with an other line at a point with more than two meet.
    */
    for (size_t i = 0; i < pp.size(); ++i) {
        ThickPolyline& polyline = pp[i];
        if (polyline.endpoints.first && polyline.endpoints.second) continue; // optimization

        ThickPolyline* best_candidate = nullptr;
        float best_dot = -1;
        size_t best_idx = 0;

        // find another polyline starting here
        for (size_t j = 0; j < pp.size(); ++j) {
            if (j == i) continue;
            ThickPolyline& other = pp[j];
            if (other.endpoints.first && other.endpoints.second) continue;
            bool me_reverse = false;
            bool other_reverse = false;
            if (polyline.last_point().coincides_with(other.last_point())) {
                other_reverse = true;
            } else if (polyline.first_point().coincides_with(other.last_point())) {
                me_reverse = true;
                other_reverse = true;
            } else if (polyline.first_point().coincides_with(other.first_point())) {
                me_reverse = true;
            } else if (!polyline.last_point().coincides_with(other.first_point())) {
                continue;
            }

            Vec2d v_poly(me_reverse ? polyline.lines().front().vector().x() : polyline.lines().back().vector().x(), 
                me_reverse ? polyline.lines().front().vector().y() : polyline.lines().back().vector().y());
            v_poly *= (1 / std::sqrt(v_poly.x()*v_poly.x() + v_poly.y()*v_poly.y()));
            Vec2d v_other(other_reverse ? other.lines().back().vector().x() : other.lines().front().vector().x(),
                other_reverse ? other.lines().back().vector().y() : other.lines().front().vector().y());
            v_other *= (1 / std::sqrt(v_other.x()*v_other.x() + v_other.y()*v_other.y()));
            float other_dot = std::abs(float( v_poly.x()*v_other.x() + v_poly.y()*v_other.y() ));
            if (other_dot > best_dot) {
                best_candidate = &other;
                best_idx = j;
                best_dot = other_dot;
            }
        }
        if (best_candidate != nullptr && best_candidate->points.size() > 1) {
            if (polyline.last_point().coincides_with(best_candidate->last_point())) {
                best_candidate->reverse();
            } else if (polyline.first_point().coincides_with(best_candidate->last_point())) {
                polyline.reverse();
                best_candidate->reverse();
            } else if (polyline.first_point().coincides_with(best_candidate->first_point())) {
                polyline.reverse();
            }
            //intersections may create over-extrusion because the included circle can be a bit larger. We have to make it short again if needed.
            if (polyline.points.size() > 1 && best_candidate->points.size() > 1
                    && polyline.width.back() > polyline.width[polyline.width.size() - 2]
                    && polyline.width.back() > best_candidate->width[1]) {
                polyline.width.back() = std::min(polyline.width[polyline.width.size() - 2], best_candidate->width[1]);
            }
            polyline.points.insert(polyline.points.end(), best_candidate->points.begin() + 1, best_candidate->points.end());
            polyline.width.insert(polyline.width.end(), best_candidate->width.begin() + 1, best_candidate->width.end());
            polyline.endpoints.second = best_candidate->endpoints.second;
            assert(polyline.width.size() == polyline.points.size());
            if (best_idx < i) i--;
            pp.erase(pp.begin() + best_idx);
        }
    }
}

void
MedialAxis::remove_too_thin_points(ThickPolylines& pp)
{
    //remove too thin polylines points (inside a polyline : split it)
    for (size_t i = 0; i < pp.size(); ++i) {
        ThickPolyline& polyline = pp[i];

        // remove bits with too small extrusion
        size_t idx_point = 0;
        while (idx_point<polyline.points.size()) {
            if (polyline.width[idx_point] < min_width) {
                if (idx_point == 0) {
                    //too thin at start
                    polyline.points.erase(polyline.points.begin());
                    polyline.width.erase(polyline.width.begin());
                    idx_point = 0;
                } else if (idx_point == 1) {
                    //too thin at start
                    polyline.points.erase(polyline.points.begin());
                    polyline.width.erase(polyline.width.begin());
                    polyline.points.erase(polyline.points.begin());
                    polyline.width.erase(polyline.width.begin());
                    idx_point = 0;
                } else if (idx_point == polyline.points.size() - 2) {
                    //too thin at (near) end
                    polyline.points.erase(polyline.points.end() - 1);
                    polyline.width.erase(polyline.width.end() - 1);
                    polyline.points.erase(polyline.points.end() - 1);
                    polyline.width.erase(polyline.width.end() - 1);
                } else if (idx_point == polyline.points.size() - 1) {
                    //too thin at end
                    polyline.points.erase(polyline.points.end() - 1);
                    polyline.width.erase(polyline.width.end() - 1);
                } else {
                    //too thin in middle : split
                    pp.emplace_back();
                    ThickPolyline &newone = pp.back();
                    newone.points.insert(newone.points.begin(), polyline.points.begin() + idx_point + 1, polyline.points.end());
                    newone.width.insert(newone.width.begin(), polyline.width.begin() + idx_point + 1, polyline.width.end());
                    polyline.points.erase(polyline.points.begin() + idx_point, polyline.points.end());
                    polyline.width.erase(polyline.width.begin() + idx_point, polyline.width.end());
                }
            } else idx_point++;

            if (polyline.points.size() < 2) {
                //remove self if too small
                pp.erase(pp.begin() + i);
                --i;
                break;
            }
        }
    }
}

void
MedialAxis::remove_too_short_polylines(ThickPolylines& pp, const coord_t min_size)
{
    //remove too short polyline
    bool changes = true;
    while (changes) {
        changes = false;

        double shortest_size = min_size;
        size_t shortest_idx = -1;
        for (size_t i = 0; i < pp.size(); ++i) {
            ThickPolyline& polyline = pp[i];
            // Remove the shortest polylines : polyline that are shorter than wider
            // (we can't do this check before endpoints extension and clipping because we don't
            // know how long will the endpoints be extended since it depends on polygon thickness
            // which is variable - extension will be <= max_width/2 on each side) 
            if ((polyline.endpoints.first || polyline.endpoints.second)
                && polyline.length() < max_width / 2) {
                if (shortest_size > polyline.length()) {
                    shortest_size = polyline.length();
                    shortest_idx = i;
                }

            }
        }
        if (shortest_idx < pp.size()) {
            pp.erase(pp.begin() + shortest_idx);
            changes = true;
        }
        if (changes) concatThickPolylines(pp);
    }
}

void
MedialAxis::check_width(ThickPolylines& pp, double max_width, std::string msg)
{
    //remove empty polyline
    int nb = 0;
    for (size_t i = 0; i < pp.size(); ++i) {
        for (size_t j = 0; j < pp[i].width.size(); ++j) {
            if (pp[i].width[j] > max_width * 1.01) {
                std::cout << "Error " << msg << " width " << unscaled(pp[i].width[j]) << "(" << i << ":" << j << ") > " << unscaled(max_width) << "\n";
                nb++;
            }
        }
    }
    if (nb > 0) std::cout << "== nbBig = " << nb << " ==\n";
}

void
MedialAxis::ensure_not_overextrude(ThickPolylines& pp)
{
    //ensure the volume extruded is correct for what we have been asked
    // => don't over-extrude
    double surface = 0;
    double volume = 0;
    for (ThickPolyline& polyline : pp) {
        for (ThickLine &l : polyline.thicklines()) {
            surface += l.length() * (l.a_width + l.b_width) / 2;
            double width_mean = (l.a_width + l.b_width) / 2;
            volume += height * (width_mean - height * (1. - 0.25 * PI)) * l.length();
        }
    }

    // compute bounds volume
    double boundsVolume = 0;
    boundsVolume += height*bounds->area();
    // add external "perimeter gap"
    double perimeterRoundGap = bounds->contour.length() * height * (1 - 0.25*PI) * 0.5;
    // add holes "perimeter gaps"
    double holesGaps = 0;
    for (const Polygon &hole : bounds->holes) {
        holesGaps += hole.length() * height * (1 - 0.25*PI) * 0.5;
    }
    boundsVolume += perimeterRoundGap + holesGaps;

    if (boundsVolume < volume) {
        //reduce width
        double reduce_by = boundsVolume / volume;
        for (ThickPolyline& polyline : pp) {
            for (coordf_t &width : polyline.width) {
                width *= reduce_by;
            }
        }
    }
}

void
MedialAxis::simplify_polygon_frontier()
{
    //it will remove every point in the surface contour that aren't on the bounds contour
    this->expolygon = this->surface;
    this->expolygon.contour.remove_collinear(SCALED_EPSILON);
    for (Polygon &hole : this->expolygon.holes)
        hole.remove_collinear(SCALED_EPSILON);
    if (&this->surface != this->bounds) {
        bool need_intersect = false;
        for (size_t i = 0; i < this->expolygon.contour.points.size(); i++) {
            Point &p_check = this->expolygon.contour.points[i];
            //if (!find) {
            if (!bounds->has_boundary_point(p_check)) {
                //check if we put it at a bound point instead of delete it
                size_t prev_i = i == 0 ? this->expolygon.contour.points.size() - 1 : (i - 1);
                size_t next_i = i == this->expolygon.contour.points.size() - 1 ? 0 : (i + 1);
                const Point* closest = bounds->contour.closest_point(p_check);
                if (closest != nullptr && closest->distance_to(p_check) + SCALED_EPSILON
                    < std::min(p_check.distance_to(this->expolygon.contour.points[prev_i]), p_check.distance_to(this->expolygon.contour.points[next_i])) / 2) {
                    p_check.x() = closest->x();
                    p_check.y() = closest->y();
                    need_intersect = true;
                } else {
                    this->expolygon.contour.points.erase(this->expolygon.contour.points.begin() + i);
                    i--;
                }
            }
        }
        if (need_intersect) {
            ExPolygons simplified_polygons = intersection_ex(this->expolygon, *bounds);
            if (simplified_polygons.size() == 1) {
                this->expolygon = simplified_polygons[0];
            } else {
                //can't simplify that much, reuse the given one
                this->expolygon = this->surface;
                this->expolygon.contour.remove_collinear(SCALED_EPSILON);
                for (Polygon &hole : this->expolygon.holes)
                    hole.remove_collinear(SCALED_EPSILON);
            }
        }
    }

    if (!this->expolygon.contour.points.empty())
        this->expolygon.remove_point_too_near((coord_t)SCALED_RESOLUTION);
}

/// Grow the extrusion to at least nozzle_diameter*1.05 (lowest safe extrusion width)
/// Do not grow points inside the anchor.
void
MedialAxis::grow_to_nozzle_diameter(ThickPolylines& pp, const ExPolygons& anchors)
{
    //compute the min width
    coord_t min_width = this->nozzle_diameter;
    if (this->height > 0) min_width = Flow::new_from_spacing(
        float(unscale_(this->nozzle_diameter)),
        float(unscale_(this->nozzle_diameter)),
        float(unscale_(this->height)), false).scaled_width();
    //ensure the width is not lower than min_width.
    for (ThickPolyline& polyline : pp) {
        for (int i = 0; i < polyline.points.size(); ++i) {
            bool is_anchored = false;
            for (const ExPolygon &poly : anchors) {
                if (poly.contains(polyline.points[i])) {
                    is_anchored = true;
                    break;
                }
            }
            if (!is_anchored && polyline.width[i] < min_width)
                polyline.width[i] = min_width;
        }
    }
}

void
MedialAxis::taper_ends(ThickPolylines& pp)
{
    // minimum size of the taper: be sure to extrude at least the "round edges" of the extrusion (0-spacing extrusion).
    const coord_t min_size = std::max(this->nozzle_diameter * 0.1, this->height * (1. - 0.25 * PI));
    const coordf_t length = std::min(this->taper_size, (this->nozzle_diameter - min_size) / 2);
    if (length <= SCALED_RESOLUTION) return;
    //ensure the width is not lower than min_size.
    for (ThickPolyline& polyline : pp) {
        if (polyline.length() < length * 2.2) continue;
        if (polyline.endpoints.first) {
            polyline.width[0] = min_size;
            coord_t current_dist = min_size;
            coord_t last_dist = min_size;
            for (size_t i = 1; i<polyline.width.size(); ++i) {
                current_dist += (coord_t) polyline.points[i - 1].distance_to(polyline.points[i]);
                if (current_dist > length) {
                    //create a new point if not near enough
                    if (current_dist > length + SCALED_RESOLUTION) {
                        coordf_t percent_dist = (length - last_dist) / (current_dist - last_dist);
                        polyline.points.insert(polyline.points.begin() + i, polyline.points[i - 1].interpolate(percent_dist, polyline.points[i]));
                        polyline.width.insert(polyline.width.begin() + i, polyline.width[i]);
                    }
                    break;
                }
                polyline.width[i] = std::max((coordf_t)min_size, min_size + (polyline.width[i] - min_size) * current_dist / length);
                last_dist = current_dist;
            }
        }
        if (polyline.endpoints.second) {
            polyline.width[polyline.width.size() - 1] = min_size;
            coord_t current_dist = min_size;
            coord_t last_dist = min_size;
            for (size_t i = polyline.width.size()-1; i > 0; --i) {
                current_dist += (coord_t)polyline.points[i].distance_to(polyline.points[i - 1]);
                if (current_dist > length) {
                    //create new point if not near enough
                    if (current_dist > length + SCALED_RESOLUTION) {
                        coordf_t percent_dist = (length - last_dist) / (current_dist - last_dist);
                        polyline.points.insert(polyline.points.begin() + i, polyline.points[i].interpolate(percent_dist, polyline.points[i - 1]));
                        polyline.width.insert(polyline.width.begin() + i, polyline.width[i - 1]);
                    }
                    break;
                }
                polyline.width[i - 1] = std::max((coordf_t)min_size, min_size + (polyline.width[i - 1] - min_size) * current_dist / length);
                last_dist = current_dist;
            }
        }
    }
}

void
MedialAxis::build(ThickPolylines &polylines_out)
{
    //std::cout << this->id << "\n";
    //{
    //    std::stringstream stri;
    //    stri << "medial_axis_0_enter_" << id << ".svg";
    //    SVG svg(stri.str());
    //    svg.draw(this->surface);
    //    svg.Close();
    //}
    simplify_polygon_frontier();
    //{
    //    std::stringstream stri;
    //    stri << "medial_axis_0.5_simplified_" << id << ".svg";
    //    SVG svg(stri.str());
    //    svg.draw(bounds);
    //    svg.draw(this->expolygon);
    //    svg.Close();
    //}
    //safety check
    if (this->expolygon.area() < this->min_width * this->min_width) this->expolygon = this->surface;
    if (this->expolygon.area() < this->min_width * this->min_width) return;

    //std::cout << "simplify_polygon_frontier\n";
    // compute the Voronoi diagram and extract medial axis polylines
    ThickPolylines pp;
    this->polyline_from_voronoi(this->expolygon.lines(), &pp);

    //sanity check, as the voronoi can return (abeit very rarely) randomly high values.
    for (size_t tp_idx = 0; tp_idx < pp.size(); tp_idx++) {
        ThickPolyline& tp = pp[tp_idx];
        for (size_t i = 0; i < tp.width.size(); i++) {
            if (tp.width[i] > this->max_width) {
                tp.width[i] = this->max_width;
            }
        }
        // voronoi bugfix: when we have a wheel, it creates a polyline at the center, completly out of the polygon.
        if (tp.endpoints.first && tp.endpoints.second && !this->expolygon.contains(tp.first_point()) && !this->expolygon.contains(tp.last_point())) {
            //delete this out-of-bounds polyline
            pp.erase(pp.begin() + tp_idx);
            --tp_idx;
        }
    }
    //std::cout << "polyline_from_voronoi\n";
    //{
    //    std::stringstream stri;
    //    stri << "medial_axis_1_voronoi_" << id << ".svg";
    //    SVG svg(stri.str());
    //    //svg.draw(bounds);
    //    svg.draw(this->expolygon);
    //    svg.draw(pp);
    //    svg.Close();
    //}

    //check_width(pp, this->max_width, "polyline_from_voronoi");
    
    concatThickPolylines(pp);

    //std::cout << "concatThickPolylines\n";
    //{
    //    std::stringstream stri;
    //    stri << "medial_axis_1_voronoi_" << id << ".svg";
    //    SVG svg(stri.str());
    //    svg.draw(bounds);
    //    svg.draw(this->expolygon);
    //    svg.draw(pp);
    //    svg.Close();
    //}

    /* Find the maximum width returned; we're going to use this for validating and
       filtering the output segments. */
    double max_w = 0;
    for (ThickPolylines::const_iterator it = pp.begin(); it != pp.end(); ++it)
        max_w = std::max(max_w, *std::max_element(it->width.begin(), it->width.end()));

    //for (auto &p : pp) {
    //    std::cout << "Start polyline : ";
    //    for (auto &w : p.width) {
    //        std::cout << ", " << w;
    //    }
    //    std::cout << "\n";
    //}

    // "remove" the little paths that are at the outside of a curve.
    fusion_curve(pp);
    //{
    //    std::stringstream stri;
    //    stri << "medial_axis_2_curve_" << id << ".svg";
    //    SVG svg(stri.str());
    //    svg.draw(bounds);
    //    svg.draw(this->expolygon);
    //    svg.draw(pp);
    //    svg.Close();
    //}


    // Aligned fusion: Fusion the bits at the end of lines by "increasing thickness"
    // For that, we have to find other lines,
    // and with a next point no more distant than the max width.
    // Then, we can merge the bit from the first point to the second by following the mean.
    //
    main_fusion(pp);
    //{
    //    std::stringstream stri;
    //    stri << "medial_axis_3_fusion_" << id << ".svg";
    //    SVG svg(stri.str());
    //    svg.draw(bounds);
    //    svg.draw(this->expolygon);
    //    svg.draw(pp);
    //    svg.Close();
    //}

    //fusion right-angle corners.
    fusion_corners(pp);

    // Loop through all returned polylines in order to extend their endpoints to the 
    //   expolygon boundaries (if done here, it may be cut later if not thick enough)
    if (stop_at_min_width) {
        extends_line_both_side(pp);
    }

    /*for (auto &p : pp) {
        std::cout << "Fusion polyline : ";
        for (auto &w : p.width) {
            std::cout << ", " << w;
        }
        std::cout << "\n";
    }*/
    //reduce extrusion when it's too thin to be printable
    remove_too_thin_extrusion(pp);
    //{
    //    std::stringstream stri;
    //    stri << "medial_axis_4_thinok_" << id << ".svg";
    //    SVG svg(stri.str());
    //    svg.draw(bounds);
    //    svg.draw(this->expolygon);
    //    svg.draw(pp);
    //    svg.Close();
    //}

    remove_too_thin_points(pp);
    //{
    //    std::stringstream stri;
    //    stri << "medial_axis_5.0_thuinner_" << id << ".svg";
    //    SVG svg(stri.str());
    //    svg.draw(bounds);
    //    svg.draw(this->expolygon);
    //    svg.draw(pp);
    //    svg.Close();
    //}

    // Loop through all returned polylines in order to extend their endpoints to the 
    //   expolygon boundaries
    if (!stop_at_min_width) {
        extends_line_both_side(pp);
    }
    //{
    //    std::stringstream stri;
    //    stri << "medial_axis_5_expand_" << id << ".svg";
    //    SVG svg(stri.str());
    //    svg.draw(*bounds);
    //    svg.draw(this->expolygon);
    //    svg.draw(pp);
    //    svg.Close();
    //}
    concatenate_polylines_with_crossing(pp);
    //{
    //    std::stringstream stri;
    //    stri << "medial_axis_6_concat_" << id << ".svg";
    //    SVG svg(stri.str());
    //    svg.draw(bounds);
    //    svg.draw(this->expolygon);
    //    svg.draw(pp);
    //    svg.Close();
    //}

    remove_too_short_polylines(pp, (coord_t)max_w * 2);
    //{
    //    std::stringstream stri;
    //    stri << "medial_axis_8_tooshort_" << id << ".svg";
    //    SVG svg(stri.str());
    //    svg.draw(bounds);
    //    svg.draw(this->expolygon);
    //    svg.draw(pp);
    //    svg.Close();
    //}

    //TODO: reduce the flow at the intersection ( + ) points ?
    ensure_not_overextrude(pp);
    //{
    //    std::stringstream stri;
    //    stri << "medial_axis_9.1_end_" << id << ".svg";
    //    SVG svg(stri.str());
    //    svg.draw(*bounds);
    //    svg.draw(this->expolygon);
    //    svg.draw(pp);
    //    svg.Close();
    //}
    if (nozzle_diameter != min_width) {
        grow_to_nozzle_diameter(pp, diff_ex(*this->bounds, this->expolygon));
    }
    if(this->taper_size != 0){
        taper_ends(pp);
    }
    //{
    //    std::stringstream stri;
    //    stri << "medial_axis_9.9_endnwithtaper_" << id << ".svg";
    //    SVG svg(stri.str());
    //    svg.draw(*bounds);
    //    svg.draw(this->expolygon);
    //    svg.draw(pp);
    //    svg.Close();
    //}

    remove_bits(pp);

    //sort_polylines(pp);

    //for (auto &p : pp) {
    //    std::cout << " polyline : ";
    //    for (auto &w : p.width) {
    //        std::cout << ", " << w;
    //    }
    //    std::cout << "\n";
    //}

    polylines_out.insert(polylines_out.end(), pp.begin(), pp.end());

}

ExtrusionEntityCollection
thin_variable_width(const ThickPolylines &polylines, ExtrusionRole role, Flow flow)
{
    // this value determines granularity of adaptive width, as G-code does not allow
    // variable extrusion within a single move; this value shall only affect the amount
    // of segments, and any pruning shall be performed before we apply this tolerance
    const double tolerance = 4*SCALED_RESOLUTION;//scale_(0.05);
    
    ExtrusionEntityCollection coll;
    for (const ThickPolyline &p : polylines) {
        ExtrusionPaths paths;
        ExtrusionPath path(role);
        ThickLines lines = p.thicklines();
        
        for (int i = 0; i < (int)lines.size(); ++i) {
            ThickLine& line = lines[i];
            
            const coordf_t line_len = line.length();
            if (line_len < SCALED_EPSILON) continue;
            
            assert(line.a_width >= 0);
            assert(line.b_width >= 0);
            double thickness_delta = fabs(line.a_width - line.b_width);
            if (thickness_delta > tolerance && ceil(thickness_delta / tolerance) > 2) {
                const uint16_t segments = 1+(uint16_t) std::min(16000.0, ceil(thickness_delta / tolerance));
                Points pp;
                std::vector<coordf_t> width;
                {
                    for (size_t j = 0; j < segments; ++j) {
                        pp.push_back(line.a.interpolate(((double)j) / segments, line.b));
                        double percent_width = ((double)j) / (segments-1);
                        width.push_back(line.a_width*(1 - percent_width) + line.b_width*percent_width);
                    }
                    pp.push_back(line.b);

                    assert(pp.size() == segments + 1);
                    assert(width.size() == segments);
                }

                // delete this line and insert new ones
                lines.erase(lines.begin() + i);
                for (size_t j = 0; j < segments; ++j) {
                    ThickLine new_line(pp[j], pp[j + 1]);
                    new_line.a_width = width[j];
                    new_line.b_width = width[j];
                    lines.insert(lines.begin() + i + j, new_line);
                }

                --i;
                continue;
            } else if (thickness_delta > 0) {
                //create a middle point
                ThickLine new_line(line.a.interpolate(0.5, line.b), line.b);
                new_line.a_width = line.b_width;
                new_line.b_width = line.b_width;
                line.b = new_line.a;
                line.b_width = line.a_width;
                lines.insert(lines.begin() + i + 1, new_line);

                --i;
                continue;
            }
            //gapfill : we want to be able to fill the voids (touching the perimeters), so the spacing is what we want.
            //thinwall: we want the extrusion to not go out of the polygon, so the width is what we want.
            //  but we can't extrude with a negative spacing, so we have to gradually fall back to spacing if the width is too small.

            // default: extrude a thin wall that doesn't go outside of the specified width.
            coordf_t wanted_width = unscale<coordf_t>(line.a_width);
            if (role == erGapFill) {
                // Convert from spacing to extrusion width based on the extrusion model
                // of a square extrusion ended with semi circles.
                wanted_width = unscale<coordf_t>(line.a_width) + flow.height * (1. - 0.25 * PI);
            } else if (unscale<coordf_t>(line.a_width) < 2 * flow.height * (1. - 0.25 * PI)) {
                //width (too) small, be sure to not extrude with negative spacing.
                //we began to fall back to spacing gradually even before the spacing go into the negative
                //  to make extrusion1 < extrusion2 if width1 < width2 even if width2 is too small. 
                wanted_width = unscale<coordf_t>(line.a_width)*0.35 + 1.3 * flow.height * (1. - 0.25 * PI);
            }

            if (path.polyline.points.empty()) {
                flow.width = wanted_width;
                path.polyline.append(line.a);
                path.polyline.append(line.b);
                path.mm3_per_mm = flow.mm3_per_mm();
                path.width = flow.width;
                path.height = flow.height;
            } else {
                thickness_delta = scale_(fabs(flow.width - wanted_width));
                if (thickness_delta <= tolerance / 2) {
                    // the width difference between this line and the current flow width is 
                    // within the accepted tolerance
                    path.polyline.append(line.b);
                } else {
                    // we need to initialize a new line
                    paths.emplace_back(std::move(path));
                    path = ExtrusionPath(role);
                    --i;
                }
            }
        }
        if (path.polyline.is_valid())
            paths.emplace_back(std::move(path));
        // Append paths to collection.
        if (!paths.empty()) {
            if (paths.front().first_point().coincides_with(paths.back().last_point())) {
                coll.append(ExtrusionLoop(paths));
            } else {
                if (role == erThinWall){
                    //thin walls : avoid to cut them, please.
                    ExtrusionEntityCollection unsortable_coll(paths);
                    unsortable_coll.no_sort = true;
                    coll.append(unsortable_coll);
                }else //gap fill : cut them as much as you want
                    coll.append(paths);
            }
        }
    }
    return coll;
}

} // namespace Slic3r
