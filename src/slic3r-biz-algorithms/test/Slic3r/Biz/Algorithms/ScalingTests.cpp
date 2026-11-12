#include "Slic3r/Biz/Algorithms/Scaling.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

using namespace Catch;
using Slic3r::Biz::Algorithms::Scaling::scaled;
using Slic3r::Biz::Algorithms::Scaling::unscaled;
using Slic3r::Domain::coord_t;
using Slic3r::Domain::Vec2d;
using Slic3r::Domain::Vec2f;
using Slic3r::Domain::Vec2crd;
using Slic3r::Domain::Vec2big;


TEST_CASE("Scale scalar", "[algorithms][algorithms-scaling]") {
    CHECK(scaled(0.1) == static_cast<coord_t>(1e5));
    CHECK(scaled<int64_t>(0.1) == static_cast<coord_t>(1e5));
}

TEST_CASE("Scale vector", "[algorithms][algorithms-scaling]") {
    CHECK(scaled(Vec2d{0.1, 0.1}) == Vec2crd{1e5, 1e5});
    CHECK(scaled<int64_t>(Vec2d{0.1, 0.1}) == Vec2big{1e5, 1e5});
}

TEST_CASE("Unscale scalar", "[algorithms][algorithms-scaling]") {
    CHECK(unscaled(static_cast<coord_t>(1e5)) == Approx(0.1F));
    CHECK(unscaled<double>(static_cast<coord_t>(1e5)) == Approx(0.1));
}

TEST_CASE("Unscale vector", "[algorithms][algorithms-scaling]") {
    CHECK(unscaled(Vec2crd{1e5, 1e5}).x() == Approx(0.1F));
    CHECK(unscaled(Vec2crd{1e5, 1e5}).y() == Approx(0.1F));
    CHECK(unscaled<double>(Vec2crd{1e5, 1e5}).x() == Approx(0.1));
    CHECK(unscaled<double>(Vec2crd{1e5, 1e5}).y() == Approx(0.1));
}
