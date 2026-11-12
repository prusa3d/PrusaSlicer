#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Biz/Algorithms/Polygon.hpp"
#include "Slic3r/Biz/GCodeReader/GCodeReader.hpp"
#include "libslic3r/Geometry/ConvexHull.hpp"

#include "test_data.hpp" // get access to init_print, etc

using namespace Slic3r::Test;
using namespace Slic3r;
using Biz::GCodeReader::GCodeReader;
using Domain::FloatOrPercentage;
using Domain::Percentage;

SCENARIO("Gaps", "[Gaps]") {
    GIVEN("Two hollow squares") {
        TestConfig config;
        config.print.items.opt("skirts").set(0);
        config.print.items.opt("perimeter_speed").set(66.0);
        config.print.items.opt("external_perimeter_speed").set(FloatOrPercentage{66.0});
        config.print.items.opt("small_perimeter_speed").set(FloatOrPercentage{66.0});
        config.print.items.opt("gap_fill_speed").set(99.0);
        config.print.items.opt("perimeters").set(1);
                    // to prevent speeds from being altered
        config.filament[0].items.opt("cooling").set(false);
                    // to prevent speeds from being altered
        config.print.items.opt("first_layer_speed").set(FloatOrPercentage{Percentage{100}});
        config.print.items.opt("perimeter_extrusion_width").set(FloatOrPercentage{0.35});
        config.print.items.opt("first_layer_extrusion_width").set(FloatOrPercentage{0.35});

        GCodeReader parser;
        const double perimeter_speed = config.print.items.opt("perimeter_speed").get<double>() * 60;
        const double gap_fill_speed  = config.print.items.opt("gap_fill_speed").get<double>() * 60;
        std::string  last; // perimeter or gap
        Points       perimeter_points;
        int          gap_fills_outside_last_perimeters = 0;
        parser.parse_buffer(
            Slic3r::Test::slice({ Slic3r::Test::TestMesh::two_hollow_squares }, config),
            [&perimeter_points, &gap_fills_outside_last_perimeters, &last, perimeter_speed, gap_fill_speed]
                (GCodeReader &self, const GCodeReader::GCodeLine &line)
        {
            if (line.extruding(self) && line.dist_XY(self) > 0) {
                double f = line.new_F(self);
                Point point = line.new_XY_scaled(self);
                if (is_approx(f, perimeter_speed)) {
                    if (last == "gap")
                        perimeter_points.clear();
                    perimeter_points.emplace_back(point);
                    last = "perimeter";
                } else if (is_approx(f, gap_fill_speed)) {
                    Polygon convex_hull = Geometry::convex_hull(perimeter_points);
                    if (! Slic3r::Biz::Algorithms::Polygon::contains(convex_hull, point))
                        ++ gap_fills_outside_last_perimeters;
                    last = "gap";
                }
            }
        });
        THEN("gap fills are printed before leaving islands") {
            REQUIRE(gap_fills_outside_last_perimeters == 0);
        }
    }
}
