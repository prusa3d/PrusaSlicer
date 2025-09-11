#include <catch2/catch_test_macros.hpp>

#include <numeric>
#include <sstream>
#include <algorithm>

#include "test_data.hpp" // get access to init_print, etc

#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/libslic3r.h"

using namespace Slic3r;

SCENARIO("Medial Axis", "[ThinWalls]") {
    GIVEN("Square with hole") {
        auto square = Polygon::new_scale({ {100, 100}, {200, 100}, {200, 200}, {100, 200} });
        auto hole_in_square = Polygon::new_scale({ {140, 140}, {140, 160}, {160, 160}, {160, 140} });
        ExPolygon expolygon{ square, hole_in_square };
        WHEN("Medial axis is extracted") {
            Polylines res = expolygon.medial_axis(scaled<double>(0.5), scaled<double>(40.));
            THEN("medial axis of a square shape is a single path") {
                REQUIRE(res.size() == 1);
            }
            THEN("polyline forms a closed loop") {
                REQUIRE(res.front().first_point() ==  res.front().last_point());
            }
            THEN("medial axis loop has reasonable length") {
                REQUIRE(res.front().length() > hole_in_square.length());
                REQUIRE(res.front().length() < square.length());
            }
        }
    }
    GIVEN("narrow rectangle") {
        ExPolygon expolygon{ Polygon::new_scale({ {100, 100}, {120, 100}, {120, 200}, {100, 200} }) };
        WHEN("Medial axis is extracted") {
            Polylines res = expolygon.medial_axis(scaled<double>(0.5), scaled<double>(20.));
            THEN("medial axis of a narrow rectangle is a single line") {
                REQUIRE(res.size() == 1);
            }
            THEN("medial axis has reasonable length") {
                REQUIRE(res.front().length() >= scaled<double>(200.-100. - (120.-100.)) - SCALED_EPSILON);
            }
        }
    }
#if 0
    //FIXME this test never worked
    GIVEN("narrow rectangle with an extra vertex") {
        ExPolygon expolygon{ Polygon::new_scale({ 
            {100, 100}, {120, 100}, {120, 200}, 
            {105, 200} /* extra point in the short side*/, 
            {100, 200} 
        })};
        WHEN("Medial axis is extracted") {
            Polylines res = expolygon.medial_axis(scaled<double>(0.5), scaled<double>(1.));
            THEN("medial axis of a narrow rectangle with an extra vertex is still a single line") {
                REQUIRE(res.size() == 1);
            }
            THEN("medial axis has still a reasonable length") {
                REQUIRE(res.front().length() >= scaled<double>(200.-100. - (120.-100.)) - SCALED_EPSILON);
            }
            THEN("extra vertices don't influence medial axis") {
                size_t invalid = 0;
                for (const Polyline &pl : res)
                    for (const Point &p : pl.points)
                        if (std::abs(p.y() - scaled<coord_t>(150.)) < SCALED_EPSILON)
                            ++ invalid;
                REQUIRE(invalid == 0);
            }
        }
    }
#endif
    GIVEN("semicircumference") {
        ExPolygon expolygon{{ 
            {1185881,829367},{1421988,1578184},{1722442,2303558},{2084981,2999998},{2506843,3662186},{2984809,4285086},{3515250,4863959},{4094122,5394400},
            {4717018,5872368},{5379210,6294226},{6075653,6656769},{6801033,6957229},{7549842,7193328},{8316383,7363266},{9094809,7465751},{9879211,7500000},
            {10663611,7465750},{11442038,7363265},{12208580,7193327},{12957389,6957228},{13682769,6656768},{14379209,6294227},{15041405,5872366},
            {15664297,5394401},{16243171,4863960},{16758641,4301424},{17251579,3662185},{17673439,3000000},{18035980,2303556},{18336441,1578177},
            {18572539,829368},{18750748,0},{19758422,0},{19727293,236479},{19538467,1088188},{19276136,1920196},{18942292,2726179},{18539460,3499999},
            {18070731,4235755},{17539650,4927877},{16950279,5571067},{16307090,6160437},{15614974,6691519},{14879209,7160248},{14105392,7563079},
            {13299407,7896927},{12467399,8159255},{11615691,8348082},{10750769,8461952},{9879211,8500000},{9007652,8461952},{8142729,8348082},
            {7291022,8159255},{6459015,7896927},{5653029,7563079},{4879210,7160247},{4143447,6691519},{3451331,6160437},{2808141,5571066},{2218773,4927878},
            {1687689,4235755},{1218962,3499999},{827499,2748020},{482284,1920196},{219954,1088186},{31126,236479},{0,0},{1005754,0}
        }};
        WHEN("Medial axis is extracted") {
            Polylines res = expolygon.medial_axis(scaled<double>(0.25), scaled<double>(1.324888));
            THEN("medial axis of a semicircumference is a single line") {
                REQUIRE(res.size() == 1);
            }
            THEN("all medial axis segments of a semicircumference have the same orientation") {
                int nccw = 0;
                int ncw = 0;
                for (const Polyline &pl : res)
                    for (size_t i = 1; i + 1 < pl.size(); ++ i) {
                        double cross = cross2((pl.points[i] - pl.points[i - 1]).cast<double>(), (pl.points[i + 1] - pl.points[i]).cast<double>());
                        if (cross > 0.)
                            ++ nccw;
                        else if (cross < 0.)
                            ++ ncw;
                    }
                REQUIRE((ncw == 0 || nccw == 0));
            }
        }
    }
    GIVEN("narrow trapezoid") {
        ExPolygon expolygon{ Polygon::new_scale({ {100, 100}, {120, 100}, {112, 200}, {108, 200} }) };
        WHEN("Medial axis is extracted") {
            Polylines res = expolygon.medial_axis(scaled<double>(0.5), scaled<double>(20.));
            THEN("medial axis of a narrow trapezoid is a single line") {
                REQUIRE(res.size() == 1);
            }
            THEN("medial axis has reasonable length") {
                REQUIRE(res.front().length() >= scaled<double>(200.-100. - (120.-100.)) - SCALED_EPSILON);
            }
        }
    }
    GIVEN("L shape") {
        ExPolygon expolygon{ Polygon::new_scale({ {100, 100}, {120, 100}, {120, 180}, {200, 180}, {200, 200}, {100, 200}, }) };
        WHEN("Medial axis is extracted") {
            Polylines res = expolygon.medial_axis(scaled<double>(0.5), scaled<double>(20.));
            THEN("medial axis of an L shape is a single line") {
                REQUIRE(res.size() == 1);
            }
            THEN("medial axis has reasonable length") {
                // 20 is the thickness of the expolygon, which is subtracted from the ends
                auto len = unscale<double>(res.front().length()) + 20;
                REQUIRE(len > 80. * 2.);
                REQUIRE(len < 100. * 2.);
            }
        }
    }
    GIVEN("whatever shape") {
        ExPolygon expolygon{{ 
            {-203064906,-51459966},{-219312231,-51459966},{-219335477,-51459962},{-219376095,-51459962},{-219412047,-51459966},
            {-219572094,-51459966},{-219624814,-51459962},{-219642183,-51459962},{-219656665,-51459966},{-220815482,-51459966},
            {-220815482,-37738966},{-221117540,-37738966},{-221117540,-51762024},{-203064906,-51762024},
        }};
        WHEN("Medial axis is extracted") {
            Polylines res = expolygon.medial_axis(102499.75, 819998.);
            THEN("medial axis is a single line") {
                REQUIRE(res.size() == 1);
            }
            THEN("medial axis has reasonable length") {
                double perimeter = expolygon.contour.split_at_first_point().length();
                REQUIRE(total_length(res) > perimeter / 2. / 4. * 3.);
            }
        }
    }
    GIVEN("narrow triangle") {
        ExPolygon expolygon{ Polygon::new_scale({ {50, 100}, {1000, 102}, {50, 104} }) };
        WHEN("Medial axis is extracted") {
            Polylines res = expolygon.medial_axis(scaled<double>(0.5), scaled<double>(4.));
            THEN("medial axis of a narrow triangle is a single line") {
                REQUIRE(res.size() == 1);
            }
            THEN("medial axis has reasonable length") {
                REQUIRE(res.front().length() >= scaled<double>(200.-100. - (120.-100.)) - SCALED_EPSILON);
            }
        }
    }
    GIVEN("GH #2474") {
        ExPolygon expolygon{{ {91294454,31032190},{11294481,31032190},{11294481,29967810},{44969182,29967810},{89909960,29967808},{91294454,29967808} }};
        WHEN("Medial axis is extracted") {
            Polylines res = expolygon.medial_axis(500000, 1871238);
            THEN("medial axis is a single line") {
                REQUIRE(res.size() == 1);
            }
            Polyline &polyline = res.front();
            THEN("medial axis is horizontal and is centered") {
                double expected_y = expolygon.contour.bounding_box().center().y();
                double center_y   = 0.;
                for (auto &p : polyline.points)
                    center_y += double(p.y());
                REQUIRE(std::abs(center_y / polyline.size() - expected_y) < SCALED_EPSILON);
            }
            // order polyline from left to right
            if (polyline.first_point().x() > polyline.last_point().x())
                polyline.reverse();
            BoundingBox polyline_bb = polyline.bounding_box();
            THEN("expected x_min") {
                REQUIRE(polyline.first_point().x() == polyline_bb.min.x());
            }
            THEN("expected x_max") {
                REQUIRE(polyline.last_point().x() == polyline_bb.max.x());
            }
            THEN("medial axis is monotonous in x (not self intersecting)") {
                Polyline sorted { polyline };
                std::sort(sorted.begin(), sorted.end());
                REQUIRE(polyline == sorted);
            }
        }
    }
}
