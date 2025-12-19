///|/ Copyright (c) Prusa Research 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_FiberStatistics_hpp_
#define slic3r_FiberStatistics_hpp_

#include "libslic3r/Point.hpp"
#include <string>
#include <vector>
#include <map>

namespace Slic3r {

// Forward declarations
class FiberPrint;
class FiberPrintObject;
class FiberLayer;
class FiberPath;
class DynamicPrintConfig;

/**
 * @brief Statistics for fiber printing
 * 
 * Contains information about fiber usage, paths, validation, and timing.
 */
struct FiberStatistics
{
    // Fiber usage
    double total_fiber_length;        // Total length of fiber used (mm)
    double total_fiber_weight;        // Total weight of fiber used (g)
    double total_fiber_cost;          // Total cost of fiber used
    
    // Path statistics
    size_t total_path_count;          // Total number of fiber paths
    size_t continuous_path_count;     // Number of continuous paths (no breaks)
    size_t broken_path_count;          // Number of paths with breaks/retractions
    double average_path_length;        // Average length of a fiber path (mm)
    double longest_path_length;        // Length of longest path (mm)
    double shortest_path_length;      // Length of shortest path (mm)
    
    // Layer statistics
    size_t layers_with_fiber;          // Number of layers containing fiber
    size_t total_layers;               // Total number of layers
    
    // Coverage statistics
    double fiber_coverage_percentage;  // Percentage of layer area covered by fiber
    
    // Print time (fiber-specific)
    double fiber_print_time_seconds;   // Estimated time for fiber printing (seconds)
    
    // Validation warnings
    std::vector<std::string> warnings; // List of validation warnings
    
    // Per-layer statistics
    struct LayerStats {
        size_t layer_id;
        double fiber_length;
        size_t path_count;
        double coverage_percentage;
    };
    std::vector<LayerStats> layer_stats;
    
    FiberStatistics() { clear(); }
    
    void clear() {
        total_fiber_length = 0.0;
        total_fiber_weight = 0.0;
        total_fiber_cost = 0.0;
        total_path_count = 0;
        continuous_path_count = 0;
        broken_path_count = 0;
        average_path_length = 0.0;
        longest_path_length = 0.0;
        shortest_path_length = 0.0;
        layers_with_fiber = 0;
        total_layers = 0;
        fiber_coverage_percentage = 0.0;
        fiber_print_time_seconds = 0.0;
        warnings.clear();
        layer_stats.clear();
    }
    
    // Calculate statistics from FiberPrint
    static FiberStatistics calculate(const FiberPrint* fiber_print, const DynamicPrintConfig* config);
    
    // Calculate statistics from a single FiberPrintObject
    static FiberStatistics calculate_object(const FiberPrintObject* fiber_object, const DynamicPrintConfig* config);
    
    // Validate fiber paths and generate warnings
    void validate_paths(const FiberPrint* fiber_print);
    
    // Format statistics for display
    std::string format_summary() const;
};

} // namespace Slic3r

#endif /* slic3r_FiberStatistics_hpp_ */

