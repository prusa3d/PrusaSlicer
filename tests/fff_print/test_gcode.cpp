/**
 * Mostly ported from t/gcode.t
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <memory>
#include <regex>
#include <fstream>

#include "libslic3r/GCode.hpp"
#include "libslic3r/Geometry/ConvexHull.hpp"
#include "test_data.hpp"

using namespace Slic3r;
using namespace Test;
using namespace Catch;

constexpr bool debug_files = false;

SCENARIO("Origin manipulation", "[GCode]") {
	Slic3r::GCodeGenerator gcodegen;
	WHEN("set_origin to (10,0)") {
    	gcodegen.set_origin(Vec2d(10,0));
    	REQUIRE(gcodegen.origin() == Vec2d(10, 0));
    }
	WHEN("set_origin to (10,0) and translate by (5, 5)") {
		gcodegen.set_origin(Vec2d(10,0));
		gcodegen.set_origin(gcodegen.origin() + Vec2d(5, 5));
		THEN("origin returns reference to point") {
    		REQUIRE(gcodegen.origin() == Vec2d(15,5));
    	}
    }
}


TEST_CASE("Wiping speeds", "[GCode]") {
    DynamicPrintConfig config = Slic3r::DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
	    { "wipe", "1" },
        { "retract_layer_change", "0" },
    });
    bool have_wipe = false;
    std::vector<double> retract_speeds;
    bool extruded_on_this_layer = false;
    bool wiping_on_new_layer = false;

	GCodeReader parser;
    std::string gcode = Slic3r::Test::slice({TestMesh::cube_20x20x20}, config);
    parser.parse_buffer(gcode, [&] (Slic3r::GCodeReader &self, const Slic3r::GCodeReader::GCodeLine &line) {
        if (line.travel() && line.dist_Z(self) != 0) {
            extruded_on_this_layer = false;
        } else if (line.extruding(self) && line.dist_XY(self) > 0) {
            extruded_on_this_layer = true;
        } else if (line.retracting(self) && line.dist_XY(self) > 0) {
            have_wipe = true;
            wiping_on_new_layer = !extruded_on_this_layer;
            const double f = line.has_f() ? line.f() : self.f();
            double move_time = line.dist_XY(self) / f;
            retract_speeds.emplace_back(std::abs(line.dist_E(self)) / move_time);
        }
    });
    CHECK(have_wipe);
    double expected_retract_speed = config.option<ConfigOptionFloats>("retract_speed")->get_at(0) * 60;
    for (const double retract_speed : retract_speeds) {
        INFO("Wipe moves don\'t retract faster than configured speed");
        CHECK(retract_speed < expected_retract_speed);
    }
    INFO("No wiping after layer change");
    CHECK(!wiping_on_new_layer);
}

bool has_moves_below_z_offset(const DynamicPrintConfig& config) {
	GCodeReader parser;
    std::string gcode = Slic3r::Test::slice({TestMesh::cube_20x20x20}, config);

    unsigned moves_below_z_offset{};
    double configured_offset = config.opt_float("z_offset");
    parser.parse_buffer(gcode, [&] (Slic3r::GCodeReader &self, const Slic3r::GCodeReader::GCodeLine &line) {
        if (line.travel() && line.has_z() && line.z() < configured_offset) {
            moves_below_z_offset++;
        }
    });
    return moves_below_z_offset > 0;
}

TEST_CASE("Z moves with offset", "[GCode]") {
    DynamicPrintConfig config = Slic3r::DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
	    { "z_offset", 5 },
        { "start_gcode", "" },
    });

    INFO("No lift");
    CHECK(!has_moves_below_z_offset(config));

    config.set_deserialize_strict({{ "retract_lift", "3" }});
    INFO("Lift < z offset");
    CHECK(!has_moves_below_z_offset(config));

    config.set_deserialize_strict({{ "retract_lift", "6" }});
    INFO("Lift > z offset");
    CHECK(!has_moves_below_z_offset(config));
}

std::optional<double> parse_axis(const std::string& line, const std::string& axis) {
    std::smatch matches;
    if (std::regex_search(line, matches, std::regex{axis + "(\\d+)"})) {
        std::string matchedValue = matches[1].str();
        return std::stod(matchedValue);
    }
    return std::nullopt;
}

/**
* This tests the following behavior:
* - complete objects does not crash
* - no hard-coded "E" are generated
* - Z moves are correctly generated for both objects
* - no travel moves go outside skirt
* - temperatures are set correctly
*/
TEST_CASE("Extrusion, travels, temperatures", "[GCode]") {
    DynamicPrintConfig config = Slic3r::DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
        { "gcode_comments", 1 },
        { "complete_objects", 1 },
        { "extrusion_axis", "A" },
        { "start_gcode", "" },  // prevent any default extra Z move
        { "layer_height", 0.4 },
        { "first_layer_height", 0.4 },
        { "temperature", "200" },
        { "first_layer_temperature", "210" },
        { "retract_length", "0" }
    });

    std::vector<double> z_moves;
    Points travel_moves;
    Points extrusions;
    std::vector<double> temps;

	GCodeReader parser;

    Print print;
    Model model;
    Test::init_print({TestMesh::cube_20x20x20}, print, model, config, false, 2);
    std::string gcode = Test::gcode(print);

    if constexpr (debug_files) {
        std::ofstream gcode_file{"sequential_print.gcode"};
        gcode_file << gcode;
    }

    parser.parse_buffer(gcode, [&] (Slic3r::GCodeReader &self, const Slic3r::GCodeReader::GCodeLine &line) {
        INFO("Unexpected E argument");
        CHECK(!line.has_e());

        if (line.has_z() && std::abs(line.dist_Z(self)) > 0) {
            z_moves.emplace_back(line.z());
        }
        if (line.has_x() || line.has_y()) {
            if (line.extruding(self) || line.has_unknown_axis()) {
                extrusions.emplace_back(scaled(line.x()), scaled(line.y()));
            } else if (!extrusions.empty()){ // skip initial travel move to first skirt point
                travel_moves.emplace_back(scaled(line.x()), scaled(line.y()));
            }
        } else if (line.cmd_is("M104") || line.cmd_is("M109")) {
            const std::optional<double> parsed_temperature = parse_axis(line.raw(), "S");
            if (!parsed_temperature) {
                FAIL("Failed to parse temperature!");
            }
            if (temps.empty() || temps.back() != parsed_temperature) {
                temps.emplace_back(*parsed_temperature);
            }
        }
    });

    // Remove last travel_moves returning to origin
    if (travel_moves.back().x() == 0 && travel_moves.back().y() == 0) {
        travel_moves.pop_back();
    }

    const unsigned layer_count = 20 / 0.4;
    INFO("Complete_objects generates the correct number of Z moves.");
    CHECK(z_moves.size() == layer_count * 2);
    auto first_moves = tcb::span{z_moves}.subspan(0, layer_count);
    auto second_moves = tcb::span{z_moves}.subspan(layer_count);

    CHECK( std::vector(first_moves.begin(), first_moves.end()) == std::vector(second_moves.begin(), second_moves.end()));
    const Polygon convex_hull{Geometry::convex_hull(extrusions)};
    INFO("All travel moves happen within skirt.");
    for (const Point& travel_move : travel_moves) {
        CHECK(convex_hull.contains(travel_move));
    }
    INFO("Expected temperature changes");
    CHECK(temps == std::vector<double>{210, 200, 210, 200, 0});
}


