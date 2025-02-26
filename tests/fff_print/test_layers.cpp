/**
* Ported from t/layers.t
*/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "test_data.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;
using namespace Catch;

void check_layers(const DynamicPrintConfig& config) {
	GCodeReader parser;
    std::string gcode = Slic3r::Test::slice({TestMesh::cube_20x20x20}, config);

    std::vector<double> z;
    std::vector<double> increments;

    parser.parse_buffer(gcode, [&] (Slic3r::GCodeReader &self, const Slic3r::GCodeReader::GCodeLine &line) {
        if (line.has_z()) {
            z.emplace_back(line.z());
            increments.emplace_back(line.dist_Z(self));
        }
    });

    const double first_layer_height = config.opt_float("first_layer_height");
    const double z_offset = config.opt_float("z_offset");
    const double layer_height = config.opt_float("layer_height");
    INFO("Correct first layer height.");
    CHECK(z.at(0) == Approx(first_layer_height + z_offset));
    INFO("Correct second layer height");
    CHECK(z.at(1) == Approx(first_layer_height + layer_height + z_offset));

    INFO("Correct layer height");
    for (const double increment : tcb::span{increments}.subspan(1)) {
        CHECK(increment == Approx(layer_height));
    }
}

TEST_CASE("Layer heights are correct", "[Layers]") {
    DynamicPrintConfig config = Slic3r::DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
        { "start_gcode", "" },
        { "layer_height", 0.3 },
        { "first_layer_height", 0.2 },
        { "retract_length", "0" }
    });

    SECTION("Absolute first layer height") {
        check_layers(config);
    }

    SECTION("Relative layer height") {
        const double layer_height = config.opt_float("layer_height");
        config.set_deserialize_strict({
            { "first_layer_height", 0.6 * layer_height },
        });

        check_layers(config);
    }

    SECTION("Positive z offset") {
        config.set_deserialize_strict({
            { "z_offset", 0.9 },
        });

        check_layers(config);
    }

    SECTION("Negative z offset") {
        config.set_deserialize_strict({
            { "z_offset", -0.8 },
        });

        check_layers(config);
    }
}

TEST_CASE("GCode has reasonable height", "[Layers]") {
    DynamicPrintConfig config = Slic3r::DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
        { "fill_density", 0 },
        { "gcode_binary", 0 },
    });

    Print print;
    Model model;
    TriangleMesh test_mesh{mesh(TestMesh::cube_20x20x20)};
    test_mesh.scale(2);
    Test::init_print({test_mesh}, print, model, config);
    const std::string gcode{Test::gcode(print)};

    std::vector<double> z;

	GCodeReader parser;
    parser.parse_buffer(gcode, [&] (Slic3r::GCodeReader &self, const Slic3r::GCodeReader::GCodeLine &line) {
        if (line.dist_Z(self) != Approx(0)) {
            z.emplace_back(line.z());
        }
    });

    REQUIRE(!z.empty());
    INFO("Last Z is: " + std::to_string(z.back()));
    CHECK((z.back() > 20*1.8 && z.back() < 20*2.2));
}
