///|/ Copyright (c) Prusa Research 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_PathPlanner_hpp_
#define slic3r_PathPlanner_hpp_

#include <vector>
#include <memory>
#include "libslic3r/Polyline.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Fiber/FiberLayer.hpp"
#include "libslic3r/BoundingBox.hpp"

namespace Slic3r {

// Forward declarations
class FiberPrintObject;

/**
 * @brief Represents a continuous fiber path
 * 
 * A ContinuousPath is a single unbroken fiber strand that can be printed
 * without retractions. It may consist of multiple connected segments.
 */
class ContinuousPath
{
public:
    // The complete polyline representing the continuous path
    Polyline polyline;
    
    // Layer Z height
    coordf_t z;
    
    // Path metadata
    double angle;
    double spacing;
    std::string fiber_type;
    
    // Whether this path is closed (loop)
    bool is_closed;
    
    ContinuousPath() : z(0.0), angle(0.0), spacing(0.0), is_closed(false) {}
    
    ContinuousPath(const Polyline& polyline, coordf_t z, double angle = 0.0, double spacing = 0.0)
        : polyline(polyline), z(z), angle(angle), spacing(spacing), is_closed(false)
    {
        check_closed();
    }
    
    // Check if path is closed (start == end)
    void check_closed() {
        if (polyline.points.size() >= 2) {
            is_closed = (polyline.points.front() == polyline.points.back());
        }
    }
    
    // Get first point
    const Point& first_point() const { return polyline.points.front(); }
    
    // Get last point
    const Point& last_point() const { return polyline.points.back(); }
    
    // Calculate path length
    double length() const;
    
    // Check if empty
    bool empty() const { return polyline.empty(); }
    
    // Reverse the path
    void reverse() { polyline.reverse(); }
};

using ContinuousPaths = std::vector<ContinuousPath>;

/**
 * @brief Configuration for path planning
 */
struct PathPlanningConfig
{
    // Maximum distance to connect paths (mm)
    // Paths closer than this will be connected
    double max_connection_distance = 5.0;
    
    // Minimum path length to keep (mm)
    // Shorter paths will be discarded
    double min_path_length = 1.0;
    
    // Enable path smoothing
    bool enable_smoothing = true;
    
    // Smoothing tolerance (mm)
    double smoothing_tolerance = 0.1;
    
    // Enable collision detection
    bool enable_collision_detection = true;
    
    // Collision margin (mm) - safety distance from obstacles
    double collision_margin = 0.5;
    
    // Printer limits (for collision detection)
    BoundingBox printer_bounds;
    
    // Enable path optimization (shortest path ordering)
    bool enable_optimization = true;
};

/**
 * @brief Main class for continuous path planning
 * 
 * This class takes fiber path segments and connects them into continuous
 * paths that can be printed without retractions. It handles:
 * - Path connection (greedy nearest neighbor)
 * - Collision detection
 * - Path optimization
 * - Path smoothing
 */
class PathPlanner
{
public:
    PathPlanner() = default;
    
    /**
     * @brief Plan continuous paths from fiber path segments
     * 
     * Takes a collection of FiberPath segments and connects them into
     * continuous paths using greedy nearest-neighbor algorithm.
     * 
     * @param fiber_paths Input fiber path segments
     * @param layer_geometry Layer geometry for collision detection
     * @param config Path planning configuration
     * @return Continuous paths ready for printing
     */
    ContinuousPaths plan_continuous_paths(
        const FiberPaths& fiber_paths,
        const ExPolygons& layer_geometry,
        const PathPlanningConfig& config
    );
    
    /**
     * @brief Connect paths using greedy nearest-neighbor algorithm
     * 
     * Connects path segments into continuous paths by always choosing
     * the nearest unconnected endpoint.
     */
    ContinuousPaths connect_paths(
        const FiberPaths& fiber_paths,
        const PathPlanningConfig& config
    );
    
    /**
     * @brief Optimize path ordering for minimum travel distance
     * 
     * Reorders paths to minimize travel moves between them.
     */
    void optimize_path_order(ContinuousPaths& paths, const PathPlanningConfig& config);
    
    /**
     * @brief Smooth paths to reduce sharp corners
     */
    void smooth_paths(ContinuousPaths& paths, const PathPlanningConfig& config);
    
    /**
     * @brief Detect and remove collisions
     * 
     * Checks paths for collisions with:
     * - Part geometry
     * - Other paths
     * - Printer limits
     */
    bool detect_collisions(
        const ContinuousPath& path,
        const ExPolygons& layer_geometry,
        const ContinuousPaths& other_paths,
        const PathPlanningConfig& config,
        std::vector<std::string>& warnings
    ) const;
    
    /**
     * @brief Validate a path
     * 
     * Checks if path is valid (not too short, within bounds, etc.)
     */
    bool validate_path(
        const ContinuousPath& path,
        const PathPlanningConfig& config,
        std::string& error_message
    ) const;
    
private:
    /**
     * @brief Find nearest endpoint to connect to
     */
    size_t find_nearest_endpoint(
        const Point& current_point,
        const std::vector<FiberPath>& paths,
        const std::vector<bool>& used,
        bool& reverse,
        const PathPlanningConfig& config
    ) const;
    
    /**
     * @brief Calculate distance between two points
     */
    double point_distance(const Point& a, const Point& b) const;
};

} // namespace Slic3r

#endif /* slic3r_PathPlanner_hpp_ */

