#include "libslic3r/Point.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <libslic3r/SupportSpotsGenerator.hpp>

using namespace Slic3r;
using namespace SupportSpotsGenerator;
using namespace Catch;

namespace Rectangle {
const float width = 10;
const float height = 20;
const Polygon polygon = {
    scaled(Vec2f{-width / 2, -height / 2}),
    scaled(Vec2f{width / 2, -height / 2}),
    scaled(Vec2f{width / 2, height / 2}),
    scaled(Vec2f{-width / 2, height / 2})
};
}

TEST_CASE("Numerical integral over polygon calculation compared with exact solution.", "[SupportSpotsGenerator]") {
    const Integrals integrals{Rectangle::polygon};

    CHECK(integrals.area == Approx(Rectangle::width * Rectangle::height));
    CHECK(integrals.x_i.x() == Approx(0));
    CHECK(integrals.x_i.y() == Approx(0));
    CHECK(integrals.x_i_squared.x() == Approx(std::pow(Rectangle::width, 3) * Rectangle::height / 12));
    CHECK(integrals.x_i_squared.y() == Approx(Rectangle::width * std::pow(Rectangle::height, 3) / 12));
}

TEST_CASE("Integrals over multiple polygons", "[SupportSpotsGenerator]") {
    const Integrals integrals{{Rectangle::polygon, Rectangle::polygon}};

    CHECK(integrals.area == Approx(2 * Rectangle::width * Rectangle::height));
}

TEST_CASE("Numerical integral over line calculation compared with exact solution.", "[SupportSpotsGenerator]") {
    const float length = 10;
    const float width = 20;
    const Polyline polyline{scaled(Vec2f{-length/2.0f, 0.0f}), scaled(Vec2f{length/2.0f, 0.0f})};

    const Integrals integrals{{polyline}, {width}};
    CHECK(integrals.area == Approx(length * width));
    CHECK(integrals.x_i.x() == Approx(0));
    CHECK(integrals.x_i.y() == Approx(0));
    CHECK(integrals.x_i_squared.x() == Approx(std::pow(length, 3) * width / 12));
    CHECK(integrals.x_i_squared.y() == Approx(length * std::pow(width, 3) / 12));
}

TEST_CASE("Moment values and ratio check.", "[SupportSpotsGenerator]") {
    const float width = 40;
    const float height = 2;

    // Moments are calculated at centroid.
    // Polygon centroid must not be (0, 0).
    const Polygon polygon = {
        scaled(Vec2f{0, 0}),
        scaled(Vec2f{width, 0}),
        scaled(Vec2f{width, height}),
        scaled(Vec2f{0, height})
    };

    const Integrals integrals{polygon};

    const Vec2f x_axis{1, 0};
    const float x_axis_moment = compute_second_moment(integrals, x_axis);

    const Vec2f y_axis{0, 1};
    const float y_axis_moment = compute_second_moment(integrals, y_axis);

    const float moment_ratio = std::pow(width / height, 2);

    // Ensure the object transaltion has no effect.
    CHECK(x_axis_moment == Approx(width * std::pow(height, 3) / 12));
    CHECK(y_axis_moment == Approx(std::pow(width, 3) * height / 12));
    // If the object is "wide" the y axis moments should be large compared to x axis moment.
    CHECK(y_axis_moment / x_axis_moment == Approx(moment_ratio));
}

