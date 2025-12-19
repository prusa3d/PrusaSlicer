///|/ Copyright (c) Prusa Research 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "FiberStatistics.hpp"
#include "libslic3r/FiberPrint.hpp"
#include "libslic3r/Fiber/FiberPrintObject.hpp"
#include "libslic3r/Fiber/FiberLayer.hpp"
#include "libslic3r/Fiber/FiberAdvanced.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/format.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace Slic3r {

FiberStatistics FiberStatistics::calculate(const FiberPrint* fiber_print, const DynamicPrintConfig* config)
{
    FiberStatistics stats;
    
    if (fiber_print == nullptr || fiber_print->empty())
        return stats;
    
    // Get fiber type from config
    std::string fiber_type_str = config ? config->opt_string("fiber_type", 0) : "carbon";
    FiberType fiber_type = FiberType::Carbon; // Default
    if (fiber_type_str == "glass") fiber_type = FiberType::Glass;
    else if (fiber_type_str == "kevlar") fiber_type = FiberType::Kevlar;
    else if (fiber_type_str == "basalt") fiber_type = FiberType::Basalt;
    
    // Get material properties
    const FiberMaterialDatabase& db = FiberMaterialDatabase::instance();
    const FiberMaterialProperties& material = db.get_properties(fiber_type);
    
    // Get fiber speed from config (for time estimation)
    double fiber_speed = config ? config->opt_float("fiber_speed", 0) : material.default_speed_mm_per_s;
    if (fiber_speed <= 0.0) fiber_speed = material.default_speed_mm_per_s;
    
    // Get fiber cost per gram (if available in config)
    double fiber_cost_per_g = 0.0; // Default: no cost data
    
    // Iterate through all objects
    double total_length = 0.0;
    size_t total_paths = 0;
    size_t continuous_paths = 0;
    size_t broken_paths = 0;
    double min_path_length = std::numeric_limits<double>::max();
    double max_path_length = 0.0;
    size_t layers_with_fiber = 0;
    double total_layer_area = 0.0;
    double total_fiber_area = 0.0;
    
    for (const FiberPrintObject* fiber_object : fiber_print->objects()) {
        const std::vector<FiberLayer>& layers = fiber_object->fiber_layers();
        stats.total_layers += layers.size();
        
        for (const FiberLayer& layer : layers) {
            if (layer.fiber_paths.empty())
                continue;
            
            layers_with_fiber++;
            
            LayerStats layer_stat;
            layer_stat.layer_id = layer.id;
            layer_stat.path_count = layer.fiber_paths.size();
            layer_stat.fiber_length = 0.0;
            
            // Calculate layer area (for coverage)
            double layer_area = area(layer.layer_geometry);
            total_layer_area += layer_area;
            
            // Process each path in the layer
            for (const FiberPath& path : layer.fiber_paths) {
                if (path.empty())
                    continue;
                
                double path_length = path.length();
                total_length += path_length;
                layer_stat.fiber_length += path_length;
                total_paths++;
                
                // Check if path is continuous (no breaks)
                if (path.is_closed() || path.polyline.points.size() >= 2) {
                    continuous_paths++;
                } else {
                    broken_paths++;
                }
                
                // Track min/max path lengths
                if (path_length < min_path_length)
                    min_path_length = path_length;
                if (path_length > max_path_length)
                    max_path_length = path_length;
                
                // Estimate fiber area (assuming circular cross-section)
                // Fiber diameter from material properties
                double fiber_diameter = material.diameter_mm;
                double fiber_area_per_length = M_PI * (fiber_diameter / 2.0) * (fiber_diameter / 2.0); // mm² per mm
                total_fiber_area += path_length * fiber_area_per_length;
            }
            
            // Calculate coverage for this layer
            if (layer_area > 0.0) {
                double fiber_diameter = material.diameter_mm;
                double fiber_area_per_length = M_PI * (fiber_diameter / 2.0) * (fiber_diameter / 2.0);
                double layer_fiber_area = layer_stat.fiber_length * fiber_area_per_length;
                layer_stat.coverage_percentage = (layer_fiber_area / layer_area) * 100.0;
            } else {
                layer_stat.coverage_percentage = 0.0;
            }
            
            stats.layer_stats.push_back(layer_stat);
        }
    }
    
    // Calculate statistics
    stats.total_fiber_length = total_length;
    stats.total_path_count = total_paths;
    stats.continuous_path_count = continuous_paths;
    stats.broken_path_count = broken_paths;
    stats.layers_with_fiber = layers_with_fiber;
    
    // Average path length
    if (total_paths > 0) {
        stats.average_path_length = total_length / total_paths;
    }
    
    // Min/max path lengths
    if (min_path_length != std::numeric_limits<double>::max()) {
        stats.shortest_path_length = min_path_length;
    }
    if (max_path_length > 0.0) {
        stats.longest_path_length = max_path_length;
    }
    
    // Calculate weight (using material density)
    // Weight = length * cross-sectional area * density
    double fiber_diameter = material.diameter_mm;
    double cross_sectional_area_mm2 = M_PI * (fiber_diameter / 2.0) * (fiber_diameter / 2.0);
    double cross_sectional_area_cm2 = cross_sectional_area_mm2 / 100.0; // Convert to cm²
    double density_g_per_cm3 = material.density_g_per_cm3;
    double length_cm = total_length / 10.0; // Convert mm to cm
    stats.total_fiber_weight = length_cm * cross_sectional_area_cm2 * density_g_per_cm3;
    
    // Calculate cost
    stats.total_fiber_cost = stats.total_fiber_weight * fiber_cost_per_g;
    
    // Calculate coverage percentage
    if (total_layer_area > 0.0) {
        stats.fiber_coverage_percentage = (total_fiber_area / total_layer_area) * 100.0;
    }
    
    // Estimate print time (based on fiber length and speed)
    if (fiber_speed > 0.0) {
        stats.fiber_print_time_seconds = total_length / fiber_speed; // seconds
    }
    
    // Validate paths and generate warnings
    stats.validate_paths(fiber_print);
    
    return stats;
}

