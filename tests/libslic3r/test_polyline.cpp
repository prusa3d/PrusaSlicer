#include <catch2/catch_all.hpp>

#include "libslic3r/Point.hpp"
#include "libslic3r/Polyline.hpp"

using namespace Slic3r;

SCENARIO("Simplify polyne, template", "[Polyline]")
{
    Points polyline{ {0,0}, {1000,0}, {2000,0}, {2000,1000}, {2000,2000}, {1000,2000}, {0,2000}, {0,1000}, {0,0} };
    WHEN("simplified with Douglas-Peucker with back inserter") {
        Points out;
        douglas_peucker<int64_t>(polyline.begin(), polyline.end(), std::back_inserter(out), 10, [](const Point &p) { return p; });
        THEN("simplified correctly") {
            REQUIRE(out == Points{ {0,0}, {2000,0}, {2000,2000}, {0,2000}, {0,0} });
        }
    }
    WHEN("simplified with Douglas-Peucker in place") {
        Points out{ polyline };
        out.erase(douglas_peucker<int64_t>(out.begin(), out.end(), out.begin(), 10, [](const Point &p) { return p; }), out.end());
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
}
