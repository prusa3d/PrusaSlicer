///|/ Copyright (c) Prusa Research 2016 - 2023 Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena, Enrico Turri @enricoturri1966
///|/ Copyright (c) SuperSlicer 2023 Remi Durand @supermerill
///|/ Copyright (c) Slic3r 2013 - 2016 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2014 Petr Ledvina @ledvinap
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "ExtrusionEntity.hpp"

#include <limits>
#include <iterator>

#include "ExtrusionEntityCollection.hpp"
#include "ClipperUtils.hpp"
#include "Exception.hpp"
#include "Flow.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r {

void ExtrusionPath::intersect_expolygons(const ExPolygons &collection, ExtrusionEntityCollection* retval) const
{
    this->_inflate_collection(intersection_pl(Polylines{ polyline }, collection), retval);
}

void ExtrusionPath::subtract_expolygons(const ExPolygons &collection, ExtrusionEntityCollection* retval) const
{
    this->_inflate_collection(diff_pl(Polylines{ this->polyline }, collection), retval);
}

void ExtrusionPath::clip_end(double distance)
{
    this->polyline.clip_end(distance);
}

void ExtrusionPath::simplify(double tolerance)
{
    this->polyline.simplify(tolerance);
}

double ExtrusionPath::length() const
{
    return this->polyline.length();
}

void ExtrusionPath::_inflate_collection(const Polylines &polylines, ExtrusionEntityCollection* collection) const
{
    for (const Polyline &polyline : polylines)
        collection->entities.emplace_back(new ExtrusionPath(polyline, this->attributes()));
}

void ExtrusionPath::polygons_covered_by_width(Polygons &out, const float scaled_epsilon) const
{
    polygons_append(out, offset(this->polyline, float(scale_(m_attributes.width/2)) + scaled_epsilon));
}

void ExtrusionPath::polygons_covered_by_spacing(Polygons &out, const float scaled_epsilon) const
{
    // Instantiating the Flow class to get the line spacing.
    // Don't know the nozzle diameter, setting to zero. It shall not matter it shall be optimized out by the compiler.
    bool bridge = this->role().is_bridge();
    assert(! bridge || m_attributes.width == m_attributes.height);
    auto flow = bridge ? Flow::bridging_flow(m_attributes.width, 0.f) : Flow(m_attributes.width, m_attributes.height, 0.f);
    polygons_append(out, offset(this->polyline, 0.5f * float(flow.scaled_spacing()) + scaled_epsilon));
}

void ExtrusionMultiPath::reverse()
{
    for (ExtrusionPath &path : this->paths)
        path.reverse();
    std::reverse(this->paths.begin(), this->paths.end());
}

double ExtrusionMultiPath::length() const
{
    double len = 0;
    for (const ExtrusionPath &path : this->paths)
        len += path.polyline.length();
    return len;
}

void ExtrusionMultiPath::polygons_covered_by_width(Polygons &out, const float scaled_epsilon) const
{
    for (const ExtrusionPath &path : this->paths)
        path.polygons_covered_by_width(out, scaled_epsilon);
}

void ExtrusionMultiPath::polygons_covered_by_spacing(Polygons &out, const float scaled_epsilon) const
{
    for (const ExtrusionPath &path : this->paths)
        path.polygons_covered_by_spacing(out, scaled_epsilon);
}

double ExtrusionMultiPath::min_mm3_per_mm() const
{
    double min_mm3_per_mm = std::numeric_limits<double>::max();
    for (const ExtrusionPath &path : this->paths)
        min_mm3_per_mm = std::min(min_mm3_per_mm, path.min_mm3_per_mm());
    return min_mm3_per_mm;
}

Polyline ExtrusionMultiPath::as_polyline() const
{
    Polyline out;
    if (! paths.empty()) {
        size_t len = 0;
        for (size_t i_path = 0; i_path < paths.size(); ++ i_path) {
            assert(! paths[i_path].polyline.points.empty());
            assert(i_path == 0 || paths[i_path - 1].polyline.points.back() == paths[i_path].polyline.points.front());
            len += paths[i_path].polyline.points.size();
        }
        // The connecting points between the segments are equal.
        len -= paths.size() - 1;
        assert(len > 0);
        out.points.reserve(len);
        out.points.push_back(paths.front().polyline.points.front());
        for (size_t i_path = 0; i_path < paths.size(); ++ i_path)
            out.points.insert(out.points.end(), paths[i_path].polyline.points.begin() + 1, paths[i_path].polyline.points.end());
    }
    return out;
}

