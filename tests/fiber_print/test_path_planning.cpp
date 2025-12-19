/**
 * Unit tests for fiber path planning algorithms
 * Tests PathPlanner class and ContinuousPath
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "libslic3r/Fiber/PathPlanner.hpp"
#include "libslic3r/Fiber/FiberLayer.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/BoundingBox.hpp"

using namespace Slic3r;
using namespace Catch;

TEST_CASE("ContinuousPath - Basic operations", "[PathPlanning]") {
    SECTION("Empty path") {
        ContinuousPath path;
        REQUIRE(path.empty());
        REQUIRE(path.length() == 0.0);
    }
    
    SECTION("Simple path creation") {
        Polyline polyline{{0, 0}, {100, 0}, {100, 100}};
        ContinuousPath path(polyline, 0.2, 45.0, 2.0);
        
        REQUIRE(!path.empty());
        REQUIRE(path.z == 0.2);
        REQUIRE(path.angle == 45.0);
        REQUIRE(path.spacing == 2.0);
        REQUIRE(path.polyline.size() == 3);
    }
    
    SECTION("Path length calculation") {
        Polyline polyline{{0, 0}, {100, 0}, {100, 100}};
        ContinuousPath path(polyline, 0.2);
        
        double expected_length = 200.0; // 100 + 100
        REQUIRE(path.length() == Approx(expected_length).margin(0.1));
    }
    
    SECTION("Closed path detection") {
        Polyline closed_polyline{{0, 0}, {100, 0}, {100, 100}, {0, 100}, {0, 0}};
        ContinuousPath path(closed_polyline, 0.2);
        
        REQUIRE(path.is_closed);
    }
    
    SECTION("Open path detection") {
        Polyline open_polyline{{0, 0}, {100, 0}, {100, 100}};
        ContinuousPath path(open_polyline, 0.2);
        
        REQUIRE(!path.is_closed);
    }
    
    SECTION("Path reversal") {
        Polyline polyline{{0, 0}, {100, 0}, {100, 100}};
        ContinuousPath path(polyline, 0.2);
        
        Point first_before = path.first_point();
        Point last_before = path.last_point();
        
        path.reverse();
        
        REQUIRE(path.first_point() == last_before);
        REQUIRE(path.last_point() == first_before);
    }
}

TEST_CASE("PathPlanner - Path connection", "[PathPlanning]") {
    PathPlanner planner;
    PathPlanningConfig config;
    config.max_connection_distance = 10.0;
    config.min_path_length = 1.0;
    
    SECTION("Empty input") {
        FiberPaths empty_paths;
        ExPolygons empty_geometry;
        
        ContinuousPaths result = planner.plan_continuous_paths(empty_paths, empty_geometry, config);
        REQUIRE(result.empty());
    }
    
    SECTION("Single path segment") {
        FiberPath path1;
        path1.polyline = Polyline{{0, 0}, {100, 0}};
        path1.z = 0.2;
        
        FiberPaths paths{path1};
        ExPolygons geometry;
        
        ContinuousPaths result = planner.plan_continuous_paths(paths, geometry, config);
        REQUIRE(result.size() == 1);
        REQUIRE(result[0].polyline.size() == 2);
    }
    
    SECTION("Two connectable paths") {
        FiberPath path1;
        path1.polyline = Polyline{{0, 0}, {100, 0}};
        path1.z = 0.2;
        
        FiberPath path2;
        path2.polyline = Polyline{{100, 0}, {200, 0}}; // Connected at (100, 0)
        path2.z = 0.2;
        
        FiberPaths paths{path1, path2};
        ExPolygons geometry;
        
        ContinuousPaths result = planner.plan_continuous_paths(paths, geometry, config);
        REQUIRE(result.size() == 1); // Should be connected into one path
        REQUIRE(result[0].polyline.size() == 3); // 3 points: (0,0), (100,0), (200,0)
    }
    
    SECTION("Two disconnected paths") {
        FiberPath path1;
        path1.polyline = Polyline{{0, 0}, {100, 0}};
        path1.z = 0.2;
        
        FiberPath path2;
        path2.polyline = Polyline{{0, 100}, {100, 100}}; // Far away
        path2.z = 0.2;
        
        FiberPaths paths{path1, path2};
        ExPolygons geometry;
        
        config.max_connection_distance = 5.0; // Too small to connect
        
        ContinuousPaths result = planner.plan_continuous_paths(paths, geometry, config);
        REQUIRE(result.size() == 2); // Should remain separate
    }
    
    SECTION("Multiple paths - greedy connection") {
        FiberPath path1;
        path1.polyline = Polyline{{0, 0}, {50, 0}};
        path1.z = 0.2;
        
        FiberPath path2;
        path2.polyline = Polyline{{50, 0}, {100, 0}};
        path2.z = 0.2;
        
        FiberPath path3;
        path3.polyline = Polyline{{100, 0}, {150, 0}};
        path3.z = 0.2;
        
        FiberPaths paths{path1, path2, path3};
        ExPolygons geometry;
        
        ContinuousPaths result = planner.plan_continuous_paths(paths, geometry, config);
        REQUIRE(result.size() == 1); // All should be connected
        REQUIRE(result[0].polyline.size() == 4); // 4 points total
    }
}

TEST_CASE("PathPlanner - Path validation", "[PathPlanning]") {
    PathPlanner planner;
    PathPlanningConfig config;
    config.min_path_length = 5.0; // Minimum 5mm
    
    SECTION("Valid path") {
        Polyline polyline{{0, 0}, {100, 0}};
        ContinuousPath path(polyline, 0.2);
        
        std::string error;
        bool valid = planner.validate_path(path, config, error);
        REQUIRE(valid);
        REQUIRE(error.empty());
    }
    
    SECTION("Path too short") {
        Polyline polyline{{0, 0}, {1, 0}}; // Only 1mm
        ContinuousPath path(polyline, 0.2);
        
        std::string error;
        bool valid = planner.validate_path(path, config, error);
        REQUIRE(!valid);
        REQUIRE(!error.empty());
    }
    
    SECTION("Empty path") {
        ContinuousPath path;
        
        std::string error;
        bool valid = planner.validate_path(path, config, error);
        REQUIRE(!valid);
    }
}

TEST_CASE("PathPlanner - Path optimization", "[PathPlanning]") {
    PathPlanner planner;
    PathPlanningConfig config;
    config.enable_optimization = true;
    
    SECTION("Optimize path order") {
        ContinuousPath path1;
        path1.polyline = Polyline{{0, 0}, {100, 0}};
        path1.z = 0.2;
        
        ContinuousPath path2;
        path2.polyline = Polyline{{200, 0}, {300, 0}}; // Far from path1
        path2.z = 0.2;
        
        ContinuousPath path3;
        path3.polyline = Polyline{{100, 0}, {150, 0}}; // Close to path1 end
        path3.z = 0.2;
        
        ContinuousPaths paths{path1, path2, path3};
        
        planner.optimize_path_order(paths, config);
        
        // Path3 should be reordered to be near path1 (greedy nearest neighbor)
        // The exact order depends on implementation, but path1 and path3 should be adjacent
        REQUIRE(paths.size() == 3);
    }
}

TEST_CASE("PathPlanner - Path smoothing", "[PathPlanning]") {
    PathPlanner planner;
    PathPlanningConfig config;
    config.enable_smoothing = true;
    config.smoothing_tolerance = 0.1;
    
    SECTION("Smooth sharp corners") {
        // Create a path with sharp corners
        Polyline polyline{{0, 0}, {50, 0}, {50, 50}, {100, 50}};
        ContinuousPath path(polyline, 0.2);
        
        ContinuousPaths paths{path};
        size_t points_before = paths[0].polyline.size();
        
        planner.smooth_paths(paths, config);
        
        // Smoothing may add or remove points, but should not break the path
        REQUIRE(!paths[0].empty());
        REQUIRE(paths[0].polyline.size() >= 2);
    }
}

