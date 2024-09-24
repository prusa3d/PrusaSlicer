/**
 * Ported from t/retraction.t
 */

#include <catch2/catch.hpp>

#include <libslic3r/GCodeReader.hpp>
#include <libslic3r/Config.hpp>

#include "test_data.hpp"
#include <regex>
#include <fstream>

using namespace Slic3r;
using namespace Test;

constexpr bool debug_files {false};

void check_gcode(std::initializer_list<TestMesh> meshes, const DynamicPrintConfig& config, const unsigned duplicate) {
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
    Model model;
    Test::init_print({TestMesh::cube_20x20x20}, print, model, config, false, duplicate);
    std::string gcode = Test::gcode(print);

    if constexpr(debug_files) {
        static int count{0};
        std::ofstream file{"check_gcode_" + std::to_string(count++) + ".gcode"};
        file << gcode;
    }

	GCodeReader parser;
    parser.parse_buffer(gcode, [&] (Slic3r::GCodeReader &self, const Slic3r::GCodeReader::GCodeLine &line) {
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

        const double retract_length = config.option<ConfigOptionFloats>("retract_length")->get_at(tool);
        const double retract_before_travel = config.option<ConfigOptionFloats>("retract_before_travel")->get_at(tool);
        const double retract_length_toolchange = config.option<ConfigOptionFloats>("retract_length_toolchange")->get_at(tool);
        const double retract_restart_extra = config.option<ConfigOptionFloats>("retract_restart_extra")->get_at(tool);
        const double retract_restart_extra_toolchange = config.option<ConfigOptionFloats>("retract_restart_extra_toolchange")->get_at(tool);

        if (line.dist_Z(self) != 0) {
            // lift move or lift + change layer
            const double retract_lift = config.option<ConfigOptionFloats>("retract_lift")->get_at(tool);
            if (
                line.dist_Z(self) == Approx(retract_lift)
                || (
                    line.dist_Z(self) == Approx(config.opt_float("layer_height") + retract_lift)
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
                INFO("Must be lifted before going down.")
                CHECK(lifted);
                INFO("Going down by the same amount of the lift or by the amount needed to get to next layer");
                CHECK((
                    line.dist_Z(self) == Approx(-lift_dist)
                    || line.dist_Z(self) == Approx(-lift_dist + config.opt_float("layer_height"))
                ));
                lift_dist = 0;
                lifted = false;
            }
            const double feedrate = line.has_f() ? line.f() : self.f();
            INFO("move Z at travel speed");
            CHECK(feedrate == Approx(config.opt_float("travel_speed") * 60));
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

void test_slicing(std::initializer_list<TestMesh> meshes, DynamicPrintConfig& config, const unsigned duplicate = 1) {
    SECTION("Retraction") {
        check_gcode(meshes, config, duplicate);
    }

    SECTION("Restart extra length") {
        config.set_deserialize_strict({{ "retract_restart_extra", "1" }});
        check_gcode(meshes, config, duplicate);
    }

    SECTION("Negative restart extra length") {
        config.set_deserialize_strict({{ "retract_restart_extra", "-1" }});
        check_gcode(meshes, config, duplicate);
    }

    SECTION("Retract_lift") {
        config.set_deserialize_strict({{ "retract_lift", "1,2" }});
        check_gcode(meshes, config, duplicate);
    }

}

TEST_CASE("Slicing with retraction and lifing", "[retraction]") {
    DynamicPrintConfig config = Slic3r::DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
	    { "nozzle_diameter", "0.6,0.6,0.6,0.6" },
        { "first_layer_height", config.opt_float("layer_height") },
        { "first_layer_speed", "100%" },
        { "start_gcode", "" },  // To avoid dealing with the nozzle lift in start G-code
        { "retract_length", "1.5" },
        { "retract_before_travel", "3" },
        { "retract_layer_change", "1" },
        { "only_retract_when_crossing_perimeters", 0 },
    });

    SECTION("Standard run") {
        test_slicing({TestMesh::cube_20x20x20}, config);
    }
    SECTION("With duplicate cube") {
        test_slicing({TestMesh::cube_20x20x20}, config, 2);
    }
    SECTION("Dual extruder with multiple skirt layers") {
        config.set_deserialize_strict({
            {"infill_extruder", 2},
            {"skirts", 4},
            {"skirt_height", 3},
        });
        test_slicing({TestMesh::cube_20x20x20}, config);
    }
}

TEST_CASE("Z moves", "[retraction]") {

    DynamicPrintConfig config = Slic3r::DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
        { "start_gcode", "" },  // To avoid dealing with the nozzle lift in start G-code
        { "retract_length", "0" },
        { "retract_layer_change", "0" },
        { "retract_lift", "0.2" }
    });

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
    parser.parse_buffer(gcode, [&] (Slic3r::GCodeReader &self, const Slic3r::GCodeReader::GCodeLine &line) {
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

    DynamicPrintConfig config = Slic3r::DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
        { "use_firmware_retraction", 1 },
    });

    bool retracted = false;
    unsigned double_retractions = 0;
    unsigned double_unretractions = 0;

    std::string gcode = Slic3r::Test::slice({TestMesh::cube_20x20x20}, config);
	GCodeReader parser;
    parser.parse_buffer(gcode, [&] (Slic3r::GCodeReader &self, const Slic3r::GCodeReader::GCodeLine &line) {
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

    DynamicPrintConfig config = Slic3r::DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
        { "use_firmware_retraction", 1 },
        { "retract_length", "0" },
    });

    bool retracted = false;

    std::string gcode = Slic3r::Test::slice({TestMesh::cube_20x20x20}, config);
	GCodeReader parser;
    parser.parse_buffer(gcode, [&] (Slic3r::GCodeReader &self, const Slic3r::GCodeReader::GCodeLine &line) {
        if (line.cmd_is("G10")) {
            retracted = true;
        }
    });
    INFO("Retracting also when --retract-length is 0 but --use-firmware-retraction is enabled");
    CHECK(retracted);
}

std::vector<double> get_lift_layers(const DynamicPrintConfig& config) {
    Print print;
    Model model;
    Test::init_print({TestMesh::cube_20x20x20}, print, model, config, false, 2);
    std::string gcode = Test::gcode(print);

    std::vector<double> result;
	GCodeReader parser;
    parser.parse_buffer(gcode, [&] (Slic3r::GCodeReader &self, const Slic3r::GCodeReader::GCodeLine &line) {
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

    DynamicPrintConfig config = Slic3r::DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
	    { "nozzle_diameter", "0.6,0.6,0.6,0.6" },
	    { "start_gcode", "" },
        { "retract_lift", "3,4" },
    });

    config.set_deserialize_strict({
	    { "retract_lift_above", "0, 0" },
	    { "retract_lift_below", "0, 0" },
    });
    std::vector<double> lift_layers = get_lift_layers(config);
    INFO("lift takes place when above/below == 0");
    CHECK(!lift_layers.empty());

    config.set_deserialize_strict({
	    { "retract_lift_above", "5, 6" },
	    { "retract_lift_below", "15, 13" },
    });
    lift_layers = get_lift_layers(config);
    INFO("lift takes place when above/below != 0");
    CHECK(!lift_layers.empty());

    double retract_lift_above = config.option<ConfigOptionFloats>("retract_lift_above")->get_at(0);
    double retract_lift_below = config.option<ConfigOptionFloats>("retract_lift_below")->get_at(0);

    INFO("Z is not lifted above/below the configured value");
    CHECK(values_are_in_range(lift_layers, retract_lift_above, retract_lift_below));

    // check lifting with different values for 2. extruder
    config.set_deserialize_strict({
        {"perimeter_extruder", 2},
        {"infill_extruder", 2},
        {"retract_lift_above", "0, 0"},
        {"retract_lift_below", "0, 0"}
    });

    lift_layers = get_lift_layers(config);
    INFO("lift takes place when above/below == 0  for 2. extruder");
    CHECK(!lift_layers.empty());

    config.set_deserialize_strict({
	    { "retract_lift_above", "5, 6" },
	    { "retract_lift_below", "15, 13" },
    });
    lift_layers = get_lift_layers(config);
    INFO("lift takes place when above/below != 0 for 2. extruder");
    CHECK(!lift_layers.empty());

    retract_lift_above = config.option<ConfigOptionFloats>("retract_lift_above")->get_at(1);
    retract_lift_below = config.option<ConfigOptionFloats>("retract_lift_below")->get_at(1);

    INFO("Z is not lifted above/below the configured value for 2. extruder");
    CHECK(values_are_in_range(lift_layers, retract_lift_above, retract_lift_below));
}