double ExtrusionLoop::area() const
{
    double a = 0;
    for (const ExtrusionPath &path : this->paths) {
        assert(path.size() >= 2);
        if (path.size() >= 2) {
            // Assumming that the last point of one path segment is repeated at the start of the following path segment.
            auto it = path.polyline.points.begin();
            Point prev = *it ++;
            for (; it != path.polyline.points.end(); ++ it) {
                a += cross2(prev.cast<double>(), it->cast<double>());
                prev = *it;
            }
        }
    }
    return a * 0.5;
}

void ExtrusionLoop::reverse()
{
#if 0
    this->reverse_loop();
#else
    throw Slic3r::LogicError("ExtrusionLoop::reverse() must NOT be called");
#endif
}

void ExtrusionLoop::reverse_loop()
{
    for (ExtrusionPath &path : this->paths)
        path.reverse();
    std::reverse(this->paths.begin(), this->paths.end());
}

Polygon ExtrusionLoop::polygon() const
{
    Polygon polygon;
    for (const ExtrusionPath &path : this->paths) {
        // for each polyline, append all points except the last one (because it coincides with the first one of the next polyline)
        polygon.points.insert(polygon.points.end(), path.polyline.points.begin(), path.polyline.points.end()-1);
    }
    return polygon;
}

double ExtrusionLoop::length() const
{
    double len = 0;
    for (const ExtrusionPath &path : this->paths)
        len += path.polyline.length();
    return len;
}

bool ExtrusionLoop::split_at_vertex(const Point &point, const double scaled_epsilon)
{
    for (ExtrusionPaths::iterator path = this->paths.begin(); path != this->paths.end(); ++path)
        if (int idx = path->polyline.find_point(point, scaled_epsilon); idx != -1) {
            if (this->paths.size() == 1) {
                // just change the order of points
                path->polyline.points.insert(path->polyline.points.end(), path->polyline.points.begin() + 1, path->polyline.points.begin() + idx + 1);
                path->polyline.points.erase(path->polyline.points.begin(), path->polyline.points.begin() + idx);
            } else {
                // new paths list starts with the second half of current path
                ExtrusionPaths new_paths;
                new_paths.reserve(this->paths.size() + 1);
                {
                    ExtrusionPath p = *path;
                    p.polyline.points.erase(p.polyline.points.begin(), p.polyline.points.begin() + idx);
                    if (p.polyline.is_valid())
                        new_paths.emplace_back(std::move(p));
                }
            
                // then we add all paths until the end of current path list
                std::move(path + 1, this->paths.end(), std::back_inserter(new_paths)); // not including this path

                // then we add all paths since the beginning of current list up to the previous one
                std::move(this->paths.begin(), path, std::back_inserter(new_paths)); // not including this path
            
                // finally we add the first half of current path
                {
                    ExtrusionPath &p = *path;
                    p.polyline.points.erase(p.polyline.points.begin() + idx + 1, p.polyline.points.end());
                    if (p.polyline.is_valid())
                        new_paths.emplace_back(std::move(p));
                }
                // we can now override the old path list with the new one and stop looping
                this->paths = std::move(new_paths);
            }
            return true;
        }
    // The point was not found.
    return false;
}

ExtrusionLoop::ClosestPathPoint ExtrusionLoop::get_closest_path_and_point(const Point &point, bool prefer_non_overhang) const
{
    // Find the closest path and closest point belonging to that path. Avoid overhangs, if asked for.
    ClosestPathPoint out { 0, 0 };
    double           min2 = std::numeric_limits<double>::max();
    ClosestPathPoint best_non_overhang { 0, 0 };
    double           min2_non_overhang = std::numeric_limits<double>::max();
    for (const ExtrusionPath &path : this->paths) {
        std::pair<int, Point> foot_pt_ = foot_pt(path.polyline.points, point);
        double d2 = (foot_pt_.second - point).cast<double>().squaredNorm();
        if (d2 < min2) {
            out.foot_pt     = foot_pt_.second;
            out.path_idx    = &path - &this->paths.front();
            out.segment_idx = foot_pt_.first;
            min2            = d2;
        }
        if (prefer_non_overhang && ! path.role().is_bridge() && d2 < min2_non_overhang) {
            best_non_overhang.foot_pt     = foot_pt_.second;
            best_non_overhang.path_idx    = &path - &this->paths.front();
            best_non_overhang.segment_idx = foot_pt_.first;
            min2_non_overhang             = d2;
        }
    }
    if (prefer_non_overhang && min2_non_overhang != std::numeric_limits<double>::max())
        // Only apply the non-overhang point if there is one.
        out = best_non_overhang;
    return out;
}

