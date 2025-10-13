
/**
 * Ported from xs/t/03_point.t
 *  - it used to check ccw() but it does not exist anymore
 *  and cross product uses doubles
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_all.hpp>
#include <libslic3r/Point.hpp>
#include "test_utils.hpp"

using namespace Slic3r;
using namespace Catch;

TEST_CASE("Nearest point", "[Point]") {
    const Point point{10, 15};
    const Point point2{30, 15};

    const Point nearest{nearest_point({point2, Point{100, 200}}, point).first};
    CHECK(nearest == point2);
}

TEST_CASE("Distance to line", "[Point]") {
    const Line line{{0, 0}, {100, 0}};
    CHECK(line.distance_to(Point{0, 0}) == Approx(0));
    CHECK(line.distance_to(Point{100, 0}) == Approx(0));
    CHECK(line.distance_to(Point{50, 0}) == Approx(0));
    CHECK(line.distance_to(Point{150, 0}) == Approx(50));
    CHECK(line.distance_to(Point{0, 50}) == Approx(50));
    CHECK(line.distance_to(Point{50, 50}) == Approx(50));
    CHECK(line.perp_distance_to(Point{50, 50}) == Approx(50));
    CHECK(line.perp_distance_to(Point{150, 50}) == Approx(50));
    
    // possitive values are on the left side WRT line direction
    CHECK(line.perp_signed_distance_to(Point{50, 50}) == Approx(50));
    CHECK(line.perp_signed_distance_to(Point{50, -50}) == Approx(-50));
    const Line line2{{0, 0}, {0, 100}};
    CHECK(line2.perp_signed_distance_to(Point{50, 50}) == Approx(-50));
    CHECK(line2.perp_signed_distance_to(Point{-50, 50}) == Approx(50));
}

TEST_CASE("Distance to diagonal line", "[Point]") {
    const Line line{{50, 50}, {125, -25}};
    CHECK_THAT(std::abs(line.distance_to(Point{100, 0})), Catch::Matchers::WithinAbs(0, 1e-6));
}

TEST_CASE("Perp distance to line does not overflow", "[Point]") {
    const Line line{
        {18335846, 18335845},
        {18335846, 1664160},
    };

    CHECK(line.distance_to(Point{1664161, 18335848}) == Approx(16671685));
}
