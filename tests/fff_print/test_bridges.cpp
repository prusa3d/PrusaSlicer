#include <catch2/catch_test_macros.hpp>

#include <libslic3r/BridgeDetector.hpp>
#include <libslic3r/Geometry.hpp>

#include "test_data.hpp"

using namespace Slic3r;

SCENARIO("Bridge detector", "[Bridging]") 
{
    auto check_angle = [](const ExPolygons &lower, const ExPolygon &bridge, double expected, double tolerance = -1, double expected_coverage = -1) 
    {
        if (expected_coverage < 0)
            expected_coverage = bridge.area();
        
        BridgeDetector bridge_detector(bridge, lower, scaled<coord_t>(0.5)); // extrusion width
        if (tolerance < 0)
            tolerance = Geometry::rad2deg(bridge_detector.resolution) + EPSILON;

        bridge_detector.detect_angle();
        double   result   = bridge_detector.angle;
        Polygons coverage = bridge_detector.coverage();
        THEN("correct coverage area") {
            REQUIRE(is_approx(area(coverage), expected_coverage));
        }
        // our epsilon is equal to the steps used by the bridge detection algorithm
        //##use XXX; YYY [ rad2deg($result), $expected ];
        // returned value must be non-negative, check for that too
        double delta = Geometry::rad2deg(result) - expected;
        if (delta >= 180. - EPSILON)
            delta -= 180;
        return result >= 0. && std::abs(delta) < tolerance;
    };
    GIVEN("O-shaped overhang") {
        auto test = [&check_angle](const Point &size, double rotate, double expected_angle, double tolerance = -1) {
            ExPolygon lower{
                Polygon::new_scale({ {-2,-2}, {size.x()+2,-2}, {size.x()+2,size.y()+2}, {-2,size.y()+2} }),
                Polygon::new_scale({ {0,0}, {0,size.y()}, {size.x(),size.y()}, {size.x(),0} } )
            };
            lower.rotate(Geometry::deg2rad(rotate), size / 2);
            ExPolygon bridge_expoly(lower.holes.front());
            bridge_expoly.contour.reverse();
            return check_angle({ lower }, bridge_expoly, expected_angle, tolerance);
        };
        WHEN("Bridge size 20x10") {
            bool valid = test({20,10}, 0., 90.);
            THEN("bridging angle is 90 degrees") {
                REQUIRE(valid);
            }
        }
        WHEN("Bridge size 10x20") {
            bool valid = test({10,20}, 0., 0.);
            THEN("bridging angle is 0 degrees") {
                REQUIRE(valid);
            }
        }
        WHEN("Bridge size 20x10, rotated by 45 degrees") {
            bool valid = test({20,10}, 45., 135., 20.);
            THEN("bridging angle is 135 degrees") {
                REQUIRE(valid);
            }
        }
        WHEN("Bridge size 20x10, rotated by 135 degrees") {
            bool valid = test({20,10}, 135., 45., 20.);
            THEN("bridging angle is 45 degrees") {
                REQUIRE(valid);
            }
        }
    }
    GIVEN("two-sided bridge") {
        ExPolygon bridge{ Polygon::new_scale({ {0,0}, {20,0}, {20,10}, {0,10} }) };
        ExPolygons lower { ExPolygon{ Polygon::new_scale({ {-2,0}, {0,0}, {0,10}, {-2,10} }) } };
        lower.emplace_back(lower.front());
        lower.back().translate(Point::new_scale(22, 0));
        THEN("Bridging angle 0 degrees") {
            REQUIRE(check_angle(lower, bridge, 0));
        }
    }
    GIVEN("for C-shaped overhang") {
        ExPolygon bridge{ Polygon::new_scale({ {0,0}, {20,0}, {10,10}, {0,10} }) };
        ExPolygon lower{ Polygon::new_scale({ {0,0}, {0,10}, {10,10}, {10,12}, {-2,12}, {-2,-2}, {22,-2}, {22,0} }) };
        bool valid = check_angle({ lower }, bridge, 135);
        THEN("Bridging angle is 135 degrees") {
            REQUIRE(valid);
        }
    }
    GIVEN("square overhang with L-shaped anchors") {
        ExPolygon bridge{ Polygon::new_scale({ {10,10}, {20,10}, {20,20}, {10,20} }) };
        ExPolygon lower{ Polygon::new_scale({ {10,10}, {10,20}, {20,20}, {30,30}, {0,30}, {0,0} }) };
        bool valid = check_angle({ lower }, bridge, 45., -1., bridge.area() / 2.);
        THEN("Bridging angle is 45 degrees") {
            REQUIRE(valid);
        }
    }
}

SCENARIO("Bridging integration", "[Bridging]") {
    DynamicPrintConfig config = Slic3r::DynamicPrintConfig::full_print_config_with({
        { "top_solid_layers",       0 },
        // to prevent bridging on sparse infill
        { "bridge_speed",           99 }
    });

    std::string gcode = Slic3r::Test::slice({ Slic3r::Test::TestMesh::bridge }, config);
    
    GCodeReader                 parser;
    const double                bridge_speed = config.opt_float("bridge_speed") * 60.;
    // angle => length
    std::map<coord_t, double>   extrusions;
    parser.parse_buffer(gcode, [&extrusions, bridge_speed](Slic3r::GCodeReader &self, const Slic3r::GCodeReader::GCodeLine &line)
    {
        // if the command is a T command, set the the current tool
        if (line.cmd() == "G1" && is_approx<double>(bridge_speed, line.new_F(self))) {
            // Accumulate lengths of bridging extrusions according to bridging angle.
            Line l{ self.xy_scaled(), line.new_XY_scaled(self) };
            size_t angle = scaled<coord_t>(l.direction());
            auto it = extrusions.find(angle);
            if (it == extrusions.end())
                it = extrusions.insert(std::make_pair(angle, 0.)).first;
            it->second += l.length();
        }
    });
    THEN("bridge is generated") {
        REQUIRE(! extrusions.empty());
    }
    THEN("bridge has the expected direction 0 degrees") {
        // Bridging with the longest extrusion.
        auto it_longest_extrusion = std::max_element(extrusions.begin(), extrusions.end(), 
            [](const auto &e1, const auto &e2){ return e1.second < e2.second; });
        REQUIRE(it_longest_extrusion->first == 0);
    }
}
