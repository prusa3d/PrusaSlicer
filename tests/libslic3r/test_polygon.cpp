#include <catch2/catch_test_macros.hpp>

#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"

using namespace Slic3r;

SCENARIO("Converted Perl tests", "[Polygon]") {
    GIVEN("ccw_square") {
        Polygon ccw_square{ { 100, 100 }, { 200, 100 }, { 200, 200 }, { 100, 200 } };
        Polygon cw_square(ccw_square);
        cw_square.reverse();

        THEN("ccw_square is valid") {
            REQUIRE(ccw_square.is_valid());
        }
        THEN("cw_square is valid") {
            REQUIRE(cw_square.is_valid());
        }
        THEN("ccw_square.area") {
            REQUIRE(ccw_square.area() == 100 * 100);
        }
        THEN("cw_square.area") {
            REQUIRE(cw_square.area() == - 100 * 100);
        }
        THEN("ccw_square.centroid") {
            REQUIRE(ccw_square.centroid() == Point { 150, 150 });
        }
        THEN("cw_square.centroid") {
            REQUIRE(cw_square.centroid() == Point { 150, 150 });
        }
        THEN("ccw_square.contains_point(150, 150)") {
            REQUIRE(ccw_square.contains({ 150, 150 }));
        }
        THEN("cw_square.contains_point(150, 150)") {
            REQUIRE(cw_square.contains({ 150, 150 }));
        }
        THEN("conversion to lines") {
            REQUIRE(ccw_square.lines() == Lines{
                { { 100, 100 }, { 200, 100 } },
                { { 200, 100 }, { 200, 200 } },
                { { 200, 200 }, { 100, 200 } },
                { { 100, 200 }, { 100, 100 } } });
        }
        THEN("split_at_first_point") {
            REQUIRE(ccw_square.split_at_first_point() == Polyline { ccw_square[0], ccw_square[1], ccw_square[2], ccw_square[3], ccw_square[0] });
        }
        THEN("split_at_index(2)") {
            REQUIRE(ccw_square.split_at_index(2) == Polyline { ccw_square[2], ccw_square[3], ccw_square[0], ccw_square[1], ccw_square[2] });
        }
        THEN("split_at_vertex(ccw_square[2])") {
            REQUIRE(ccw_square.split_at_vertex(ccw_square[2]) == Polyline { ccw_square[2], ccw_square[3], ccw_square[0], ccw_square[1], ccw_square[2] });
        }
        THEN("is_counter_clockwise") {
            REQUIRE(ccw_square.is_counter_clockwise());
        }
        THEN("! is_counter_clockwise") {
            REQUIRE(! cw_square.is_counter_clockwise());
        }
        THEN("make_counter_clockwise") {
            cw_square.make_counter_clockwise();
            REQUIRE(cw_square.is_counter_clockwise());
        }
        THEN("make_counter_clockwise^2") {
            cw_square.make_counter_clockwise();
            cw_square.make_counter_clockwise();
            REQUIRE(cw_square.is_counter_clockwise());
        }
        THEN("first_point") {
            REQUIRE(&ccw_square.first_point() == &ccw_square.points.front());
        }
    }
    GIVEN("Triangulating hexagon") {
        Polygon hexagon{ { 100, 0 } };
        for (size_t i = 1; i < 6; ++ i) {
            Point p = hexagon.points.front();
            p.rotate(PI / 3 * i);
            hexagon.points.emplace_back(p);
        }
        Polygons triangles;
        hexagon.triangulate_convex(&triangles);
        THEN("right number of triangles") {
            REQUIRE(triangles.size() == 4);
        }
        THEN("all triangles are ccw") {
            auto it = std::find_if(triangles.begin(), triangles.end(), [](const Polygon &tri) { return tri.is_clockwise(); });
            REQUIRE(it == triangles.end());
        }
    }
    GIVEN("General triangle") {
        Polygon polygon { { 50000000, 100000000 }, { 300000000, 102000000 }, { 50000000, 104000000 } };
        Line    line { { 175992032, 102000000 }, { 47983964, 102000000 } };
        Point   intersection;
        bool    has_intersection = polygon.intersection(line, &intersection);
        THEN("Intersection with line") {
            REQUIRE(has_intersection);
            REQUIRE(intersection == Point { 50000000, 102000000 });
        }
    }
}

