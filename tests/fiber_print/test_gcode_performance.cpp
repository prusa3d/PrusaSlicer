/**
 * Performance and benchmark tests for large models
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include <chrono>
#include <string>

#include "libslic3r/FiberPrint.hpp"
#include "libslic3r/Fiber/FiberPrintObject.hpp"
#include "libslic3r/Fiber/FiberGCode.hpp"
#include "libslic3r/Fiber/FiberLayer.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Config.hpp"
#include "test_data.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;
using namespace Catch;

TEST_CASE("FiberPrint - Performance: Large model processing", "[Performance]") {
    GIVEN("A large model") {
        // Create a larger cube
        TriangleMesh large_cube = Slic3r::make_cube(100, 100, 100);
        
        Print print;
        Model model;
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        config.set("layer_height", 0.3); // Larger layer height for faster processing
        
        BENCHMARK("Process large model with fiber") {
            Test::init_print({large_cube}, print, model, config);
            print.process();
            
            FiberPrint fiber_print;
            DynamicPrintConfig fiber_config = DynamicPrintConfig::full_print_config();
            fiber_config.set("enable_fiber_reinforcement", true);
            fiber_config.set("fiber_pattern", "grid");
            fiber_config.set("fiber_spacing", "10.0"); // Larger spacing for performance
            
            std::vector<std::string> warnings;
            fiber_print.apply(model, fiber_config, &warnings);
            
            if (!print.objects().empty() && !fiber_print.objects().empty()) {
                FiberPrintObject* fiber_obj = fiber_print.objects()[0];
                PrintObject* fff_obj = print.get_object(0);
                fiber_obj->set_fff_print_object(fff_obj);
                fiber_obj->create_fiber_layers_from_fff_layers();
                
                FiberPlacementConfig placement_config;
                placement_config.pattern = FiberPattern::Grid;
                placement_config.spacing = 10.0;
                fiber_obj->generate_fiber_paths(placement_config);
            }
        };
    }
}

TEST_CASE("FiberPrint - Performance: G-code generation", "[Performance]") {
    GIVEN("A processed fiber print") {
        Print print;
        Model model;
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        Test::init_print({TestMesh::cube_20x20x20}, print, model, config);
        print.process();
        
        FiberPrint fiber_print;
        DynamicPrintConfig fiber_config = DynamicPrintConfig::full_print_config();
        fiber_config.set("enable_fiber_reinforcement", true);
        fiber_config.set("fiber_pattern", "grid");
        fiber_config.set("fiber_spacing", "5.0");
        
        std::vector<std::string> warnings;
        fiber_print.apply(model, fiber_config, &warnings);
        
        if (!print.objects().empty() && !fiber_print.objects().empty()) {
            FiberPrintObject* fiber_obj = fiber_print.objects()[0];
            PrintObject* fff_obj = print.get_object(0);
            fiber_obj->set_fff_print_object(fff_obj);
            fiber_obj->create_fiber_layers_from_fff_layers();
            
            FiberPlacementConfig placement_config;
            placement_config.pattern = FiberPattern::Grid;
            placement_config.spacing = 5.0;
            fiber_obj->generate_fiber_paths(placement_config);
            
            FiberGCodeConfig gcode_config;
            gcode_config.method = FiberPrintMethod::DualPrinthead;
            
            BENCHMARK("Generate G-code") {
                return fiber_obj->generate_gcode(gcode_config);
            };
        }
    }
}

TEST_CASE("FiberPrint - Performance: Path planning", "[Performance]") {
    GIVEN("A fiber print with many paths") {
        Print print;
        Model model;
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        Test::init_print({TestMesh::cube_20x20x20}, print, model, config);
        print.process();
        
        FiberPrint fiber_print;
        DynamicPrintConfig fiber_config = DynamicPrintConfig::full_print_config();
        fiber_config.set("enable_fiber_reinforcement", true);
        fiber_config.set("fiber_pattern", "grid");
        fiber_config.set("fiber_spacing", "2.0"); // Smaller spacing = more paths
        
        std::vector<std::string> warnings;
        fiber_print.apply(model, fiber_config, &warnings);
        
        if (!print.objects().empty() && !fiber_print.objects().empty()) {
            FiberPrintObject* fiber_obj = fiber_print.objects()[0];
            PrintObject* fff_obj = print.get_object(0);
            fiber_obj->set_fff_print_object(fff_obj);
            fiber_obj->create_fiber_layers_from_fff_layers();
            
            BENCHMARK("Generate fiber paths with small spacing") {
                FiberPlacementConfig placement_config;
                placement_config.pattern = FiberPattern::Grid;
                placement_config.spacing = 2.0;
                fiber_obj->generate_fiber_paths(placement_config);
            };
        }
    }
}

TEST_CASE("FiberPrint - Performance: Multiple objects", "[Performance]") {
    GIVEN("Multiple objects") {
        TriangleMesh cube1 = Slic3r::make_cube(20, 20, 20);
        cube1.translate(Vec3f(-15.0f, 0.0f, 0.0f));
        TriangleMesh cube2 = Slic3r::make_cube(20, 20, 20);
        cube2.translate(Vec3f(15.0f, 0.0f, 0.0f));
        TriangleMesh cube3 = Slic3r::make_cube(20, 20, 20);
        cube3.translate(Vec3f(0.0f, 15.0f, 0.0f));
        
        Print print;
        Model model;
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        
        BENCHMARK("Process multiple objects with fiber") {
            Test::init_print({cube1, cube2, cube3}, print, model, config);
            print.process();
            
            FiberPrint fiber_print;
            DynamicPrintConfig fiber_config = DynamicPrintConfig::full_print_config();
            fiber_config.set("enable_fiber_reinforcement", true);
            fiber_config.set("fiber_pattern", "grid");
            fiber_config.set("fiber_spacing", "5.0");
            
            std::vector<std::string> warnings;
            fiber_print.apply(model, fiber_config, &warnings);
            
            for (size_t i = 0; i < fiber_print.objects().size() && i < print.objects().size(); ++i) {
                FiberPrintObject* fiber_obj = fiber_print.objects()[i];
                PrintObject* fff_obj = print.get_object(i);
                fiber_obj->set_fff_print_object(fff_obj);
                fiber_obj->create_fiber_layers_from_fff_layers();
                
                FiberPlacementConfig placement_config;
                placement_config.pattern = FiberPattern::Grid;
                placement_config.spacing = 5.0;
                fiber_obj->generate_fiber_paths(placement_config);
            }
        };
    }
}

TEST_CASE("FiberPrint - Performance: Memory usage", "[Performance]") {
    GIVEN("A model with many layers") {
        Print print;
        Model model;
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        config.set("layer_height", 0.1); // Small layer height = many layers
        
        TriangleMesh tall_cube = Slic3r::make_cube(20, 20, 50); // Tall model
        
        Test::init_print({tall_cube}, print, model, config);
        print.process();
        
        FiberPrint fiber_print;
        DynamicPrintConfig fiber_config = DynamicPrintConfig::full_print_config();
        fiber_config.set("enable_fiber_reinforcement", true);
        fiber_config.set("fiber_pattern", "grid");
        fiber_config.set("fiber_spacing", "5.0");
        
        std::vector<std::string> warnings;
        fiber_print.apply(model, fiber_config, &warnings);
        
        if (!print.objects().empty() && !fiber_print.objects().empty()) {
            FiberPrintObject* fiber_obj = fiber_print.objects()[0];
            PrintObject* fff_obj = print.get_object(0);
            fiber_obj->set_fff_print_object(fff_obj);
            fiber_obj->create_fiber_layers_from_fff_layers();
            
            WHEN("Fiber paths are generated") {
                FiberPlacementConfig placement_config;
                placement_config.pattern = FiberPattern::Grid;
                placement_config.spacing = 5.0;
                fiber_obj->generate_fiber_paths(placement_config);
                
                THEN("Should handle many layers efficiently") {
                    REQUIRE(fiber_obj->fiber_layers().size() == fff_obj->layers().size());
                    REQUIRE(fiber_obj->fiber_layers().size() > 100); // Many layers
                }
            }
        }
    }
}

