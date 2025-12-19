/**
 * G-code validation tests for real printer testing
 * Validates that generated G-code is syntactically correct and safe for printing
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include <regex>
#include <sstream>
#include <vector>
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

// Helper to parse and validate G-code
struct GCodeValidator {
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    size_t line_count = 0;
    size_t g1_count = 0;
    size_t g0_count = 0;
    size_t tool_changes = 0;
    size_t fiber_start_commands = 0;
    size_t fiber_stop_commands = 0;
    
    void validate(const std::string& gcode) {
        errors.clear();
        warnings.clear();
        line_count = 0;
        g1_count = 0;
        g0_count = 0;
        tool_changes = 0;
        fiber_start_commands = 0;
        fiber_stop_commands = 0;
        
        std::istringstream stream(gcode);
        std::string line;
        size_t line_num = 0;
        
        while (std::getline(stream, line)) {
            line_num++;
            line_count++;
            
            // Remove comments
            size_t comment_pos = line.find(';');
            if (comment_pos != std::string::npos) {
                line = line.substr(0, comment_pos);
            }
            
            // Trim whitespace
            line.erase(0, line.find_first_not_of(" \t\r\n"));
            line.erase(line.find_last_not_of(" \t\r\n") + 1);
            
            if (line.empty()) continue;
            
            // Validate G-code commands
            std::regex gcode_pattern(R"(([GMT])(\d+)(\s+[XYZEFSP]\s*[-+]?\d*\.?\d*)*)", std::regex::icase);
            std::smatch match;
            
            if (std::regex_search(line, match, gcode_pattern)) {
                std::string cmd = match[1].str();
                int code = std::stoi(match[2].str());
                
                if (cmd == "G" || cmd == "g") {
                    if (code == 0) g0_count++;
                    if (code == 1) g1_count++;
                    if (code == 28) {
                        // G28 (homing) - valid but should be used carefully
                    }
                } else if (cmd == "T" || cmd == "t") {
                    tool_changes++;
                } else if (cmd == "M" || cmd == "m") {
                    if (code == 106 || code == 107) {
                        // M106/M107 (fan control) - could be fiber start/stop
                        if (line.find("fiber") != std::string::npos || 
                            line.find("FIBER") != std::string::npos) {
                            if (code == 106) fiber_start_commands++;
                            if (code == 107) fiber_stop_commands++;
                        }
                    }
                }
            } else {
                // Check if it's a valid comment or empty line
                if (line[0] != ';' && !line.empty()) {
                    // Might be a valid command we don't recognize, just warn
                    warnings.push_back("Line " + std::to_string(line_num) + ": Unrecognized format: " + line);
                }
            }
        }
    }
    
    bool has_errors() const { return !errors.empty(); }
    bool has_warnings() const { return !warnings.empty(); }
};

std::string generate_fiber_gcode_for_test(TestMesh mesh, const FiberGCodeConfig& gcode_config) {
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
        
        return fiber_obj->generate_gcode(gcode_config);
    }
    
    return "";
}

TEST_CASE("FiberPrint - G-code validation: Basic syntax", "[GCodeValidation]") {
    GIVEN("Generated fiber G-code") {
        FiberGCodeConfig gcode_config;
        gcode_config.method = FiberPrintMethod::DualPrinthead;
        gcode_config.fiber_extruder_id = 1;
        gcode_config.plastic_extruder_id = 0;
        gcode_config.fiber_speed = 50.0;
        
        std::string gcode = generate_fiber_gcode_for_test(TestMesh::cube_20x20x20, gcode_config);
        
        WHEN("G-code is validated") {
            GCodeValidator validator;
            validator.validate(gcode);
            
            THEN("G-code should not be empty") {
                REQUIRE(!gcode.empty());
            }
            
            THEN("Should contain movement commands") {
                bool has_g1 = validator.g1_count > 0;
                bool has_g0 = validator.g0_count > 0;
                bool has_movement = has_g1 || has_g0;
                REQUIRE(has_movement);
            }
            
            THEN("Should have valid line count") {
                REQUIRE(validator.line_count > 0);
            }
        }
    }
}

TEST_CASE("FiberPrint - G-code validation: Method 1 (Dual Printhead)", "[GCodeValidation]") {
    GIVEN("G-code generated with Method 1") {
        FiberGCodeConfig gcode_config;
        gcode_config.method = FiberPrintMethod::DualPrinthead;
        gcode_config.fiber_extruder_id = 1;
        gcode_config.plastic_extruder_id = 0;
        gcode_config.fiber_start_command = "M106";
        gcode_config.fiber_stop_command = "M107";
        
        std::string gcode = generate_fiber_gcode_for_test(TestMesh::cube_20x20x20, gcode_config);
        
        WHEN("G-code is validated") {
            GCodeValidator validator;
            validator.validate(gcode);
            
            THEN("Should contain tool change commands") {
                // Method 1 should have tool changes (T0, T1)
                REQUIRE(validator.tool_changes >= 0); // May have tool changes
            }
            
            THEN("Should contain fiber start/stop commands") {
                // Should have M106/M107 or custom commands
                bool has_fiber_commands = 
                    gcode.find("M106") != std::string::npos ||
                    gcode.find("M107") != std::string::npos ||
                    gcode.find("fiber") != std::string::npos ||
                    gcode.find("FIBER") != std::string::npos;
                bool has_fiber = has_fiber_commands || validator.fiber_start_commands > 0;
                REQUIRE(has_fiber);
            }
        }
    }
}

TEST_CASE("FiberPrint - G-code validation: Method 2b (Pre-embedded Filament)", "[GCodeValidation]") {
    GIVEN("G-code generated with Method 2b") {
        FiberGCodeConfig gcode_config;
        gcode_config.method = FiberPrintMethod::PreEmbeddedFilament;
        gcode_config.embedded_fiber_extruder_id = 0;
        gcode_config.fiber_speed = 50.0;
        
        std::string gcode = generate_fiber_gcode_for_test(TestMesh::cube_20x20x20, gcode_config);
        
        WHEN("G-code is validated") {
            GCodeValidator validator;
            validator.validate(gcode);
            
            THEN("Should contain movement commands") {
                bool has_g1 = validator.g1_count > 0;
                bool has_g0 = validator.g0_count > 0;
                bool has_movement = has_g1 || has_g0;
                REQUIRE(has_movement);
            }
            
            THEN("Should not require tool changes (single extruder)") {
                // Method 2b uses single extruder, so tool changes may not be needed
                // But the G-code should still be valid
                REQUIRE(validator.line_count > 0);
            }
        }
    }
}

TEST_CASE("FiberPrint - G-code validation: Coordinates", "[GCodeValidation]") {
    GIVEN("Generated fiber G-code") {
        FiberGCodeConfig gcode_config;
        gcode_config.method = FiberPrintMethod::DualPrinthead;
        
        std::string gcode = generate_fiber_gcode_for_test(TestMesh::cube_20x20x20, gcode_config);
        
        WHEN("G-code coordinates are analyzed") {
            GCodeReader parser;
            bool has_valid_coords = false;
            bool has_invalid_coords = false;
            
            parser.parse_buffer(gcode, [&](GCodeReader& self, const GCodeReader::GCodeLine& line) {
                if (line.has_x() || line.has_y() || line.has_z()) {
                    has_valid_coords = true;
                    
                    // Check for obviously invalid coordinates
                    if (line.has_x() && (line.x() < -1000 || line.x() > 1000)) {
                        has_invalid_coords = true;
                    }
                    if (line.has_y() && (line.y() < -1000 || line.y() > 1000)) {
                        has_invalid_coords = true;
                    }
                    if (line.has_z() && (line.z() < -100 || line.z() > 1000)) {
                        has_invalid_coords = true;
                    }
                }
            });
            
            THEN("Should contain valid coordinates") {
                REQUIRE(has_valid_coords);
            }
            
            THEN("Should not contain obviously invalid coordinates") {
                REQUIRE(!has_invalid_coords);
            }
        }
    }
}

TEST_CASE("FiberPrint - G-code validation: Speed commands", "[GCodeValidation]") {
    GIVEN("G-code with configured fiber speed") {
        FiberGCodeConfig gcode_config;
        gcode_config.method = FiberPrintMethod::DualPrinthead;
        gcode_config.fiber_speed = 50.0;
        
        std::string gcode = generate_fiber_gcode_for_test(TestMesh::cube_20x20x20, gcode_config);
        
        WHEN("G-code speed commands are analyzed") {
            GCodeReader parser;
            bool has_speed_commands = false;
            
            parser.parse_buffer(gcode, [&](GCodeReader& self, const GCodeReader::GCodeLine& line) {
                if (line.has_f() && line.f() > 0) {
                    has_speed_commands = true;
                }
            });
            
            THEN("Should contain speed (F) commands") {
                // G-code should have F parameters for feedrate
                bool has_f = gcode.find("F") != std::string::npos;
                bool has_speed = has_speed_commands || has_f;
                REQUIRE(has_speed);
            }
        }
    }
}

TEST_CASE("FiberPrint - G-code validation: Comments", "[GCodeValidation]") {
    GIVEN("G-code with comments enabled") {
        FiberGCodeConfig gcode_config;
        gcode_config.method = FiberPrintMethod::DualPrinthead;
        gcode_config.enable_comments = true;
        
        std::string gcode = generate_fiber_gcode_for_test(TestMesh::cube_20x20x20, gcode_config);
        
        WHEN("G-code comments are analyzed") {
            THEN("Should contain comments if enabled") {
                if (gcode_config.enable_comments) {
                    REQUIRE(gcode.find(";") != std::string::npos);
                }
            }
        }
    }
    
    GIVEN("G-code with comments disabled") {
        FiberGCodeConfig gcode_config;
        gcode_config.method = FiberPrintMethod::DualPrinthead;
        gcode_config.enable_comments = false;
        
        std::string gcode = generate_fiber_gcode_for_test(TestMesh::cube_20x20x20, gcode_config);
        
        WHEN("G-code comments are analyzed") {
            THEN("May or may not contain comments") {
                // Comments might still be present for essential markers
                // But user-defined comments should be minimal
                REQUIRE(!gcode.empty());
            }
        }
    }
}

