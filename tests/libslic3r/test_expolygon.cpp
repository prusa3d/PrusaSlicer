#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Slic3r/Biz/Algorithms/ExPolygon.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/ExPolygon.hpp"

using namespace Slic3r;
using namespace Slic3r::Biz;
using namespace Catch;

static inline bool points_close(const Point &p1, const Point &p2)
{
    return (p1 - p2).cast<double>().norm() < SCALED_EPSILON;
}

static bool polygons_close_permuted(const Polygon &poly1, const Polygon &poly2, const std::vector<int> &permutation2)
{
    if (poly1.size() != poly2.size() || poly1.size() != permutation2.size())
        return false;
    for (size_t i = 0; i < poly1.size(); ++ i)
        if (poly1[i] != poly2[permutation2[i]])
            return false;
    return true;
}

SCENARIO("Basics", "[ExPolygon]") {
    GIVEN("ccw_square") {
        Polygon ccw_square{ Point{ 100, 100 }, Point{ 200, 100 }, Point{ 200, 200 }, Point{ 100, 200 } };
        Polygon cw_hole_in_square{ Point{ 140, 140 }, Point{ 140, 160 }, Point{ 160, 160 }, Point{ 160, 140 } };
        ExPolygon expolygon { ccw_square, cw_hole_in_square };
        THEN("expolygon is valid") {
            REQUIRE(Algorithms::ExPolygon::is_valid(expolygon));
        }
        THEN("expolygon area") {
            REQUIRE(expolygon.area() == Approx(100*100-20*20));
        }
        WHEN("Expolygon scaled") {
            ExPolygon expolygon2 = expolygon;
            expolygon2.scale(2.5);
            REQUIRE(expolygon.contour.size() == expolygon2.contour.size());
            REQUIRE(expolygon.holes.size() == 1);
            REQUIRE(expolygon2.holes.size() == 1);
            for (size_t i = 0; i < expolygon.contour.size(); ++ i)
                REQUIRE(points_close(expolygon.contour[i] * 2.5, expolygon2.contour[i]));
            for (size_t i = 0; i < expolygon.holes.front().size(); ++ i)
                REQUIRE(points_close(expolygon.holes.front()[i] * 2.5, expolygon2.holes.front()[i]));
        }
        WHEN("Expolygon translated") {
            ExPolygon expolygon2 = expolygon;
            expolygon2.translate(10, -5);
            REQUIRE(expolygon.contour.size() == expolygon2.contour.size());
            REQUIRE(expolygon.holes.size() == 1);
            REQUIRE(expolygon2.holes.size() == 1);
            for (size_t i = 0; i < expolygon.contour.size(); ++ i)
                REQUIRE(points_close(expolygon.contour[i] + Point(10, -5), expolygon2.contour[i]));
            for (size_t i = 0; i < expolygon.holes.front().size(); ++ i)
                REQUIRE(points_close(expolygon.holes.front()[i] + Point(10, -5), expolygon2.holes.front()[i]));
        }
        WHEN("Expolygon rotated around point") {
            ExPolygon expolygon2 = expolygon;
            expolygon2.rotate(M_PI / 2, Point(150, 150));
            REQUIRE(expolygon.contour.size() == expolygon2.contour.size());
            REQUIRE(expolygon.holes.size() == 1);
            REQUIRE(expolygon2.holes.size() == 1);
            REQUIRE(polygons_close_permuted(expolygon2.contour, expolygon.contour, { 1, 2, 3, 0}));
            REQUIRE(polygons_close_permuted(expolygon2.holes.front(), expolygon.holes.front(), { 3, 0, 1, 2}));
        }
    }
}

