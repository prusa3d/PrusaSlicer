///|/ Copyright (c) Prusa Research 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "FiberAdvanced.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Point.hpp"
#include <algorithm>
#include <cmath>
#include <boost/log/trivial.hpp>

namespace Slic3r {

// SpeedProfile implementation
double SpeedProfile::get_speed_at(double position) const
{
    if (position < 0.0) position = 0.0;
    if (position > 1.0) position = 1.0;
    
    if (interpolation == Constant) {
        return start_speed;
    }
    
    if (control_points.empty()) {
        // Linear interpolation between start and end
        if (interpolation == Linear) {
            return start_speed + (end_speed - start_speed) * position;
        }
        // Smooth interpolation (cubic)
        double t = position;
        double t2 = t * t;
        double t3 = t2 * t;
        return start_speed * (2.0 * t3 - 3.0 * t2 + 1.0) + 
               end_speed * (-2.0 * t3 + 3.0 * t2);
    }
    
    // Use control points
    // Find the two control points that bracket the position
    for (size_t i = 0; i < control_points.size() - 1; ++i) {
        if (position >= control_points[i].first && position <= control_points[i + 1].first) {
            double t = (position - control_points[i].first) / 
                      (control_points[i + 1].first - control_points[i].first);
            if (interpolation == Linear) {
                return control_points[i].second + 
                       (control_points[i + 1].second - control_points[i].second) * t;
            } else {
                // Smooth interpolation
                double t2 = t * t;
                double t3 = t2 * t;
                return control_points[i].second * (2.0 * t3 - 3.0 * t2 + 1.0) + 
                       control_points[i + 1].second * (-2.0 * t3 + 3.0 * t2);
            }
        }
    }
    
    // Fallback to linear interpolation
    return start_speed + (end_speed - start_speed) * position;
}

// FiberMaterialDatabase implementation
FiberMaterialDatabase& FiberMaterialDatabase::instance()
{
    static FiberMaterialDatabase db;
    return db;
}

FiberMaterialDatabase::FiberMaterialDatabase()
{
    // Initialize default materials
    FiberMaterialProperties carbon;
    carbon.type = FiberType::Carbon;
    carbon.name = "Carbon Fiber";
    carbon.diameter_mm = 0.1;
    carbon.density_g_per_cm3 = 1.8;
    carbon.tensile_strength_mpa = 3500.0;
    carbon.modulus_gpa = 230.0;
    carbon.default_speed_mm_per_s = 50.0;
    carbon.min_speed_mm_per_s = 10.0;
    carbon.max_speed_mm_per_s = 100.0;
    carbon.max_temperature_c = 200.0;
    m_materials[FiberType::Carbon] = carbon;
    
    FiberMaterialProperties glass;
    glass.type = FiberType::Glass;
    glass.name = "Glass Fiber";
    glass.diameter_mm = 0.1;
    glass.density_g_per_cm3 = 2.5;
    glass.tensile_strength_mpa = 3400.0;
    glass.modulus_gpa = 72.0;
    glass.default_speed_mm_per_s = 40.0;
    glass.min_speed_mm_per_s = 10.0;
    glass.max_speed_mm_per_s = 80.0;
    glass.max_temperature_c = 300.0;
    m_materials[FiberType::Glass] = glass;
    
    FiberMaterialProperties kevlar;
    kevlar.type = FiberType::Kevlar;
    kevlar.name = "Kevlar Fiber";
    kevlar.diameter_mm = 0.1;
    kevlar.density_g_per_cm3 = 1.44;
    kevlar.tensile_strength_mpa = 3620.0;
    kevlar.modulus_gpa = 112.0;
    kevlar.default_speed_mm_per_s = 45.0;
    kevlar.min_speed_mm_per_s = 10.0;
    kevlar.max_speed_mm_per_s = 90.0;
    kevlar.max_temperature_c = 150.0;
    m_materials[FiberType::Kevlar] = kevlar;
    
    FiberMaterialProperties basalt;
    basalt.type = FiberType::Basalt;
    basalt.name = "Basalt Fiber";
    basalt.diameter_mm = 0.1;
    basalt.density_g_per_cm3 = 2.65;
    basalt.tensile_strength_mpa = 3000.0;
    basalt.modulus_gpa = 89.0;
    basalt.default_speed_mm_per_s = 35.0;
    basalt.min_speed_mm_per_s = 10.0;
    basalt.max_speed_mm_per_s = 70.0;
    basalt.max_temperature_c = 700.0;
    m_materials[FiberType::Basalt] = basalt;
}

const FiberMaterialProperties& FiberMaterialDatabase::get_properties(FiberType type) const
{
    auto it = m_materials.find(type);
    if (it != m_materials.end()) {
        return it->second;
    }
    // Return carbon as default
    static FiberMaterialProperties default_props;
    return default_props;
}

void FiberMaterialDatabase::register_custom_material(const std::string& name, const FiberMaterialProperties& props)
{
    m_materials[props.type] = props;
    m_materials[props.type].name = name;
}

std::vector<FiberType> FiberMaterialDatabase::get_available_types() const
{
    std::vector<FiberType> types;
    types.reserve(m_materials.size());
    for (const auto& pair : m_materials) {
        types.push_back(pair.first);
    }
    return types;
}

std::string FiberMaterialDatabase::get_material_name(FiberType type) const
{
    auto it = m_materials.find(type);
    if (it != m_materials.end()) {
        return it->second.name;
    }
    return "Unknown";
}

// AdvancedFiberPlacement implementation
AdvancedFiberPaths AdvancedFiberPlacement::generate_paths_with_variable_speed(
    const FiberPaths& base_paths,
    const FiberMaterialProperties& material,
    const ExPolygons& layer_geometry) const
{
    AdvancedFiberPaths advanced_paths;
    advanced_paths.reserve(base_paths.size());
    
    for (const FiberPath& base_path : base_paths) {
        AdvancedFiberPath advanced_path(base_path, FiberType::Carbon); // Will be set by caller
        
        // Initialize speed profile
        SpeedProfile profile;
        profile.start_speed = material.default_speed_mm_per_s;
        profile.end_speed = material.default_speed_mm_per_s;
        profile.interpolation = SpeedProfile::Smooth;
        
        // Adjust speed based on path curvature
        if (base_path.polyline.points.size() >= 3) {
            // Calculate average curvature
            double total_curvature = 0.0;
            int curvature_count = 0;
            
            for (size_t i = 1; i < base_path.polyline.points.size() - 1; ++i) {
                const Point& p1 = base_path.polyline.points[i - 1];
                const Point& p2 = base_path.polyline.points[i];
                const Point& p3 = base_path.polyline.points[i + 1];
                
                double speed = calculate_optimal_speed(p1, p2, p3, material);
                total_curvature += speed;
                curvature_count++;
            }
            
            if (curvature_count > 0) {
                double avg_speed = total_curvature / curvature_count;
                profile.start_speed = avg_speed;
                profile.end_speed = avg_speed;
            }
        }
        
        // Clamp speeds to material limits
        profile.start_speed = std::max(material.min_speed_mm_per_s, 
                                      std::min(material.max_speed_mm_per_s, profile.start_speed));
        profile.end_speed = std::max(material.min_speed_mm_per_s, 
                                    std::min(material.max_speed_mm_per_s, profile.end_speed));
        
        advanced_path.speed_profile = profile;
        advanced_paths.push_back(advanced_path);
    }
    
    return advanced_paths;
}

std::map<FiberType, AdvancedFiberPaths> AdvancedFiberPlacement::optimize_for_multiple_fibers(
    const std::vector<std::pair<FiberType, FiberPaths>>& fiber_paths_by_type) const
{
    std::map<FiberType, AdvancedFiberPaths> optimized;
    
    FiberMaterialDatabase& db = FiberMaterialDatabase::instance();
    
    for (const auto& pair : fiber_paths_by_type) {
        FiberType type = pair.first;
        const FiberPaths& paths = pair.second;
        const FiberMaterialProperties& props = db.get_properties(type);
        
        // Convert to advanced paths with material properties
        AdvancedFiberPaths advanced_paths;
        advanced_paths.reserve(paths.size());
        
        for (const FiberPath& path : paths) {
            AdvancedFiberPath advanced_path(path, type);
            advanced_path.speed_profile.start_speed = props.default_speed_mm_per_s;
            advanced_path.speed_profile.end_speed = props.default_speed_mm_per_s;
            advanced_paths.push_back(advanced_path);
        }
        
        optimized[type] = std::move(advanced_paths);
    }
    
    return optimized;
}

AdvancedFiberPaths AdvancedFiberPlacement::add_anchor_points(
    const FiberPaths& paths,
    const ExPolygons& layer_geometry,
    double anchor_radius_mm) const
{
    AdvancedFiberPaths advanced_paths;
    advanced_paths.reserve(paths.size());
    
    for (const FiberPath& path : paths) {
        AdvancedFiberPath advanced_path(path);
        
        // Add anchor point at start
        if (!path.polyline.points.empty()) {
            advanced_path.anchor_points.push_back(path.polyline.points.front());
        }
        
        // Add anchor point at end (if different from start)
        if (path.polyline.points.size() > 1) {
            const Point& start = path.polyline.points.front();
            const Point& end = path.polyline.points.back();
            if (start != end) {
                advanced_path.anchor_points.push_back(end);
            }
        }
        
        advanced_paths.push_back(advanced_path);
    }
    
    return advanced_paths;
}

SpeedProfile AdvancedFiberPlacement::smooth_speed_profile(const SpeedProfile& profile) const
{
    SpeedProfile smoothed = profile;
    
    // Apply smoothing to control points if present
    if (smoothed.control_points.size() > 2) {
        // Simple moving average smoothing
        std::vector<std::pair<double, double>> smoothed_points;
        smoothed_points.reserve(smoothed.control_points.size());
        
        for (size_t i = 0; i < smoothed.control_points.size(); ++i) {
            double speed = smoothed.control_points[i].second;
            if (i > 0 && i < smoothed.control_points.size() - 1) {
                speed = (smoothed.control_points[i - 1].second + 
                        smoothed.control_points[i].second + 
                        smoothed.control_points[i + 1].second) / 3.0;
            }
            smoothed_points.push_back({smoothed.control_points[i].first, speed});
        }
        smoothed.control_points = std::move(smoothed_points);
    }
    
    return smoothed;
}

double AdvancedFiberPlacement::calculate_optimal_speed(
    const Point& p1,
    const Point& p2,
    const Point& p3,
    const FiberMaterialProperties& material) const
{
    // Calculate angle at p2
    Vec2d v1 = (p2 - p1).cast<double>();
    Vec2d v2 = (p3 - p2).cast<double>();
    
    double len1 = v1.norm();
    double len2 = v2.norm();
    
    if (len1 < EPSILON || len2 < EPSILON) {
        return material.default_speed_mm_per_s;
    }
    
    v1.normalize();
    v2.normalize();
    
    // Calculate angle (0 = straight, PI = 180° turn)
    double dot = v1.dot(v2);
    dot = std::max(-1.0, std::min(1.0, dot)); // Clamp to [-1, 1]
    double angle = std::acos(dot);
    
    // Reduce speed for sharp turns
    // Straight (0°) = full speed, 90° turn = 50% speed, 180° turn = 25% speed
    double speed_factor = 1.0 - (angle / M_PI) * 0.75;
    speed_factor = std::max(0.25, speed_factor); // Minimum 25% speed
    
    double speed = material.default_speed_mm_per_s * speed_factor;
    
    // Clamp to material limits
    return std::max(material.min_speed_mm_per_s, 
                   std::min(material.max_speed_mm_per_s, speed));
}

} // namespace Slic3r

