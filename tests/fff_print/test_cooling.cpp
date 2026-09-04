#include <catch2/catch_test_macros.hpp>

#include <numeric>
#include <sstream>

#include "test_data.hpp" // get access to init_print, etc

#include "libslic3r/GCode.hpp"
#include "Slic3r/Biz/GCodeReader/GCodeReader.hpp"
#include "libslic3r/GCode/CoolingBuffer.hpp"
#include "libslic3r/libslic3r.h"

using namespace Slic3r;
using Biz::GCodeReader::GCodeReader;
using Domain::FullConfigFDM;
using Domain::FullConfigFDM;
using Domain::FloatOrPercentage;
using Domain::Percentage;
using Test::TestConfig;

std::unique_ptr<CoolingBuffer> make_cooling_buffer(
    GCodeGenerator                  &gcode,
    const PrintConfigView &config = {std::make_shared<FullConfigFDM>(FullConfigFDM::defaults())},
    const std::vector<unsigned int> &extruder_ids   = { 0 })
{
    gcode.set_layer_count(10);
    gcode.writer().set_extruders(extruder_ids);
    gcode.writer().set_extruder(0);
    return std::make_unique<CoolingBuffer>(gcode, Biz::Slicing::CoolingBufferConfig{config});
}

SCENARIO("Cooling unit tests", "[Cooling]") {
    const std::string   gcode1        = "G1 X100 E1 F3000\n";
    // 2 sec
    const double        print_time1   = 100. / (3000. / 60.);
    const std::string   gcode2        = gcode1 + "G1 X0 E1 F3000\n";
    // 4 sec
    const double        print_time2   = 2. * print_time1;

    TestConfig config;

    // Default cooling settings.
    config.filament[0].items.opt("bridge_fan_speed").set(100);
    config.filament[0].items.opt("cooling").set(true);
    config.filament[0].items.opt("fan_always_on").set(false);
    config.filament[0].items.opt("fan_below_layer_time").set(60);
    config.filament[0].items.opt("max_fan_speed").set(100);
    config.filament[0].items.opt("min_print_speed").set(10.0);
    config.filament[0].items.opt("slowdown_below_layer_time").set(5);
    // Default print speeds.
    config.print.items.opt("bridge_speed").set(60.0);
    config.print.items.opt("external_perimeter_speed").set(FloatOrPercentage{Percentage{50}});
    config.print.items.opt("first_layer_speed").set(FloatOrPercentage{30.0});
    config.print.items.opt("gap_fill_speed").set(20.0);
    config.print.items.opt("infill_speed").set(80.0);
    config.print.items.opt("perimeter_speed").set(60.0);
    config.print.items.opt("small_perimeter_speed").set(FloatOrPercentage{15.0});
    config.print.items.opt("solid_infill_speed").set(FloatOrPercentage{20.0});
    config.print.items.opt("top_solid_infill_speed").set(FloatOrPercentage{15.0});
    config.print.items.opt("max_print_speed").set(80.0);
    // Override for tests.
    config.filament[0].items.opt("disable_fan_first_layers").set(0);

    Print print;
    WHEN("G-code block 3") {
        THEN("speed is not altered when elapsed time is greater than slowdown threshold") {
            // Print time of gcode.
            const double print_time = 100. / (3000. / 60.);
            //FIXME slowdown_below_layer_time is rounded down significantly from 1.8s to 1s.
            config.filament[0].items.opt("slowdown_below_layer_time").set(int(print_time * 0.999));

            const auto config_view{config.get_view()};
            print.set_config(config_view);
            GCodeGenerator gcodegen{&print};
            auto buffer = make_cooling_buffer(gcodegen, config_view);
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
        config.filament[0].items.opt("slowdown_below_layer_time").set(int(print_time * 1.001));

        const auto config_view{config.get_view()};
        print.set_config(config_view);
        GCodeGenerator gcodegen{&print};
        auto buffer = make_cooling_buffer(gcodegen, config_view);
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
            config.filament[0].items.opt("fan_below_layer_time").set(int(print_time1 * 0.88));
            config.filament[0].items.opt("slowdown_below_layer_time").set(int(print_time1 * 0.99));
            const auto config_view{config.get_view()};
            print.set_config(config_view);
            GCodeGenerator gcodegen{&print};
            auto buffer = make_cooling_buffer(gcodegen, config_view);
            std::string gcode = buffer->process_layer(gcode1, 0, true);
            bool fan_not_activated = gcode.find("M106") == gcode.npos;
            REQUIRE(fan_not_activated);
        }
    }
    WHEN("G-code block 1 with two extruders") {
        config.filament.emplace_back();
        config.tool.emplace_back();
        config.hw_config = Test::create_dummy_hw_config(2);
        config.filament[0].items.opt("cooling").set(true);
        config.filament[1].items.opt("cooling").set(false);
        config.filament[0].items.opt("fan_below_layer_time").set(int(print_time2 + 1.));
        config.filament[1].items.opt("fan_below_layer_time").set(int(print_time2 + 1.));
        config.filament[0].items.opt("slowdown_below_layer_time").set(int(print_time2 + 2.));
        config.filament[1].items.opt("slowdown_below_layer_time").set(int(print_time2 + 2.));

        const auto config_view{config.get_view()};
        print.set_config(config_view);
        GCodeGenerator gcodegen{&print};
        auto buffer = make_cooling_buffer(gcodegen, config_view, { 0, 1 });
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
            config.filament[0].items.opt("slowdown_below_layer_time").set(int(print_time2 * 0.99));
            const auto config_view{config.get_view()};
            print.set_config(config_view);
            GCodeGenerator gcodegen{&print};
            auto buffer = make_cooling_buffer(gcodegen, config_view);
            std::string gcode = buffer->process_layer(gcode2, 0, true);
            bool ok = gcode.find("F3000") != gcode.npos;
            REQUIRE(ok);
        }
        THEN("fan is not activated on all objects printing at different Z") {
            config.filament[0].items.opt("fan_below_layer_time").set(int(print_time2 * 0.65));
            config.filament[0].items.opt("slowdown_below_layer_time").set(int(print_time2 * 0.7));
            const auto config_view{config.get_view()};
            print.set_config(config_view);
            GCodeGenerator gcodegen{&print};
            auto buffer = make_cooling_buffer(gcodegen, config_view);
            // use an elapsed time which is < the threshold but greater than it when summed twice
            std::string gcode = buffer->process_layer(gcode2, 0, true) + buffer->process_layer(gcode2, 1, true);
            bool fan_not_activated = gcode.find("M106") == gcode.npos;
            REQUIRE(fan_not_activated);
        }
        THEN("fan is activated on all objects printing at different Z") {
            // use an elapsed time which is < the threshold even when summed twice
            config.filament[0].items.opt("fan_below_layer_time").set(int(print_time2 + 1));
            config.filament[0].items.opt("slowdown_below_layer_time").set(int(print_time2 + 1));
            const auto config_view{config.get_view()};
            print.set_config(config_view);
            GCodeGenerator gcodegen{&print};
            auto buffer = make_cooling_buffer(gcodegen, config_view);
            // use an elapsed time which is < the threshold but greater than it when summed twice
            std::string gcode = buffer->process_layer(gcode2, 0, true) + buffer->process_layer(gcode2, 1, true);
            bool fan_activated = gcode.find("M106") != gcode.npos;
            REQUIRE(fan_activated);
        }
    }
}