TEST_CASE("Used filament", "[GCode]") {
    DynamicPrintConfig config1 = Slic3r::DynamicPrintConfig::full_print_config();
    config1.set_deserialize_strict({
        { "retract_length", "0" },
        { "use_relative_e_distances", 1 },
        { "layer_gcode", "G92 E0\n" },
    });
    Print print1;
    Model model1;
    Test::init_print({TestMesh::cube_20x20x20}, print1, model1, config1);
    Test::gcode(print1);

    DynamicPrintConfig config2 = Slic3r::DynamicPrintConfig::full_print_config();
    config2.set_deserialize_strict({
        { "retract_length", "999" },
        { "use_relative_e_distances", 1 },
        { "layer_gcode", "G92 E0\n" },
    });
    Print print2;
    Model model2;
    Test::init_print({TestMesh::cube_20x20x20}, print2, model2, config2);
    Test::gcode(print2);

    INFO("Final retraction is not considered in total used filament");
    CHECK(print1.print_statistics().total_used_filament == print2.print_statistics().total_used_filament);
}

void check_m73s(Print& print){
    std::vector<double> percent{};
    bool got_100 = false;
    bool extruding_after_100 = 0;

	GCodeReader parser;
    std::string gcode = Slic3r::Test::gcode(print);
    parser.parse_buffer(gcode, [&] (Slic3r::GCodeReader &self, const Slic3r::GCodeReader::GCodeLine &line) {

        if (line.cmd_is("M73")) {
            std::optional<double> p = parse_axis(line.raw(), "P");
            if (!p) {
                FAIL("Failed to parse percent");
            }
            percent.emplace_back(*p);
            got_100 = p == Approx(100);
        }
        if (line.extruding(self) && got_100) {
            extruding_after_100 = true;
        }
    });
    INFO("M73 is never given more than 100%");
    for (const double value : percent) {
        CHECK(value <= 100);
    }
    INFO("No extrusions after M73 P100.");
    CHECK(!extruding_after_100);
}


