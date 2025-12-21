/**
 * Unit tests for fiber pattern generation
 * Tests FiberPlacement class - grid and concentric patterns
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "libslic3r/Fiber/FiberPlacement.hpp"
#include "libslic3r/Fiber/FiberLayer.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Polygon.hpp"

using namespace Slic3r;
using namespace Catch;

TEST_CASE("FiberPlacement - Grid pattern generation", "[PatternGeneration]") {
    FiberPlacement placement;
    FiberPlacementConfig config;
    config.pattern = FiberPattern::Grid;
    config.spacing = 2.0;
    config.angle = 0.0; // Horizontal lines
    
    SECTION("Empty geometry") {
        ExPolygons empty;
        FiberLayer layer;
        layer.id = 0;
        layer.print_z = 0.2;
        bool result = placement.generate_paths_for_layer(layer, empty, config);
        REQUIRE(!result); // Should fail for empty geometry
    }
    
    SECTION("Simple rectangle - horizontal lines") {
        // Create a simple rectangle
        Polygon rect;
        rect.points = {{0, 0}, {100, 0}, {100, 100}, {0, 100}};
        ExPolygon expoly;
        expoly.contour = rect;
        ExPolygons geometry{expoly};
        
        config.angle = 0.0; // Horizontal
        config.spacing = 10.0;
        
        FiberLayer layer;
        layer.id = 0;
        layer.print_z = 0.2;
        bool success = placement.generate_paths_for_layer(layer, geometry, config);
        REQUIRE(success);
        
        Polylines result;
        for (const FiberPath& path : layer.fiber_paths) {
            result.push_back(path.polyline);
        }
        
        // Should generate multiple horizontal lines
        REQUIRE(!result.empty());
        REQUIRE(result.size() >= 5); // At least 5 lines for 100mm height with 10mm spacing
        
        // Check that lines are roughly horizontal
        for (const Polyline& line : result) {
            if (line.size() >= 2) {
                // Y coordinate should be approximately constant
                double y = line.points[0].y();
                for (size_t i = 1; i < line.size(); ++i) {
                    REQUIRE(std::abs(line.points[i].y() - y) < 1.0); // Allow small tolerance
                }
            }
        }
    }
    
    SECTION("Simple rectangle - vertical lines") {
        Polygon rect;
        rect.points = {{0, 0}, {100, 0}, {100, 100}, {0, 100}};
        ExPolygon expoly;
        expoly.contour = rect;
        ExPolygons geometry{expoly};
        
        config.angle = 90.0; // Vertical
        config.spacing = 10.0;
        
        FiberLayer layer;
        layer.id = 0;
        layer.print_z = 0.2;
        bool success = placement.generate_paths_for_layer(layer, geometry, config);
        REQUIRE(success);
        
        Polylines result;
        for (const FiberPath& path : layer.fiber_paths) {
            result.push_back(path.polyline);
        }
        
        REQUIRE(!result.empty());
        
        // Check that lines are roughly vertical
        for (const Polyline& line : result) {
            if (line.size() >= 2) {
                // X coordinate should be approximately constant
                double x = line.points[0].x();
                for (size_t i = 1; i < line.size(); ++i) {
                    REQUIRE(std::abs(line.points[i].x() - x) < 1.0);
                }
            }
        }
    }
    
    SECTION("Simple rectangle - 45 degree angle") {
        Polygon rect;
        rect.points = {{0, 0}, {100, 0}, {100, 100}, {0, 100}};
        ExPolygon expoly;
        expoly.contour = rect;
        ExPolygons geometry{expoly};
        
        config.angle = 45.0;
        config.spacing = 10.0;
        
        FiberLayer layer;
        layer.id = 0;
        layer.print_z = 0.2;
        bool success = placement.generate_paths_for_layer(layer, geometry, config);
        REQUIRE(success);
        
        Polylines result;
        for (const FiberPath& path : layer.fiber_paths) {
            result.push_back(path.polyline);
        }
        
        REQUIRE(!result.empty());
        
        // Check that lines have approximately 45 degree slope
        for (const Polyline& line : result) {
            if (line.size() >= 2) {
                double dx = line.points.back().x() - line.points.front().x();
                double dy = line.points.back().y() - line.points.front().y();
                if (std::abs(dx) > 0.1) {
                    double slope = dy / dx;
                    REQUIRE(std::abs(slope - 1.0) < 0.2); // Approximately 1.0 for 45 degrees
                }
            }
        }
    }
    
    SECTION("Spacing affects line count") {
        Polygon rect;
        rect.points = {{0, 0}, {100, 0}, {100, 100}, {0, 100}};
        ExPolygon expoly;
        expoly.contour = rect;
        ExPolygons geometry{expoly};
        
        config.angle = 0.0;
        
        config.spacing = 20.0;
        FiberLayer layer1;
        layer1.id = 0;
        layer1.print_z = 0.2;
        placement.generate_paths_for_layer(layer1, geometry, config);
        Polylines result_large;
        for (const FiberPath& path : layer1.fiber_paths) {
            result_large.push_back(path.polyline);
        }
        
        config.spacing = 5.0;
        FiberLayer layer2;
        layer2.id = 0;
        layer2.print_z = 0.2;
        placement.generate_paths_for_layer(layer2, geometry, config);
        Polylines result_small;
        for (const FiberPath& path : layer2.fiber_paths) {
            result_small.push_back(path.polyline);
        }
        
        // Smaller spacing should generate more lines
        REQUIRE(result_small.size() > result_large.size());
    }
}

TEST_CASE("FiberPlacement - Concentric pattern generation", "[PatternGeneration]") {
    FiberPlacement placement;
    FiberPlacementConfig config;
    config.pattern = FiberPattern::Concentric;
    config.spacing = 2.0;
    
    SECTION("Empty geometry") {
        ExPolygons empty;
        FiberLayer layer;
        layer.id = 0;
        layer.print_z = 0.2;
        bool result = placement.generate_paths_for_layer(layer, empty, config);
        REQUIRE(!result);
    }
    
    SECTION("Simple rectangle - concentric loops") {
        Polygon rect;
        rect.points = {{0, 0}, {100, 0}, {100, 100}, {0, 100}};
        ExPolygon expoly;
        expoly.contour = rect;
        ExPolygons geometry{expoly};
        
        config.spacing = 10.0;
        
        FiberLayer layer;
        layer.id = 0;
        layer.print_z = 0.2;
        bool success = placement.generate_paths_for_layer(layer, geometry, config);
        REQUIRE(success);
        
        Polylines result;
        for (const FiberPath& path : layer.fiber_paths) {
            result.push_back(path.polyline);
        }
        
        // Should generate multiple concentric loops
        REQUIRE(!result.empty());
        REQUIRE(result.size() >= 3); // At least 3 loops for 100mm with 10mm spacing
        
        // Check that paths are closed (concentric loops)
        for (const Polyline& path : result) {
            if (path.size() >= 3) {
                // First and last points should be close (closed loop)
                double dist = std::sqrt(
                    std::pow(path.points.front().x() - path.points.back().x(), 2) +
                    std::pow(path.points.front().y() - path.points.back().y(), 2)
                );
                REQUIRE(dist < 1.0); // Should be closed (within 1 unit)
            }
        }
    }
    
    SECTION("Circle-like shape - concentric") {
        // Create a roughly circular polygon
        Polygon circle;
        const int num_points = 32;
        const double radius = 50.0;
        const Point center(50, 50);
        for (int i = 0; i < num_points; ++i) {
            double angle = 2.0 * M_PI * i / num_points;
            int x = center.x() + static_cast<int>(radius * std::cos(angle));
            int y = center.y() + static_cast<int>(radius * std::sin(angle));
            circle.points.emplace_back(x, y);
        }
        
        ExPolygon expoly;
        expoly.contour = circle;
        ExPolygons geometry{expoly};
        
        config.spacing = 5.0;
        
        FiberLayer layer;
        layer.id = 0;
        layer.print_z = 0.2;
        bool success = placement.generate_paths_for_layer(layer, geometry, config);
        REQUIRE(success);
        
        Polylines result;
        for (const FiberPath& path : layer.fiber_paths) {
            result.push_back(path.polyline);
        }
        
        REQUIRE(!result.empty());
        REQUIRE(result.size() >= 5); // Multiple concentric circles
    }
    
    SECTION("Spacing affects loop count") {
        Polygon rect;
        rect.points = {{0, 0}, {100, 0}, {100, 100}, {0, 100}};
        ExPolygon expoly;
        expoly.contour = rect;
        ExPolygons geometry{expoly};
        
        config.spacing = 20.0;
        FiberLayer layer1;
        layer1.id = 0;
        layer1.print_z = 0.2;
        placement.generate_paths_for_layer(layer1, geometry, config);
        Polylines result_large;
        for (const FiberPath& path : layer1.fiber_paths) {
            result_large.push_back(path.polyline);
        }
        
        config.spacing = 5.0;
        FiberLayer layer2;
        layer2.id = 0;
        layer2.print_z = 0.2;
        placement.generate_paths_for_layer(layer2, geometry, config);
        Polylines result_small;
        for (const FiberPath& path : layer2.fiber_paths) {
            result_small.push_back(path.polyline);
        }
        
        // Smaller spacing should generate more loops
        REQUIRE(result_small.size() > result_large.size());
    }
}

TEST_CASE("FiberPlacement - Layer selection", "[PatternGeneration]") {
    FiberPlacement placement;
    FiberPlacementConfig config;
    
    SECTION("Layer interval") {
        config.layer_interval = 2; // Every 2nd layer
        
        REQUIRE(placement.should_place_fiber_on_layer(0, config)); // Layer 0
        REQUIRE(!placement.should_place_fiber_on_layer(1, config)); // Layer 1
        REQUIRE(placement.should_place_fiber_on_layer(2, config)); // Layer 2
        REQUIRE(!placement.should_place_fiber_on_layer(3, config)); // Layer 3
        REQUIRE(placement.should_place_fiber_on_layer(4, config)); // Layer 4
    }
    
    SECTION("Start layer") {
        config.start_layer = 5;
        config.layer_interval = 1;
        
        REQUIRE(!placement.should_place_fiber_on_layer(4, config));
        REQUIRE(placement.should_place_fiber_on_layer(5, config));
        REQUIRE(placement.should_place_fiber_on_layer(6, config));
    }
    
    SECTION("End layer") {
        config.end_layer = 10;
        config.layer_interval = 1;
        
        REQUIRE(placement.should_place_fiber_on_layer(9, config));
        REQUIRE(placement.should_place_fiber_on_layer(10, config));
        REQUIRE(!placement.should_place_fiber_on_layer(11, config));
    }
    
    SECTION("Angle per layer") {
        config.angle = 0.0; // Default
        config.angle_per_layer = {0.0, 45.0, 90.0};
        
        REQUIRE(placement.get_angle_for_layer(0, config) == 0.0);
        REQUIRE(placement.get_angle_for_layer(1, config) == 45.0);
        REQUIRE(placement.get_angle_for_layer(2, config) == 90.0);
        REQUIRE(placement.get_angle_for_layer(3, config) == 0.0); // Falls back to default
    }
}

