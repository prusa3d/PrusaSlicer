#include <catch2/catch_test_macros.hpp>

#include "test_data.hpp"
#include "Slic3r/Biz/GCodeReader/GCodeReader.hpp"

using namespace Slic3r;
using Biz::GCodeReader::GCodeReader;
using Domain::BrimType;
using Domain::FloatOrPercentage;
using Domain::Percentage;
using Domain::TriangleMesh;

SCENARIO("Avoid crossing perimeters", "[AvoidCrossingPerimeters]") {
	WHEN("Two 20mm cubes sliced") {
        Test::TestConfig config;
        config.print.items.opt("avoid_crossing_perimeters").set(true);
        std::string gcode = Slic3r::Test::slice(
            { Slic3r::Test::TestMesh::cube_20x20x20, Slic3r::Test::TestMesh::cube_20x20x20 },
            config
        );
        THEN("gcode not empty") {
            REQUIRE(! gcode.empty());
        }
    }
}

SCENARIO("Minimize travel over printed areas", "[PrintedAreaTravel]") {
    WHEN("A travel can follow a printed boundary") {
        Test::TestConfig direct_config;
        direct_config.print.items.opt("perimeters").set(3);
        direct_config.print.items.opt("fill_density").set(Percentage{15});
        direct_config.print.items.opt("skirts").set(0);
        direct_config.print.items.opt("layer_height").set(0.4);
        direct_config.print.items.opt("first_layer_height").set(FloatOrPercentage{0.4});
        const TriangleMesh test_mesh = Slic3r::Test::mesh(
            Slic3r::Test::TestMesh::cube_with_hole, Vec3d::Zero(), Vec3d(1., 1., 0.08));
        std::string direct_gcode =
            Slic3r::Test::slice({test_mesh}, direct_config);

        auto minimized_config = direct_config;
        minimized_config.print.items.opt("avoid_crossing_printed_areas").set(true);
        std::string minimized_gcode =
            Slic3r::Test::slice({test_mesh}, minimized_config);

        auto xy_travel_segments = [](const std::string &gcode) {
            size_t segments = 0;
            GCodeReader parser;
            parser.parse_buffer(
                gcode,
                [&segments](GCodeReader &reader, const GCodeReader::GCodeLine &line) {
                    if (!line.extruding(reader) && line.dist_XY(reader) > 0.)
                        ++segments;
                }
            );
            return segments;
        };

        THEN("contained travels gain boundary-following segments") {
            REQUIRE(!minimized_gcode.empty());
            REQUIRE(xy_travel_segments(minimized_gcode) > xy_travel_segments(direct_gcode));
        }

        THEN("an unavoidable fresh crossing cools before the next layer") {
            auto fallback_config = minimized_config;
            fallback_config.print.items.opt("avoid_crossing_perimeters_max_detour").set(0.01);
            const std::string fallback_gcode =
                Slic3r::Test::slice({test_mesh}, fallback_config);
            const std::string cooldown =
                "M106 S255\nG4 S2 ; cool freshly printed travel route\n";
            REQUIRE(fallback_gcode.find(cooldown) != std::string::npos);
        }
    }

    WHEN("A brim move would shortcut across an object footprint") {
        Test::TestConfig direct_config;
        direct_config.print.items.opt("perimeters").set(2);
        direct_config.print.items.opt("fill_density").set(Percentage{15});
        direct_config.print.items.opt("skirts").set(0);
        direct_config.print.items.opt("brim_width").set(5.0);
        direct_config.print.items.opt("layer_height").set(0.4);
        direct_config.print.items.opt("first_layer_height").set(FloatOrPercentage{0.4});
        direct_config.print.items.opt("brim_type").set(BrimType::OuterAndInner);
        const TriangleMesh test_mesh = Slic3r::Test::mesh(
            Slic3r::Test::TestMesh::cube_with_hole, Vec3d::Zero(), Vec3d(1., 1., 0.08));

        const std::string direct_gcode = Slic3r::Test::slice({test_mesh}, direct_config);
        auto minimized_config = direct_config;
        minimized_config.print.items.opt("avoid_crossing_printed_areas").set(true);
        const std::string minimized_gcode = Slic3r::Test::slice({test_mesh}, minimized_config);

        auto brim_travel_segments = [](const std::string &gcode) {
            const size_t begin = gcode.find(";TYPE:Skirt/Brim");
            if (begin == std::string::npos)
                return size_t(0);
            const size_t end = gcode.find(";TYPE:", begin + 1);
            const std::string brim_gcode = gcode.substr(begin, end - begin);

            size_t segments = 0;
            GCodeReader parser;
            parser.parse_buffer(
                brim_gcode,
                [&segments](GCodeReader &reader, const GCodeReader::GCodeLine &line) {
                    if (!line.extruding(reader) && line.dist_XY(reader) > 0.)
                        ++segments;
                }
            );
            return segments;
        };

        THEN("the brim travel follows the outer outline") {
            REQUIRE(brim_travel_segments(minimized_gcode) > brim_travel_segments(direct_gcode));
        }
    }
}
