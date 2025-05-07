#include <catch2/catch_test_macros.hpp>

#include <numeric>
#include <sstream>

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Geometry/ConvexHull.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/libslic3r.h"

#include "test_data.hpp"

using namespace Slic3r;
using namespace std::literals;

SCENARIO("Basic tests", "[Multi]")
{
    WHEN("Slicing multi-material print with non-consecutive extruders") {
        std::string gcode = Slic3r::Test::slice({ Slic3r::Test::TestMesh::cube_20x20x20 }, 
            {
                { "nozzle_diameter",                "0.6, 0.6, 0.6, 0.6" },
                { "extruder",                       2 },
                { "infill_extruder",                4 },
                { "support_material_extruder",      0 }
            });
        THEN("Sliced successfully") {
            REQUIRE(! gcode.empty());
        }
        THEN("T3 toolchange command found") {
            bool T1_found = gcode.find("\nT3\n") != gcode.npos;
            REQUIRE(T1_found);
        }
    }
    WHEN("Slicing with multiple skirts with a single, non-zero extruder") {
        std::string gcode = Slic3r::Test::slice({ Slic3r::Test::TestMesh::cube_20x20x20 }, 
            {
                { "nozzle_diameter",                        "0.6, 0.6, 0.6, 0.6" },
                { "perimeter_extruder",                     2 },
                { "infill_extruder",                        2 },
                { "support_material_extruder",              2 },
                { "support_material_interface_extruder",    2 },
            });
        THEN("Sliced successfully") {
            REQUIRE(! gcode.empty());
        }
    }
}

SCENARIO("Ooze prevention", "[Multi]")
{
    DynamicPrintConfig config = Slic3r::DynamicPrintConfig::full_print_config_with({
        { "nozzle_diameter",                "0.6, 0.6, 0.6, 0.6" },
        { "raft_layers",                    2 },
        { "infill_extruder",                2 },
        { "solid_infill_extruder",          3 },
        { "support_material_extruder",      4 },
        { "ooze_prevention",                1 },
        { "extruder_offset",                "0x0, 20x0, 0x20, 20x20" },
        { "temperature",                    "200, 180, 170, 160" },
        { "first_layer_temperature",        "206, 186, 166, 156" },
        // test that it doesn't crash when this is supplied
        { "toolchange_gcode",               "T[next_extruder] ;toolchange" }
    });
    FullPrintConfig print_config;
    print_config.apply(config);

    // Since July 2019, PrusaSlicer only emits automatic Tn command in case that the toolchange_gcode is empty
    // The "T[next_extruder]" is therefore needed in this test.
    
    std::string gcode = Slic3r::Test::slice({ Slic3r::Test::TestMesh::cube_20x20x20 }, config);

    GCodeReader parser;
    int         tool = -1;
    int         tool_temp[] = { 0, 0, 0, 0};
    Points      toolchange_points;
    Points      extrusion_points;
    parser.parse_buffer(gcode, [&tool, &tool_temp, &toolchange_points, &extrusion_points, &print_config]
        (Slic3r::GCodeReader &self, const Slic3r::GCodeReader::GCodeLine &line)
    {
        // if the command is a T command, set the the current tool
        if (boost::starts_with(line.cmd(), "T")) {
            // Ignore initial toolchange.
            if (tool != -1) {
                int expected_temp = is_approx<double>(self.z(), print_config.get_abs_value("first_layer_height") + print_config.z_offset) ?
                    print_config.first_layer_temperature.get_at(tool) :
                    print_config.temperature.get_at(tool);
                if (tool_temp[tool] != expected_temp + print_config.standby_temperature_delta)
                    throw std::runtime_error("Standby temperature was not set before toolchange.");
                toolchange_points.emplace_back(self.xy_scaled());
            }
            tool = atoi(line.cmd().data() + 1);
        } else if (line.cmd_is("M104") || line.cmd_is("M109")) {
            // May not be defined on this line.
            int t = tool;
            line.has_value('T', t);
            // Should be available on this line.
            int s;
            if (! line.has_value('S', s))
                throw std::runtime_error("M104 or M109 without S");

            // Following is obsolete. The first printing extruder is newly set to its first layer temperature immediately, not to the standby.
            //if (tool_temp[t] == 0 && s != print_config.first_layer_temperature.get_at(t) + print_config.standby_temperature_delta)
            //    throw std::runtime_error("initial temperature is not equal to first layer temperature + standby delta");

            tool_temp[t] = s;
        } else if (line.cmd_is("G1") && line.extruding(self) && line.dist_XY(self) > 0) {
            extrusion_points.emplace_back(line.new_XY_scaled(self) + scaled<coord_t>(print_config.extruder_offset.get_at(tool)));
        }
    });

    Polygon convex_hull = Geometry::convex_hull(extrusion_points);
    
    // THEN("all nozzles are outside skirt at toolchange") {
    //     Points t;
    //     sort_remove_duplicates(toolchange_points);
    //     size_t inside = 0;
    //     for (const auto &point : toolchange_points)
    //         for (const Vec2d &offset : print_config.extruder_offset.values) {
    //             Point p = point + scaled<coord_t>(offset);
    //             if (convex_hull.contains(p))
    //                 ++ inside;
    //         }
    //     REQUIRE(inside == 0);
    // }

#if 0
    require "Slic3r/SVG.pm";
    Slic3r::SVG::output(
        "ooze_prevention_test.svg",
        no_arrows   => 1,
        polygons    => [$convex_hull],
        red_points  => \@t,
        points      => \@toolchange_points,
    );
#endif
    
    THEN("all toolchanges happen within expected area") {
        // offset the skirt by the maximum displacement between extruders plus a safety extra margin
        const float delta = scaled<float>(20. * sqrt(2.) + 1.);
        Polygon outer_convex_hull = expand(convex_hull, delta).front();
        size_t inside = std::count_if(toolchange_points.begin(), toolchange_points.end(), [&outer_convex_hull](const Point &p){ return outer_convex_hull.contains(p); });
        REQUIRE(inside == toolchange_points.size());
    }
}

