#include <catch2/catch_test_macros.hpp>

#include "test_data.hpp"
#include "libslic3r/GCodeReader.hpp"

using namespace Slic3r;

SCENARIO("Avoid crossing perimeters", "[AvoidCrossingPerimeters]") {
    WHEN("Two 20mm cubes sliced") {
        std::string gcode = Slic3r::Test::slice(
            { Slic3r::Test::TestMesh::cube_20x20x20, Slic3r::Test::TestMesh::cube_20x20x20 },
            { { "avoid_crossing_perimeters", true } });
        THEN("gcode not empty") {
            REQUIRE(! gcode.empty());
        }
    }
}

SCENARIO("Minimize travel over printed areas", "[PrintedAreaTravel]") {
    WHEN("A travel can follow a printed boundary") {
        const auto config = Slic3r::DynamicPrintConfig::full_print_config_with(
            {{"perimeters", 3}, {"fill_density", 15}, {"skirts", 0},
             {"layer_height", 0.4}, {"first_layer_height", 0.4}}
        );
        const TriangleMesh test_mesh = Slic3r::Test::mesh(
            Slic3r::Test::TestMesh::cube_with_hole, Vec3d::Zero(), Vec3d(1., 1., 0.08));
        std::string direct_gcode =
            Slic3r::Test::slice({test_mesh}, config);

        auto minimized_config = config;
        minimized_config
            .set_key_value("avoid_crossing_printed_areas", new Slic3r::ConfigOptionBool(true));
        std::string minimized_gcode =
            Slic3r::Test::slice({test_mesh}, minimized_config);

        auto xy_travel_segments = [](const std::string &gcode) {
            size_t segments = 0;
            Slic3r::GCodeReader parser;
            parser.parse_buffer(
                gcode,
                [&segments](Slic3r::GCodeReader &reader, const Slic3r::GCodeReader::GCodeLine &line) {
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
            fallback_config.set_deserialize_strict(
                {{"avoid_crossing_perimeters_max_detour", 0.01}});
            const std::string fallback_gcode =
                Slic3r::Test::slice({test_mesh}, fallback_config);
            const std::string cooldown =
                "M106 S255\nG4 S2 ; cool freshly printed travel route\n";
            REQUIRE(fallback_gcode.find(cooldown) != std::string::npos);
        }
    }

    WHEN("A brim move would shortcut across an object footprint") {
        auto config = Slic3r::DynamicPrintConfig::full_print_config_with(
            {{"perimeters", 2}, {"fill_density", 15}, {"skirts", 0},
             {"brim_width", 5}, {"layer_height", 0.4}, {"first_layer_height", 0.4}}
        );
        config.set_deserialize_strict({{"brim_type", "outer_and_inner"}});
        const TriangleMesh test_mesh = Slic3r::Test::mesh(
            Slic3r::Test::TestMesh::cube_with_hole, Vec3d::Zero(), Vec3d(1., 1., 0.08));

        const std::string direct_gcode = Slic3r::Test::slice({test_mesh}, config);
        config.set_key_value(
            "avoid_crossing_printed_areas", new Slic3r::ConfigOptionBool(true));
        const std::string minimized_gcode = Slic3r::Test::slice({test_mesh}, config);

        auto brim_travel_segments = [](const std::string &gcode) {
            const size_t begin = gcode.find(";TYPE:Skirt/Brim");
            if (begin == std::string::npos)
                return size_t(0);
            const size_t end = gcode.find(";TYPE:", begin + 1);
            const std::string brim_gcode = gcode.substr(begin, end - begin);

            size_t segments = 0;
            Slic3r::GCodeReader parser;
            parser.parse_buffer(
                brim_gcode,
                [&segments](Slic3r::GCodeReader &reader, const Slic3r::GCodeReader::GCodeLine &line) {
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
