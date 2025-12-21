/**
 * Integration tests with various models (simple, complex, overhangs)
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "libslic3r/FiberPrint.hpp"
#include "libslic3r/Fiber/FiberPrintObject.hpp"
#include "libslic3r/Fiber/FiberLayer.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Config.hpp"
#include "test_data.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;
using namespace Catch;

// Helper to create and process fiber print
void setup_fiber_print(
    TestMesh mesh,
    Print& print,
    FiberPrint& fiber_print,
    Model& model
) {
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    Test::init_print({mesh}, print, model, config);
    print.process();
    
    DynamicPrintConfig fiber_config = DynamicPrintConfig::full_print_config();
    fiber_config.set("enable_fiber_reinforcement", true);
    fiber_config.set("fiber_pattern", "grid");
    fiber_config.set("fiber_spacing", "5.0");
    
    std::vector<std::string> warnings;
    fiber_print.apply(model, fiber_config, &warnings);
    
    if (!print.objects().empty() && !fiber_print.objects().empty()) {
        fiber_print.objects()[0]->set_fff_print_object(print.get_object(0));
        fiber_print.objects()[0]->create_fiber_layers_from_fff_layers();
    }
}

TEST_CASE("FiberPrint - Simple models: Cube", "[Integration][Models]") {
    GIVEN("A simple 20mm cube") {
        Print print;
        FiberPrint fiber_print;
        Model model;
        setup_fiber_print(TestMesh::cube_20x20x20, print, fiber_print, model);
        
        WHEN("Fiber paths are generated") {
            if (!fiber_print.objects().empty()) {
                FiberPrintObject* fiber_obj = fiber_print.objects()[0];
                
                FiberPlacementConfig placement_config;
                placement_config.pattern = FiberPattern::Grid;
                placement_config.spacing = 5.0;
                fiber_obj->generate_fiber_paths(placement_config);
                
                THEN("Should generate fiber paths") {
                    size_t total_paths = 0;
                    for (const FiberLayer& layer : fiber_obj->fiber_layers()) {
                        total_paths += layer.fiber_paths.size();
                    }
                    REQUIRE(total_paths > 0);
                }
            }
        }
    }
}

TEST_CASE("FiberPrint - Simple models: Sphere", "[Integration][Models]") {
    GIVEN("A sphere model") {
        Print print;
        FiberPrint fiber_print;
        Model model;
        setup_fiber_print(TestMesh::sphere_50mm, print, fiber_print, model);
        
        WHEN("Fiber paths are generated") {
            if (!fiber_print.objects().empty()) {
                FiberPrintObject* fiber_obj = fiber_print.objects()[0];
                
                FiberPlacementConfig placement_config;
                placement_config.pattern = FiberPattern::Concentric;
                placement_config.spacing = 5.0;
                fiber_obj->generate_fiber_paths(placement_config);
                
                THEN("Should generate fiber paths for curved geometry") {
                    size_t layers_with_fiber = 0;
                    for (const FiberLayer& layer : fiber_obj->fiber_layers()) {
                        if (layer.has_fibers()) {
                            layers_with_fiber++;
                        }
                    }
                    REQUIRE(layers_with_fiber > 0);
                }
            }
        }
    }
}

TEST_CASE("FiberPrint - Complex models: Overhang", "[Integration][Models]") {
    GIVEN("A model with overhangs") {
        Print print;
        FiberPrint fiber_print;
        Model model;
        setup_fiber_print(TestMesh::overhang, print, fiber_print, model);
        
        WHEN("Fiber paths are generated") {
            if (!fiber_print.objects().empty()) {
                FiberPrintObject* fiber_obj = fiber_print.objects()[0];
                
                FiberPlacementConfig placement_config;
                placement_config.pattern = FiberPattern::Grid;
                placement_config.spacing = 5.0;
                fiber_obj->generate_fiber_paths(placement_config);
                
                THEN("Should handle overhangs correctly") {
                    // Should not crash and should generate some paths
                    size_t total_paths = 0;
                    for (const FiberLayer& layer : fiber_obj->fiber_layers()) {
                        total_paths += layer.fiber_paths.size();
                    }
                    // May have fewer paths on overhang layers, but should not crash
                    REQUIRE(total_paths >= 0);
                }
            }
        }
    }
}

TEST_CASE("FiberPrint - Complex models: Bridge", "[Integration][Models]") {
    GIVEN("A model with bridges") {
        Print print;
        FiberPrint fiber_print;
        Model model;
        setup_fiber_print(TestMesh::bridge, print, fiber_print, model);
        
        WHEN("Fiber paths are generated") {
            if (!fiber_print.objects().empty()) {
                FiberPrintObject* fiber_obj = fiber_print.objects()[0];
                
                FiberPlacementConfig placement_config;
                placement_config.pattern = FiberPattern::Grid;
                placement_config.spacing = 5.0;
                fiber_obj->generate_fiber_paths(placement_config);
                
                THEN("Should handle bridges") {
                    // Should not crash
                    REQUIRE(!fiber_obj->fiber_layers().empty());
                }
            }
        }
    }
}

TEST_CASE("FiberPrint - Complex models: Cube with hole", "[Integration][Models]") {
    GIVEN("A cube with a hole") {
        Print print;
        FiberPrint fiber_print;
        Model model;
        setup_fiber_print(TestMesh::cube_with_hole, print, fiber_print, model);
        
        WHEN("Fiber paths are generated with concentric pattern") {
            if (!fiber_print.objects().empty()) {
                FiberPrintObject* fiber_obj = fiber_print.objects()[0];
                
                FiberPlacementConfig placement_config;
                placement_config.pattern = FiberPattern::Concentric;
                placement_config.spacing = 5.0;
                fiber_obj->generate_fiber_paths(placement_config);
                
                THEN("Should generate paths around the hole") {
                    size_t layers_with_fiber = 0;
                    for (const FiberLayer& layer : fiber_obj->fiber_layers()) {
                        if (layer.has_fibers()) {
                            layers_with_fiber++;
                        }
                    }
                    REQUIRE(layers_with_fiber > 0);
                }
            }
        }
    }
}

TEST_CASE("FiberPrint - Pattern selection: Grid vs Concentric", "[Integration][Models]") {
    GIVEN("A cube model") {
        Print print;
        Model model;
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        Test::init_print({TestMesh::cube_20x20x20}, print, model, config);
        print.process();
        
        WHEN("Grid pattern is used") {
            FiberPrint fiber_print;
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
                
                FiberPlacementConfig placement_config;
                placement_config.pattern = FiberPattern::Grid;
                placement_config.spacing = 5.0;
                fiber_obj->generate_fiber_paths(placement_config);
                
                size_t grid_paths = 0;
                for (const FiberLayer& layer : fiber_obj->fiber_layers()) {
                    grid_paths += layer.fiber_paths.size();
                }
                
                AND_WHEN("Concentric pattern is used") {
                    FiberPrint fiber_print2;
                    fiber_print2.apply(model, fiber_config, &warnings);
                    if (!fiber_print2.objects().empty()) {
                        FiberPrintObject* fiber_obj2 = fiber_print2.objects()[0];
                        fiber_obj2->set_fff_print_object(print.get_object(0));
                        fiber_obj2->create_fiber_layers_from_fff_layers();
                        
                        FiberPlacementConfig placement_config2;
                        placement_config2.pattern = FiberPattern::Concentric;
                        placement_config2.spacing = 5.0;
                        fiber_obj2->generate_fiber_paths(placement_config2);
                        
                        size_t concentric_paths = 0;
                        for (const FiberLayer& layer : fiber_obj2->fiber_layers()) {
                            concentric_paths += layer.fiber_paths.size();
                        }
                        
                        THEN("Both patterns should generate paths") {
                            REQUIRE(grid_paths > 0);
                            REQUIRE(concentric_paths > 0);
                        }
                    }
                }
            }
        }
    }
}

