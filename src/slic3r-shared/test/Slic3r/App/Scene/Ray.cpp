#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Slic3r/App/Scene/Ray.hpp"

using namespace Slic3r;
using namespace Slic3r::App::Scene;

TEST_CASE("Ray closest point", "[Ray]") {
    Ray r0{ Domain::Vec3d{ 0.0, 0.0, 0.0 }, Domain::Vec3d{ 0.0, 1.0, 0.0 } };
    Ray r1{ Domain::Vec3d{ 1.0, 0.0, 0.0 }, Domain::Vec3d{ 0.0, 1.0, 0.0 } };
    Ray r2{ Domain::Vec3d{ 0.0, 1.0, 1.0 }, Domain::Vec3d{ 0.0, 0.0, 1.0 } };
    double t = 0.0;
    REQUIRE(r0.closest_point_from_ray(r1, t) == false);
    REQUIRE(t == 0.0);
    REQUIRE(r0.closest_point_from_ray(r2, t) == true);
    REQUIRE_THAT(t, Catch::Matchers::WithinRel(1, 0.0001));
}
