/**
 * Unit tests for variable speed profiles
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "libslic3r/Fiber/FiberAdvanced.hpp"
#include "libslic3r/Fiber/FiberLayer.hpp"
#include "libslic3r/Point.hpp"

using namespace Slic3r;
using namespace Catch;

TEST_CASE("SpeedProfile - Basic operations", "[VariableSpeed]") {
    SECTION("Default construction") {
        SpeedProfile profile;
        
        REQUIRE(profile.start_speed > 0.0);
        REQUIRE(profile.end_speed > 0.0);
        REQUIRE(profile.interpolation == SpeedProfile::Linear);
    }
    
    SECTION("Constant speed") {
        SpeedProfile profile;
        profile.start_speed = 50.0;
        profile.end_speed = 50.0;
        profile.interpolation = SpeedProfile::Constant;
        
        REQUIRE(profile.get_speed_at(0.0) == Approx(50.0));
        REQUIRE(profile.get_speed_at(0.5) == Approx(50.0));
        REQUIRE(profile.get_speed_at(1.0) == Approx(50.0));
    }
    
    SECTION("Linear interpolation") {
        SpeedProfile profile;
        profile.start_speed = 30.0;
        profile.end_speed = 70.0;
        profile.interpolation = SpeedProfile::Linear;
        
        REQUIRE(profile.get_speed_at(0.0) == Approx(30.0));
        REQUIRE(profile.get_speed_at(0.5) == Approx(50.0).margin(0.1));
        REQUIRE(profile.get_speed_at(1.0) == Approx(70.0));
    }
    
    SECTION("Control points") {
        SpeedProfile profile;
        profile.start_speed = 30.0;
        profile.end_speed = 70.0;
        profile.interpolation = SpeedProfile::Linear;
        profile.control_points = {{0.25, 40.0}, {0.75, 60.0}};
        
        // Should interpolate through control points
        double speed_025 = profile.get_speed_at(0.25);
        double speed_075 = profile.get_speed_at(0.75);
        
        REQUIRE(speed_025 == Approx(40.0).margin(1.0));
        REQUIRE(speed_075 == Approx(60.0).margin(1.0));
    }
}

TEST_CASE("AdvancedFiberPath - Variable speed", "[VariableSpeed]") {
    SECTION("Default construction") {
        AdvancedFiberPath path;
        
        REQUIRE(path.fiber_type == FiberType::Carbon);
        REQUIRE(path.start_pressure == 0.0);
        REQUIRE(path.end_pressure == 0.0);
        REQUIRE(path.speed_profile.start_speed > 0.0);
    }
    
    SECTION("Path with speed profile") {
        FiberPath base_path;
        base_path.polyline = Polyline{{0, 0}, {100, 0}, {100, 100}};
        base_path.z = 0.2;
        
        AdvancedFiberPath advanced_path(base_path, FiberType::Carbon);
        advanced_path.speed_profile.start_speed = 30.0;
        advanced_path.speed_profile.end_speed = 70.0;
        advanced_path.speed_profile.interpolation = SpeedProfile::Linear;
        
        REQUIRE(advanced_path.polyline.size() == 3);
        REQUIRE(advanced_path.fiber_type == FiberType::Carbon);
        REQUIRE(advanced_path.speed_profile.start_speed == 30.0);
        REQUIRE(advanced_path.speed_profile.end_speed == 70.0);
    }
    
    SECTION("Path with anchor points") {
        AdvancedFiberPath path;
        path.anchor_points = {Point(0, 0), Point(100, 100)};
        
        REQUIRE(path.anchor_points.size() == 2);
    }
    
    SECTION("Path with custom G-code") {
        AdvancedFiberPath path;
        path.pre_gcode = "M106 S255";
        path.post_gcode = "M107";
        
        REQUIRE(path.pre_gcode == "M106 S255");
        REQUIRE(path.post_gcode == "M107");
    }
}

TEST_CASE("AdvancedFiberPlacement - Variable speed generation", "[VariableSpeed]") {
    AdvancedFiberPlacement placement;
    
    SECTION("Generate paths with variable speed") {
        FiberPath base_path;
        base_path.polyline = Polyline{{0, 0}, {100, 0}, {100, 100}};
        base_path.z = 0.2;
        
        FiberPaths base_paths{base_path};
        
        FiberMaterialProperties material;
        material.default_speed_mm_per_s = 50.0;
        material.min_speed_mm_per_s = 20.0;
        material.max_speed_mm_per_s = 100.0;
        
        ExPolygons geometry;
        
        AdvancedFiberPaths result = placement.generate_paths_with_variable_speed(
            base_paths, material, geometry
        );
        
        REQUIRE(result.size() == 1);
        REQUIRE(result[0].speed_profile.start_speed > 0.0);
    }
}

TEST_CASE("AdvancedFiberPlacement - Speed calculation", "[VariableSpeed]") {
    AdvancedFiberPlacement placement;
    
    SECTION("Calculate optimal speed for straight segment") {
        FiberMaterialProperties material;
        material.default_speed_mm_per_s = 50.0;
        material.min_speed_mm_per_s = 20.0;
        material.max_speed_mm_per_s = 100.0;
        
        Point p1(0, 0);
        Point p2(100, 0);
        Point p3(200, 0); // Straight line
        
        double speed = placement.calculate_optimal_speed(p1, p2, p3, material);
        
        REQUIRE(speed >= material.min_speed_mm_per_s);
        REQUIRE(speed <= material.max_speed_mm_per_s);
    }
    
    SECTION("Calculate optimal speed for curved segment") {
        FiberMaterialProperties material;
        material.default_speed_mm_per_s = 50.0;
        material.min_speed_mm_per_s = 20.0;
        material.max_speed_mm_per_s = 100.0;
        
        Point p1(0, 0);
        Point p2(50, 50);
        Point p3(0, 100); // Sharp turn
        
        double speed = placement.calculate_optimal_speed(p1, p2, p3, material);
        
        // Curved segments should generally be slower
        REQUIRE(speed >= material.min_speed_mm_per_s);
        REQUIRE(speed <= material.max_speed_mm_per_s);
    }
}

TEST_CASE("AdvancedFiberPlacement - Speed profile smoothing", "[VariableSpeed]") {
    AdvancedFiberPlacement placement;
    
    SECTION("Smooth speed profile") {
        SpeedProfile profile;
        profile.start_speed = 30.0;
        profile.end_speed = 70.0;
        profile.interpolation = SpeedProfile::Linear;
        
        SpeedProfile smoothed = placement.smooth_speed_profile(profile);
        
        REQUIRE(smoothed.start_speed > 0.0);
        REQUIRE(smoothed.end_speed > 0.0);
    }
}

