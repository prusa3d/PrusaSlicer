#include <catch2/catch_test_macros.hpp>

#include <numeric>
#include <sstream>

#include "test_data.hpp" // get access to init_print, etc

#include "libslic3r/Config.hpp"
#include "libslic3r/GCode.hpp"
#include "libslic3r/GCodeReader.hpp"
#include "libslic3r/GCode/CoolingBuffer.hpp"
#include "libslic3r/libslic3r.h"

using namespace Slic3r;

std::unique_ptr<CoolingBuffer> make_cooling_buffer(
    GCodeGenerator                  &gcode,
    const DynamicPrintConfig        &config         = DynamicPrintConfig{}, 
    const std::vector<unsigned int> &extruder_ids   = { 0 })
{
    PrintConfig print_config;
    print_config.apply(config, true); // ignore_nonexistent
    gcode.apply_print_config(print_config);
    gcode.set_layer_count(10);
    gcode.writer().set_extruders(extruder_ids);
    gcode.writer().set_extruder(0);
    return std::make_unique<CoolingBuffer>(gcode);
}

SCENARIO("Cooling unit tests", "[Cooling]") {
    const std::string   gcode1        = "G1 X100 E1 F3000\n";
    // 2 sec
    const double        print_time1   = 100. / (3000. / 60.);
    const std::string   gcode2        = gcode1 + "G1 X0 E1 F3000\n";
    // 4 sec
    const double        print_time2   = 2. * print_time1;

    auto config = DynamicPrintConfig::full_print_config_with({
        // Default cooling settings.
        { "bridge_fan_speed",            "100" },
        { "cooling",                     "1" },
        { "fan_always_on",               "0" },
        { "fan_below_layer_time",        "60" },
        { "max_fan_speed",               "100" },
        { "min_print_speed",             "10" },
        { "slowdown_below_layer_time",   "5" },
        // Default print speeds.
        { "bridge_speed",                60 },
        { "external_perimeter_speed",    "50%" },
        { "first_layer_speed",           30 },
        { "gap_fill_speed",              20 },
        { "infill_speed",                80 },
        { "perimeter_speed",             60 },
        { "small_perimeter_speed",       15 },
        { "solid_infill_speed",          20 },
        { "top_solid_infill_speed",      15 },
        { "max_print_speed",             80 },
        // Override for tests.
        { "disable_fan_first_layers",    "0" }
    });

    WHEN("G-code block 3") {
        THEN("speed is not altered when elapsed time is greater than slowdown threshold") {
            // Print time of gcode.
            const double print_time = 100. / (3000. / 60.);
            //FIXME slowdown_below_layer_time is rounded down significantly from 1.8s to 1s.
            config.set_deserialize_strict({ { "slowdown_below_layer_time", { int(print_time * 0.999) } } });
            GCodeGenerator gcodegen;
            auto buffer = make_cooling_buffer(gcodegen, config);
            std::string gcode = buffer->process_layer("G1 F3000;_EXTRUDE_SET_SPEED\nG1 X100 E1", 0, true);
            bool speed_not_altered = gcode.find("F3000") != gcode.npos;
            REQUIRE(speed_not_altered);
        }
    }

    WHEN("G-code block 4") {
        const std::string gcode_src = 
            "G1 X50 F2500\n"
            "G1 F3000;_EXTRUDE_SET_SPEED\n"
            "G1 X100 E1\n"
            ";_EXTRUDE_END\n"
            "G1 E4 F400";
        // Print time of gcode.
        const double print_time = 50. / (2500. / 60.) + 100. / (3000. / 60.) + 4. / (400. / 60.);
        config.set_deserialize_strict({ { "slowdown_below_layer_time", { int(print_time * 1.001) } } });
        GCodeGenerator gcodegen;
        auto buffer = make_cooling_buffer(gcodegen, config);
        std::string gcode = buffer->process_layer(gcode_src, 0, true);
        THEN("speed is altered when elapsed time is lower than slowdown threshold") {
            bool speed_is_altered = gcode.find("F3000") == gcode.npos;
            REQUIRE(speed_is_altered);
        }
        THEN("speed is not altered for travel moves") {
            bool speed_not_altered = gcode.find("F2500") != gcode.npos;
            REQUIRE(speed_not_altered);
        }
        THEN("speed is not altered for extruder-only moves") {
            bool speed_not_altered = gcode.find("F400") != gcode.npos;
            REQUIRE(speed_not_altered);   
        }
    }

    WHEN("G-code block 1") {
        THEN("fan is not activated when elapsed time is greater than fan threshold") {
            config.set_deserialize_strict({
                { "fan_below_layer_time"      , int(print_time1 * 0.88) },
                { "slowdown_below_layer_time" , int(print_time1 * 0.99) }
            });
            GCodeGenerator gcodegen;
            auto buffer = make_cooling_buffer(gcodegen, config);
            std::string gcode = buffer->process_layer(gcode1, 0, true);
            bool fan_not_activated = gcode.find("M106") == gcode.npos;
            REQUIRE(fan_not_activated);      
        }
    }
    WHEN("G-code block 1 with two extruders") {
        config.set_deserialize_strict({
            { "cooling",                   "1, 0" },
            { "fan_below_layer_time",      { int(print_time2 + 1.), int(print_time2 + 1.) } },
            { "slowdown_below_layer_time", { int(print_time2 + 2.), int(print_time2 + 2.) } }
        });
        GCodeGenerator gcodegen;
        auto buffer = make_cooling_buffer(gcodegen, config, { 0, 1 });
        std::string gcode = buffer->process_layer(gcode1 + "T1\nG1 X0 E1 F3000\n", 0, true);
        THEN("fan is activated for the 1st tool") {
            bool ok = gcode.find("M106") == 0;
            REQUIRE(ok);      
        }
        THEN("fan is disabled for the 2nd tool") {
            bool ok = gcode.find("\nM107") > 0;
            REQUIRE(ok);      
        }
    }
    WHEN("G-code block 2") {
        THEN("slowdown is computed on all objects printing at the same Z") {
            config.set_deserialize_strict({ { "slowdown_below_layer_time", int(print_time2 * 0.99) } });
            GCodeGenerator gcodegen;
            auto buffer = make_cooling_buffer(gcodegen, config);
            std::string gcode = buffer->process_layer(gcode2, 0, true);
            bool ok = gcode.find("F3000") != gcode.npos;
            REQUIRE(ok);      
        }
        THEN("fan is not activated on all objects printing at different Z") {
            config.set_deserialize_strict({ 
                { "fan_below_layer_time",      int(print_time2 * 0.65) },
                { "slowdown_below_layer_time", int(print_time2 * 0.7) }
            });
            GCodeGenerator gcodegen;
            auto buffer = make_cooling_buffer(gcodegen, config);
            // use an elapsed time which is < the threshold but greater than it when summed twice
            std::string gcode = buffer->process_layer(gcode2, 0, true) + buffer->process_layer(gcode2, 1, true);
            bool fan_not_activated = gcode.find("M106") == gcode.npos;
            REQUIRE(fan_not_activated);      
        }
        THEN("fan is activated on all objects printing at different Z") {
            // use an elapsed time which is < the threshold even when summed twice
            config.set_deserialize_strict({ 
                { "fan_below_layer_time",      int(print_time2 + 1) },
                { "slowdown_below_layer_time", int(print_time2 + 1) }
            });
            GCodeGenerator gcodegen;
            auto buffer = make_cooling_buffer(gcodegen, config);
            // use an elapsed time which is < the threshold but greater than it when summed twice
            std::string gcode = buffer->process_layer(gcode2, 0, true) + buffer->process_layer(gcode2, 1, true);
            bool fan_activated = gcode.find("M106") != gcode.npos;
            REQUIRE(fan_activated);      
        }
    }
}

