///|/ Copyright (c) Prusa Research 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_FiberLayer_hpp_
#define slic3r_FiberLayer_hpp_

#include <vector>
#include <memory>
#include "libslic3r/Point.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/ExPolygon.hpp"

namespace Slic3r {

// Forward declarations
class FiberPrintObject;

/**
 * @brief Represents a single continuous fiber strand/path
 * 
 * A FiberPath represents one continuous fiber strand that will be laid down
 * in a single continuous motion (no retractions). This is the fundamental
 * unit of fiber placement.
 */
class FiberPath
{
public:
    // The polyline representing the fiber path
    Polyline polyline;
    
    // Fiber type (e.g., carbon, glass, kevlar)
    // Will be expanded to enum in later phases
    std::string fiber_type;
    
    // Direction angle in degrees (0 = along X axis, 90 = along Y axis)
    double angle;
    
    // Spacing from other fibers (mm)
    double spacing;
    
    // Layer Z height where this fiber is placed
    coordf_t z;
    
    FiberPath() : angle(0.0), spacing(0.0), z(0.0) {}
    
    FiberPath(const Polyline &polyline, coordf_t z, double angle = 0.0, double spacing = 0.0)
        : polyline(polyline), z(z), angle(angle), spacing(spacing) {}
    
    // Calculate the length of the fiber path
    double length() const;
    
    // Check if path is empty
    bool empty() const { return polyline.empty(); }
    
    // Get first point
    const Point& first_point() const { return polyline.points.front(); }
    
    // Get last point
    const Point& last_point() const { return polyline.points.back(); }
    
    // Check if path is closed (start == end)
    bool is_closed() const;
};

using FiberPaths = std::vector<FiberPath>;

/**
 * @brief Represents fiber data for a single layer
 * 
 * FiberLayer stores all fiber paths that will be placed on a specific layer.
 * It corresponds to a regular Layer but contains fiber-specific information.
 */
class FiberLayer
{
public:
    // Layer ID (corresponds to Layer::id())
    size_t id;
    
    // Z height of this layer (corresponds to Layer::print_z)
    coordf_t print_z;
    
    // Slice Z used for slicing (corresponds to Layer::slice_z)
    coordf_t slice_z;
    
    // Layer height (corresponds to Layer::height)
    coordf_t height;
    
    // All fiber paths for this layer
    FiberPaths fiber_paths;
    
    // Reference to the corresponding regular layer's geometry (for fiber placement)
    // This will be used to determine where fibers can be placed
    ExPolygons layer_geometry;
    
    FiberLayer() : id(0), print_z(0.0), slice_z(0.0), height(0.0) {}
    
    FiberLayer(size_t id, coordf_t print_z, coordf_t slice_z, coordf_t height)
        : id(id), print_z(print_z), slice_z(slice_z), height(height) {}
    
    // Add a fiber path to this layer
    void add_fiber_path(const FiberPath &path) { fiber_paths.push_back(path); }
    void add_fiber_path(FiberPath &&path) { fiber_paths.emplace_back(std::move(path)); }
    
    // Check if layer has any fiber paths
    bool has_fibers() const { return !fiber_paths.empty(); }
    
    // Get total fiber length in this layer
    double total_fiber_length() const;
    
    // Clear all fiber paths
    void clear() { fiber_paths.clear(); }
};

using FiberLayerPtrs = std::vector<FiberLayer*>;

} // namespace Slic3r

#endif /* slic3r_FiberLayer_hpp_ */

