#include <catch2/catch_test_macros.hpp>

#include <exception>
#include <numeric>
#include <sstream>

#include <boost/regex.hpp>

#include "libslic3r/Print.hpp"
#include "libslic3r/libslic3r.h"
#include "Slic3r/Biz/GCodeReader/GCodeReader.hpp"

#include "test_data.hpp"

using namespace Slic3r;
using Biz::GCodeReader::GCodeReader;
using Test::TestConfig;
using Domain::SupportMode;

TEST_CASE("Custom G-code", "[CustomGCode]")
{
    WHEN("start_gcode and layer_gcode set") {

        TestConfig config;
        config.printer.items.opt("start_gcode").set("_MY_CUSTOM_START_GCODE_" );
        config.printer.items.opt("layer_gcode").set("_MY_CUSTOM_LAYER_GCODE_" );

        GCodeReader parser;
        bool        last_move_was_z_change = false;
        bool        first_z_move = true; // First z move is not a layer change.
        int         num_layer_changes_not_applied = 0;
        parser.parse_buffer(Slic3r::Test::slice({ Test::TestMesh::cube_2x20x10 }, config),
            [&](GCodeReader &self, const GCodeReader::GCodeLine &line)
        {
            if (last_move_was_z_change != line.cmd_is("_MY_CUSTOM_LAYER_GCODE_")) {
                ++ num_layer_changes_not_applied;
            }
            if (line.dist_Z(self) > 0 && first_z_move) {
                first_z_move = false;
            } else if (line.dist_Z(self) > 0){
                last_move_was_z_change = true;
            } else {
                last_move_was_z_change = false;
            }
        });
        THEN("custom layer G-code is applied after Z move and before other moves") {
            REQUIRE(num_layer_changes_not_applied == 0);
        }
    };

    Test::TestConfig config{4, 0.6};
    config.print.items.opt("infill_extruder").set(2);
    config.print.items.opt("perimeter_extruder").set(2);
    config.filament[0].items.opt("first_layer_temperature").set(200);
    config.filament[1].items.opt("first_layer_temperature").set(205);

    WHEN("Printing with single but non-zero extruder") {
        std::string gcode = Slic3r::Test::slice({ Slic3r::Test::TestMesh::cube_20x20x20 }, config);
        THEN("temperature set correctly for non-zero yet single extruder") {
            REQUIRE(Slic3r::Test::contains(gcode, "\nM104 S205 T1 ;"));
        }
        THEN("unused extruder correctly ignored") {
            REQUIRE(! Slic3r::Test::contains_regex(gcode, "M104 S\\d+ T0"));
        }
    }
    WHEN("Printing with two extruders") {
        config.print.items.opt("infill_extruder").set(1);
        std::string gcode = Slic3r::Test::slice({ Slic3r::Test::TestMesh::cube_20x20x20 }, config);
        THEN("temperature set correctly for first extruder") {
            REQUIRE(Slic3r::Test::contains(gcode, "\nM104 S200 T0 ;"));
        };
        THEN("temperature set correctly for second extruder") {
            REQUIRE(Slic3r::Test::contains(gcode, "\nM104 S205 T1 ;"));
        };
    }

    auto test = [=](TestConfig &config) {
        // we use the [infill_extruder] placeholder to make sure this test doesn't
        // catch a false positive caused by the unparsed start G-code option itself
        // being embedded in the G-code
        config.print.items.opt("infill_extruder").set(1);

        std::string gcode = Slic3r::Test::slice({ Slic3r::Test::TestMesh::cube_20x20x20 }, config);
        THEN("temperature placeholder for first extruder correctly populated") {
            REQUIRE(Slic3r::Test::contains(gcode, "temp0:200"));
        }
        THEN("temperature placeholder for second extruder correctly populated") {
            REQUIRE(Slic3r::Test::contains(gcode, "temp1:205"));
        }
        THEN("temperature placeholder for unused extruder populated with first value") {
            REQUIRE(Slic3r::Test::contains(gcode, "temp2:200"));
        }
    };
    WHEN("legacy syntax") {
        config.printer.items.opt("start_gcode").set(
            ";__temp0:[first_layer_temperature_0]__\n"
            ";__temp1:[first_layer_temperature_1]__\n"
            ";__temp2:[first_layer_temperature_2]__\n");
        test(config);
    }
    WHEN("new syntax") {
        config.printer.items.opt("start_gcode").set(
            ";__temp0:{first_layer_temperature[0]}__\n"
            ";__temp1:{first_layer_temperature[1]}__\n"
            ";__temp2:{first_layer_temperature[2]}__\n");
        test(config);
    }
    WHEN("Vojtech's syntax") {
        config.print.items.opt("infill_extruder").set(1 );
        config.printer.items.opt("start_gcode").set(
                ";substitution:{if infill_extruder==1}extruder1"
                "{elsif infill_extruder==2}extruder2"
                "{else}extruder3{endif}"
        );
        std::string gcode = Slic3r::Test::slice({ Slic3r::Test::TestMesh::cube_20x20x20 }, config);
        THEN("if / else / endif - first block returned") {
            REQUIRE(Test::contains(gcode, "\n;substitution:extruder1\n"));
        }
    }
    GIVEN("Layer change G-codes")
    {
        TestConfig config;
        config.printer.items.opt("before_layer_gcode").set(";BEFORE [layer_num]" );
        config.printer.items.opt("layer_gcode").set(";CHANGE [layer_num]" );
        config.print.items.opt("support_material").set(SupportMode::Everywhere);
        config.print.items.opt("layer_height").set(0.2);
        WHEN("before and after layer change G-codes set") {
            std::string gcode = Slic3r::Test::slice({ Slic3r::Test::TestMesh::overhang }, config);
            GCodeReader parser;
            std::vector<int> before;
            std::vector<int> change;
            parser.parse_buffer(gcode, [&before, &change](GCodeReader &self, const GCodeReader::GCodeLine &line){
                int d;
                if (sscanf(line.raw().c_str(), ";BEFORE %d", &d) == 1)
                    before.emplace_back(d);
                else if (sscanf(line.raw().c_str(), ";CHANGE %d", &d) == 1) {
                    change.emplace_back(d);
                    if (d != before.back())
                        throw std::runtime_error("inconsistent layer_num before and after layer change");
                }
            });
            THEN("layer_num is consistent before and after layer changes") {
                REQUIRE(before == change);
            }
            THEN("layer_num grows continously") {
                // i.e. no duplicates or regressions
                bool successive = true;
                for (size_t i = 1; i < change.size(); ++ i)
                    if (change[i - 1] + 1 != change[i])
                        successive = false;
                REQUIRE(successive);
            }
        }
    }
    GIVEN("if / elsif / elsif / elsif / else / endif")
    {

        Test::TestConfig config{5, 0.6};
        config.printer.items.opt("start_gcode").set(
                ";substitution:{if infill_extruder==1}if block"
                "{elsif infill_extruder==2}elsif block 1"
                "{elsif infill_extruder==3}elsif block 2"
                "{elsif infill_extruder==4}elsif block 3"
                "{else}endif block{endif}"
                ":end"
        );

        std::string returned[] = { "" /* indexed by one based extruder ID */, "if block", "elsif block 1", "elsif block 2", "elsif block 3", "endif block" };
        auto test = [&](int i) {
            config.print.items.opt("infill_extruder").set(i);
            std::string gcode = Slic3r::Test::slice({ Slic3r::Test::TestMesh::cube_20x20x20 }, config);
            int found_error = 0;
            for (int j = 1; j <= 5; ++ j)
                if (i != j && Slic3r::Test::contains(gcode, std::string("substitution:") + returned[j] + ":end"))
                    // failure
                    ++ found_error;
            THEN(std::string("if / else / endif returned ") + returned[i]) {
                REQUIRE(Slic3r::Test::contains(gcode, std::string("substitution:") + returned[i] + ":end"));
            }
            THEN(std::string("if / else / endif - only ") + returned[i] + "returned") {
                REQUIRE(found_error == 0);
            }
        };
        WHEN("infill_extruder == 1") { test(1); }
        WHEN("infill_extruder == 2") { test(2); }
        WHEN("infill_extruder == 3") { test(3); }
        WHEN("infill_extruder == 4") { test(4); }
        WHEN("infill_extruder == 5") { test(5); }
    }
    GIVEN("nested if / if / else / endif") {
        Test::TestConfig config{5, 0.6};

        config.printer.items.opt("start_gcode").set(
            ";substitution:{if infill_extruder==1}{if perimeter_extruder==1}block11{else}block12{endif}"
            "{elsif infill_extruder==2}{if perimeter_extruder==1}block21{else}block22{endif}"
            "{else}{if perimeter_extruder==1}block31{else}block32{endif}{endif}:end"
        );

        auto test = [&](int i) {
            config.print.items.opt("infill_extruder").set(i);
            int failed = 0;
            for (int j = 1; j <= 2; ++ j) {
                config.print.items.opt("perimeter_extruder").set(j);
                std::string gcode = Slic3r::Test::slice({ Slic3r::Test::TestMesh::cube_20x20x20 }, config);
                if (! Slic3r::Test::contains(gcode, std::string("substitution:block") + std::to_string(i) + std::to_string(j) + ":end"))
                    ++ failed;
            }
            THEN(std::string("two level if / else / endif - block for infill_extruder ") + std::to_string(i) + "succeeded") {
                REQUIRE(failed == 0);
            }
        };
        WHEN("infill_extruder == 1") { test(1); }
        WHEN("infill_extruder == 2") { test(2); }
        WHEN("infill_extruder == 3") { test(3); }
    }
    GIVEN("printer type in notes") {
        config.printer.items.opt("start_gcode").set(
              ";substitution:{if notes==\"MK2\"}MK2{elsif notes==\"MK3\"}MK3{else}MK1{endif}:end"
        );
        auto test = [&](const std::string &printer_name) {
            config.print.items.opt("notes").set(printer_name);
            std::string gcode = Slic3r::Test::slice({ Slic3r::Test::TestMesh::cube_20x20x20 }, config);
            THEN(std::string("printer name ") + printer_name + " matched") {
                REQUIRE(Slic3r::Test::contains(gcode, std::string("substitution:") + printer_name + ":end"));
            }
        };
        WHEN("printer MK2") { test("MK2"); }
        WHEN("printer MK3") { test("MK3"); }
        WHEN("printer MK1") { test("MK1"); }
    }
    GIVEN("sequential print with between_objects_gcode") {
        TestConfig config;
        config.print.items.opt("complete_objects").set(true);
        config.printer.items.opt("between_objects_gcode").set("_MY_CUSTOM_GCODE_");
        std::string gcode = Slic3r::Test::slice(
            // 3x 20mm box
            { Slic3r::Test::TestMesh::cube_20x20x20, Slic3r::Test::TestMesh::cube_20x20x20, Slic3r::Test::TestMesh::cube_20x20x20 },
            config);
        THEN("between_objects_gcode is applied correctly") {
            const boost::regex expression("^_MY_CUSTOM_GCODE_");
            const std::ptrdiff_t match_count = 
                std::distance(boost::sregex_iterator(gcode.begin(), gcode.end(), expression), boost::sregex_iterator());
            REQUIRE(match_count == 2);
        }
    }
    GIVEN("before_layer_gcode increments global variable") {
        TestConfig config;
        config.printer.items.opt("start_gcode").set("{global counter=0}");
        config.printer.items.opt("before_layer_gcode").set(";Counter{counter=counter+1;counter}\n");
        std::string gcode = Slic3r::Test::slice({ Slic3r::Test::TestMesh::cube_20x20x20 }, config);
        THEN("The counter is emitted multiple times before layer change.") {
            REQUIRE(Slic3r::Test::contains(gcode, ";Counter1\n"));
            REQUIRE(Slic3r::Test::contains(gcode, ";Counter2\n"));
            REQUIRE(Slic3r::Test::contains(gcode, ";Counter3\n"));
        }
    }
}
