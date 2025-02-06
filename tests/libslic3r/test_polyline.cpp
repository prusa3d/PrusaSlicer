/**
* Ported from xs/t/09_polyline.t
*/
#include <catch2/catch.hpp>

#include "libslic3r/Point.hpp"
#include "libslic3r/Polyline.hpp"

using namespace Slic3r;

struct PolylineTestCase {
    Polyline polyline{
        {100, 100},
        {200, 100},
        {200, 200}
    };
};

TEST_CASE_METHOD(PolylineTestCase, "Lines can be retrieved", "[Polyline]") {

    CHECK(polyline.lines() == Lines{
        {{100, 100}, {200, 100}},
        {{200, 100}, {200, 200}},
    });
}

TEST_CASE_METHOD(PolylineTestCase, "Clip", "[Polyline]") {
    const double len = polyline.length();
    polyline.clip_end(len/3);
    CHECK(std::abs(polyline.length() - 2.0/3.0*len) < 1);
}

TEST_CASE_METHOD(PolylineTestCase, "Append", "[Polyline]") {
    Polyline tested_polyline{polyline};
    tested_polyline.append(tested_polyline);
    Points expected{polyline.points};
    expected.insert(expected.end(), polyline.points.begin(), polyline.points.end());

    CHECK(tested_polyline.points == expected);
}

TEST_CASE_METHOD(PolylineTestCase, "Extend end", "[Polyline]") {
    CHECK(polyline.length() == 100*2);
    polyline.extend_end(50);
    CHECK(polyline.length() == 100*2 + 50);
}

TEST_CASE_METHOD(PolylineTestCase, "Extend start", "[Polyline]") {
    CHECK(polyline.length() == 100*2);
    polyline.extend_start(50);
    CHECK(polyline.length() == 100*2 + 50);
}

TEST_CASE_METHOD(PolylineTestCase, "Split", "[Polyline]") {
    Polyline p1;
    Polyline p2;
    const Point point{150, 100};
    polyline.split_at(point, &p1, &p2);
    CHECK(p1.size() == 2);
    CHECK(p2.size() == 3);
    CHECK(p1.last_point() == point);
    CHECK(p2.first_point() == point);
}

TEST_CASE_METHOD(PolylineTestCase, "Split at first point", "[Polyline]") {
    Polyline to_split{
        polyline.points[0],
        polyline.points[1],
        polyline.points[2],
        polyline.points[0]
    };
    Polyline p1;
    Polyline p2;
    to_split.split_at(to_split.first_point(), &p1, &p2);
    CHECK(p1.size() == 1);
    CHECK(p2.size() == 4);
}

SCENARIO("Simplify polyne, template", "[Polyline]")
{
    Points polyline{ {0,0}, {1000,0}, {2000,0}, {2000,1000}, {2000,2000}, {1000,2000}, {0,2000}, {0,1000}, {0,0} };
    WHEN("simplified with Douglas-Peucker with back inserter") {
        Points out;
        douglas_peucker<int64_t>(polyline.begin(), polyline.end(), std::back_inserter(out), 10., [](const Point &p) { return p; });
        THEN("simplified correctly") {
            REQUIRE(out == Points{ {0,0}, {2000,0}, {2000,2000}, {0,2000}, {0,0} });
        }
    }
    WHEN("simplified with Douglas-Peucker in place") {
        Points out{ polyline };
        out.erase(douglas_peucker<int64_t>(out.begin(), out.end(), out.begin(), 10., [](const Point &p) { return p; }), out.end());
        THEN("simplified correctly") {
            REQUIRE(out == Points{ {0,0}, {2000,0}, {2000,2000}, {0,2000}, {0,0} });
        }
    }
}

SCENARIO("Simplify polyline", "[Polyline]")
{
    GIVEN("polyline 1") {
        auto polyline = Polyline{ {0,0},{1,0},{2,0},{2,1},{2,2},{1,2},{0,2},{0,1},{0,0} };
        WHEN("simplified with Douglas-Peucker") {
            polyline.simplify(1.);
            THEN("simplified correctly") {
                REQUIRE(polyline == Polyline{ {0,0}, {2,0}, {2,2}, {0,2}, {0,0} });
            }
        }
    }
    GIVEN("polyline 2") {
        auto polyline = Polyline{ {0,0}, {50,50}, {100,0}, {125,-25}, {150,50} };
        WHEN("simplified with Douglas-Peucker") {
            polyline.simplify(25.);
            THEN("not simplified") {
                REQUIRE(polyline == Polyline{ {0,0}, {50,50}, {125,-25}, {150,50} });
            }
        }
    }

    GIVEN("polyline 3") {
        auto polyline = Polyline{ {0,0}, {100,0}, {50,10} };
        WHEN("simplified with Douglas-Peucker") {
            polyline.simplify(25.);
            THEN("not simplified") {
                REQUIRE(polyline == Polyline{ {0,0}, {100, 0}, {50,10} });
            }
        }
    }

    GIVEN("polyline 4") {
        auto polyline = Polyline{ {0,0}, {20,0}, {50,0}, {80,0}, {100,0} };
        WHEN("simplified with Douglas-Peucker") {
            polyline.simplify(2.);
            THEN("not simplified") {
                REQUIRE(polyline == Polyline{ {0,0}, {100,0} });
            }
        }
    }
}
