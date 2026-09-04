/**
 * Ported from t/retraction.t
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "libslic3r/GCode/GCodeWriter.hpp"
#include <Slic3r/Biz/GCodeReader/GCodeReader.hpp>

#include "test_data.hpp"
#include <regex>
#include <fstream>

using namespace Slic3r;
using namespace Test;
using namespace Catch;
using Biz::GCodeReader::GCodeReader;
using Domain::FloatOrPercentage;
using Domain::Percentage;

constexpr bool debug_files {false};

void check_gcode(std::initializer_list<TestMesh> meshes, const TestConfig& config, const unsigned duplicate) {
    constexpr std::size_t tools_count = 4;
    std::size_t tool = 0;
    std::array<unsigned, tools_count> toolchange_count{0}; // Track first usages so that we don't expect retract_length_toolchange when extruders are used for the first time
    std::array<bool, tools_count> retracted{false};
    std::array<double, tools_count> retracted_length{0};
    bool lifted = false;
    double lift_dist = 0; // Track lifted distance for toolchanges and extruders with different retract_lift values
    bool changed_tool = false;
    bool wait_for_toolchange = false;

    Print print;
    Domain::Model model;
    Test::init_print({TestMesh::cube_20x20x20}, print, model, config, duplicate);
    std::string gcode = Test::gcode(print);

    if constexpr(debug_files) {
        static int count{0};
        std::ofstream file{"check_gcode_" + std::to_string(count++) + ".gcode"};
        file << gcode;
    }

	GCodeReader parser;
    parser.parse_buffer(gcode, [&] (GCodeReader &self, const GCodeReader::GCodeLine &line) {
        std::regex regex{"^T(\\d+)"};
        std::smatch matches;
        std::string cmd{line.cmd()};
        if (std::regex_match(cmd, matches, regex)) {
            tool = std::stoul(matches[1].str());
            changed_tool = true;
            wait_for_toolchange = false;
            toolchange_count[tool]++;
        } else if (std::regex_match(cmd, std::regex{"^G[01]$"}) && !line.has(Z)) { // ignore lift taking place after retraction
            INFO("Toolchange must not happen right after retraction.");
            CHECK(!wait_for_toolchange);
        }

        auto get_tool_value_double{[&](const std::string& key) {
            std::optional<Domain::ConfigItem> override{config.tool.at(tool).overrides.get(key)};
            if (override) {
                return override->get<double>();
            }
            return config.print.items.opt(key).get<double>();
        }};

        const double retract_length = get_tool_value_double("retract_length");
        const double retract_before_travel = get_tool_value_double("retract_before_travel");
        const double retract_length_toolchange = get_tool_value_double("retract_length_toolchange");
        const double retract_restart_extra = get_tool_value_double("retract_restart_extra");
        const double retract_restart_extra_toolchange = get_tool_value_double("retract_restart_extra_toolchange");

        const double travel_speed = config.print.items.opt("travel_speed").get<double>();

        const double feedrate = line.has_f() ? line.f() : self.f();

        if (line.dist_Z(self) != 0) {
            // lift move or lift + change layer
            const double retract_lift = get_tool_value_double("retract_lift");
            if (
                line.dist_Z(self) == Approx(retract_lift)
                || (
                    line.dist_Z(self) == Approx(config.print.items.opt("layer_height").get<double>() + retract_lift)
                    && retract_lift > 0
                )
            ) {
                INFO("Only lift while retracted");
                CHECK(retracted[tool]);
                INFO("No double lift");
                CHECK(!lifted);
                lifted = true;
                lift_dist = line.dist_Z(self);
            }
            if (line.dist_Z(self) < 0) {
                INFO("Must be lifted before going down.");
                CHECK(lifted);
                INFO("Going down by the same amount of the lift or by the amount needed to get to next layer");
                CHECK((
                    line.dist_Z(self) == Approx(-lift_dist)
                    || line.dist_Z(self) == Approx(-lift_dist + config.print.items.opt("layer_height").get<double>())
                ));
                lift_dist = 0;
                lifted = false;
            }
            const double travel_speed_z = config.print.items.opt("travel_speed_z").get<double>();
            if (travel_speed_z) {
                Vec3d move{line.dist_X(self), line.dist_Y(self), line.dist_Z(self)};
                const double move_u_z = move.z() / move.norm();
                const double travel_speed_ = std::abs(travel_speed_z / move_u_z);
                INFO("move Z feedrate Z component is less than or equal to travel_speed_z");
                CHECK(feedrate * std::abs(move_u_z) <= Approx(travel_speed_z * 60).epsilon(GCodeFormatter::XYZ_EPSILON));
                if (travel_speed_ < travel_speed) {
                    INFO("move Z at travel speed Z");
                    CHECK(feedrate == Approx(travel_speed_ * 60).epsilon(GCodeFormatter::XYZ_EPSILON));
                    INFO("move Z feedrate Z component is equal to travel_speed_z");
                    CHECK(feedrate * std::abs(move_u_z) == Approx(travel_speed_z * 60).epsilon(GCodeFormatter::XYZ_EPSILON));
                } else {
                    INFO("move Z at travel speed");
                    CHECK(feedrate == Approx(travel_speed * 60).epsilon(GCodeFormatter::XYZ_EPSILON));
                }
            } else {
                INFO("move Z at travel speed");
                CHECK(feedrate == Approx(travel_speed * 60).epsilon(GCodeFormatter::XYZ_EPSILON));
            }
        }
        if (line.retracting(self)) {
            retracted[tool] = true;
            retracted_length[tool] += -line.dist_E(self);
            if (retracted_length[tool] == Approx(retract_length)) {
                // okay
            } else if (retracted_length[tool] == Approx(retract_length_toolchange)) {
                wait_for_toolchange = true;
            } else {
                INFO("Not retracted by the correct amount.");
                CHECK(false);
            }
        }
        if (line.extruding(self)) {
            INFO("Only extruding while not lifted");
            CHECK(!lifted);
            if (retracted[tool]) {
                double expected_amount = retracted_length[tool] + retract_restart_extra;
                if (changed_tool && toolchange_count[tool] > 1) {
                    expected_amount = retract_length_toolchange + retract_restart_extra_toolchange;
                    changed_tool = false;
                }
                INFO("Unretracted by the correct amount");
                REQUIRE(line.dist_E(self) == Approx(expected_amount));
                retracted[tool] = false;
                retracted_length[tool] = 0;
            }
        }
        if (line.travel() && line.dist_XY(self) >= retract_before_travel) {
            INFO("Retracted before long travel move");
            CHECK(retracted[tool]);
        }
    });
}

void test_slicing(std::initializer_list<TestMesh> meshes, TestConfig& config, const unsigned duplicate = 1) {
    SECTION("Retraction") {
        check_gcode(meshes, config, duplicate);
    }

    SECTION("Restart extra length") {
        for (auto& tool : config.tool) {
            tool.overrides.set("retract_restart_extra", 1.0);
        }
        check_gcode(meshes, config, duplicate);
    }

    SECTION("Retract_lift") {
        config.tool.at(0).overrides.set("retract_lift", 1.0);
        config.tool.at(1).overrides.set("retract_lift", 2.0);
        check_gcode(meshes, config, duplicate);
    }
}

TEST_CASE("Slicing with retraction and lifting", "[retraction]") {
    TestConfig config{4, 0.6};

    config.print.items.opt("first_layer_height").set(FloatOrPercentage{Percentage{100}});
    config.printer.items.opt("start_gcode").set("");
    config.print.items.opt("first_layer_speed").set(FloatOrPercentage{Percentage{100}});
    config.print.items.opt("only_retract_when_crossing_perimeters").set(false);
    config.print.items.opt("retract_length").set(1.5);
    config.print.items.opt("retract_before_travel").set(3.0);
    config.print.items.opt("retract_layer_change").set(true);

    SECTION("Standard run") {
        test_slicing({TestMesh::cube_20x20x20}, config);
    }
    SECTION("With duplicate cube") {
        test_slicing({TestMesh::cube_20x20x20}, config, 2);
    }
    SECTION("Dual extruder with multiple skirt layers") {
        config.print.items.opt("infill_extruder").set(2);
        config.print.items.opt("skirts").set(4);
        config.print.items.opt("skirt_height").set(3);
        test_slicing({TestMesh::cube_20x20x20}, config);
    }
}

TEST_CASE("Slicing with retraction and lifting with travel_speed_z=10", "[retraction]") {
    TestConfig config{4, 0.6};

    config.print.items.opt("first_layer_height").set(FloatOrPercentage{Percentage{100}});
    config.printer.items.opt("start_gcode").set("");
    config.print.items.opt("first_layer_speed").set(FloatOrPercentage{Percentage{100}});
    config.print.items.opt("travel_speed").set(600.0);
    config.print.items.opt("travel_speed_z").set(10.0);
    config.print.items.opt("only_retract_when_crossing_perimeters").set(false);

    config.print.items.opt("retract_length").set(1.5);
    config.print.items.opt("retract_before_travel").set(3.0);
    config.print.items.opt("retract_layer_change").set(true);

    SECTION("Standard run") {
        test_slicing({TestMesh::cube_20x20x20}, config);
    }
    SECTION("With duplicate cube") {
        test_slicing({TestMesh::cube_20x20x20}, config, 2);
    }
    SECTION("Dual extruder with multiple skirt layers") {
        config.print.items.opt("infill_extruder").set(2);
        config.print.items.opt("skirts").set(4);
        config.print.items.opt("skirt_height").set(3);
        test_slicing({TestMesh::cube_20x20x20}, config);
    }
}

TEST_CASE("Z moves", "[retraction]") {

    TestConfig config;
    config.printer.items.opt("start_gcode").set("");
    config.print.items.opt("retract_length").set(0.0);
    config.print.items.opt("retract_layer_change").set(false);
    config.print.items.opt("retract_lift").set(0.2);

    bool retracted = false;
    unsigned layer_changes_with_retraction = 0;
    unsigned retractions = 0;
    unsigned z_restores = 0;

    std::string gcode = Slic3r::Test::slice({TestMesh::cube_20x20x20}, config);

    if constexpr(debug_files) {
        std::ofstream file{"zmoves.gcode"};
        file << gcode;
    }

	GCodeReader parser;
    parser.parse_buffer(gcode, [&] (GCodeReader &self, const GCodeReader::GCodeLine &line) {
        if (line.retracting(self)) {
            retracted = true;
            retractions++;
        } else if (line.extruding(self) && retracted) {
            retracted = 0;
        }

        if (line.dist_Z(self) != 0 && retracted) {
            layer_changes_with_retraction++;
        }

        if (line.dist_Z(self) < 0) {
            z_restores++;
        }
    });

    INFO("no retraction on layer change");
    CHECK(layer_changes_with_retraction == 0);
    INFO("no retractions");
    CHECK(retractions == 0);
    INFO("no lift");
    CHECK(z_restores == 0);
}

TEST_CASE("Firmware retraction handling", "[retraction]") {

    TestConfig config;
    config.printer.items.opt("use_firmware_retraction").set(true);

    bool retracted = false;
    unsigned double_retractions = 0;
    unsigned double_unretractions = 0;

    std::string gcode = Slic3r::Test::slice({TestMesh::cube_20x20x20}, config);
	GCodeReader parser;
    parser.parse_buffer(gcode, [&] (GCodeReader &self, const GCodeReader::GCodeLine &line) {
        if (line.cmd_is("G10")) {
            if (retracted)
                double_retractions++;
            retracted = true;
        } else if (line.cmd_is("G11")) {
            if (!retracted)
                double_unretractions++;
            retracted = 0;
        }
    });
    INFO("No double retractions");
    CHECK(double_retractions == 0);
    INFO("No double unretractions");
    CHECK(double_unretractions == 0);
}

TEST_CASE("Firmware retraction when length is 0", "[retraction]") {

    TestConfig config;
    config.printer.items.opt("use_firmware_retraction").set(true);
    config.print.items.opt("retract_length").set(0.0);

    bool retracted = false;

    std::string gcode = Slic3r::Test::slice({TestMesh::cube_20x20x20}, config);
	GCodeReader parser;
    parser.parse_buffer(gcode, [&] (GCodeReader &self, const GCodeReader::GCodeLine &line) {
        if (line.cmd_is("G10")) {
            retracted = true;
        }
    });
    INFO("Retracting also when --retract-length is 0 but --use-firmware-retraction is enabled");
    CHECK(retracted);
}

std::vector<double> get_lift_layers(const TestConfig& config) {
    Print print;
    Domain::Model model;
    Test::init_print({TestMesh::cube_20x20x20}, print, model, config, 2);
    std::string gcode = Test::gcode(print);

    std::vector<double> result;
	GCodeReader parser;
    parser.parse_buffer(gcode, [&] (GCodeReader &self, const GCodeReader::GCodeLine &line) {
        if (line.cmd_is("G1") && line.dist_Z(self) < 0) {
            result.push_back(line.new_Z(self));
        }
    });
    return result;
}

bool values_are_in_range(const std::vector<double>& values, double from, double to) {
    for (const double& value : values) {
        if (value < from || value > to) {
            return false;
        }
    }
    return true;
}

TEST_CASE("Lift above/bellow layers", "[retraction]") {

    TestConfig config{4, 0.6};

    for (auto& tool : config.tool) {
        tool.overrides.set("retract_lift", 3.0);
        tool.overrides.set("retract_lift_above", 0.0);
        tool.overrides.set("retract_lift_below", 0.0);
    }
    config.tool.at(1).overrides.set("retract_lift", 4.0);
    config.tool.at(1).overrides.set("retract_lift_above", 0.0);
    config.tool.at(1).overrides.set("retract_lift_below", 0.0);

    config.printer.items.opt("start_gcode").set("" );

    std::vector<double> lift_layers = get_lift_layers(config);
    INFO("lift takes place when above/below == 0");
    CHECK(!lift_layers.empty());

    for (auto& tool : config.tool) {
        tool.overrides.set("retract_lift_above", 5.0);
        tool.overrides.set("retract_lift_below", 15.0);
    }

    config.tool.at(1).overrides.set("retract_lift_above", 6.0);
    config.tool.at(1).overrides.set("retract_lift_below", 13.0);

    lift_layers = get_lift_layers(config);
    INFO("lift takes place when above/below != 0");
    CHECK(!lift_layers.empty());

    double retract_lift_above = config.tool.at(0).overrides.get("retract_lift_above")->get<double>();
    double retract_lift_below = config.tool.at(0).overrides.get("retract_lift_below")->get<double>();

    INFO("Z is not lifted above/below the configured value");
    CHECK(values_are_in_range(lift_layers, retract_lift_above, retract_lift_below));

    // check lifting with different values for 2. extruder
    config.print.items.opt("perimeter_extruder").set(2);
    config.print.items.opt("infill_extruder").set(2);

    for (auto& tool : config.tool) {
        tool.overrides.set("retract_lift_above", 0.0);
        tool.overrides.set("retract_lift_below", 0.0);
    }

    config.tool.at(1).overrides.set("retract_lift_above", 0.0);
    config.tool.at(1).overrides.set("retract_lift_below", 0.0);

    lift_layers = get_lift_layers(config);
    INFO("lift takes place when above/below == 0  for 2. extruder");
    CHECK(!lift_layers.empty());

    for (auto& tool : config.tool) {
        tool.overrides.set("retract_lift_above", 5.0);
        tool.overrides.set("retract_lift_below", 15.0);
    }
    config.tool.at(1).overrides.set("retract_lift_above", 6.0);
    config.tool.at(1).overrides.set("retract_lift_below", 13.0);

    lift_layers = get_lift_layers(config);
    INFO("lift takes place when above/below != 0 for 2. extruder");
    CHECK(!lift_layers.empty());

    retract_lift_above = config.tool.at(1).overrides.get("retract_lift_above")->get<double>();
    retract_lift_below = config.tool.at(1).overrides.get("retract_lift_below")->get<double>();

    INFO("Z is not lifted above/below the configured value for 2. extruder");
    CHECK(values_are_in_range(lift_layers, retract_lift_above, retract_lift_below));
}
