/**
* Ported from t/layers.t
*/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <span>
#include "test_data.hpp"
#include "Slic3r/Biz/GCodeReader/GCodeReader.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;
using namespace Catch;
using Biz::GCodeReader::GCodeReader;
using Domain::FloatOrPercentage;
using Domain::Percentage;

void check_layers(const TestConfig& config) {
	GCodeReader parser;
    std::string gcode = Slic3r::Test::slice({TestMesh::cube_20x20x20}, config);

    std::vector<double> z;
    std::vector<double> increments;

    parser.parse_buffer(gcode, [&] (GCodeReader &self, const GCodeReader::GCodeLine &line) {
        if (line.has_z()) {
            z.emplace_back(line.z());
            increments.emplace_back(line.dist_Z(self));
        }
    });

    const double first_layer_height = config.print.items.opt("first_layer_height").get<Domain::FloatOrPercentage>().float_value();
    const double z_offset = config.printer.items.opt("z_offset").get<double>();
    const double layer_height = config.print.items.opt("layer_height").get<double>();
    INFO("Correct first layer height.");
    CHECK(z.at(0) == Approx(first_layer_height + z_offset));
    INFO("Correct second layer height");
    CHECK(z.at(1) == Approx(first_layer_height + layer_height + z_offset));

    INFO("Correct layer height");
    for (const double increment : std::span{increments}.subspan(1)) {
        CHECK(increment == Approx(layer_height));
    }
}

TEST_CASE("Layer heights are correct", "[Layers]") {
    TestConfig config;
    config.printer.items.opt("start_gcode").set("" );
    config.print.items.opt("layer_height").set(0.3);
    config.print.items.opt("first_layer_height").set(FloatOrPercentage{0.2});
    config.print.items.opt("retract_length").set(0.0);

    SECTION("Absolute first layer height") {
        check_layers(config);
    }

    SECTION("Relative layer height") {
        const double layer_height = config.print.items.opt("layer_height").get<double>();
        config.print.items.opt("first_layer_height").set(FloatOrPercentage{0.6 * layer_height});
        check_layers(config);
    }

    SECTION("Positive z offset") {
        config.printer.items.opt("z_offset").set(0.9);
        check_layers(config);
    }

    SECTION("Negative z offset") {
        config.printer.items.opt("z_offset").set(-0.8);
        check_layers(config);
    }
}

TEST_CASE("GCode has reasonable height", "[Layers]") {
    TestConfig config;
    config.print.items.opt("fill_density").set(Percentage{0});

    Print print;
    Domain::Model model;
    Domain::TriangleMesh test_mesh{mesh(TestMesh::cube_20x20x20)};
    test_mesh.scale(2);
    Test::init_print({test_mesh}, print, model, config);
    const std::string gcode{Test::gcode(print)};

    std::vector<double> z;

	GCodeReader parser;
    parser.parse_buffer(gcode, [&] (GCodeReader &self, const GCodeReader::GCodeLine &line) {
        if (line.dist_Z(self) != Approx(0)) {
            z.emplace_back(line.z());
        }
    });

    REQUIRE(!z.empty());
    INFO("Last Z is: " + std::to_string(z.back()));
    CHECK((z.back() > 20*1.8 && z.back() < 20*2.2));
}
