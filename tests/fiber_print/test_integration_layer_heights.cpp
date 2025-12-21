/**
 * Integration tests for different layer heights
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

void test_layer_height(double layer_height, const char* test_name) {
    Print print;
    FiberPrint fiber_print;
    Model model;
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set("layer_height", layer_height);
    config.set("first_layer_height", layer_height);
    
    Test::init_print({TestMesh::cube_20x20x20}, print, model, config);
    print.process();
    
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
        
        // Verify layer count matches
        REQUIRE(fiber_obj->fiber_layers().size() == fff_obj->layers().size());
        
        // Verify Z heights match
        const auto& fff_layers = fff_obj->layers();
        const auto& fiber_layers = fiber_obj->fiber_layers();
        for (size_t i = 0; i < fff_layers.size(); ++i) {
            REQUIRE(fiber_layers[i].print_z == Approx(fff_layers[i]->print_z).margin(0.01));
        }
    }
}

TEST_CASE("FiberPrint - Layer heights: 0.1mm", "[Integration][LayerHeights]") {
    test_layer_height(0.1, "0.1mm layer height");
}

TEST_CASE("FiberPrint - Layer heights: 0.2mm", "[Integration][LayerHeights]") {
    test_layer_height(0.2, "0.2mm layer height");
}

TEST_CASE("FiberPrint - Layer heights: 0.3mm", "[Integration][LayerHeights]") {
    test_layer_height(0.3, "0.3mm layer height");
}

TEST_CASE("FiberPrint - Layer heights: 0.4mm", "[Integration][LayerHeights]") {
    test_layer_height(0.4, "0.4mm layer height");
}

TEST_CASE("FiberPrint - Layer heights: Different first layer", "[Integration][LayerHeights]") {
    GIVEN("A print with different first layer height") {
        Print print;
        FiberPrint fiber_print;
        Model model;
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        config.set("layer_height", 0.2);
        config.set("first_layer_height", 0.3); // Different first layer
        
        Test::init_print({TestMesh::cube_20x20x20}, print, model, config);
        print.process();
        
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
                
                THEN("First layer Z should match FFF first layer") {
                    if (!fiber_obj->fiber_layers().empty() && !fff_obj->layers().empty()) {
                        REQUIRE(fiber_obj->fiber_layers()[0].print_z == 
                               Approx(fff_obj->layers()[0]->print_z).margin(0.01));
                    }
                }
            }
        }
    }
}

TEST_CASE("FiberPrint - Layer heights: Layer interval", "[Integration][LayerHeights]") {
    GIVEN("A print with fiber layer interval") {
        Print print;
        FiberPrint fiber_print;
        Model model;
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        config.set("layer_height", 0.2);
        
        Test::init_print({TestMesh::cube_20x20x20}, print, model, config);
        print.process();
        
        DynamicPrintConfig fiber_config = DynamicPrintConfig::full_print_config();
        fiber_config.set("enable_fiber_reinforcement", true);
        fiber_config.set("fiber_pattern", "grid");
        fiber_config.set("fiber_spacing", "5.0");
        fiber_config.set("fiber_layer_interval", 2); // Every 2nd layer
        
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
                placement_config.layer_interval = 2;
                fiber_obj->generate_fiber_paths(placement_config);
                
                THEN("Fiber should only be on interval layers") {
                    size_t layers_with_fiber = 0;
                    for (size_t i = 0; i < fiber_obj->fiber_layers().size(); ++i) {
                        const FiberLayer& layer = fiber_obj->fiber_layers()[i];
                        if (layer.has_fibers()) {
                            layers_with_fiber++;
                            // Layer index should be divisible by interval
                            REQUIRE(i % 2 == 0);
                        }
                    }
                    REQUIRE(layers_with_fiber > 0);
                }
            }
        }
    }
}

