/**
 * Unit tests for fiber G-code generation
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "libslic3r/Fiber/FiberGCode.hpp"
#include "libslic3r/Fiber/FiberLayer.hpp"
#include "libslic3r/GCode/GCodeWriter.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polyline.hpp"

using namespace Slic3r;
using namespace Catch;

TEST_CASE("FiberGCodeWriter - Fiber start/stop commands", "[GCodeGeneration]") {
    FiberGCodeWriter writer;
    FiberGCodeConfig config;
    
    SECTION("Default fiber start command") {
        config.fiber_start_command = "M106";
        std::string gcode = writer.generate_fiber_start(config);
        
        REQUIRE(!gcode.empty());
        REQUIRE(gcode.find("M106") != std::string::npos);
    }
    
    SECTION("Custom fiber start command") {
        config.fiber_start_command = "M800";
        std::string gcode = writer.generate_fiber_start(config);
        
        REQUIRE(gcode.find("M800") != std::string::npos);
    }
    
    SECTION("Default fiber stop command") {
        config.fiber_stop_command = "M107";
        std::string gcode = writer.generate_fiber_stop(config);
        
        REQUIRE(!gcode.empty());
        REQUIRE(gcode.find("M107") != std::string::npos);
    }
    
    SECTION("Fiber start with speed and pressure") {
        config.fiber_start_command = "M106";
        config.fiber_speed_command = "M108";
        std::string gcode = writer.generate_fiber_start(config, 50.0, 10.0);
        
        // Should include speed command if configured
        REQUIRE(!gcode.empty());
    }
}

TEST_CASE("FiberGCodeWriter - Tool change commands", "[GCodeGeneration]") {
    FiberGCodeWriter writer;
    FiberGCodeConfig config;
    GCodeWriter gcode_writer;
    
    SECTION("Tool change to fiber extruder") {
        config.fiber_extruder_id = 1;
        config.method = FiberPrintMethod::DualPrinthead;
        
        std::string gcode = writer.generate_toolchange_to_fiber(gcode_writer, config);
        
        REQUIRE(!gcode.empty());
        // Should contain tool change command (exact format depends on GCodeWriter)
    }
    
    SECTION("Tool change to plastic extruder") {
        config.plastic_extruder_id = 0;
        config.method = FiberPrintMethod::DualPrinthead;
        
        std::string gcode = writer.generate_toolchange_to_plastic(gcode_writer, config);
        
        REQUIRE(!gcode.empty());
    }
}

TEST_CASE("FiberGCodeWriter - Path G-code generation", "[GCodeGeneration]") {
    FiberGCodeWriter writer;
    FiberGCodeConfig config;
    GCodeWriter gcode_writer;
    
    SECTION("Simple path") {
        FiberPath path;
        path.polyline = Polyline{{0, 0}, {100, 0}, {100, 100}};
        path.z = 0.2;
        
        config.fiber_speed = 50.0;
        config.enable_comments = true;
        
        std::string gcode = writer.generate_path_gcode(path, gcode_writer, config);
        
        REQUIRE(!gcode.empty());
        // Should contain movement commands (G1 or G0)
        bool has_g1 = gcode.find("G1") != std::string::npos;
        bool has_g0 = gcode.find("G0") != std::string::npos;
        bool has_movement = has_g1 || has_g0;
        REQUIRE(has_movement);
    }
    
    SECTION("Path with comments") {
        FiberPath path;
        path.polyline = Polyline{{0, 0}, {100, 0}};
        path.z = 0.2;
        
        config.enable_comments = true;
        config.fiber_comment_format = ";FIBER_PATH:{path_id}";
        
        std::string gcode = writer.generate_path_gcode(path, gcode_writer, config);
        
        if (config.enable_comments) {
            REQUIRE(gcode.find(";") != std::string::npos);
        }
    }
}

TEST_CASE("FiberGCodeWriter - Layer G-code generation", "[GCodeGeneration]") {
    FiberGCodeWriter writer;
    FiberGCodeConfig config;
    GCodeWriter gcode_writer;
    
    SECTION("Empty layer") {
        FiberLayer layer;
        layer.id = 0;
        layer.print_z = 0.2;
        
        std::string gcode = writer.generate_layer_gcode(layer, gcode_writer, config);
        
        // Should still generate layer marker and start/stop commands
        REQUIRE(!gcode.empty());
    }
    
    SECTION("Layer with paths") {
        FiberLayer layer;
        layer.id = 1;
        layer.print_z = 0.2;
        
        FiberPath path1;
        path1.polyline = Polyline{{0, 0}, {100, 0}};
        path1.z = 0.2;
        layer.fiber_paths.push_back(path1);
        
        FiberPath path2;
        path2.polyline = Polyline{{0, 50}, {100, 50}};
        path2.z = 0.2;
        layer.fiber_paths.push_back(path2);
        
        config.enable_comments = true;
        config.layer_comment_format = ";LAYER:{layer_id}";
        
        std::string gcode = writer.generate_layer_gcode(layer, gcode_writer, config);
        
        REQUIRE(!gcode.empty());
        if (config.enable_comments) {
            REQUIRE(gcode.find(";LAYER:1") != std::string::npos);
        }
    }
}

TEST_CASE("FiberGCodeWriter - Delay generation", "[GCodeGeneration]") {
    FiberGCodeWriter writer;
    FiberGCodeConfig config;
    
    SECTION("Generate delay command") {
        std::string gcode = writer.generate_delay(2.5, config);
        
        REQUIRE(!gcode.empty());
        // Should contain G4 (dwell) command
        REQUIRE(gcode.find("G4") != std::string::npos);
        bool has_2_5 = gcode.find("2.5") != std::string::npos;
        bool has_2500 = gcode.find("2500") != std::string::npos;
        bool has_time = has_2_5 || has_2500;
        REQUIRE(has_time);
    }
}

TEST_CASE("FiberGCodeWriter - Method 1 (Dual Printhead)", "[GCodeGeneration]") {
    FiberGCodeWriter writer;
    FiberGCodeConfig config;
    config.method = FiberPrintMethod::DualPrinthead;
    config.fiber_extruder_id = 1;
    config.plastic_extruder_id = 0;
    
    GCodeWriter gcode_writer;
    
    SECTION("Tool change sequence") {
        // Test that tool changes are generated correctly
        std::string to_fiber = writer.generate_toolchange_to_fiber(gcode_writer, config);
        std::string to_plastic = writer.generate_toolchange_to_plastic(gcode_writer, config);
        
        REQUIRE(!to_fiber.empty());
        REQUIRE(!to_plastic.empty());
    }
}

TEST_CASE("FiberGCodeWriter - Method 2b (Pre-embedded Filament)", "[GCodeGeneration]") {
    FiberGCodeWriter writer;
    FiberGCodeConfig config;
    config.method = FiberPrintMethod::PreEmbeddedFilament;
    config.embedded_fiber_extruder_id = 0;
    
    GCodeWriter gcode_writer;
    
    SECTION("Embedded fiber extrusion") {
        Polyline extrusion_path{{0, 0}, {100, 0}};
        
        std::string gcode = writer.generate_embedded_fiber_extrusion(
            extrusion_path, true, gcode_writer, config
        );
        
        REQUIRE(!gcode.empty());
        // Should contain movement commands
        bool has_g1_cmd = gcode.find("G1") != std::string::npos;
        bool has_g0_cmd = gcode.find("G0") != std::string::npos;
        bool has_movement = has_g1_cmd || has_g0_cmd;
        REQUIRE(has_movement);
    }
}