SCENARIO("Cooling integration tests", "[Cooling]") {
    GIVEN("overhang") {
        auto config = Slic3r::DynamicPrintConfig::full_print_config_with({
            { "cooling",                    { 1 } },
            { "bridge_fan_speed",           { 100 } },
            { "fan_below_layer_time",       { 0 } },
            { "slowdown_below_layer_time",  { 0 } },
            { "bridge_speed",               99 },
            { "enable_dynamic_overhang_speeds", false },
            // internal bridges use solid_infil speed
            { "bottom_solid_layers",        1 },
            // internal bridges use solid_infil speed
        });
    
        GCodeReader parser;
        int fan = 0;
        int fan_with_incorrect_speeds = 0;
        int fan_with_incorrect_print_speeds = 0;
        int bridge_with_no_fan = 0;
        const double bridge_speed = config.opt_float("bridge_speed") * 60;
        parser.parse_buffer(
            Slic3r::Test::slice({ Slic3r::Test::TestMesh::overhang }, config),
            [&fan, &fan_with_incorrect_speeds, &fan_with_incorrect_print_speeds, &bridge_with_no_fan, bridge_speed]
                (Slic3r::GCodeReader &self, const Slic3r::GCodeReader::GCodeLine &line)
        {
            if (line.cmd_is("M106")) {
                line.has_value('S', fan);
                if (fan != 255)
                    ++ fan_with_incorrect_speeds;
            } else if (line.cmd_is("M107")) {
                fan = 0;
            } else if (line.extruding(self) && line.dist_XY(self) > 0) {
                if (is_approx<double>(line.new_F(self), bridge_speed)) {
                    if (fan != 255)
                        ++ bridge_with_no_fan;
                } else {
                    if (fan != 0)
                        ++ fan_with_incorrect_print_speeds;
                }
            }
        });
        THEN("bridge fan speed is applied correctly") {
            REQUIRE(fan_with_incorrect_speeds == 0);
        }
        THEN("bridge fan is only turned on for bridges") {
            REQUIRE(fan_with_incorrect_print_speeds == 0);
        }
        THEN("bridge fan is turned on for all bridges") {
            REQUIRE(bridge_with_no_fan == 0);
        }
    }
    GIVEN("20mm cube") {
        auto config = Slic3r::DynamicPrintConfig::full_print_config_with({
            { "cooling",                    { 1 } },
            { "fan_below_layer_time",       { 0 } },
            { "slowdown_below_layer_time",  { 10 } },
            { "min_print_speed",            { 0 } },
            { "start_gcode",                "" },
            { "first_layer_speed",          "100%" },
            { "external_perimeter_speed",   99 }
        });
        GCodeReader parser;
        const double external_perimeter_speed = config.opt<ConfigOptionFloatOrPercent>("external_perimeter_speed")->value * 60;
        std::vector<double> layer_times;
        // z => 1
        std::map<coord_t, int> layer_external;
        parser.parse_buffer(
            Slic3r::Test::slice({ Slic3r::Test::TestMesh::cube_20x20x20 }, config),
            [&layer_times, &layer_external, external_perimeter_speed]
                (Slic3r::GCodeReader &self, const Slic3r::GCodeReader::GCodeLine &line)
        {
            if (line.cmd_is("G1")) {
                if (line.dist_Z(self) != 0) {
                    layer_times.emplace_back(0.);
                    layer_external[scaled<coord_t>(line.new_Z(self))] = 0;
                }
                double l = line.dist_XY(self);
                if (l == 0)
                    l = line.dist_E(self);
                if (l == 0)
                    l = line.dist_Z(self);
                if (l > 0.) {
                    if (!layer_times.empty()) { // Ignore anything before first z move.
                        layer_times.back() += 60. * std::abs(l) / line.new_F(self);
                    }
                }
                if (line.has('F') && line.f() == external_perimeter_speed)
                    ++ layer_external[scaled<coord_t>(self.z())];
            }
        });            
        THEN("slowdown_below_layer_time is honored") {
            // Account for some inaccuracies.
            const double slowdown_below_layer_time = config.opt<ConfigOptionInts>("slowdown_below_layer_time")->values.front() - 0.5;
            size_t minimum_time_honored = std::count_if(layer_times.begin(), layer_times.end(), 
                [slowdown_below_layer_time](double t){ return t > slowdown_below_layer_time; });
            REQUIRE(minimum_time_honored == layer_times.size());
        }
        THEN("slowdown_below_layer_time does not alter external perimeters") {
            // Broken by Vojtech
            // check that all layers have at least one unaltered external perimeter speed
            // my $external = all { $_ > 0 } values %layer_external;
            // ok $external, '';
        }
    }
}