// Splitting an extrusion loop, possibly made of multiple segments, some of the segments may be bridging.
void ExtrusionLoop::split_at(const Point &point, bool prefer_non_overhang, const double scaled_epsilon)
{
    if (this->paths.empty())
        return;
    
    auto [path_idx, segment_idx, p] = get_closest_path_and_point(point, prefer_non_overhang);

    // Snap p to start or end of segment_idx if closer than scaled_epsilon.
    {
        const Point *p1 = this->paths[path_idx].polyline.points.data() + segment_idx;
        const Point *p2 = p1;
        ++ p2;
        double d2_1 = (point - *p1).cast<double>().squaredNorm();
        double d2_2 = (point - *p2).cast<double>().squaredNorm();
        const double thr2 = scaled_epsilon * scaled_epsilon;
        if (d2_1 < d2_2) {
            if (d2_1 < thr2)
                p = *p1;
        } else {
            if (d2_2 < thr2) 
                p = *p2;
        }
    }
    
    // now split path_idx in two parts
    const ExtrusionPath &path = this->paths[path_idx];
    ExtrusionPath p1(path.attributes());
    ExtrusionPath p2(path.attributes());
    path.polyline.split_at(p, &p1.polyline, &p2.polyline);
    
    if (this->paths.size() == 1) {
        if (p2.polyline.is_valid()) {
            if (p1.polyline.is_valid())
                p2.polyline.points.insert(p2.polyline.points.end(), p1.polyline.points.begin() + 1, p1.polyline.points.end());
            this->paths.front().polyline.points = std::move(p2.polyline.points);
        } else
            this->paths.front().polyline.points = std::move(p1.polyline.points);
    } else {
        // install the two paths
        this->paths.erase(this->paths.begin() + path_idx);
        if (p2.polyline.is_valid()) this->paths.insert(this->paths.begin() + path_idx, p2);
        if (p1.polyline.is_valid()) this->paths.insert(this->paths.begin() + path_idx, p1);
    }
    
    // split at the new vertex
    this->split_at_vertex(p, 0.);
}

void ExtrusionLoop::clip_end(double distance, ExtrusionPaths* paths) const
{
    *paths = this->paths;
    
    while (distance > 0 && !paths->empty()) {
        ExtrusionPath &last = paths->back();
        double len = last.length();
        if (len <= distance) {
            paths->pop_back();
            distance -= len;
        } else {
            last.polyline.clip_end(distance);
            break;
        }
    }
}

bool ExtrusionLoop::has_overhang_point(const Point &point) const
{
    for (const ExtrusionPath &path : this->paths) {
        int pos = path.polyline.find_point(point);
        if (pos != -1) {
            // point belongs to this path
            // we consider it overhang only if it's not an endpoint
            return (path.role().is_bridge() && pos > 0 && pos != int(path.polyline.points.size())-1);
        }
    }
    return false;
}

void ExtrusionLoop::polygons_covered_by_width(Polygons &out, const float scaled_epsilon) const
{
    for (const ExtrusionPath &path : this->paths)
        path.polygons_covered_by_width(out, scaled_epsilon);
}

void ExtrusionLoop::polygons_covered_by_spacing(Polygons &out, const float scaled_epsilon) const
{
    for (const ExtrusionPath &path : this->paths)
        path.polygons_covered_by_spacing(out, scaled_epsilon);
}

double ExtrusionLoop::min_mm3_per_mm() const
{
    double min_mm3_per_mm = std::numeric_limits<double>::max();
    for (const ExtrusionPath &path : this->paths)
        min_mm3_per_mm = std::min(min_mm3_per_mm, path.min_mm3_per_mm());
    return min_mm3_per_mm;
}

}
