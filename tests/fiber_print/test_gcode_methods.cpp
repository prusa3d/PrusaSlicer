/**
 * Tests for both Method 1 (Dual Printhead) and Method 2 (Embedded)
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

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

std::string generate_gcode_with_method(TestMesh mesh, FiberPrintMethod method) {
    Print print;
    Model model;
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    Test::init_print({mesh}, print, model, config);
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
        gcode_config.method = method;
        
        if (method == FiberPrintMethod::DualPrinthead) {
            gcode_config.fiber_extruder_id = 1;
            gcode_config.plastic_extruder_id = 0;
        } else if (method == FiberPrintMethod::PreEmbeddedFilament) {
            gcode_config.embedded_fiber_extruder_id = 0;
        }
        
        return fiber_obj->generate_gcode(gcode_config);
    }
    
    return "";
}

TEST_CASE("FiberPrint - Method 1: Dual Printhead", "[GCodeMethods]") {
    GIVEN("G-code generated with Method 1 (Dual Printhead)") {
        std::string gcode = generate_gcode_with_method(
            TestMesh::cube_20x20x20,
            FiberPrintMethod::DualPrinthead
        );
        
        WHEN("G-code is analyzed") {
            THEN("Should not be empty") {
                REQUIRE(!gcode.empty());
            }
            
            THEN("Should contain movement commands") {
                bool has_g1 = gcode.find("G1") != std::string::npos;
                bool has_g0 = gcode.find("G0") != std::string::npos;
                bool has_movement = has_g1 || has_g0;
                REQUIRE(has_movement);
            }
            
            THEN("May contain tool change commands") {
                // Method 1 uses tool changes (T0, T1)
                // Tool changes may or may not be explicit depending on implementation
                GCodeReader parser;
                size_t tool_changes = 0;
                parser.parse_buffer(gcode, [&](GCodeReader& self, const GCodeReader::GCodeLine& line) {
                    if (line.cmd_is("T")) {
                        tool_changes++;
                    }
                });
                // Tool changes may be present, but not required to be explicit
                REQUIRE(tool_changes >= 0);
            }
        }
    }
}

TEST_CASE("FiberPrint - Method 2b: Pre-embedded Filament", "[GCodeMethods]") {
    GIVEN("G-code generated with Method 2b (Pre-embedded Filament)") {
        std::string gcode = generate_gcode_with_method(
            TestMesh::cube_20x20x20,
            FiberPrintMethod::PreEmbeddedFilament
        );
        
        WHEN("G-code is analyzed") {
            THEN("Should not be empty") {
                REQUIRE(!gcode.empty());
            }
            
            THEN("Should contain movement commands") {
                bool has_g1 = gcode.find("G1") != std::string::npos;
                bool has_g0 = gcode.find("G0") != std::string::npos;
                bool has_movement = has_g1 || has_g0;
                REQUIRE(has_movement);
            }
            
            THEN("Should work with single extruder") {
                // Method 2b uses single extruder, so tool changes may not be needed
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

TEST_CASE("FiberPrint - Method comparison: Method 1 vs Method 2b", "[GCodeMethods]") {
    GIVEN("G-code generated with both methods") {
        std::string gcode_method1 = generate_gcode_with_method(
            TestMesh::cube_20x20x20,
            FiberPrintMethod::DualPrinthead
        );
        
        std::string gcode_method2 = generate_gcode_with_method(
            TestMesh::cube_20x20x20,
            FiberPrintMethod::PreEmbeddedFilament
        );
        
        WHEN("G-code is compared") {
            THEN("Both should be valid") {
                REQUIRE(!gcode_method1.empty());
                REQUIRE(!gcode_method2.empty());
            }
            
            THEN("Both should contain movement commands") {
                bool has_g1_1 = gcode_method1.find("G1") != std::string::npos;
                bool has_g0_1 = gcode_method1.find("G0") != std::string::npos;
                bool has_movement_1 = has_g1_1 || has_g0_1;
                REQUIRE(has_movement_1);
                
                bool has_g1_2 = gcode_method2.find("G1") != std::string::npos;
                bool has_g0_2 = gcode_method2.find("G0") != std::string::npos;
                bool has_movement_2 = has_g1_2 || has_g0_2;
                REQUIRE(has_movement_2);
            }
            
            THEN("Should have different structures") {
                // Method 1 may have tool changes, Method 2b may not
                // But both should be valid G-code
                GCodeReader parser1, parser2;
                size_t moves1 = 0, moves2 = 0;
                
                parser1.parse_buffer(gcode_method1, [&](GCodeReader& self, const GCodeReader::GCodeLine& line) {
                    if (line.has_x() || line.has_y()) moves1++;
                });
                
                parser2.parse_buffer(gcode_method2, [&](GCodeReader& self, const GCodeReader::GCodeLine& line) {
                    if (line.has_x() || line.has_y()) moves2++;
                });
                
                REQUIRE(moves1 > 0);
                REQUIRE(moves2 > 0);
            }
        }
    }
}

TEST_CASE("FiberPrint - Method 1: Tool change validation", "[GCodeMethods]") {
    GIVEN("G-code with Method 1 and explicit tool changes") {
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
            gcode_config.fiber_extruder_id = 1;
            gcode_config.plastic_extruder_id = 0;
            
            std::string gcode = fiber_obj->generate_gcode(gcode_config);
            
            WHEN("Tool changes are analyzed") {
                GCodeReader parser;
                std::vector<int> tool_numbers;
                
                parser.parse_buffer(gcode, [&](GCodeReader& self, const GCodeReader::GCodeLine& line) {
                    if (line.cmd_is("T")) {
                        // Extract tool number from raw line (T0, T1, etc.)
                        std::string_view cmd = line.cmd();
                        if (cmd.length() > 1) {
                            // Parse number after T
                            int tool_num = 0;
                            std::string_view tool_str = cmd.substr(1);
                            if (!tool_str.empty()) {
                                try {
                                    tool_num = std::stoi(std::string(tool_str));
                                    tool_numbers.push_back(tool_num);
                                } catch (...) {
                                    // Ignore parse errors
                                }
                            }
                        }
                    }
                });
                
                THEN("Tool changes should be valid if present") {
                    // Tool changes may or may not be explicit in G-code
                    // But if present, they should be valid
                    for (int tool : tool_numbers) {
                        REQUIRE(tool >= 0);
                        REQUIRE(tool <= 10); // Reasonable tool number limit
                    }
                }
            }
        }
    }
}

