///|/ Copyright (c) Prusa Research 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_FiberAdvanced_hpp_
#define slic3r_FiberAdvanced_hpp_

#include <string>
#include <vector>
#include <map>
#include "libslic3r/Fiber/FiberLayer.hpp"
#include "libslic3r/Point.hpp"

namespace Slic3r {

/**
 * @brief Fiber type enumeration
 */
enum class FiberType {
    Carbon,         // Carbon fiber
    Glass,          // Glass fiber
    Kevlar,         // Kevlar fiber
    Basalt,         // Basalt fiber
    Custom,         // Custom fiber type
};

/**
 * @brief Fiber material properties
 */
struct FiberMaterialProperties
{
    FiberType type;
    std::string name;
    
    // Physical properties
    double diameter_mm;          // Fiber diameter in mm
    double density_g_per_cm3;    // Density in g/cm³
    double tensile_strength_mpa; // Tensile strength in MPa
    double modulus_gpa;          // Young's modulus in GPa
    
    // Printing properties
    double default_speed_mm_per_s;  // Default printing speed
    double min_speed_mm_per_s;       // Minimum speed
    double max_speed_mm_per_s;       // Maximum speed
    double default_pressure;         // Default pressure/tension
    
    // Temperature properties (if applicable)
    double max_temperature_c;        // Maximum temperature
    
    FiberMaterialProperties()
        : type(FiberType::Carbon)
        , name("Carbon Fiber")
        , diameter_mm(0.1)
        , density_g_per_cm3(1.8)
        , tensile_strength_mpa(3500.0)
        , modulus_gpa(230.0)
        , default_speed_mm_per_s(50.0)
        , min_speed_mm_per_s(10.0)
        , max_speed_mm_per_s(100.0)
        , default_pressure(0.0)
        , max_temperature_c(200.0)
    {}
};

/**
 * @brief Variable speed profile along a path
 */
struct SpeedProfile
{
    // Speed at start of path (mm/s)
    double start_speed;
    
    // Speed at end of path (mm/s)
    double end_speed;
    
    // Speed control points along the path (0.0 to 1.0, speed in mm/s)
    std::vector<std::pair<double, double>> control_points;
    
    // Interpolation method
    enum Interpolation {
        Linear,      // Linear interpolation
        Smooth,      // Smooth curve (cubic)
        Constant,    // Constant speed
    } interpolation;
    
    SpeedProfile()
        : start_speed(50.0)
        , end_speed(50.0)
        , interpolation(Linear)
    {}
    
    // Get speed at a specific position along path (0.0 to 1.0)
    double get_speed_at(double position) const;
};

/**
 * @brief Advanced fiber path with variable properties
 */
class AdvancedFiberPath : public FiberPath
{
public:
    // Fiber type for this path
    FiberType fiber_type;
    
    // Variable speed profile
    SpeedProfile speed_profile;
    
    // Pressure/tension profile (if applicable)
    double start_pressure;
    double end_pressure;
    
    // Anchoring points (where fiber starts/ends are anchored in plastic)
    std::vector<Point> anchor_points;
    
    // Custom G-code to insert before this path
    std::string pre_gcode;
    
    // Custom G-code to insert after this path
    std::string post_gcode;
    
    AdvancedFiberPath()
        : FiberPath()
        , fiber_type(FiberType::Carbon)
        , start_pressure(0.0)
        , end_pressure(0.0)
    {}
    
    AdvancedFiberPath(const FiberPath& base_path, FiberType type = FiberType::Carbon)
        : FiberPath(base_path)
        , fiber_type(type)
        , start_pressure(0.0)
        , end_pressure(0.0)
    {}
};

using AdvancedFiberPaths = std::vector<AdvancedFiberPath>;

/**
 * @brief Fiber material database
 */
class FiberMaterialDatabase
{
public:
    static FiberMaterialDatabase& instance();
    
    // Get material properties for a fiber type
    const FiberMaterialProperties& get_properties(FiberType type) const;
    
    // Register a custom material
    void register_custom_material(const std::string& name, const FiberMaterialProperties& props);
    
    // Get all available fiber types
    std::vector<FiberType> get_available_types() const;
    
    // Get material name
    std::string get_material_name(FiberType type) const;

private:
    FiberMaterialDatabase();
    std::map<FiberType, FiberMaterialProperties> m_materials;
};

/**
 * @brief Advanced fiber placement and optimization
 */
class AdvancedFiberPlacement
{
public:
    /**
     * @brief Generate paths with variable speed based on geometry
     * 
     * Adjusts speed based on:
     * - Curvature (slower on tight curves)
     * - Layer height (slower on first layer)
     * - Path length (slower on short paths)
     */
    AdvancedFiberPaths generate_paths_with_variable_speed(
        const FiberPaths& base_paths,
        const FiberMaterialProperties& material,
        const ExPolygons& layer_geometry
    ) const;
    
    /**
     * @brief Optimize paths for multiple fiber types
     * 
     * Groups paths by fiber type and optimizes ordering
     */
    std::map<FiberType, AdvancedFiberPaths> optimize_for_multiple_fibers(
        const std::vector<std::pair<FiberType, FiberPaths>>& fiber_paths_by_type
    ) const;
    
    /**
     * @brief Add anchor points to paths
     * 
     * Ensures fiber starts/ends are properly anchored in plastic
     */
    AdvancedFiberPaths add_anchor_points(
        const FiberPaths& paths,
        const ExPolygons& layer_geometry,
        double anchor_radius_mm = 2.0
    ) const;
    
    /**
     * @brief Smooth speed transitions
     * 
     * Applies smoothing to speed profiles to avoid sudden changes
     */
    SpeedProfile smooth_speed_profile(const SpeedProfile& profile) const;
    
    /**
     * @brief Calculate optimal speed for a path segment
     * 
     * Based on curvature, length, and material properties
     */
    double calculate_optimal_speed(
        const Point& p1,
        const Point& p2,
        const Point& p3,
        const FiberMaterialProperties& material
    ) const;
};

} // namespace Slic3r

#endif /* slic3r_FiberAdvanced_hpp_ */