TEST_CASE("Moments calculation for rotated axis.", "[SupportSpotsGenerator]") {
    Polygon polygon = {
        scaled(Vec2f{6.362284076172198, 138.9674202217155}),
        scaled(Vec2f{97.48779843751677, 106.08136606617076}),
        scaled(Vec2f{135.75221821532384, 66.84428834668765}),
        scaled(Vec2f{191.5308049852741, 45.77905628725614}),
        scaled(Vec2f{182.7525148049201, 74.01799041087513}),
        scaled(Vec2f{296.83210979283473, 196.80022572637228}),
        scaled(Vec2f{215.16434429179148, 187.45715418834143}),
        scaled(Vec2f{64.64574271229334, 284.293883209721}),
        scaled(Vec2f{110.76507036894843, 174.35633141113783}),
        scaled(Vec2f{77.56229640885199, 189.33057746591336})
    };

    Integrals integrals{polygon};

    // Meassured counterclockwise from (1, 0)
    const float angle = 1.432f;
    Vec2f axis{std::cos(angle), std::sin(angle)};

    float moment_calculated_then_rotated = compute_second_moment(
        integrals,
        axis
    );

    // We want to rotate the object clockwise by angle to align the axis with (1, 0)
    // Method .rotate is counterclockwise for positive angle
    polygon.rotate(-angle);

    Integrals integrals_rotated{{polygon}};
    float moment_rotated_polygon = compute_second_moment(
        integrals_rotated,
        Vec2f{1, 0}
    );

    // Up to 0.1% accuracy
    CHECK_THAT(moment_calculated_then_rotated, Catch::Matchers::WithinRel(moment_rotated_polygon, 0.001f));
}

struct ObjectPartFixture {
    const Polyline polyline{
        Point{scaled(Vec2f{0, 0})},
        Point{scaled(Vec2f{1, 0})},
    };
    const float width = 0.1f;
    bool connected_to_bed = true;
    coordf_t print_head_z = 0.2;
    coordf_t layer_height = 0.2;
    ExtrusionAttributes attributes;
    ExtrusionEntityCollection collection;
    std::vector<const ExtrusionEntityCollection*> extrusions{};
    Polygon expected_polygon{
        Point{scaled(Vec2f{0, -width / 2})},
        Point{scaled(Vec2f{1, -width / 2})},
        Point{scaled(Vec2f{1, width / 2})},
        Point{scaled(Vec2f{0, width / 2})}
    };

    ObjectPartFixture() {
        attributes.width = width;
        const ExtrusionPath path{polyline, attributes};
        collection.append(path);
        extrusions.push_back(&collection);
    }
};

TEST_CASE_METHOD(ObjectPartFixture, "Constructing ObjectPart using extrusion collections", "[SupportSpotsGenerator]") {
    ObjectPart part{
        extrusions,
        connected_to_bed,
        print_head_z,
        layer_height,
        std::nullopt
    };

    Integrals expected{expected_polygon};

    CHECK(part.connected_to_bed == true);
    Vec3f volume_centroid{part.volume_centroid_accumulator / part.volume};
    CHECK(volume_centroid.x() == Approx(0.5));
    CHECK(volume_centroid.y() == Approx(0));
    CHECK(volume_centroid.z() == Approx(layer_height / 2));
    CHECK(part.sticking_area == Approx(expected.area));
    CHECK(part.sticking_centroid_accumulator.x() == Approx(expected.x_i.x()));
    CHECK(part.sticking_centroid_accumulator.y() == Approx(expected.x_i.y()));
    CHECK(part.sticking_second_moment_of_area_accumulator.x() == Approx(expected.x_i_squared.x()));
    CHECK(part.sticking_second_moment_of_area_accumulator.y() == Approx(expected.x_i_squared.y()));
    CHECK(part.sticking_second_moment_of_area_covariance_accumulator == Approx(expected.xy).margin(1e-6));
    CHECK(part.volume == Approx(layer_height * width));
}

TEST_CASE_METHOD(ObjectPartFixture, "Constructing ObjectPart with brim", "[SupportSpotsGenerator]") {
    float brim_width = 1;
    Polygons brim = get_brim(ExPolygon{expected_polygon}, BrimType::btOuterOnly, brim_width);

    ObjectPart part{
        extrusions,
        connected_to_bed,
        print_head_z,
        layer_height,
        brim
    };

    CHECK(part.sticking_area == Approx((1 + 2*brim_width) * (width + 2*brim_width)));
}