SCENARIO("Cooling integration tests", "[Cooling]") {
    GIVEN("overhang") {
        TestConfig config;
        config.filament[0].items.opt("cooling").set(true);
        config.filament[0].items.opt("bridge_fan_speed").set(100);
        config.filament[0].items.opt("fan_below_layer_time").set(0);
        config.filament[0].items.opt("slowdown_below_layer_time").set(0);
        config.print.items.opt("bridge_speed").set(99.0);
        config.print.items.opt("enable_dynamic_overhang_speeds").set(false);
        // internal bridges use solid_infil speed
        config.print.items.opt("bottom_solid_layers").set(1);

        GCodeReader parser;
        int fan = 0;
        int fan_with_incorrect_speeds = 0;
        int fan_with_incorrect_print_speeds = 0;
        int bridge_with_no_fan = 0;
        const double bridge_speed = config.print.items.opt("bridge_speed").get<double>() * 60;
        parser.parse_buffer(
            Slic3r::Test::slice({ Slic3r::Test::TestMesh::overhang }, config),
            [&fan, &fan_with_incorrect_speeds, &fan_with_incorrect_print_speeds, &bridge_with_no_fan, bridge_speed]
                (GCodeReader &self, const GCodeReader::GCodeLine &line)
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

        TestConfig config;
        config.filament[0].items.opt("cooling").set(true);
        config.filament[0].items.opt("fan_below_layer_time").set(0);
        config.filament[0].items.opt("slowdown_below_layer_time").set(10);
        config.filament[0].items.opt("min_print_speed").set(0.0);
        config.printer.items.opt("start_gcode").set(std::string{""});
        config.print.items.opt("first_layer_speed").set(FloatOrPercentage{Percentage{100}});
        // internal bridges use solid_infil speed
        config.print.items.opt("external_perimeter_speed").set(FloatOrPercentage{99.0});

        GCodeReader parser;
        const double external_perimeter_speed = config.print.items.opt("external_perimeter_speed").get<FloatOrPercentage>().float_value() * 60;
        std::vector<double> layer_times;
        // z => 1
        std::map<coord_t, int> layer_external;
        parser.parse_buffer(
            Slic3r::Test::slice({ Slic3r::Test::TestMesh::cube_20x20x20 }, config),
            [&layer_times, &layer_external, external_perimeter_speed]
                (GCodeReader &self, const GCodeReader::GCodeLine &line)
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
            const double slowdown_below_layer_time = config.filament[0].items.opt("slowdown_below_layer_time").get<int>() - 0.5;
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
