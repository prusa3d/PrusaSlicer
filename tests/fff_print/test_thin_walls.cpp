#include <catch2/catch_test_macros.hpp>

#include <numeric>
#include <sstream>
#include <algorithm>

#include "test_data.hpp" // get access to init_print, etc

#include "Slic3r/Biz/Algorithms/Polyline.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/libslic3r.h"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"

using namespace Slic3r;
using namespace Slic3r::Biz;
namespace BB = Biz::Algorithms::BoundingBox;

SCENARIO("Medial Axis", "[ThinWalls]") {
    GIVEN("Square with hole") {
        auto square = Algorithms::Polygon::scaled({ {100, 100}, {200, 100}, {200, 200}, {100, 200} });
        auto hole_in_square = Algorithms::Polygon::scaled({ {140, 140}, {140, 160}, {160, 160}, {160, 140} });
        ExPolygon expolygon{ square, hole_in_square };
        WHEN("Medial axis is extracted") {
            Polylines res = Slic3r::medial_axis(expolygon, scaled<double>(0.5), scaled<double>(40.));
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
        ExPolygon expolygon{ Algorithms::Polygon::scaled({ {100, 100}, {120, 100}, {120, 200}, {100, 200} }) };
        WHEN("Medial axis is extracted") {
            Polylines res = Slic3r::medial_axis(expolygon, scaled<double>(0.5), scaled<double>(20.));
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
        ExPolygon expolygon{ Algorithms::Polygon::scaled({
            {100, 100}, {120, 100}, {120, 200}, 
            {105, 200} /* extra point in the short side*/, 
            {100, 200} 
        })};
        WHEN("Medial axis is extracted") {
            Polylines res = Slic3r::medial_axis(expolygon, scaled<double>(0.5), scaled<double>(1.));
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
            Point{1185881,829367},Point{1421988,1578184},Point{1722442,2303558},Point{2084981,2999998},Point{2506843,3662186},Point{2984809,4285086},Point{3515250,4863959},Point{4094122,5394400},
            Point{4717018,5872368},Point{5379210,6294226},Point{6075653,6656769},Point{6801033,6957229},Point{7549842,7193328},Point{8316383,7363266},Point{9094809,7465751},Point{9879211,7500000},
            Point{10663611,7465750},Point{11442038,7363265},Point{12208580,7193327},Point{12957389,6957228},Point{13682769,6656768},Point{14379209,6294227},Point{15041405,5872366},
            Point{15664297,5394401},Point{16243171,4863960},Point{16758641,4301424},Point{17251579,3662185},Point{17673439,3000000},Point{18035980,2303556},Point{18336441,1578177},
            Point{18572539,829368},Point{18750748,0},Point{19758422,0},Point{19727293,236479},Point{19538467,1088188},Point{19276136,1920196},Point{18942292,2726179},Point{18539460,3499999},
            Point{18070731,4235755},Point{17539650,4927877},Point{16950279,5571067},Point{16307090,6160437},Point{15614974,6691519},Point{14879209,7160248},Point{14105392,7563079},
            Point{13299407,7896927},Point{12467399,8159255},Point{11615691,8348082},Point{10750769,8461952},Point{9879211,8500000},Point{9007652,8461952},Point{8142729,8348082},
            Point{7291022,8159255},Point{6459015,7896927},Point{5653029,7563079},Point{4879210,7160247},Point{4143447,6691519},Point{3451331,6160437},Point{2808141,5571066},Point{2218773,4927878},
            Point{1687689,4235755},Point{1218962,3499999},Point{827499,2748020},Point{482284,1920196},Point{219954,1088186},Point{31126,236479},Point{0,0},Point{1005754,0}
        }};
        WHEN("Medial axis is extracted") {
            Polylines res = Slic3r::medial_axis(expolygon, scaled<double>(0.25), scaled<double>(1.324888));
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
        ExPolygon expolygon{ Algorithms::Polygon::scaled({ {100, 100}, {120, 100}, {112, 200}, {108, 200} }) };
        WHEN("Medial axis is extracted") {
            Polylines res = Slic3r::medial_axis(expolygon, scaled<double>(0.5), scaled<double>(20.));
            THEN("medial axis of a narrow trapezoid is a single line") {
                REQUIRE(res.size() == 1);
            }
            THEN("medial axis has reasonable length") {
                REQUIRE(res.front().length() >= scaled<double>(200.-100. - (120.-100.)) - SCALED_EPSILON);
            }
        }
    }
    GIVEN("L shape") {
        ExPolygon expolygon{ Algorithms::Polygon::scaled({ {100, 100}, {120, 100}, {120, 180}, {200, 180}, {200, 200}, {100, 200}, }) };
        WHEN("Medial axis is extracted") {
            Polylines res = Slic3r::medial_axis(expolygon, scaled<double>(0.5), scaled<double>(20.));
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
            Point{-203064906,-51459966},Point{-219312231,-51459966},Point{-219335477,-51459962},Point{-219376095,-51459962},Point{-219412047,-51459966},
            Point{-219572094,-51459966},Point{-219624814,-51459962},Point{-219642183,-51459962},Point{-219656665,-51459966},Point{-220815482,-51459966},
            Point{-220815482,-37738966},Point{-221117540,-37738966},Point{-221117540,-51762024},Point{-203064906,-51762024},
        }};
        WHEN("Medial axis is extracted") {
            Polylines res = Slic3r::medial_axis(expolygon, 102499.75, 819998.);
            THEN("medial axis is a single line") {
                REQUIRE(res.size() == 1);
            }
            THEN("medial axis has reasonable length") {
                double perimeter = Algorithms::Polygon::split_at_first_point(expolygon.contour).length();
                REQUIRE(Algorithms::Polyline::total_length(res) > perimeter / 2. / 4. * 3.);
            }
        }
    }
    GIVEN("narrow triangle") {
        ExPolygon expolygon{ Algorithms::Polygon::scaled({ {50, 100}, {1000, 102}, {50, 104} }) };
        WHEN("Medial axis is extracted") {
            Polylines res = Slic3r::medial_axis(expolygon, scaled<double>(0.5), scaled<double>(4.));
            THEN("medial axis of a narrow triangle is a single line") {
                REQUIRE(res.size() == 1);
            }
            THEN("medial axis has reasonable length") {
                REQUIRE(res.front().length() >= scaled<double>(200.-100. - (120.-100.)) - SCALED_EPSILON);
            }
        }
    }
    GIVEN("GH #2474") {
        ExPolygon expolygon{{ Point{91294454,31032190},Point{11294481,31032190},Point{11294481,29967810},Point{44969182,29967810},Point{89909960,29967808},Point{91294454,29967808} }};
        WHEN("Medial axis is extracted") {
            Polylines res = Slic3r::medial_axis(expolygon, 500000, 1871238);
            THEN("medial axis is a single line") {
                REQUIRE(res.size() == 1);
            }
            Polyline &polyline = res.front();
            THEN("medial axis is horizontal and is centered") {
                double expected_y = BB::center(Algorithms::Polygon::get_bounding_box(expolygon.contour)).y();
                double center_y   = 0.;
                for (auto &p : polyline.points)
                    center_y += double(p.y());
                REQUIRE(std::abs(center_y / polyline.size() - expected_y) < SCALED_EPSILON);
            }
            // order polyline from left to right
            if (polyline.first_point().x() > polyline.last_point().x())
                polyline.reverse();

            Domain::BoundingBox2crd polyline_bb = Algorithms::Polyline::get_bounding_box(polyline);
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