std::string slice_stacked_cubes(const DynamicPrintConfig &config, const DynamicPrintConfig &volume1config, const DynamicPrintConfig &volume2config)
{
    Model        model;
    ModelObject *object = model.add_object();
    object->name = "object.stl";
    ModelVolume *v1 = object->add_volume(Test::mesh(Test::TestMesh::cube_20x20x20));
    v1->set_material_id("lower_material");
    v1->config.assign_config(volume1config);
    ModelVolume *v2 = object->add_volume(Test::mesh(Test::TestMesh::cube_20x20x20));
    v2->set_material_id("upper_material");
    v2->translate(0., 0., 20.);
    v2->config.assign_config(volume2config);
    object->add_instance();
    object->ensure_on_bed();
    Print print;
    print.auto_assign_extruders(object);
    THEN("auto_assign_extruders() assigned correct extruder to first volume") {
        REQUIRE(v1->config.extruder() == 1);
    }
    THEN("auto_assign_extruders() assigned correct extruder to second volume") {
        REQUIRE(v2->config.extruder() == 2);
    }
    print.apply(model, config);
    print.validate();
    return Test::gcode(print);
}

SCENARIO("Stacked cubes", "[Multi]")
{
    DynamicPrintConfig lower_config;
    lower_config.set_deserialize_strict({
        { "extruder",               1 },
        { "bottom_solid_layers",    0 },
        { "top_solid_layers",       1 },
    });

    DynamicPrintConfig upper_config;
    upper_config.set_deserialize_strict({
        { "extruder",               2 },
        { "bottom_solid_layers",    1 },
        { "top_solid_layers",       0 }
    });

    static constexpr const double solid_infill_speed = 99;
    auto config = Slic3r::DynamicPrintConfig::full_print_config_with({
        { "nozzle_diameter",        "0.6, 0.6, 0.6, 0.6" },
        { "fill_density",           0 },
        { "solid_infill_speed",     solid_infill_speed },
        { "top_solid_infill_speed", solid_infill_speed },
        // for preventing speeds from being altered
        { "cooling",                "0, 0, 0, 0" },
        // for preventing speeds from being altered
        { "first_layer_speed",      "100%" }
    });

    auto test_shells = [](const std::string &gcode) {
        GCodeReader       parser;
        int               tool = -1;
        // Scaled Z heights.
        std::set<coord_t> T0_shells, T1_shells;
        parser.parse_buffer(gcode, [&tool, &T0_shells, &T1_shells]
            (Slic3r::GCodeReader &self, const Slic3r::GCodeReader::GCodeLine &line)
        {
            if (boost::starts_with(line.cmd(), "T")) {
                tool = atoi(line.cmd().data() + 1);
            } else if (line.cmd() == "G1" && line.extruding(self) && line.dist_XY(self) > 0) {
                if (is_approx<double>(line.new_F(self), solid_infill_speed * 60.) && (tool == 0 || tool == 1))
                    (tool == 0 ? T0_shells : T1_shells).insert(scaled<coord_t>(self.z()));
            }
        });
        return std::make_pair(T0_shells, T1_shells);
    };
    
    WHEN("Interface shells disabled") {
        std::string gcode = slice_stacked_cubes(config, lower_config, upper_config);
        auto [t0, t1] = test_shells(gcode);
        THEN("no interface shells") {
            REQUIRE(t0.empty());
            REQUIRE(t1.empty());
        }
    }
    WHEN("Interface shells enabled") {
        config.set_deserialize_strict("interface_shells", "1");
        std::string gcode = slice_stacked_cubes(config, lower_config, upper_config);
        auto [t0, t1] = test_shells(gcode);
        THEN("top interface shells") {
            REQUIRE(t0.size() == lower_config.opt_int("top_solid_layers"));
        }
        THEN("bottom interface shells") {
            REQUIRE(t1.size() == upper_config.opt_int("bottom_solid_layers"));
        }
    }
    WHEN("Slicing with auto-assigned extruders") {
        auto config = Slic3r::DynamicPrintConfig::full_print_config_with({
            { "nozzle_diameter",        "0.6,0.6,0.6,0.6" },
            { "layer_height",           0.4 },
            { "first_layer_height",     0.4 },
            { "skirts",                 0 }
        });
        std::string gcode = slice_stacked_cubes(config, DynamicPrintConfig{}, DynamicPrintConfig{});
        GCodeReader       parser;
        int               tool = -1;
        // Scaled Z heights.
        std::set<coord_t> T0_shells, T1_shells;
        parser.parse_buffer(gcode, [&tool, &T0_shells, &T1_shells](Slic3r::GCodeReader &self, const Slic3r::GCodeReader::GCodeLine &line)
        {
            if (boost::starts_with(line.cmd(), "T")) {
                tool = atoi(line.cmd().data() + 1);
            } else if (line.cmd() == "G1" && line.extruding(self) && line.dist_XY(self) > 0) {
                if (tool == 0 && self.z() > 20)
                    // Layers incorrectly extruded with T0 at the top object.
                    T0_shells.insert(scaled<coord_t>(self.z()));
                else if (tool == 1 && self.z() < 20)
                    // Layers incorrectly extruded with T1 at the bottom object.
                    T1_shells.insert(scaled<coord_t>(self.z()));
            }
        });
        THEN("T0 is never used for upper object") {
            REQUIRE(T0_shells.empty());
        }
        THEN("T0 is never used for lower object") {
            REQUIRE(T1_shells.empty());
        }
    }
}
