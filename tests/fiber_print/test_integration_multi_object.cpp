/**
 * Integration tests for multi-object prints
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

TEST_CASE("FiberPrint - Multi-object: Two cubes", "[Integration][MultiObject]") {
    GIVEN("Two cube models") {
        Print print;
        FiberPrint fiber_print;
        Model model;
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        
        // Create two separate cubes
        TriangleMesh cube1 = Slic3r::make_cube(20, 20, 20);
        cube1.translate(Vec3f(-15.0f, 0.0f, 0.0f));
        TriangleMesh cube2 = Slic3r::make_cube(20, 20, 20);
        cube2.translate(Vec3f(15.0f, 0.0f, 0.0f));
        
        Test::init_print({cube1, cube2}, print, model, config);
        print.process();
        
        DynamicPrintConfig fiber_config = DynamicPrintConfig::full_print_config();
        fiber_config.set("enable_fiber_reinforcement", true);
        fiber_config.set("fiber_pattern", "grid");
        fiber_config.set("fiber_spacing", "5.0");
        
        std::vector<std::string> warnings;
        fiber_print.apply(model, fiber_config, &warnings);
        
        WHEN("FiberPrint is processed") {
            THEN("Should have multiple objects") {
                REQUIRE(fiber_print.objects().size() == 2);
                REQUIRE(print.objects().size() == 2);
            }
            
            THEN("Each object should be linked to FFF PrintObject") {
                for (size_t i = 0; i < fiber_print.objects().size() && i < print.objects().size(); ++i) {
                    FiberPrintObject* fiber_obj = fiber_print.objects()[i];
                    PrintObject* fff_obj = print.get_object(i);
                    
                    fiber_obj->set_fff_print_object(fff_obj);
                    fiber_obj->create_fiber_layers_from_fff_layers();
                    
                    REQUIRE(fiber_obj->fff_print_object() == fff_obj);
                    REQUIRE(fiber_obj->fiber_layers().size() == fff_obj->layers().size());
                }
            }
            
            WHEN("Fiber paths are generated for all objects") {
                FiberPlacementConfig placement_config;
                placement_config.pattern = FiberPattern::Grid;
                placement_config.spacing = 5.0;
                
                for (FiberPrintObject* fiber_obj : fiber_print.objects()) {
                    fiber_obj->generate_fiber_paths(placement_config);
                }
                
                THEN("All objects should have fiber paths") {
                    for (FiberPrintObject* fiber_obj : fiber_print.objects()) {
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
}

TEST_CASE("FiberPrint - Multi-object: Different patterns", "[Integration][MultiObject]") {
    GIVEN("Two cube models with different patterns") {
        Print print;
        FiberPrint fiber_print;
        Model model;
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        
        TriangleMesh cube1 = Slic3r::make_cube(20, 20, 20);
        cube1.translate(Vec3f(-15.0f, 0.0f, 0.0f));
        TriangleMesh cube2 = Slic3r::make_cube(20, 20, 20);
        cube2.translate(Vec3f(15.0f, 0.0f, 0.0f));
        
        Test::init_print({cube1, cube2}, print, model, config);
        print.process();
        
        DynamicPrintConfig fiber_config = DynamicPrintConfig::full_print_config();
        fiber_config.set("enable_fiber_reinforcement", true);
        fiber_config.set("fiber_spacing", "5.0");
        
        std::vector<std::string> warnings;
        fiber_print.apply(model, fiber_config, &warnings);
        
        // Link objects
        for (size_t i = 0; i < fiber_print.objects().size() && i < print.objects().size(); ++i) {
            fiber_print.objects()[i]->set_fff_print_object(print.get_object(i));
            fiber_print.objects()[i]->create_fiber_layers_from_fff_layers();
        }
        
        WHEN("Different patterns are applied to each object") {
            FiberPlacementConfig placement_config1;
            placement_config1.pattern = FiberPattern::Grid;
            placement_config1.spacing = 5.0;
            fiber_print.objects()[0]->generate_fiber_paths(placement_config1);
            
            FiberPlacementConfig placement_config2;
            placement_config2.pattern = FiberPattern::Concentric;
            placement_config2.spacing = 5.0;
            fiber_print.objects()[1]->generate_fiber_paths(placement_config2);
            
            THEN("Each object should have its own pattern") {
                REQUIRE(!fiber_print.objects()[0]->fiber_layers().empty());
                REQUIRE(!fiber_print.objects()[1]->fiber_layers().empty());
                
                // Both should have paths, but patterns may differ
                size_t paths1 = 0, paths2 = 0;
                for (const FiberLayer& layer : fiber_print.objects()[0]->fiber_layers()) {
                    paths1 += layer.fiber_paths.size();
                }
                for (const FiberLayer& layer : fiber_print.objects()[1]->fiber_layers()) {
                    paths2 += layer.fiber_paths.size();
                }
                
                REQUIRE(paths1 > 0);
                REQUIRE(paths2 > 0);
            }
        }
    }
}

TEST_CASE("FiberPrint - Multi-object: G-code generation", "[Integration][MultiObject]") {
    GIVEN("Two cube models") {
        Print print;
        FiberPrint fiber_print;
        Model model;
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        
        TriangleMesh cube1 = Slic3r::make_cube(20, 20, 20);
        cube1.translate(Vec3f(-15.0f, 0.0f, 0.0f));
        TriangleMesh cube2 = Slic3r::make_cube(20, 20, 20);
        cube2.translate(Vec3f(15.0f, 0.0f, 0.0f));
        
        Test::init_print({cube1, cube2}, print, model, config);
        print.process();
        
        DynamicPrintConfig fiber_config = DynamicPrintConfig::full_print_config();
        fiber_config.set("enable_fiber_reinforcement", true);
        fiber_config.set("fiber_pattern", "grid");
        fiber_config.set("fiber_spacing", "5.0");
        
        std::vector<std::string> warnings;
        fiber_print.apply(model, fiber_config, &warnings);
        
        // Link and process objects
        for (size_t i = 0; i < fiber_print.objects().size() && i < print.objects().size(); ++i) {
            FiberPrintObject* fiber_obj = fiber_print.objects()[i];
            fiber_obj->set_fff_print_object(print.get_object(i));
            fiber_obj->create_fiber_layers_from_fff_layers();
            
            FiberPlacementConfig placement_config;
            placement_config.pattern = FiberPattern::Grid;
            placement_config.spacing = 5.0;
            fiber_obj->generate_fiber_paths(placement_config);
        }
        
        WHEN("G-code is generated for all objects") {
            FiberGCodeConfig gcode_config;
            gcode_config.method = FiberPrintMethod::DualPrinthead;
            gcode_config.fiber_extruder_id = 1;
            gcode_config.plastic_extruder_id = 0;
            
            std::string gcode1 = fiber_print.objects()[0]->generate_gcode(gcode_config);
            std::string gcode2 = fiber_print.objects()[1]->generate_gcode(gcode_config);
            
            THEN("Both objects should generate G-code") {
                REQUIRE(!gcode1.empty());
                REQUIRE(!gcode2.empty());
            }
        }
    }
}

TEST_CASE("FiberPrint - Multi-object: Object count validation", "[Integration][MultiObject]") {
    GIVEN("Multiple models") {
        Print print;
        FiberPrint fiber_print;
        Model model;
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        
        // Create three cubes
        TriangleMesh cube1 = Slic3r::make_cube(10, 10, 10);
        cube1.translate(Vec3f(-20.0f, 0.0f, 0.0f));
        TriangleMesh cube2 = Slic3r::make_cube(10, 10, 10);
        TriangleMesh cube3 = Slic3r::make_cube(10, 10, 10);
        cube3.translate(Vec3f(20.0f, 0.0f, 0.0f));
        
        Test::init_print({cube1, cube2, cube3}, print, model, config);
        print.process();
        
        DynamicPrintConfig fiber_config = DynamicPrintConfig::full_print_config();
        fiber_config.set("enable_fiber_reinforcement", true);
        
        std::vector<std::string> warnings;
        fiber_print.apply(model, fiber_config, &warnings);
        
        THEN("FiberPrint should have same number of objects as Print") {
            REQUIRE(fiber_print.objects().size() == print.objects().size());
            REQUIRE(fiber_print.objects().size() == 3);
        }
    }
}