TEST_CASE("M73s have correct percent values", "[GCode]") {
    DynamicPrintConfig config = Slic3r::DynamicPrintConfig::full_print_config();

    SECTION("Single object") {
        config.set_deserialize_strict({
            {" gcode_flavor", "sailfish" },
            {" raft_layers", 3 },
        });

        Print print;
        Model model;
        Test::init_print({TestMesh::cube_20x20x20}, print, model, config);
        check_m73s(print);
    }

    SECTION("Two copies of single object") {
        config.set_deserialize_strict({
            {" gcode_flavor", "sailfish" },
        });
        Print print;
        Model model;

        Test::init_print({TestMesh::cube_20x20x20}, print, model, config, false, 2);
        check_m73s(print);

        if constexpr (debug_files) {
            std::ofstream gcode_file{"M73_2_copies.gcode"};
            gcode_file << Test::gcode(print);
        }
    }

    SECTION("Two objects") {
        config.set_deserialize_strict({
            {" gcode_flavor", "sailfish" },
        });
        Print print;
        Model model;
        Test::init_print({TestMesh::cube_20x20x20, TestMesh::cube_20x20x20}, print, model, config);
        check_m73s(print);
    }

    SECTION("One layer object") {
        config.set_deserialize_strict({
            {" gcode_flavor", "sailfish" },
        });
        Print print;
        Model model;
        TriangleMesh test_mesh{mesh(TestMesh::cube_20x20x20)};
        const auto layer_height = static_cast<float>(config.opt_float("layer_height"));
        test_mesh.scale(Vec3f{1.0F, 1.0F, layer_height/20.0F});
        Test::init_print({test_mesh}, print, model, config);
        check_m73s(print);

        if constexpr (debug_files) {
            std::ofstream gcode_file{"M73_one_layer.gcode"};
            gcode_file << Test::gcode(print);
        }
    }
}


TEST_CASE("M201 for acceleation reset", "[GCode]") {
    DynamicPrintConfig config = Slic3r::DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
	    { "gcode_flavor", "repetier" },
        { "default_acceleration", 1337 },
    });

	GCodeReader parser;
    std::string gcode = Slic3r::Test::slice({TestMesh::cube_with_hole}, config);

    bool has_accel = false;
    bool has_m204 = false;

    parser.parse_buffer(gcode, [&] (Slic3r::GCodeReader &self, const Slic3r::GCodeReader::GCodeLine &line) {
        if (line.cmd_is("M201") && line.has_x() && line.has_y()) {
            if (line.x() == 1337 && line.y() == 1337) {
                has_accel = true;
            }
        }
        if (line.cmd_is("M204") && line.raw().find('S') != std::string::npos) {
            has_m204 = true;
        }
    });

    INFO("M201 is generated for repetier firmware.");
    CHECK(has_accel);
    INFO("M204 is not generated for repetier firmware");
    CHECK(!has_m204);
}
