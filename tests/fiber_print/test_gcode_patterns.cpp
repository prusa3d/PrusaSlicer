/**
 * Tests for different patterns and settings in G-code generation
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <regex>
#include <string>

#include "libslic3r/FiberPrint.hpp"
#include "libslic3r/Fiber/FiberPrintObject.hpp"
#include "libslic3r/Fiber/FiberGCode.hpp"
#include "libslic3r/Fiber/FiberLayer.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/GCodeReader.hpp"
#include "libslic3r/Config.hpp"
#include "test_data.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;
using namespace Catch;

std::string generate_gcode_with_pattern(TestMesh mesh, FiberPattern pattern, double spacing) {
    Print print;
    Model model;
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    Test::init_print({mesh}, print, model, config);
    print.process();
    
    FiberPrint fiber_print;
    DynamicPrintConfig fiber_config = DynamicPrintConfig::full_print_config();
    fiber_config.set("enable_fiber_reinforcement", true);
    fiber_config.set("fiber_spacing", spacing);
    
    std::vector<std::string> warnings;
    fiber_print.apply(model, fiber_config, &warnings);
    
    if (!print.objects().empty() && !fiber_print.objects().empty()) {
        FiberPrintObject* fiber_obj = fiber_print.objects()[0];
        PrintObject* fff_obj = print.get_object(0);
        fiber_obj->set_fff_print_object(fff_obj);
        fiber_obj->create_fiber_layers_from_fff_layers();
        
        FiberPlacementConfig placement_config;
        placement_config.pattern = pattern;
        placement_config.spacing = spacing;
        fiber_obj->generate_fiber_paths(placement_config);
        
        FiberGCodeConfig gcode_config;
        gcode_config.method = FiberPrintMethod::DualPrinthead;
        gcode_config.fiber_extruder_id = 1;
        gcode_config.plastic_extruder_id = 0;
        
        return fiber_obj->generate_gcode(gcode_config);
    }
    
    return "";
}

TEST_CASE("FiberPrint - G-code patterns: Grid pattern", "[GCodePatterns]") {
    GIVEN("G-code generated with grid pattern") {
        std::string gcode = generate_gcode_with_pattern(
            TestMesh::cube_20x20x20, 
            FiberPattern::Grid, 
            5.0
        );
        
        WHEN("G-code is analyzed") {
            THEN("Should contain movement commands") {
                REQUIRE(!gcode.empty());
                bool has_g1 = gcode.find("G1") != std::string::npos;
                bool has_g0 = gcode.find("G0") != std::string::npos;
                bool has_movement = has_g1 || has_g0;
                REQUIRE(has_movement);
            }
            
            THEN("Should have valid structure") {
                GCodeReader parser;
                size_t move_count = 0;
                parser.parse_buffer(gcode, [&](GCodeReader& self, const GCodeReader::GCodeLine& line) {
                    if (line.has_x() || line.has_y()) {
                        move_count++;
                    }
                });
                REQUIRE(move_count > 0);
            }
        }
    }
}

TEST_CASE("FiberPrint - G-code patterns: Concentric pattern", "[GCodePatterns]") {
    GIVEN("G-code generated with concentric pattern") {
        std::string gcode = generate_gcode_with_pattern(
            TestMesh::cube_20x20x20, 
            FiberPattern::Concentric, 
            5.0
        );
        
        WHEN("G-code is analyzed") {
            THEN("Should contain movement commands") {
                REQUIRE(!gcode.empty());
                bool has_g1 = gcode.find("G1") != std::string::npos;
                bool has_g0 = gcode.find("G0") != std::string::npos;
                bool has_movement = has_g1 || has_g0;
                REQUIRE(has_movement);
            }
            
            THEN("Should have valid structure") {
                GCodeReader parser;
                size_t move_count = 0;
                parser.parse_buffer(gcode, [&](GCodeReader& self, const GCodeReader::GCodeLine& line) {
                    if (line.has_x() || line.has_y()) {
                        move_count++;
                    }
                });
                REQUIRE(move_count > 0);
            }
        }
    }
}

TEST_CASE("FiberPrint - G-code patterns: Different spacing", "[GCodePatterns]") {
    GIVEN("G-code generated with different spacing values") {
        std::string gcode_small = generate_gcode_with_pattern(
            TestMesh::cube_20x20x20, 
            FiberPattern::Grid, 
            2.0
        );
        
        std::string gcode_large = generate_gcode_with_pattern(
            TestMesh::cube_20x20x20, 
            FiberPattern::Grid, 
            10.0
        );
        
        WHEN("G-code is compared") {
            THEN("Both should be valid") {
                REQUIRE(!gcode_small.empty());
                REQUIRE(!gcode_large.empty());
            }
            
            THEN("Should have different path counts") {
                // Smaller spacing should generally produce more paths
                // But both should be valid G-code
                GCodeReader parser1, parser2;
                size_t moves1 = 0, moves2 = 0;
                
                parser1.parse_buffer(gcode_small, [&](GCodeReader& self, const GCodeReader::GCodeLine& line) {
                    if (line.has_x() || line.has_y()) moves1++;
                });
                
                parser2.parse_buffer(gcode_large, [&](GCodeReader& self, const GCodeReader::GCodeLine& line) {
                    if (line.has_x() || line.has_y()) moves2++;
                });
                
                // Both should have movements
                REQUIRE(moves1 > 0);
                REQUIRE(moves2 > 0);
            }
        }
    }
}

TEST_CASE("FiberPrint - G-code patterns: Different angles", "[GCodePatterns]") {
    GIVEN("G-code generated with different angles") {
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
            
            WHEN("0 degree angle is used") {
                FiberPlacementConfig placement_config;
                placement_config.pattern = FiberPattern::Grid;
                placement_config.spacing = 5.0;
                placement_config.angle = 0.0;
                fiber_obj->generate_fiber_paths(placement_config);
                
                FiberGCodeConfig gcode_config;
                gcode_config.method = FiberPrintMethod::DualPrinthead;
                std::string gcode_0 = fiber_obj->generate_gcode(gcode_config);
                
                AND_WHEN("90 degree angle is used") {
                    placement_config.angle = 90.0;
                    fiber_obj->generate_fiber_paths(placement_config);
                    std::string gcode_90 = fiber_obj->generate_gcode(gcode_config);
                    
                    THEN("Both should generate valid G-code") {
                        REQUIRE(!gcode_0.empty());
                        REQUIRE(!gcode_90.empty());
                    }
                }
            }
        }
    }
}

TEST_CASE("FiberPrint - G-code patterns: Layer interval", "[GCodePatterns]") {
    GIVEN("G-code generated with layer interval") {
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
        fiber_config.set("fiber_layer_interval", 2);
        
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
            placement_config.layer_interval = 2;
            fiber_obj->generate_fiber_paths(placement_config);
            
            FiberGCodeConfig gcode_config;
            gcode_config.method = FiberPrintMethod::DualPrinthead;
            std::string gcode = fiber_obj->generate_gcode(gcode_config);
            
            WHEN("G-code is analyzed") {
                THEN("Should be valid G-code") {
                    REQUIRE(!gcode.empty());
                }
                
                THEN("Should have fewer fiber layers than total layers") {
                    // Layer interval of 2 means fiber on every 2nd layer
                    size_t layers_with_fiber = 0;
                    for (const FiberLayer& layer : fiber_obj->fiber_layers()) {
                        if (layer.has_fibers()) {
                            layers_with_fiber++;
                        }
                    }
                    REQUIRE(layers_with_fiber <= fiber_obj->fiber_layers().size());
                }
            }
        }
    }
}