FiberStatistics FiberStatistics::calculate_object(const FiberPrintObject* fiber_object, const DynamicPrintConfig* config)
{
    FiberStatistics stats;
    
    if (fiber_object == nullptr)
        return stats;
    
    // Similar calculation but for a single object
    // This is a simplified version - can be expanded if needed
    const std::vector<FiberLayer>& layers = fiber_object->fiber_layers();
    stats.total_layers = layers.size();
    
    // Get fiber type and material properties
    std::string fiber_type_str = config ? config->opt_string("fiber_type", 0) : "carbon";
    FiberType fiber_type = FiberType::Carbon;
    if (fiber_type_str == "glass") fiber_type = FiberType::Glass;
    else if (fiber_type_str == "kevlar") fiber_type = FiberType::Kevlar;
    else if (fiber_type_str == "basalt") fiber_type = FiberType::Basalt;
    
    const FiberMaterialDatabase& db = FiberMaterialDatabase::instance();
    const FiberMaterialProperties& material = db.get_properties(fiber_type);
    
    double total_length = 0.0;
    size_t layers_with_fiber_count = 0;
    for (const FiberLayer& layer : layers) {
        if (!layer.fiber_paths.empty()) {
            layers_with_fiber_count++;
            for (const FiberPath& path : layer.fiber_paths) {
                total_length += path.length();
                stats.total_path_count++;
            }
        }
    }
    
    stats.total_fiber_length = total_length;
    stats.layers_with_fiber = layers_with_fiber_count;
    
    // Calculate weight
    double fiber_diameter = material.diameter_mm;
    double cross_sectional_area_cm2 = (M_PI * (fiber_diameter / 2.0) * (fiber_diameter / 2.0)) / 100.0;
    double length_cm = total_length / 10.0;
    stats.total_fiber_weight = length_cm * cross_sectional_area_cm2 * material.density_g_per_cm3;
    
    return stats;
}

void FiberStatistics::validate_paths(const FiberPrint* fiber_print)
{
    warnings.clear();
    
    if (fiber_print == nullptr || fiber_print->empty())
        return;
    
    // Check for very short paths (might indicate issues)
    if (shortest_path_length > 0.0 && shortest_path_length < 1.0) {
        warnings.push_back("Some fiber paths are very short (< 1mm), which may cause printing issues.");
    }
    
    // Check for broken paths
    if (broken_path_count > 0) {
        warnings.push_back(format("Found %1% broken fiber paths (non-continuous). Consider optimizing path planning.", broken_path_count));
    }
    
    // Check coverage
    if (fiber_coverage_percentage < 1.0) {
        warnings.push_back(format("Fiber coverage is very low (%1$.1f%%). Consider increasing fiber density.", fiber_coverage_percentage));
    }
    
    // Check if no fiber paths exist
    if (total_path_count == 0) {
        warnings.push_back("No fiber paths generated. Check fiber placement settings.");
    }
    
    // Check layer distribution
    if (total_layers > 0) {
        double fiber_layer_ratio = static_cast<double>(layers_with_fiber) / total_layers;
        if (fiber_layer_ratio < 0.1) {
            warnings.push_back(format("Fiber is only placed on %1$.1f%% of layers. Check layer interval settings.", fiber_layer_ratio * 100.0));
        }
    }
}

std::string FiberStatistics::format_summary() const
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    
    oss << "Fiber Statistics:\n";
    oss << "  Total Length: " << (total_fiber_length / 1000.0) << " m\n";
    oss << "  Total Weight: " << total_fiber_weight << " g\n";
    if (total_fiber_cost > 0.0) {
        oss << "  Total Cost: $" << total_fiber_cost << "\n";
    }
    oss << "  Path Count: " << total_path_count << " (";
    oss << continuous_path_count << " continuous, " << broken_path_count << " broken)\n";
    oss << "  Layers with Fiber: " << layers_with_fiber << " / " << total_layers << "\n";
    oss << "  Coverage: " << fiber_coverage_percentage << "%\n";
    if (fiber_print_time_seconds > 0.0) {
        int hours = static_cast<int>(fiber_print_time_seconds / 3600);
        int minutes = static_cast<int>((fiber_print_time_seconds - hours * 3600) / 60);
        int seconds = static_cast<int>(fiber_print_time_seconds - hours * 3600 - minutes * 60);
        oss << "  Estimated Fiber Print Time: " << hours << "h " << minutes << "m " << seconds << "s\n";
    }
    
    if (!warnings.empty()) {
        oss << "\nWarnings:\n";
        for (const std::string& warning : warnings) {
            oss << "  - " << warning << "\n";
        }
    }
    
    return oss.str();
}

} // namespace Slic3r

