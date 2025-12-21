/**
 * Integration tests for full fiber printing pipeline (STL → G-code)
 * Tests the complete workflow from model loading to G-code generation
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

// Helper function to create a FiberPrint from test meshes
std::unique_ptr<FiberPrint> create_fiber_print(
    std::initializer_list<TestMesh> meshes,
    std::initializer_list<ConfigBase::SetDeserializeItem> fiber_config_items = {}
) {
    // Create regular Print first (for FFF slicing)
    Print print;
    Model model;
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    
    // Initialize regular print
    Test::init_print(meshes, print, model, config);
    print.process();
    
    // Create FiberPrint
    auto fiber_print = std::make_unique<FiberPrint>();
    
    // Apply model to fiber print
    DynamicPrintConfig fiber_config = DynamicPrintConfig::full_print_config();
    for (const auto& item : fiber_config_items) {
        fiber_config.set_deserialize_strict({item});
    }
    
    // Enable fiber reinforcement
    fiber_config.set("enable_fiber_reinforcement", true);
    
    std::vector<std::string> warnings;
    fiber_print->apply(model, fiber_config, &warnings);
    
    // Link FiberPrintObject to PrintObject
    if (!print.objects().empty() && !fiber_print->objects().empty()) {
        fiber_print->objects()[0]->set_fff_print_object(print.get_object(0));
    }
    
    return fiber_print;
}

TEST_CASE("FiberPrint - Full pipeline: Simple cube", "[Integration]") {
    GIVEN("A 20mm cube and fiber print enabled") {
        auto fiber_print = create_fiber_print(
            {TestMesh::cube_20x20x20},
            {{"fiber_pattern", "grid"}, {"fiber_spacing", "5.0"}}
        );
        
        WHEN("FiberPrint is processed") {
            fiber_print->process();
            
            THEN("FiberPrint should have objects") {
                REQUIRE(!fiber_print->empty());
                REQUIRE(fiber_print->objects().size() == 1);
            }
            
            THEN("FiberPrintObject should be created") {
                FiberPrintObject* fiber_obj = fiber_print->objects()[0];
                REQUIRE(fiber_obj != nullptr);
            }
        }
    }
}

TEST_CASE("FiberPrint - Full pipeline: Create fiber layers from FFF layers", "[Integration]") {
    GIVEN("A processed print and fiber print") {
        Print print;
        Model model;
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        Test::init_print({TestMesh::cube_20x20x20}, print, model, config);
        print.process();
        
        auto fiber_print = std::make_unique<FiberPrint>();
        DynamicPrintConfig fiber_config = DynamicPrintConfig::full_print_config();
        fiber_config.set("enable_fiber_reinforcement", true);
        
        std::vector<std::string> warnings;
        fiber_print->apply(model, fiber_config, &warnings);
        
        WHEN("FFF PrintObject is linked to FiberPrintObject") {
            if (!print.objects().empty() && !fiber_print->objects().empty()) {
                FiberPrintObject* fiber_obj = fiber_print->objects()[0];
                PrintObject* fff_obj = print.get_object(0);
                
                fiber_obj->set_fff_print_object(fff_obj);
                
                AND_WHEN("Fiber layers are created from FFF layers") {
                    fiber_obj->create_fiber_layers_from_fff_layers();
                    
                    THEN("Fiber layers should be created") {
                        REQUIRE(!fiber_obj->fiber_layers().empty());
                    }
                    
                    THEN("Number of fiber layers should match FFF layers") {
                        size_t fff_layer_count = fff_obj->layers().size();
                        size_t fiber_layer_count = fiber_obj->fiber_layers().size();
                        REQUIRE(fiber_layer_count == fff_layer_count);
                    }
                    
                    THEN("Fiber layer Z heights should match FFF layers") {
                        const auto& fff_layers = fff_obj->layers();
                        const auto& fiber_layers = fiber_obj->fiber_layers();
                        
                        REQUIRE(fff_layers.size() == fiber_layers.size());
                        for (size_t i = 0; i < fff_layers.size(); ++i) {
                            REQUIRE(fiber_layers[i].print_z == Approx(fff_layers[i]->print_z).margin(0.01));
                        }
                    }
                }
            }
        }
    }
}

TEST_CASE("FiberPrint - Full pipeline: Generate fiber paths", "[Integration]") {
    GIVEN("A fiber print with layers created") {
        Print print;
        Model model;
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        Test::init_print({TestMesh::cube_20x20x20}, print, model, config);
        print.process();
        
        auto fiber_print = std::make_unique<FiberPrint>();
        DynamicPrintConfig fiber_config = DynamicPrintConfig::full_print_config();
        fiber_config.set("enable_fiber_reinforcement", true);
        fiber_config.set("fiber_pattern", "grid");
        fiber_config.set("fiber_spacing", "5.0");
        fiber_config.set("fiber_angle", "0.0");
        
        std::vector<std::string> warnings;
        fiber_print->apply(model, fiber_config, &warnings);
        
        if (!print.objects().empty() && !fiber_print->objects().empty()) {
            FiberPrintObject* fiber_obj = fiber_print->objects()[0];
            PrintObject* fff_obj = print.get_object(0);
            fiber_obj->set_fff_print_object(fff_obj);
            fiber_obj->create_fiber_layers_from_fff_layers();
            
            WHEN("Fiber paths are generated") {
                FiberPlacementConfig placement_config;
                placement_config.pattern = FiberPattern::Grid;
                placement_config.spacing = 5.0;
                placement_config.angle = 0.0;
                placement_config.zone = FiberPlacementZone::Both;
                
                fiber_obj->generate_fiber_paths(placement_config);
                
                THEN("At least some layers should have fiber paths") {
                    size_t layers_with_fiber = 0;
                    for (const FiberLayer& layer : fiber_obj->fiber_layers()) {
                        if (layer.has_fibers()) {
                            layers_with_fiber++;
                        }
                    }
                    
                    // Should have fiber on at least some layers
                    REQUIRE(layers_with_fiber > 0);
                }
                
                THEN("Fiber paths should have valid geometry") {
                    for (const FiberLayer& layer : fiber_obj->fiber_layers()) {
                        if (layer.has_fibers()) {
                            for (const FiberPath& path : layer.fiber_paths) {
                                REQUIRE(!path.polyline.empty());
                                REQUIRE(path.polyline.size() >= 2);
                            }
                        }
                    }
                }
            }
        }
    }
}

TEST_CASE("FiberPrint - Full pipeline: Generate G-code", "[Integration]") {
    GIVEN("A fiber print with paths generated") {
        Print print;
        Model model;
        DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
        Test::init_print({TestMesh::cube_20x20x20}, print, model, config);
        print.process();
        
        auto fiber_print = std::make_unique<FiberPrint>();
        DynamicPrintConfig fiber_config = DynamicPrintConfig::full_print_config();
        fiber_config.set("enable_fiber_reinforcement", true);
        fiber_config.set("fiber_pattern", "grid");
        fiber_config.set("fiber_spacing", "5.0");
        
        std::vector<std::string> warnings;
        fiber_print->apply(model, fiber_config, &warnings);
        
        if (!print.objects().empty() && !fiber_print->objects().empty()) {
            FiberPrintObject* fiber_obj = fiber_print->objects()[0];
            PrintObject* fff_obj = print.get_object(0);
            fiber_obj->set_fff_print_object(fff_obj);
            fiber_obj->create_fiber_layers_from_fff_layers();
            
            FiberPlacementConfig placement_config;
            placement_config.pattern = FiberPattern::Grid;
            placement_config.spacing = 5.0;
            fiber_obj->generate_fiber_paths(placement_config);
            
            WHEN("G-code is generated") {
                FiberGCodeConfig gcode_config;
                gcode_config.method = FiberPrintMethod::DualPrinthead;
                gcode_config.fiber_extruder_id = 1;
                gcode_config.plastic_extruder_id = 0;
                gcode_config.fiber_speed = 50.0;
                
                std::string gcode = fiber_obj->generate_gcode(gcode_config);
                
                THEN("G-code should not be empty") {
                    REQUIRE(!gcode.empty());
                }
                
                THEN("G-code should contain movement commands") {
                    bool has_g1 = gcode.find("G1") != std::string::npos;
                    bool has_g0 = gcode.find("G0") != std::string::npos;
                    bool has_movement = has_g1 || has_g0;
                    REQUIRE(has_movement);
                }
            }
        }
    }
}

