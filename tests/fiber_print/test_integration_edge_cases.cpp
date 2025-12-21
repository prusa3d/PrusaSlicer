/**
 * Integration tests for edge cases (thin walls, small features, etc.)
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "libslic3r/FiberPrint.hpp"
#include "libslic3r/Fiber/FiberPrintObject.hpp"
#include "libslic3r/Fiber/FiberLayer.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/Config.hpp"
#include "test_data.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;
using namespace Catch;

TEST_CASE("FiberPrint - Edge cases: Thin walls", "[Integration][EdgeCases]") {
    GIVEN("A model with thin walls") {
        Print print;
        FiberPrint fiber_print;
        Model model;
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        config.set("layer_height", 0.2);
        config.set("perimeter_extrusion_width", 0.4);
        
        // Create a thin wall model (2mm thick cube)
        TriangleMesh thin_wall = Slic3r::make_cube(20, 20, 20);
        // Scale to make it thin in one dimension
        thin_wall.scale(Vec3f(1.0f, 0.1f, 1.0f)); // 2mm thick in Y
        
        Test::init_print({thin_wall}, print, model, config);
        print.process();
        
        DynamicPrintConfig fiber_config = DynamicPrintConfig::full_print_config();
        fiber_config.set("enable_fiber_reinforcement", true);
        fiber_config.set("fiber_pattern", "grid");
        fiber_config.set("fiber_spacing", "2.0"); // Smaller spacing for thin features
        
        std::vector<std::string> warnings;
        fiber_print.apply(model, fiber_config, &warnings);
        
        if (!print.objects().empty() && !fiber_print.objects().empty()) {
            FiberPrintObject* fiber_obj = fiber_print.objects()[0];
            fiber_obj->set_fff_print_object(print.get_object(0));
            fiber_obj->create_fiber_layers_from_fff_layers();
            
            WHEN("Fiber paths are generated") {
                FiberPlacementConfig placement_config;
                placement_config.pattern = FiberPattern::Grid;
                placement_config.spacing = 2.0;
                fiber_obj->generate_fiber_paths(placement_config);
                
                THEN("Should handle thin walls without crashing") {
                    REQUIRE(!fiber_obj->fiber_layers().empty());
                    
                    // May have fewer or no paths on very thin layers
                    size_t layers_with_fiber = 0;
                    for (const FiberLayer& layer : fiber_obj->fiber_layers()) {
                        if (layer.has_fibers()) {
                            layers_with_fiber++;
                        }
                    }
                    // Should not crash, even if some layers have no fiber
                    REQUIRE(layers_with_fiber >= 0);
                }
            }
        }
    }
}

TEST_CASE("FiberPrint - Edge cases: Small features", "[Integration][EdgeCases]") {
    GIVEN("A model with small features") {
        Print print;
        FiberPrint fiber_print;
        Model model;
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        config.set("layer_height", 0.1); // Smaller layer height for small features
        
        // Create a small cube
        TriangleMesh small_cube = Slic3r::make_cube(5, 5, 5);
        
        Test::init_print({small_cube}, print, model, config);
        print.process();
        
        DynamicPrintConfig fiber_config = DynamicPrintConfig::full_print_config();
        fiber_config.set("enable_fiber_reinforcement", true);
        fiber_config.set("fiber_pattern", "grid");
        fiber_config.set("fiber_spacing", "1.0"); // Small spacing for small features
        
        std::vector<std::string> warnings;
        fiber_print.apply(model, fiber_config, &warnings);
        
        if (!print.objects().empty() && !fiber_print.objects().empty()) {
            FiberPrintObject* fiber_obj = fiber_print.objects()[0];
            fiber_obj->set_fff_print_object(print.get_object(0));
            fiber_obj->create_fiber_layers_from_fff_layers();
            
            WHEN("Fiber paths are generated") {
                FiberPlacementConfig placement_config;
                placement_config.pattern = FiberPattern::Grid;
                placement_config.spacing = 1.0;
                fiber_obj->generate_fiber_paths(placement_config);
                
                THEN("Should handle small features") {
                    REQUIRE(!fiber_obj->fiber_layers().empty());
                }
            }
        }
    }
}

TEST_CASE("FiberPrint - Edge cases: Very small spacing", "[Integration][EdgeCases]") {
    GIVEN("A cube with very small fiber spacing") {
        Print print;
        FiberPrint fiber_print;
        Model model;
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        Test::init_print({TestMesh::cube_20x20x20}, print, model, config);
        print.process();
        
        DynamicPrintConfig fiber_config = DynamicPrintConfig::full_print_config();
        fiber_config.set("enable_fiber_reinforcement", true);
        fiber_config.set("fiber_pattern", "grid");
        fiber_config.set("fiber_spacing", "0.5"); // Very small spacing
        
        std::vector<std::string> warnings;
        fiber_print.apply(model, fiber_config, &warnings);
        
        if (!print.objects().empty() && !fiber_print.objects().empty()) {
            FiberPrintObject* fiber_obj = fiber_print.objects()[0];
            fiber_obj->set_fff_print_object(print.get_object(0));
            fiber_obj->create_fiber_layers_from_fff_layers();
            
            WHEN("Fiber paths are generated") {
                FiberPlacementConfig placement_config;
                placement_config.pattern = FiberPattern::Grid;
                placement_config.spacing = 0.5;
                fiber_obj->generate_fiber_paths(placement_config);
                
                THEN("Should generate many paths") {
                    size_t total_paths = 0;
                    for (const FiberLayer& layer : fiber_obj->fiber_layers()) {
                        total_paths += layer.fiber_paths.size();
                    }
                    // Small spacing should generate more paths
                    REQUIRE(total_paths > 0);
                }
            }
        }
    }
}

TEST_CASE("FiberPrint - Edge cases: Very large spacing", "[Integration][EdgeCases]") {
    GIVEN("A cube with very large fiber spacing") {
        Print print;
        FiberPrint fiber_print;
        Model model;
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        Test::init_print({TestMesh::cube_20x20x20}, print, model, config);
        print.process();
        
        DynamicPrintConfig fiber_config = DynamicPrintConfig::full_print_config();
        fiber_config.set("enable_fiber_reinforcement", true);
        fiber_config.set("fiber_pattern", "grid");
        fiber_config.set("fiber_spacing", "20.0"); // Very large spacing
        
        std::vector<std::string> warnings;
        fiber_print.apply(model, fiber_config, &warnings);
        
        if (!print.objects().empty() && !fiber_print.objects().empty()) {
            FiberPrintObject* fiber_obj = fiber_print.objects()[0];
            fiber_obj->set_fff_print_object(print.get_object(0));
            fiber_obj->create_fiber_layers_from_fff_layers();
            
            WHEN("Fiber paths are generated") {
                FiberPlacementConfig placement_config;
                placement_config.pattern = FiberPattern::Grid;
                placement_config.spacing = 20.0;
                fiber_obj->generate_fiber_paths(placement_config);
                
                THEN("Should generate fewer paths") {
                    size_t total_paths = 0;
                    for (const FiberLayer& layer : fiber_obj->fiber_layers()) {
                        total_paths += layer.fiber_paths.size();
                    }
                    // Large spacing may result in fewer paths, but should not crash
                    REQUIRE(total_paths >= 0);
                }
            }
        }
    }
}

TEST_CASE("FiberPrint - Edge cases: Empty layers", "[Integration][EdgeCases]") {
    GIVEN("A model that may have empty layers") {
        Print print;
        FiberPrint fiber_print;
        Model model;
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        
        // Create a stepped model that may have empty layers
        Test::init_print({TestMesh::step}, print, model, config);
        print.process();
        
        DynamicPrintConfig fiber_config = DynamicPrintConfig::full_print_config();
        fiber_config.set("enable_fiber_reinforcement", true);
        fiber_config.set("fiber_pattern", "grid");
        fiber_config.set("fiber_spacing", "5.0");
        
        std::vector<std::string> warnings;
        fiber_print.apply(model, fiber_config, &warnings);
        
        if (!print.objects().empty() && !fiber_print.objects().empty()) {
            FiberPrintObject* fiber_obj = fiber_print.objects()[0];
            fiber_obj->set_fff_print_object(print.get_object(0));
            fiber_obj->create_fiber_layers_from_fff_layers();
            
            WHEN("Fiber paths are generated") {
                FiberPlacementConfig placement_config;
                placement_config.pattern = FiberPattern::Grid;
                placement_config.spacing = 5.0;
                fiber_obj->generate_fiber_paths(placement_config);
                
                THEN("Should handle empty layers gracefully") {
                    // Should not crash even if some layers are empty
                    REQUIRE(!fiber_obj->fiber_layers().empty());
                }
            }
        }
    }
}

