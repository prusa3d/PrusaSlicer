#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/Algorithms/Scaling.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

using namespace Catch;
using Slic3r::Domain::Vec2crd;
using Slic3r::Domain::Vec2d;
using Slic3r::Domain::Vec2f;
namespace bb = Slic3r::Biz::Algorithms::BoundingBox;
using Slic3r::Domain::BoundingBox2crd;
using Slic3r::Domain::BoundingBox3crd;
using Slic3r::Domain::BoundingBox2d;
using Slic3r::Domain::BoundingBox3d;
using Slic3r::Domain::BoundingBox2f;
using Slic3r::Biz::Algorithms::Scaling::scaled;


TEST_CASE("Construct bounding box", "[algorithms][algorithms-bounding-box]") {
    const std::vector<Vec2crd> points{
        Vec2crd{-2, 3},
        Vec2crd{-1, 2},
        Vec2crd{0, 1},
        Vec2crd{1, 0},
        Vec2crd{2, -1},
        Vec2crd{3, -2},
        Vec2crd{4, -3},
    };

    const BoundingBox2crd result{bb::construct(points)};
    CHECK(result == BoundingBox2crd{{-2, -3}, {4, 3}});
    CHECK(bb::construct(points.begin(), points.end()) == result);
}

TEST_CASE("Bounding box approx equals", "[algorithms][algorithms-bounding-box]") {
    CHECK(bb::approx_equals(
        BoundingBox2d{{0.1, 0.1}, {0.2, 0.2}},
        BoundingBox2d{{0.1, 0.1}, {0.2, 0.2}}
    ));
}

TEST_CASE("Bounding box point distance", "[algorithms][algorithms-bounding-box]") {
    const BoundingBox2d box{{0, 0}, {1, 1}};
    CHECK(bb::distance(box, {0.5, 1.5}) == Approx(0.5));
    CHECK(bb::distance_squared(box, {0.5, 1.5}) == Approx(0.25));

    const BoundingBox2d other_box{{2.5, 0}, {3.5, 1}};
    CHECK(bb::distance(box, other_box) == Approx(1.5));
}

TEST_CASE("Bounding box merge with bounding box", "[algorithms][algorithms-bounding-box]") {
    const BoundingBox2d a{{-1, -2}, {1, 2}};
    const BoundingBox2d b{{-2, -1}, {2, 1}};
    CHECK(bb::approx_equals(bb::merge(a, b), {{-2, -2}, {2, 2}}));
}

TEST_CASE("Bounding box merge with point", "[algorithms][algorithms-bounding-box]") {
    const BoundingBox2d box{{0, 0}, {1, 1}};
    CHECK(bb::approx_equals(bb::merge(box, {-1, -1}), {{-1, -1}, {1, 1}}));
}

// Hopefully we can get rid of this behavior in the future.
TEST_CASE("Undefined bounding box merged with a point is defined", "[algorithms][algorithms-bounding-box]") {
    const BoundingBox2d box;
    const BoundingBox2d result{bb::merge(box, {-1, -1})};

    // It is not possible to do BoundingBox2d{{-1, -1}, {-1, -1}} as it would be undefined.
    CHECK(result.max.isApprox(Vec2d{-1, -1}));
    CHECK(result.min.isApprox(Vec2d{-1, -1}));
    CHECK(result.defined == true);
}

// Hopefully we can get rid of this behavior in the future.
TEST_CASE("Undefined bounding box merged with a bounding box is defined", "[algorithms][algorithms-bounding-box]") {
    const BoundingBox2d box;
    const BoundingBox2d result{bb::merge(box, {{-1, -1}, {1, 1}})};
    CHECK(bb::approx_equals(result, {{-1, -1}, {1, 1}}));
}

TEST_CASE("Bounding box inflation", "[algorithms][algorithms-bounding-box]") {
    const BoundingBox2d box{{-1, 0}, {1, 2}};
    const BoundingBox2d result{bb::inflated(box, 0.5)};
    CHECK(bb::approx_equals(result, {{-1.5, -0.5}, {1.5, 2.5}}));
}

TEST_CASE("Bounding box scale", "[algorithms][algorithms-bounding-box]") {
    const BoundingBox2d box{{0.5, 0.0}, {1.5, 2.0}};
    const BoundingBox2crd result{bb::scaled(box)};
    CHECK(result == BoundingBox2crd{{scaled(0.5), scaled(0.0)}, {scaled(1.5), scaled(2.0)}});
}

TEST_CASE("Bounding box unscale", "[algorithms][algorithms-bounding-box]") {
    const BoundingBox2crd box{{scaled(0.5), scaled(0.0)}, {scaled(1.5), scaled(2.0)}};
    const BoundingBox2f result{bb::unscaled(box)};
    CHECK(bb::approx_equals(result, {{0.5, 0.0}, {1.5, 2.0}}));
}

TEST_CASE("Bounding box to 2d", "[algorithms][algorithms-bounding-box]") {
    const BoundingBox3crd box{{0, 0, 0}, {1, 1, 1}};
    CHECK(bb::to_2d(box) == BoundingBox2crd{{0, 0}, {1, 1}});
}

TEST_CASE("Bounding box translation", "[algorithms][algorithms-bounding-box]") {
    const BoundingBox3d box{{0, 0, 0}, {1, 1, 1}};
    const BoundingBox3d result{bb::translated(box, {2, 1, -1})};
    CHECK(bb::approx_equals(result, {{2, 1, -1}, {3, 2, 0}}));
}

TEST_CASE("Bounding box contains", "[algorithms][algorithms-bounding-box]") {
    const BoundingBox2d box{{0, 0}, {1, 1}};
    CHECK(box.contains({0.8, 0.8}));
    CHECK(!box.contains({-1.1, 0.8}));
    const BoundingBox2d smaller_box{{0.1, 0.1}, {0.9, 0.9}};
    CHECK(box.contains(smaller_box));
    const BoundingBox2d bigger_box{{0.1, -0.1}, {0.9, 0.9}};
    CHECK(!box.contains(bigger_box));
}

TEST_CASE("Bounding box overlap", "[algorithms][algorithms-bounding-box]") {
    const BoundingBox2d box{{0, 0}, {1, 1}};
    const BoundingBox2d overlapping_box{{0.5, 0.5}, {1.5, 1.5}};
    CHECK(box.overlap(overlapping_box));
    const BoundingBox2d not_overlapping_box{{2, 2}, {3, 3}};
    CHECK(!box.overlap(not_overlapping_box));
}

TEST_CASE("Bounding box cast", "[algorithms][algorithms-bounding-box]") {
    const BoundingBox2d box{{0, 0}, {1, 1}};
    const BoundingBox2f result{bb::cast<float>(box)};
    CHECK(result.min.isApprox(Vec2f{0, 0}));
    CHECK(result.max.isApprox(Vec2f{1, 1}));
}

