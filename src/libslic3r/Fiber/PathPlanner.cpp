///|/ Copyright (c) Prusa Research 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "PathPlanner.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/BoundingBox.hpp"
#include <boost/log/trivial.hpp>
#include <algorithm>
#include <limits>
#include <cmath>

namespace Slic3r {

double ContinuousPath::length() const
{
    if (polyline.points.size() < 2)
        return 0.0;
    
    double total_length = 0.0;
    for (size_t i = 1; i < polyline.points.size(); ++i) {
        Point diff = polyline.points[i] - polyline.points[i-1];
        total_length += unscale<double>(std::sqrt(double(diff.x() * diff.x() + diff.y() * diff.y())));
    }
    return total_length;
}

ContinuousPaths PathPlanner::plan_continuous_paths(
    const FiberPaths& fiber_paths,
    const ExPolygons& layer_geometry,
    const PathPlanningConfig& config)
{
    ContinuousPaths result;
    
    if (fiber_paths.empty()) {
        return result;
    }
    
    // Step 1: Connect paths into continuous paths
    ContinuousPaths connected_paths = connect_paths(fiber_paths, config);
    
    // Step 2: Validate and filter paths
    for (ContinuousPath& path : connected_paths) {
        std::string error_message;
        if (!validate_path(path, config, error_message)) {
            BOOST_LOG_TRIVIAL(debug) << "PathPlanner::plan_continuous_paths - Invalid path: " << error_message;
            continue;
        }
        
        // Check collisions if enabled
        if (config.enable_collision_detection) {
            std::vector<std::string> warnings;
            if (detect_collisions(path, layer_geometry, result, config, warnings)) {
                BOOST_LOG_TRIVIAL(debug) << "PathPlanner::plan_continuous_paths - Path has collisions, skipping";
                continue;
            }
        }
        
        result.push_back(std::move(path));
    }
    
    // Step 3: Smooth paths if enabled
    if (config.enable_smoothing) {
        smooth_paths(result, config);
    }
    
    // Step 4: Optimize path ordering if enabled
    if (config.enable_optimization) {
        optimize_path_order(result, config);
    }
    
    BOOST_LOG_TRIVIAL(info) << "PathPlanner::plan_continuous_paths - Generated " 
                           << result.size() << " continuous paths from " 
                           << fiber_paths.size() << " segments";
    
    return result;
}

ContinuousPaths PathPlanner::connect_paths(
    const FiberPaths& fiber_paths,
    const PathPlanningConfig& config)
{
    ContinuousPaths result;
    
    if (fiber_paths.empty()) {
        return result;
    }
    
    // Track which paths have been used
    std::vector<bool> used(fiber_paths.size(), false);
    
    // Maximum connection distance in scaled coordinates
    coord_t max_dist_scaled = scale_(config.max_connection_distance);
    
    // Process each unused path as a starting point
    for (size_t start_idx = 0; start_idx < fiber_paths.size(); ++start_idx) {
        if (used[start_idx]) {
            continue;
        }
        
        // Start a new continuous path
        ContinuousPath continuous_path;
        const FiberPath& start_path = fiber_paths[start_idx];
        
        // Initialize with first path
        continuous_path.polyline = start_path.polyline;
        continuous_path.z = start_path.z;
        continuous_path.angle = start_path.angle;
        continuous_path.spacing = start_path.spacing;
        continuous_path.fiber_type = start_path.fiber_type;
        continuous_path.check_closed();
        
        used[start_idx] = true;
        Point current_point = continuous_path.last_point();
        
        // Greedy connection: always connect to nearest unconnected endpoint
        bool found_connection = true;
        while (found_connection) {
            found_connection = false;
            bool reverse_next = false;
            size_t next_idx = find_nearest_endpoint(current_point, fiber_paths, used, reverse_next, config);
            
            if (next_idx < fiber_paths.size()) {
                const FiberPath& next_path = fiber_paths[next_idx];
                
                // Check if connection is within max distance
                double dist_to_start = point_distance(current_point, next_path.first_point());
                double dist_to_end = point_distance(current_point, next_path.last_point());
                
                double min_dist = std::min(dist_to_start, dist_to_end);
                if (min_dist <= max_dist_scaled) {
                    // Connect the paths
                    if (dist_to_end < dist_to_start) {
                        // Connect to end, reverse the path
                        Polyline reversed = next_path.polyline;
                        reversed.reverse();
                        continuous_path.polyline.append(reversed);
                    } else {
                        // Connect to start
                        continuous_path.polyline.append(next_path.polyline);
                    }
                    
                    current_point = continuous_path.last_point();
                    used[next_idx] = true;
                    found_connection = true;
                    continuous_path.check_closed();
                }
            }
        }
        
        // Only add path if it meets minimum length requirement
        if (continuous_path.length() >= config.min_path_length) {
            result.push_back(std::move(continuous_path));
        }
    }
    
    return result;
}

void PathPlanner::optimize_path_order(ContinuousPaths& paths, const PathPlanningConfig& config)
{
    if (paths.size() <= 1) {
        return;
    }
    
    // Simple greedy optimization: reorder paths to minimize travel distance
    // Start with first path
    ContinuousPaths optimized;
    optimized.reserve(paths.size());
    
    std::vector<bool> used(paths.size(), false);
    Point current_point = paths[0].first_point();
    optimized.push_back(std::move(paths[0]));
    used[0] = true;
    
    // For each remaining path, find the nearest one
    for (size_t i = 1; i < paths.size(); ++i) {
        double min_dist = std::numeric_limits<double>::max();
        size_t best_idx = paths.size();
        bool reverse_best = false;
        
        for (size_t j = 0; j < paths.size(); ++j) {
            if (used[j]) {
                continue;
            }
            
            // Check distance to start
            double dist_start = point_distance(current_point, paths[j].first_point());
            if (dist_start < min_dist) {
                min_dist = dist_start;
                best_idx = j;
                reverse_best = false;
            }
            
            // Check distance to end
            double dist_end = point_distance(current_point, paths[j].last_point());
            if (dist_end < min_dist) {
                min_dist = dist_end;
                best_idx = j;
                reverse_best = true;
            }
        }
        
        if (best_idx < paths.size()) {
            ContinuousPath& best_path = paths[best_idx];
            if (reverse_best) {
                best_path.reverse();
            }
            optimized.push_back(std::move(best_path));
            used[best_idx] = true;
            current_point = optimized.back().last_point();
        }
    }
    
    // Add any remaining unused paths
    for (size_t i = 0; i < paths.size(); ++i) {
        if (!used[i]) {
            optimized.push_back(std::move(paths[i]));
        }
    }
    
    paths = std::move(optimized);
}

void PathPlanner::smooth_paths(ContinuousPaths& paths, const PathPlanningConfig& config)
{
    // Simple smoothing: reduce sharp corners
    // This is a basic implementation - can be enhanced with more sophisticated algorithms
    
    coord_t tolerance_scaled = scale_(config.smoothing_tolerance);
    
    for (ContinuousPath& path : paths) {
        if (path.polyline.points.size() < 3) {
            continue; // Need at least 3 points to smooth
        }
        
        Polyline smoothed;
        smoothed.points.reserve(path.polyline.points.size());
        
        // Keep first point
        smoothed.points.push_back(path.polyline.points[0]);
        
        // Smooth intermediate points
        for (size_t i = 1; i < path.polyline.points.size() - 1; ++i) {
            const Point& prev = path.polyline.points[i - 1];
            const Point& curr = path.polyline.points[i];
            const Point& next = path.polyline.points[i + 1];
            
            // Calculate angle at this point
            Vec2d v1 = (curr - prev).cast<double>();
            Vec2d v2 = (next - curr).cast<double>();
            
            double angle = std::acos(v1.normalized().dot(v2.normalized()));
            
            // If angle is too sharp, smooth it
            if (angle < M_PI * 0.8) { // Less than ~144 degrees
                // Use average of neighbors (simple smoothing)
                Point smoothed_point(
                    (prev.x() + curr.x() + next.x()) / 3,
                    (prev.y() + curr.y() + next.y()) / 3
                );
                smoothed.points.push_back(smoothed_point);
            } else {
                smoothed.points.push_back(curr);
            }
        }
        
        // Keep last point
        smoothed.points.push_back(path.polyline.points.back());
        
        path.polyline = std::move(smoothed);
        path.check_closed();
    }
}

bool PathPlanner::detect_collisions(
    const ContinuousPath& path,
    const ExPolygons& layer_geometry,
    const ContinuousPaths& other_paths,
    const PathPlanningConfig& config,
    std::vector<std::string>& warnings) const
{
    bool has_collisions = false;
    
    // Check printer bounds
    if (config.printer_bounds.defined) {
        for (const Point& point : path.polyline.points) {
            if (!config.printer_bounds.contains(point)) {
                warnings.push_back("Path point outside printer bounds");
                has_collisions = true;
            }
        }
    }
    
    // Check collision with layer geometry (if path goes outside)
    // This is simplified - full implementation would check if path stays within geometry
    // For now, we just check if path points are within the geometry
    if (!layer_geometry.empty()) {
        for (const Point& point : path.polyline.points) {
            bool inside = false;
            for (const ExPolygon& expoly : layer_geometry) {
                if (expoly.contour.contains(point)) {
                    // Check if not in a hole
                    bool in_hole = false;
                    for (const Polygon& hole : expoly.holes) {
                        if (hole.contains(point)) {
                            in_hole = true;
                            break;
                        }
                    }
                    if (!in_hole) {
                        inside = true;
                        break;
                    }
                }
            }
            if (!inside) {
                warnings.push_back("Path point outside layer geometry");
                has_collisions = true;
            }
        }
    }
    
    // Check collision with other paths (simplified - check if paths are too close)
    coord_t min_distance_scaled = scale_(config.collision_margin);
    for (const ContinuousPath& other_path : other_paths) {
        for (const Point& p1 : path.polyline.points) {
            for (const Point& p2 : other_path.polyline.points) {
                Point diff = p2 - p1;
                double dist = std::sqrt(double(diff.x() * diff.x() + diff.y() * diff.y()));
                if (dist < min_distance_scaled) {
                    warnings.push_back("Path too close to another path");
                    has_collisions = true;
                    break;
                }
            }
            if (has_collisions) break;
        }
        if (has_collisions) break;
    }
    
    return has_collisions;
}

bool PathPlanner::validate_path(
    const ContinuousPath& path,
    const PathPlanningConfig& config,
    std::string& error_message) const
{
    if (path.empty()) {
        error_message = "Path is empty";
        return false;
    }
    
    if (path.length() < config.min_path_length) {
        error_message = "Path too short";
        return false;
    }
    
    if (path.polyline.points.size() < 2) {
        error_message = "Path has insufficient points";
        return false;
    }
    
    return true;
}

size_t PathPlanner::find_nearest_endpoint(
    const Point& current_point,
    const std::vector<FiberPath>& paths,
    const std::vector<bool>& used,
    bool& reverse,
    const PathPlanningConfig& config) const
{
    size_t best_idx = paths.size();
    double min_dist = std::numeric_limits<double>::max();
    bool reverse_best = false;
    
    coord_t max_dist_scaled = scale_(config.max_connection_distance);
    
    for (size_t i = 0; i < paths.size(); ++i) {
        if (used[i] || paths[i].empty()) {
            continue;
        }
        
        // Check distance to start
        double dist_start = point_distance(current_point, paths[i].first_point());
        if (dist_start < min_dist && dist_start <= max_dist_scaled) {
            min_dist = dist_start;
            best_idx = i;
            reverse_best = false;
        }
        
        // Check distance to end
        double dist_end = point_distance(current_point, paths[i].last_point());
        if (dist_end < min_dist && dist_end <= max_dist_scaled) {
            min_dist = dist_end;
            best_idx = i;
            reverse_best = true;
        }
    }
    
    reverse = reverse_best;
    return best_idx;
}

double PathPlanner::point_distance(const Point& a, const Point& b) const
{
    Point diff = b - a;
    return unscale<double>(std::sqrt(double(diff.x() * diff.x() + diff.y() * diff.y())));
}

} // namespace Slic3r

