#include <catch2/catch_test_macros.hpp>

#include "libslic3r/GCodeReader.hpp"
#include "libslic3r/Geometry/ConvexHull.hpp"
#include "libslic3r/Layer.hpp"

#include "test_data.hpp" // get access to init_print, etc

using namespace Slic3r::Test;
using namespace Slic3r;

SCENARIO("Gaps", "[Gaps]") {
    GIVEN("Two hollow squares") {
        auto config = Slic3r::DynamicPrintConfig::full_print_config_with({
            { "skirts",                         0 },
            { "perimeter_speed",                66 },
            { "external_perimeter_speed",       66 },
            { "small_perimeter_speed",          66 },
            { "gap_fill_speed",                 99 },
            { "perimeters",                     1 },
            // to prevent speeds from being altered
            { "cooling",                        0 },
            // to prevent speeds from being altered
            { "first_layer_speed",              "100%" },
            { "perimeter_extrusion_width",      0.35 },
            { "first_layer_extrusion_width",    0.35 }
        });
    
        GCodeReader parser;
        const double perimeter_speed = config.opt_float("perimeter_speed") * 60;
        const double gap_fill_speed  = config.opt_float("gap_fill_speed") * 60;
        std::string  last; // perimeter or gap
        Points       perimeter_points;
        int          gap_fills_outside_last_perimeters = 0;
        parser.parse_buffer(
            Slic3r::Test::slice({ Slic3r::Test::TestMesh::two_hollow_squares }, config),
            [&perimeter_points, &gap_fills_outside_last_perimeters, &last, perimeter_speed, gap_fill_speed]
                (Slic3r::GCodeReader &self, const Slic3r::GCodeReader::GCodeLine &line)
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
                    if (! convex_hull.contains(point))
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
