/**
 * Ported from xs/t/10_line.t
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <libslic3r/Line.hpp>
#include "test_utils.hpp"

using namespace Slic3r;

TEST_CASE("Line can be translated", "[Line]") {
    Line line{{100, 100}, {200, 100}};

    line.translate(10, -5);
    CHECK(Points{line.a, line.b} == Points{{110, 95}, {210, 95}});
}

TEST_CASE("Check if lines are parallel", "[Line]") {
    CHECK(Line{{0, 0}, {100, 0}}.parallel_to(Line{{200, 200}, {0, 200}}));
}

TEST_CASE("Parallel lines under angles", "[Line]") {
    auto base_angle = GENERATE(0, M_PI/3, M_PI/2, M_PI);

    Line line{{0, 0}, {100, 0}};
    line.rotate(base_angle, {0, 0});
    Line clone{line};

    INFO("Line is parallel to self");
    CHECK(line.parallel_to(clone));

    clone.reverse();
    INFO("Line is parallel to self + PI");
    CHECK(line.parallel_to(clone));

    INFO("Line is parallel to its direction");
    CHECK(line.parallel_to(line.direction()));
    INFO("Line is parallel to its direction + PI");
    line.parallel_to(line.direction() + M_PI);
    INFO("line is parallel to its direction - PI");
    line.parallel_to(line.direction() - M_PI);

    SECTION("Line is parallel within epsilon") {
        clone = line;
        clone.rotate(EPSILON/2, {0, 0});
        CHECK(line.parallel_to(clone));
        clone = line;
        clone.rotate(-EPSILON/2, {0, 0});
        CHECK(line.parallel_to(clone));
    }
}

TEST_CASE("Intersection infinite", "[Line]") {
    const Line a{{100, 0}, {200, 0}};
    const Line b{{300, 300}, {300, 100}};
    Point r;
    a.intersection_infinite(b, &r);
    CHECK(r == Point{300, 0});
}
