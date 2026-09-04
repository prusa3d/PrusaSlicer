/**
 * Mostly ported from t/gcode.t
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <memory>
#include <regex>
#include <fstream>
#include <span>
#include <vector>

#include "Slic3r/Biz/Algorithms/Polygon.hpp"
#include "libslic3r/GCode.hpp"
#include "libslic3r/Geometry/ConvexHull.hpp"
#include "test_data.hpp"
#include <boost/lexical_cast.hpp>

using namespace Slic3r;
using namespace Test;
using namespace Catch;
using Domain::FloatOrPercentage;
using Domain::Percentage;

using Biz::GCodeReader::GCodeReader;

constexpr bool debug_files = false;

TEST_CASE("Origin manipulation", "[GCode]") {
    Print print;
    TestConfig test_config;
    print.set_config(test_config.get_view());
	Slic3r::GCodeGenerator gcodegen{&print};
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

    TestConfig config;
    config.print.items.opt("wipe").set(true);
    config.print.items.opt("retract_layer_change").set(false);

    bool have_wipe = false;
    std::vector<double> retract_speeds;
    bool extruded_on_this_layer = false;
    bool wiping_on_new_layer = false;

	GCodeReader parser;
    std::string gcode = Slic3r::Test::slice({TestMesh::cube_20x20x20}, config);
    parser.parse_buffer(gcode, [&] (GCodeReader &self, const GCodeReader::GCodeLine &line) {
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
    double expected_retract_speed = config.print.items.opt("retract_speed").get<double>() * 60;
    for (const double retract_speed : retract_speeds) {
        INFO("Wipe moves don\'t retract faster than configured speed");
        CHECK(retract_speed < expected_retract_speed);
    }
    INFO("No wiping after layer change");
    CHECK(!wiping_on_new_layer);
}

bool has_moves_below_z_offset(const TestConfig& config) {
	GCodeReader parser;
    std::string gcode = Slic3r::Test::slice({TestMesh::cube_20x20x20}, config);

    unsigned moves_below_z_offset{};
    double configured_offset = config.printer.items.opt("z_offset").get<double>();
    parser.parse_buffer(gcode, [&] (GCodeReader &self, const GCodeReader::GCodeLine &line) {
        if (line.travel() && line.has_z() && line.z() < configured_offset) {
            moves_below_z_offset++;
        }
    });
    return moves_below_z_offset > 0;
}

TEST_CASE("Z moves with offset", "[GCode]") {
    TestConfig config;
    config.printer.items.opt("z_offset").set(5.0);
    config.printer.items.opt("start_gcode").set("");

    INFO("No lift");
    CHECK(!has_moves_below_z_offset(config));

    config.print.items.opt("retract_lift").set(3.0);
    INFO("Lift < z offset");
    CHECK(!has_moves_below_z_offset(config));

    config.print.items.opt("retract_lift").set(6.0);
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
    TestConfig config;
    config.print.items.opt("gcode_comments").set(true);
    config.print.items.opt("complete_objects").set(true );
    config.printer.items.opt("start_gcode").set("");
    config.print.items.opt("layer_height").set(0.4);
    config.print.items.opt("first_layer_height").set(FloatOrPercentage{0.4});
    config.filament.at(0).items.opt("temperature").set(200);
    config.filament.at(0).items.opt("first_layer_temperature").set(210);
    config.print.items.opt("retract_length").set(0.0);

    std::vector<double> z_moves;
    Points travel_moves;
    Points extrusions;
    std::vector<double> temps;

	GCodeReader parser;

    Print print;
    Domain::Model model;
    Test::init_print({TestMesh::cube_20x20x20}, print, model, config, 2);
    std::string gcode = Test::gcode(print);

    if constexpr (debug_files) {
        std::ofstream gcode_file{"sequential_print.gcode"};
        gcode_file << gcode;
    }

    parser.parse_buffer(gcode, [&] (GCodeReader &self, const GCodeReader::GCodeLine &line) {
        if (line.has_z() && std::abs(line.dist_Z(self)) > 0) {
            z_moves.emplace_back(line.z());
        }
        if (line.has_x() || line.has_y()) {
            if (line.extruding(self)) {
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
    auto first_moves = std::span{z_moves}.subspan(0, layer_count);
    auto second_moves = std::span{z_moves}.subspan(layer_count);

    CHECK( std::vector(first_moves.begin(), first_moves.end()) == std::vector(second_moves.begin(), second_moves.end()));
    const Polygon convex_hull{Geometry::convex_hull(extrusions)};
    INFO("All travel moves happen within skirt.");
    for (const Point& travel_move : travel_moves) {
        CHECK(Slic3r::Biz::Algorithms::Polygon::contains(convex_hull, travel_move));
    }
    INFO("Expected temperature changes");
    CHECK(temps == std::vector<double>{210, 200, 210, 200, 0});
}

std::optional<float> parse_option_from_gcode(
    const std::string& key,
    const std::string& gcode
) {
    std::regex re("; " + key + R"(.*= (\d+(\.\d+)?))");
    std::smatch match;
    if (!std::regex_search(gcode, match, re)) {
        return std::nullopt;
    }
    return boost::lexical_cast<float>(match[1].str());
}


TEST_CASE("Used filament", "[GCode]") {
    TestConfig config1;
    config1.print.items.opt("retract_length").set(0.0);
    config1.printer.items.opt("use_relative_e_distances").set(true);
    config1.printer.items.opt("layer_gcode").set("G92 E0\n");
    Print print1;
    Domain::Model model1;
    Test::init_print({TestMesh::cube_20x20x20}, print1, model1, config1);
    std::string gcode1{Test::gcode(print1)};

    TestConfig config2;
    config2.print.items.opt("retract_length").set(999.0);
    config2.printer.items.opt("use_relative_e_distances").set(true);
    config2.printer.items.opt("layer_gcode").set("G92 E0\n");
    Print print2;
    Domain::Model model2;
    Test::init_print({TestMesh::cube_20x20x20}, print2, model2, config2);
    std::string gcode2{Test::gcode(print2)};

    INFO("Final retraction is not considered in total used filament");
    const auto total_used_filament1{parse_option_from_gcode("total filament used \\[g\\]", gcode1)};
    const auto total_used_filament2{parse_option_from_gcode("total filament used", gcode2)};
    REQUIRE(total_used_filament1);
    REQUIRE(total_used_filament2);
    REQUIRE(total_used_filament1 == total_used_filament2);
}

void check_m73s(Print& print){
    std::vector<double> percent{};
    bool got_100 = false;
    bool extruding_after_100 = 0;

	GCodeReader parser;
    std::string gcode = Slic3r::Test::gcode(print);
    parser.parse_buffer(gcode, [&] (GCodeReader &self, const GCodeReader::GCodeLine &line) {

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
    TestConfig config;

    SECTION("Single object") {
        config.printer.items.opt("gcode_flavor").set(Domain::GCodeFlavor::gcfSailfish);
        config.print.items.opt("raft_layers").set(3);

        Print print;
        Domain::Model model;
        Test::init_print({TestMesh::cube_20x20x20}, print, model, config);
        check_m73s(print);
    }

    SECTION("Two copies of single object") {
        config.printer.items.opt("gcode_flavor").set(Domain::GCodeFlavor::gcfSailfish);
        Print print;
        Domain::Model model;

        Test::init_print({TestMesh::cube_20x20x20}, print, model, config, 2);
        check_m73s(print);

        if constexpr (debug_files) {
            std::ofstream gcode_file{"M73_2_copies.gcode"};
            gcode_file << Test::gcode(print);
        }
    }

    SECTION("Two objects") {
        config.printer.items.opt("gcode_flavor").set(Domain::GCodeFlavor::gcfSailfish);
        Print print;
        Domain::Model model;
        Test::init_print({TestMesh::cube_20x20x20, TestMesh::cube_20x20x20}, print, model, config);
        check_m73s(print);
    }

    SECTION("One layer object") {
        config.printer.items.opt("gcode_flavor").set(Domain::GCodeFlavor::gcfSailfish);
        Print print;
        Domain::Model model;
        Domain::TriangleMesh test_mesh{mesh(TestMesh::cube_20x20x20)};
        const auto layer_height = static_cast<float>(config.print.items.opt("layer_height").get<double>());
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
    TestConfig config;
    config.printer.items.opt("gcode_flavor").set(Domain::GCodeFlavor::gcfRepetier);
    config.print.items.opt("default_acceleration").set(1337.0);

	GCodeReader parser;
    std::string gcode = Slic3r::Test::slice({TestMesh::cube_with_hole}, config);

    bool has_accel = false;
    bool has_m204 = false;

    parser.parse_buffer(gcode, [&] (GCodeReader &self, const GCodeReader::GCodeLine &line) {
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

namespace {
std::vector<int> collect_emitted_travel_accelerations(const std::string& gcode)
{
    std::vector<int> emitted_travel_accelerations;

    GCodeReader parser;
    parser.parse_buffer(
        gcode,
        [&emitted_travel_accelerations](GCodeReader& self, const GCodeReader::GCodeLine& line)
        {
            int travel_acceleration = 0;
            if (line.cmd_is("M204") && line.has_value('T', travel_acceleration)) {
                emitted_travel_accelerations.emplace_back(travel_acceleration);
            }
        }
    );

    return emitted_travel_accelerations;
}
} // namespace

TEST_CASE(
    "Travel short distance acceleration is not emitted when travel acceleration is disabled",
    "[GCode]"
)
{
    constexpr int travel_short_distance_acceleration = 250;

    TestConfig config;
    config.printer.items.opt("gcode_flavor").set(Domain::GCodeFlavor::gcfMarlinFirmware);
    config.print.items.opt("retract_before_travel").set(5.0);
    config.print.items.opt("travel_short_distance_acceleration")
        .set(static_cast<double>(travel_short_distance_acceleration));

    WHEN("travel acceleration is enabled")
    {
        config.print.items.opt("travel_acceleration").set(1000.0);

        const std::vector<int> emitted_travel_accelerations{
            collect_emitted_travel_accelerations(slice({TestMesh::cube_with_hole}, config))
        };

        THEN("travel short distance acceleration is emitted")
        {
            CHECK(
                std::ranges::find(emitted_travel_accelerations, travel_short_distance_acceleration)
                != emitted_travel_accelerations.end()
            );
        }
    }

    WHEN("travel acceleration is disabled")
    {
        config.print.items.opt("travel_acceleration").set(0.0);

        const std::vector<int> emitted_travel_accelerations{
            collect_emitted_travel_accelerations(slice({TestMesh::cube_with_hole}, config))
        };

        THEN("travel short distance acceleration is never emitted")
        {
            CHECK(
                std::ranges::find(emitted_travel_accelerations, travel_short_distance_acceleration)
                == emitted_travel_accelerations.end()
            );
        }
    }
}
