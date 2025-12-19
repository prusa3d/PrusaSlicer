///|/ Copyright (c) Prusa Research 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "FiberPlacement.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Line.hpp"
#include "libslic3r/Point.hpp"
#include <boost/log/trivial.hpp>
#include <cmath>
#include <algorithm>

namespace Slic3r {

bool FiberPlacement::generate_paths_for_layer(FiberLayer& layer, const ExPolygons& layer_geometry, const FiberPlacementConfig& config)
{
    if (layer_geometry.empty()) {
        BOOST_LOG_TRIVIAL(debug) << "FiberPlacement::generate_paths_for_layer - Empty layer geometry";
        return false;
    }
    
    // Check if this layer should have fiber
    if (!should_place_fiber_on_layer(layer.id, config)) {
        BOOST_LOG_TRIVIAL(debug) << "FiberPlacement::generate_paths_for_layer - Layer " << layer.id << " skipped by layer selection";
        return true; // Not an error, just skipped
    }
    
    // Get angle for this layer
    double angle = get_angle_for_layer(layer.id, config);
    
    // Generate pattern based on type
    Polylines polylines;
    switch (config.pattern) {
        case FiberPattern::Grid:
            polylines = generate_grid_pattern(layer_geometry, config);
            break;
        case FiberPattern::Concentric:
            polylines = generate_concentric_pattern(layer_geometry, config);
            break;
        default:
            BOOST_LOG_TRIVIAL(warning) << "FiberPlacement::generate_paths_for_layer - Unknown pattern type";
            return false;
    }
    
    // Filter by placement zone if needed
    if (config.zone != FiberPlacementZone::Both) {
        polylines = filter_by_zone(polylines, layer_geometry, config.zone);
    }
    
    // Convert to fiber paths
    FiberPaths fiber_paths = polylines_to_fiber_paths(polylines, layer.print_z, config);
    
    // Add to layer
    for (FiberPath& path : fiber_paths) {
        layer.add_fiber_path(std::move(path));
    }
    
    BOOST_LOG_TRIVIAL(debug) << "FiberPlacement::generate_paths_for_layer - Generated " 
                            << fiber_paths.size() << " fiber paths for layer " << layer.id;
    
    return true;
}

bool FiberPlacement::should_place_fiber_on_layer(size_t layer_id, const FiberPlacementConfig& config)
{
    // Check start/end layer bounds
    if (layer_id < config.start_layer) {
        return false;
    }
    if (layer_id > config.end_layer) {
        return false;
    }
    
    // Check interval
    size_t relative_layer = layer_id - config.start_layer;
    if (relative_layer % config.layer_interval != 0) {
        return false;
    }
    
    return true;
}

double FiberPlacement::get_angle_for_layer(size_t layer_id, const FiberPlacementConfig& config)
{
    // Check if there's a per-layer angle override
    if (!config.angle_per_layer.empty() && layer_id < config.angle_per_layer.size()) {
        return config.angle_per_layer[layer_id];
    }
    
    return config.angle;
}

Polylines FiberPlacement::generate_grid_pattern(const ExPolygons& geometry, const FiberPlacementConfig& config)
{
    Polylines result;
    
    if (geometry.empty()) {
        return result;
    }
    
    // Calculate spacing in scaled coordinates
    coord_t spacing_scaled = scale_(config.spacing);
    
    // Get bounding box of geometry
    BoundingBox bbox = get_extents(geometry);
    if (!bbox.defined) {
        return result;
    }
    
    // Calculate angle in radians
    double angle_rad = config.angle * M_PI / 180.0;
    double cos_a = std::cos(angle_rad);
    double sin_a = std::sin(angle_rad);
    
    // Calculate the extent needed to cover the geometry
    coord_t width = bbox.size().x();
    coord_t height = bbox.size().y();
    coord_t diagonal = std::sqrt(double(width) * width + double(height) * height);
    coord_t extent = diagonal / 2 + spacing_scaled * 2;
    
    Point center = bbox.center();
    
    // Generate parallel lines at the specified angle
    // Number of lines needed to cover the geometry
    int num_lines = static_cast<int>(std::ceil(diagonal / spacing_scaled)) + 2;
    
    for (int i = 0; i < num_lines; ++i) {
        // Calculate offset from center (perpendicular to angle direction)
        coord_t offset = (i - num_lines / 2) * spacing_scaled;
        
        // Direction perpendicular to angle (for offset)
        Point perp_dir(static_cast<coord_t>(-sin_a * 1000), static_cast<coord_t>(cos_a * 1000));
        Point perp_offset = perp_dir * offset / 1000;
        
        // Line direction (along the angle)
        Point line_dir(static_cast<coord_t>(cos_a * extent), static_cast<coord_t>(sin_a * extent));
        
        // Create line endpoints
        Point p1 = center + perp_offset - line_dir;
        Point p2 = center + perp_offset + line_dir;
        
        // Create a line and intersect with geometry
        Line line(p1, p2);
        
        // For each expolygon, find intersections
        for (const ExPolygon& expoly : geometry) {
            Points intersections;
            
            // Check intersection with contour edges
            for (size_t j = 0; j < expoly.contour.points.size(); ++j) {
                size_t next = (j + 1) % expoly.contour.points.size();
                Line edge(expoly.contour.points[j], expoly.contour.points[next]);
                
                Point intersection;
                if (line.intersection(edge, &intersection)) {
                    intersections.push_back(intersection);
                }
            }
            
            // If we have at least 2 intersections, create a segment
            if (intersections.size() >= 2) {
                // Sort intersections along the line
                std::sort(intersections.begin(), intersections.end(), 
                    [&line](const Point& a, const Point& b) {
                        Vec2d da = (a - line.a).cast<double>();
                        Vec2d db = (b - line.a).cast<double>();
                        Vec2d dir = (line.b - line.a).cast<double>();
                        double dot_a = da.dot(dir);
                        double dot_b = db.dot(dir);
                        return dot_a < dot_b;
                    });
                
                // Create segments between pairs of intersections
                for (size_t j = 0; j < intersections.size() - 1; j += 2) {
                    if (j + 1 < intersections.size()) {
                        Polyline segment;
                        segment.points.push_back(intersections[j]);
                        segment.points.push_back(intersections[j + 1]);
                        result.push_back(segment);
                    }
                }
            } else {
                // Check if line midpoint is inside the polygon (line might be fully inside)
                Point test_point = line.midpoint();
                if (expoly.contour.contains(test_point)) {
                    // Check if it's not in a hole
                    bool in_hole = false;
                    for (const Polygon& hole : expoly.holes) {
                        if (hole.contains(test_point)) {
                            in_hole = true;
                            break;
                        }
                    }
                    if (!in_hole) {
                        // Line is fully inside, use the line segment
                        Polyline segment;
                        segment.points.push_back(p1);
                        segment.points.push_back(p2);
                        result.push_back(segment);
                    }
                }
            }
        }
    }
    
    // TODO: For true grid pattern, also generate perpendicular lines (angle + 90 degrees)
    // TODO: Connect segments into continuous paths where possible
    // TODO: Optimize path ordering for continuous printing
    
    return result;
}

Polylines FiberPlacement::generate_concentric_pattern(const ExPolygons& geometry, const FiberPlacementConfig& config)
{
    Polylines result;
    
    if (geometry.empty()) {
        return result;
    }
    
    // Calculate spacing in scaled coordinates
    coord_t spacing_scaled = scale_(config.spacing);
    
    // For concentric pattern, we offset the geometry inward by spacing
    // This is a simplified implementation
    ExPolygons current = geometry;
    
    while (!current.empty()) {
        // Extract contours as polylines
        for (const ExPolygon& expoly : current) {
            Polyline contour;
            contour.points = expoly.contour.points;
            // Close the loop
            if (!contour.points.empty() && contour.points.front() != contour.points.back()) {
                contour.points.push_back(contour.points.front());
            }
            result.push_back(contour);
            
            // Add holes as separate paths
            for (const Polygon& hole : expoly.holes) {
                Polyline hole_path;
                hole_path.points = hole.points;
                if (!hole_path.points.empty() && hole_path.points.front() != hole_path.points.back()) {
                    hole_path.points.push_back(hole_path.points.front());
                }
                result.push_back(hole_path);
            }
        }
        
        // Offset inward for next iteration
        ExPolygons offset = offset_ex(current, -spacing_scaled);
        if (offset.empty() || offset == current) {
            break; // No more offset possible
        }
        current = std::move(offset);
    }
    
    return result;
}

FiberPaths FiberPlacement::polylines_to_fiber_paths(const Polylines& polylines, coordf_t z, const FiberPlacementConfig& config)
{
    FiberPaths result;
    result.reserve(polylines.size());
    
    for (const Polyline& polyline : polylines) {
        if (polyline.points.size() < 2) {
            continue; // Skip invalid polylines
        }
        
        FiberPath path(polyline, z, config.angle, config.spacing);
        path.fiber_type = config.fiber_type;
        result.push_back(std::move(path));
    }
    
    return result;
}

Polylines FiberPlacement::filter_by_zone(const Polylines& paths, const ExPolygons& geometry, FiberPlacementZone zone)
{
    // Placeholder implementation
    // Full implementation would require:
    // 1. Detection of perimeter vs infill regions
    // 2. Filtering paths based on which region they're in
    
    // For now, return all paths
    // This will be enhanced in later phases when we have better region detection
    return paths;
}

} // namespace Slic3r

