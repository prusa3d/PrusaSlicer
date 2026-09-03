#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Slic3r/Biz/Emboss/TextBender.hpp"
#include "Slic3r/Biz/Emboss/BendedProjection.hpp"

using namespace Slic3r;
using namespace Slic3r::Biz::Emboss;

TEST_CASE("TextBender zero bend identity", "[TextBender]")
{
    Domain::BoundingBox3f bbox(
        Domain::Vec3f(-50.0f, -10.0f, 0.0f),
        Domain::Vec3f(50.0f, 10.0f, 5.0f)
    );
    BendParams params{ .horizontal_bend = 0.0f, .vertical_curl = 0.0f };

    Domain::Vec3d pt(25.0, 5.0, 2.5);
    Domain::Vec3d result = TextBender::bend_point(pt, params, bbox);

    REQUIRE_THAT(result.x(), Catch::Matchers::WithinRel(25.0, 1e-5));
    REQUIRE_THAT(result.y(), Catch::Matchers::WithinRel(5.0, 1e-5));
    REQUIRE_THAT(result.z(), Catch::Matchers::WithinRel(2.5, 1e-5));
}

TEST_CASE("TextBender horizontal bend symmetry", "[TextBender]")
{
    Domain::BoundingBox3f bbox(
        Domain::Vec3f(-50.0f, -10.0f, 0.0f),
        Domain::Vec3f(50.0f, 10.0f, 5.0f)
    );
    // 90 degree horizontal bend (pi/2)
    BendParams params{ .horizontal_bend = static_cast<float>(std::numbers::pi / 2.0), .vertical_curl = 0.0f };

    Domain::Vec3d center(0.0, 0.0, 2.5);
    Domain::Vec3d bent_center = TextBender::bend_point(center, params, bbox);
    REQUIRE_THAT(bent_center.x(), Catch::Matchers::WithinAbs(0.0, 1e-5));
    REQUIRE_THAT(bent_center.z(), Catch::Matchers::WithinRel(2.5, 1e-5));

    Domain::Vec3d right(40.0, 0.0, 2.5);
    Domain::Vec3d left(-40.0, 0.0, 2.5);
    Domain::Vec3d bent_right = TextBender::bend_point(right, params, bbox);
    Domain::Vec3d bent_left  = TextBender::bend_point(left, params, bbox);

    // Symmetric displacement along X and Z
    REQUIRE_THAT(bent_right.x(), Catch::Matchers::WithinRel(-bent_left.x(), 1e-5));
    REQUIRE_THAT(bent_right.z(), Catch::Matchers::WithinRel(bent_left.z(), 1e-5));
    // Convex outward bend has positive Z offset
    REQUIRE(bent_right.z() > 2.5);
}

TEST_CASE("TextBender vertical curl symmetry", "[TextBender]")
{
    Domain::BoundingBox3f bbox(
        Domain::Vec3f(-50.0f, -10.0f, 0.0f),
        Domain::Vec3f(50.0f, 10.0f, 5.0f)
    );
    // 60 degree vertical curl (pi/3)
    BendParams params{ .horizontal_bend = 0.0f, .vertical_curl = static_cast<float>(std::numbers::pi / 3.0) };

    Domain::Vec3d top(0.0, 8.0, 2.5);
    Domain::Vec3d bottom(0.0, -8.0, 2.5);
    Domain::Vec3d bent_top    = TextBender::bend_point(top, params, bbox);
    Domain::Vec3d bent_bottom = TextBender::bend_point(bottom, params, bbox);

    REQUIRE_THAT(bent_top.y(), Catch::Matchers::WithinRel(-bent_bottom.y(), 1e-5));
    REQUIRE_THAT(bent_top.z(), Catch::Matchers::WithinRel(bent_bottom.z(), 1e-5));
    REQUIRE(bent_top.z() > 2.5);
}

TEST_CASE("TextBender mesh bend", "[TextBender]")
{
    Domain::BoundingBox3f bbox(
        Domain::Vec3f(-20.0f, -5.0f, 0.0f),
        Domain::Vec3f(20.0f, 5.0f, 2.0f)
    );

    indexed_triangle_set its;
    its.vertices = {
        Domain::Vec3f(-20.0f, -5.0f, 0.0f),
        Domain::Vec3f( 20.0f, -5.0f, 0.0f),
        Domain::Vec3f(  0.0f,  5.0f, 0.0f)
    };
    its.indices.push_back({0, 1, 2});

    BendParams params{ .horizontal_bend = 0.5f, .vertical_curl = 0.2f };
    TextBender::bend_mesh(its, params, bbox);

    REQUIRE(its.vertices.size() == 3);
    REQUIRE(its.vertices[0].z() > 0.0f);
    REQUIRE(its.vertices[1].z() > 0.0f);
}

TEST_CASE("BendedProjection front and back generation", "[BendedProjection]")
{
    BendedProjection proj(3.0, 100.0, 20.0, Domain::Vec3d(0.0, 0.0, 0.0), 0.5f, 0.2f);
    auto [front, back] = proj.create_front_back(Domain::Vec2crd(10, 5));

    // Back is displaced along depth (Z)
    REQUIRE(back.z() > front.z());
}

TEST_CASE("TextBender bend and unbend roundtrip", "[TextBender]")
{
    Domain::BoundingBox3f bbox(
        Domain::Vec3f(-50.0f, -10.0f, 0.0f),
        Domain::Vec3f(50.0f, 10.0f, 5.0f)
    );
    BendParams params{ .horizontal_bend = 0.8f, .vertical_curl = -0.4f };

    Domain::Vec3d original(25.0, 5.0, 2.5);
    Domain::Vec3d bent = TextBender::bend_point(original, params, bbox);
    Domain::Vec3d unbent = TextBender::unbend_point(bent, params, bbox);

    REQUIRE_THAT(unbent.x(), Catch::Matchers::WithinRel(original.x(), 1e-4));
    REQUIRE_THAT(unbent.y(), Catch::Matchers::WithinRel(original.y(), 1e-4));
    REQUIRE_THAT(unbent.z(), Catch::Matchers::WithinRel(original.z(), 1e-4));
}

