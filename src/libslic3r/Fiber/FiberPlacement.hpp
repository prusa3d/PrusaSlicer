///|/ Copyright (c) Prusa Research 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_FiberPlacement_hpp_
#define slic3r_FiberPlacement_hpp_

#include <vector>
#include <string>
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/Fiber/FiberLayer.hpp"

namespace Slic3r {

// Forward declarations
class FiberPrintObject;
class FiberPrintConfig;

/**
 * @brief Fiber placement pattern types
 */
enum class FiberPattern {
    Grid,           // Rectangular grid pattern
    Concentric,     // Concentric loops following layer boundaries
    // Future: Custom, StressBased, etc.
};

/**
 * @brief Fiber placement zone types
 */
enum class FiberPlacementZone {
    Perimeter,      // Only in perimeter areas
    Infill,         // Only in infill areas
    Both,           // Both perimeter and infill
};

/**
 * @brief Configuration for fiber placement
 */
struct FiberPlacementConfig
{
    // Pattern type
    FiberPattern pattern = FiberPattern::Grid;
    
    // Angle in degrees (0 = along X axis, 90 = along Y axis)
    double angle = 0.0;
    
    // Spacing between fiber strands (mm)
    double spacing = 2.0;
    
    // Density as percentage (0-100), alternative to spacing
    double density = 50.0;
    
    // Placement zone
    FiberPlacementZone zone = FiberPlacementZone::Both;
    
    // Layer selection
    size_t layer_interval = 1;      // Place fiber every N layers (1 = every layer)
    size_t start_layer = 0;         // First layer to place fiber (0 = from first layer)
    size_t end_layer = SIZE_MAX;    // Last layer to place fiber (SIZE_MAX = to last layer)
    
    // Per-layer angle overrides (optional)
    std::vector<double> angle_per_layer;
    
    // Fiber type
    std::string fiber_type = "carbon";
};

/**
 * @brief Main class for fiber placement strategy
 * 
 * This class decides WHERE to place fiber strands based on:
 * - Layer selection (which layers)
 * - Pattern type (grid, concentric, etc.)
 * - Angle/direction
 * - Density/spacing
 * - Placement zones
 */
class FiberPlacement
{
public:
    FiberPlacement() = default;
    
    /**
     * @brief Generate fiber paths for a single layer
     * 
     * @param layer The fiber layer to populate with paths
     * @param layer_geometry The geometry of the layer (ExPolygons)
     * @param config Placement configuration
     * @return true if paths were generated successfully
     */
    bool generate_paths_for_layer(FiberLayer& layer, const ExPolygons& layer_geometry, const FiberPlacementConfig& config);
    
    /**
     * @brief Check if a layer should have fiber based on layer selection config
     */
    static bool should_place_fiber_on_layer(size_t layer_id, const FiberPlacementConfig& config);
    
    /**
     * @brief Get angle for a specific layer (handles per-layer overrides)
     */
    static double get_angle_for_layer(size_t layer_id, const FiberPlacementConfig& config);
    
private:
    /**
     * @brief Generate grid pattern paths
     */
    Polylines generate_grid_pattern(const ExPolygons& geometry, const FiberPlacementConfig& config);
    
    /**
     * @brief Generate concentric pattern paths
     */
    Polylines generate_concentric_pattern(const ExPolygons& geometry, const FiberPlacementConfig& config);
    
    /**
     * @brief Convert polylines to fiber paths
     */
    FiberPaths polylines_to_fiber_paths(const Polylines& polylines, coordf_t z, const FiberPlacementConfig& config);
    
    /**
     * @brief Filter paths by placement zone (perimeter vs infill)
     * Note: This is a placeholder - full implementation requires perimeter/infill detection
     */
    Polylines filter_by_zone(const Polylines& paths, const ExPolygons& geometry, FiberPlacementZone zone);
};

} // namespace Slic3r

#endif /* slic3r_FiberPlacement_hpp_ */