TEST_CASE("Centroid of Trapezoid must be inside", "[Polygon][Utils]")
{
    Slic3r::Polygon trapezoid {
        { 4702134, 1124765853 },
        { -4702134, 1124765853 },
        { -9404268, 1049531706 },
        { 9404268, 1049531706 },
    };
    Point centroid = trapezoid.centroid();
    CHECK(trapezoid.contains(centroid));
}

// This test currently only covers remove_collinear_points.
// All remaining tests are to be ported from xs/t/06_polygon.t

Slic3r::Points collinear_circle({
    Slic3r::Point::new_scale(0, 0), // 3 collinear points at beginning
    Slic3r::Point::new_scale(10, 0),
    Slic3r::Point::new_scale(20, 0),
    Slic3r::Point::new_scale(30, 10),
    Slic3r::Point::new_scale(40, 20), // 2 collinear points
    Slic3r::Point::new_scale(40, 30),
    Slic3r::Point::new_scale(30, 40), // 3 collinear points
    Slic3r::Point::new_scale(20, 40),
    Slic3r::Point::new_scale(10, 40),
    Slic3r::Point::new_scale(-10, 20),
    Slic3r::Point::new_scale(-20, 10),
    Slic3r::Point::new_scale(-20, 0), // 3 collinear points at end
    Slic3r::Point::new_scale(-10, 0),
    Slic3r::Point::new_scale(-5, 0)
});

SCENARIO("Remove collinear points from Polygon", "[Polygon]") {
    GIVEN("Polygon with collinear points"){
        Slic3r::Polygon p(collinear_circle);
        WHEN("collinear points are removed") {
            remove_collinear(p);
            THEN("Leading collinear points are removed") {
                REQUIRE(p.points.front() == Slic3r::Point::new_scale(20, 0));
            }
            THEN("Trailing collinear points are removed") {
                REQUIRE(p.points.back() == Slic3r::Point::new_scale(-20, 0));
            }
            THEN("Number of remaining points is correct") {
                REQUIRE(p.points.size() == 7);
            }
        }
    }
}

