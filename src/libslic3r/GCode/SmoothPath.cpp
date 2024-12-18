#include "SmoothPath.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>
#include <cassert>

#include "../ExtrusionEntity.hpp"
#include "../ExtrusionEntityCollection.hpp"
#include "libslic3r/Geometry/ArcWelder.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r::GCode {

// Length of a smooth path.
double length(const SmoothPath &path)
{
    double l = 0;
    for (const SmoothPathElement &el : path)
        l += Geometry::ArcWelder::path_length<double>(el.path);
    return l;
}

// Returns true if the smooth path is longer than a threshold.
bool longer_than(const SmoothPath &path, double length)
{
    for (const SmoothPathElement &el : path) {
        for (auto it = std::next(el.path.begin()); it != el.path.end(); ++ it) {
            length -= Geometry::ArcWelder::segment_length<double>(*std::prev(it), *it);
            if (length < 0)
                return true;
        }
    }
    return length < 0;
}

std::optional<Point> sample_path_point_at_distance_from_start(const SmoothPath &path, double distance)
{
    if (distance >= 0) {
        for (const SmoothPathElement &el : path) {
            Point prev_point = el.path.front().point;
            for (auto segment_it = el.path.begin() + 1; segment_it != el.path.end(); ++segment_it) {
                Point point = segment_it->point;
                if (segment_it->linear()) {
                    // Linear segment
                    const Vec2d  v    = (point - prev_point).cast<double>();
                    const double lsqr = v.squaredNorm();
                    if (lsqr > sqr(distance))
                        return prev_point + (v * (distance / sqrt(lsqr))).cast<coord_t>();
                    distance -= sqrt(lsqr);
                } else {
                    // Circular segment
                    const float  angle = Geometry::ArcWelder::arc_angle(prev_point.cast<float>(), point.cast<float>(), segment_it->radius);
                    const double len   = std::abs(segment_it->radius) * angle;
                    if (len > distance) {
                        const Point center_pt    = Geometry::ArcWelder::arc_center(prev_point.cast<float>(), point.cast<float>(), segment_it->radius, segment_it->ccw()).cast<coord_t>();
                        const float rotation_dir = (segment_it->ccw() ? 1.f : -1.f);
                        // Rotate the segment start point based on the arc orientation.
                        return prev_point.rotated(rotation_dir * angle * (distance / len), center_pt);
                    }

                    distance -= len;
                }

                if (distance < 0)
                    return point;

                prev_point = point;
            }
        }
    }

    // Failed.
    return {};
}

std::optional<Point> sample_path_point_at_distance_from_end(const SmoothPath &path, double distance) {
    SmoothPath path_reversed = path;
    reverse(path_reversed);
    return sample_path_point_at_distance_from_start(path_reversed, distance);
}

// Clip length of a smooth path, for seam hiding.
// When clipping the end of a path, don't create segments shorter than min_point_distance_threshold, 
// rather discard such a degenerate segment.
double clip_end(SmoothPath &path, double distance, double min_point_distance_threshold)
{
    while (! path.empty() && distance > 0) {
        Geometry::ArcWelder::Path &p = path.back().path;
        distance = clip_end(p, distance);
        if (p.empty()) {
            path.pop_back();
        } else {
            // Trailing path was trimmed and it is valid.
            Geometry::ArcWelder::Path &last_path = path.back().path;
            assert(last_path.size() > 1);
            assert(distance == 0);
            // Distance to go is zero.
            // Remove the last segment if its length is shorter than min_point_distance_threshold.
            const Geometry::ArcWelder::Segment &prev_segment = last_path[last_path.size() - 2];
            const Geometry::ArcWelder::Segment &last_segment = last_path.back();
            if (Geometry::ArcWelder::segment_length<double>(prev_segment, last_segment) < min_point_distance_threshold) {
                last_path.pop_back();
                if (last_path.size() < 2)
                    path.pop_back();
            }
            return 0;
        }
    }
    // Return distance to go after the whole smooth path was trimmed to zero.
    return distance;
}

void reverse(SmoothPath &path) {
    std::reverse(path.begin(), path.end());
    for (SmoothPathElement &path_element : path)
        Geometry::ArcWelder::reverse(path_element.path);
}

void SmoothPathCache::interpolate_add(const ExtrusionPath &path, const InterpolationParameters &params)
{
    double tolerance = params.tolerance;
    if (path.role().is_sparse_infill())
        // Use 3x lower resolution than the object fine detail for sparse infill.
        tolerance *= 3.;
    else if (path.role().is_support())
        // Use 4x lower resolution than the object fine detail for support.
        tolerance *= 4.;
    else if (path.role().is_skirt())
        // Brim is currently marked as skirt.
        // Use 4x lower resolution than the object fine detail for skirt & brim.
        tolerance *= 4.;
    m_cache[&path.polyline] = Slic3r::Geometry::ArcWelder::fit_path(path.polyline.points, tolerance, params.fit_circle_tolerance);
}

void SmoothPathCache::interpolate_add(const ExtrusionMultiPath &multi_path, const InterpolationParameters &params)
{
    for (const ExtrusionPath &path : multi_path.paths)
        this->interpolate_add(path, params);
}

void SmoothPathCache::interpolate_add(const ExtrusionLoop &loop, const InterpolationParameters &params)
{
    for (const ExtrusionPath &path : loop.paths)
        this->interpolate_add(path, params);
}

void SmoothPathCache::interpolate_add(const ExtrusionEntityCollection &eec, const InterpolationParameters &params)
{
    for (const ExtrusionEntity *ee : eec) {
        if (ee->is_collection())
            this->interpolate_add(*static_cast<const ExtrusionEntityCollection*>(ee), params);
        else if (const ExtrusionPath *path = dynamic_cast<const ExtrusionPath*>(ee); path)
            this->interpolate_add(*path, params);
        else if (const ExtrusionMultiPath *multi_path = dynamic_cast<const ExtrusionMultiPath*>(ee); multi_path)
            this->interpolate_add(*multi_path, params);
        else if (const ExtrusionLoop *loop = dynamic_cast<const ExtrusionLoop*>(ee); loop)
            this->interpolate_add(*loop, params);
        else
            assert(false);
    }
}

const Geometry::ArcWelder::Path* SmoothPathCache::resolve(const Polyline *pl) const
{
    auto it = m_cache.find(pl);
    return it == m_cache.end() ? nullptr : &it->second;
}

const Geometry::ArcWelder::Path* SmoothPathCache::resolve(const ExtrusionPath &path) const
{
    return this->resolve(&path.polyline);
}

Geometry::ArcWelder::Path SmoothPathCache::resolve_or_fit(const ExtrusionPath &path, bool reverse, double tolerance) const
{
    Geometry::ArcWelder::Path out;
    if (const Geometry::ArcWelder::Path *cached = this->resolve(path); cached)
        out = *cached;
    else
        out = Geometry::ArcWelder::fit_polyline(path.polyline.points, tolerance);
    if (reverse)
        Geometry::ArcWelder::reverse(out);
    return out;
}

SmoothPath SmoothPathCache::resolve_or_fit(tcb::span<const ExtrusionPath> paths, bool reverse, double resolution) const
{
    SmoothPath out;
    out.reserve(paths.size());
    if (reverse) {
        for (auto it = paths.rbegin(); it != paths.rend(); ++ it)
            out.push_back({ it->attributes(), this->resolve_or_fit(*it, true, resolution) });
    } else {
        for (auto it = paths.begin(); it != paths.end(); ++ it)
            out.push_back({ it->attributes(), this->resolve_or_fit(*it, false, resolution) });
    }
    return out;
}

SmoothPath SmoothPathCache::resolve_or_fit(const ExtrusionMultiPath &multipath, bool reverse, double resolution) const
{
    return this->resolve_or_fit(tcb::span{multipath.paths}, reverse, resolution);
}

SmoothPath SmoothPathCache::resolve_or_fit_split_with_seam(
    const ExtrusionLoop &loop, const bool reverse, const double resolution,
    const Point &seam_point, const double seam_point_merge_distance_threshold) const
{
    SmoothPath out = this->resolve_or_fit(tcb::span{loop.paths}, reverse, resolution);
    assert(! out.empty());
    if (! out.empty()) {
        // Find a closest point on a vector of smooth paths.
        Geometry::ArcWelder::PathSegmentProjection proj;
        int                                        proj_path = -1;
        for (const SmoothPathElement &el : out)
            if (Geometry::ArcWelder::PathSegmentProjection this_proj = Geometry::ArcWelder::point_to_path_projection(el.path, seam_point, proj.distance2);
                this_proj.valid()) {
                // Found a better (closer) projection.
                assert(this_proj.distance2 < proj.distance2);
                assert(this_proj.segment_id >= 0 && this_proj.segment_id < el.path.size());
                proj = this_proj;
                proj_path = &el - out.data();
                if (proj.distance2 == 0)
                    // There will be no better split point found than one with zero distance.
                    break;
            }
        assert(proj_path >= 0);
        // Split the path at the closest point.
        Geometry::ArcWelder::Path &path = out[proj_path].path;
        std::pair<Geometry::ArcWelder::Path, Geometry::ArcWelder::Path> split = Geometry::ArcWelder::split_at(
            path, proj, seam_point_merge_distance_threshold);
        if (split.second.empty()) {
            std::rotate(out.begin(), out.begin() + proj_path + 1, out.end());
            assert(out.back().path == split.first);
        } else {
            ExtrusionAttributes attr = out[proj_path].path_attributes;
            std::rotate(out.begin(), out.begin() + proj_path, out.end());
            out.front().path = std::move(split.second);
            if (! split.first.empty()) {
                if (out.back().path_attributes == attr) {
                    // Merge with the last segment.
                    out.back().path.insert(out.back().path.end(), std::next(split.first.begin()), split.first.end());
                } else
                    out.push_back({ attr, std::move(split.first) });
            }
        }
    }

    return out;
}

} // namespace Slic3r::GCode