SCENARIO("Simplify polygon", "[Polygon]")
{
    GIVEN("gear") {
        auto gear = Polygon::new_scale({
            {144.9694,317.1543}, {145.4181,301.5633}, {146.3466,296.921}, {131.8436,294.1643}, {131.7467,294.1464},
            {121.7238,291.5082}, {117.1631,290.2776}, {107.9198,308.2068}, {100.1735,304.5101}, {104.9896,290.3672},
            {106.6511,286.2133}, {93.453,279.2327}, {81.0065,271.4171}, {67.7886,286.5055}, {60.7927,280.1127},
            {69.3928,268.2566}, {72.7271,264.9224}, {61.8152,253.9959}, {52.2273,242.8494}, {47.5799,245.7224},
            {34.6577,252.6559}, {30.3369,245.2236}, {42.1712,236.3251}, {46.1122,233.9605}, {43.2099,228.4876},
            {35.0862,211.5672}, {33.1441,207.0856}, {13.3923,212.1895}, {10.6572,203.3273}, {6.0707,204.8561},
            {7.2775,204.4259}, {29.6713,196.3631}, {25.9815,172.1277}, {25.4589,167.2745}, {19.8337,167.0129},
            {5.0625,166.3346}, {5.0625,156.9425}, {5.3701,156.9282}, {21.8636,156.1628}, {25.3713,156.4613},
            {25.4243,155.9976}, {29.3432,155.8157}, {30.3838,149.3549}, {26.3596,147.8137}, {27.1085,141.2604},
            {29.8466,126.8337}, {24.5841,124.9201}, {10.6664,119.8989}, {13.4454,110.9264}, {33.1886,116.0691},
            {38.817,103.1819}, {45.8311,89.8133}, {30.4286,76.81}, {35.7686,70.0812}, {48.0879,77.6873},
            {51.564,81.1635}, {61.9006,69.1791}, {72.3019,58.7916}, {60.5509,42.5416}, {68.3369,37.1532},
            {77.9524,48.1338}, {80.405,52.2215}, {92.5632,44.5992}, {93.0123,44.3223}, {106.3561,37.2056},
            {100.8631,17.4679}, {108.759,14.3778}, {107.3148,11.1283}, {117.0002,32.8627}, {140.9109,27.3974},
            {145.7004,26.4994}, {145.1346,6.1011}, {154.502,5.4063}, {156.9398,25.6501}, {171.0557,26.2017},
            {181.3139,27.323}, {186.2377,27.8532}, {191.6031,8.5474}, {200.6724,11.2756}, {197.2362,30.2334},
            {220.0789,39.1906}, {224.3261,41.031}, {236.3506,24.4291}, {243.6897,28.6723}, {234.2956,46.7747},
            {245.6562,55.1643}, {257.2523,65.0901}, {261.4374,61.5679}, {273.1709,52.8031}, {278.555,59.5164},
            {268.4334,69.8001}, {264.1615,72.3633}, {268.2763,77.9442}, {278.8488,93.5305}, {281.4596,97.6332},
            {286.4487,95.5191}, {300.2821,90.5903}, {303.4456,98.5849}, {286.4523,107.7253}, {293.7063,131.1779},
            {294.9748,135.8787}, {314.918,133.8172}, {315.6941,143.2589}, {300.9234,146.1746}, {296.6419,147.0309},
            {297.1839,161.7052}, {296.6136,176.3942}, {302.1147,177.4857}, {316.603,180.3608}, {317.1658,176.7341},
            {315.215,189.6589}, {315.1749,189.6548}, {294.9411,187.5222}, {291.13,201.7233}, {286.2615,215.5916},
            {291.1944,218.2545}, {303.9158,225.1271}, {299.2384,233.3694}, {285.7165,227.6001}, {281.7091,225.1956},
            {273.8981,237.6457}, {268.3486,245.2248}, {267.4538,246.4414}, {264.8496,250.0221}, {268.6392,253.896},
            {278.5017,265.2131}, {272.721,271.4403}, {257.2776,258.3579}, {234.4345,276.5687}, {242.6222,294.8315},
            {234.9061,298.5798}, {227.0321,286.2841}, {225.2505,281.8301}, {211.5387,287.8187}, {202.3025,291.0935},
            {197.307,292.831}, {199.808,313.1906}, {191.5298,315.0787}, {187.3082,299.8172}, {186.4201,295.3766},
            {180.595,296.0487}, {161.7854,297.4248}, {156.8058,297.6214}, {154.3395,317.8592}
        });
     
        WHEN("simplified") {
            size_t num_points = gear.size();
            Polygons simplified = gear.simplify(1000.);
            THEN("gear simplified to a single polygon") {
                REQUIRE(simplified.size() == 1);
            }
            THEN("gear was reduced using Douglas-Peucker") {
                //note printf "original points: %d\nnew points: %d", $num_points, scalar(@{$simplified->[0]});
                REQUIRE(simplified.front().size() < num_points);
            }
        }
    }
}

#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExPolygonsIndex.hpp"
TEST_CASE("Indexing expolygons", "[ExPolygon]")
{
    ExPolygons expolys{
        ExPolygon{Polygon{{0, 0}, {10, 0}, {0, 5}}, Polygon{{4, 3}, {6, 3}, {5, 2}}},
        ExPolygon{Polygon{{100, 0}, {110, 0}, {100, 5}}, Polygon{{104, 3}, {106, 3}, {105, 2}}}    
    };
    Points points = to_points(expolys);
    Lines lines = to_lines(expolys);
    Linesf linesf = to_linesf(expolys);
    ExPolygonsIndices ids(expolys);
    REQUIRE(points.size() == lines.size());
    REQUIRE(points.size() == linesf.size());
    REQUIRE(points.size() == ids.get_count());
    for (size_t i = 0; i < ids.get_count(); i++) { 
        ExPolygonsIndex id = ids.cvt(i);
        const ExPolygon &expoly = expolys[id.expolygons_index];
        const Polygon &poly     = id.is_contour() ? expoly.contour : expoly.holes[id.hole_index()];
        const Points &pts       = poly.points;
        const Point &p          = pts[id.point_index];
        CHECK(points[i] == p);
        CHECK(lines[i].a == p);
        CHECK(linesf[i].a.cast<int>() == p);
        CHECK(ids.cvt(id) == i);
        const Point &p_b = ids.is_last_point(id) ? pts.front() : pts[id.point_index + 1];
        CHECK(lines[i].b == p_b);
        CHECK(linesf[i].b.cast<int>() == p_b);
    }
}
