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

    REQUIRE(its.vertices.size() >= 3);
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

static Domain::BoundingBox3f calc_mesh_bounds(const indexed_triangle_set& mesh)
{
    Domain::Vec3f lo = mesh.vertices.front(), hi = lo;
    for (const auto& p : mesh.vertices) {
        lo = lo.cwiseMin(p);
        hi = hi.cwiseMax(p);
    }
    return {lo, hi};
}

TEST_CASE("Review: setting a bent mesh back to zero restores the original", "[review]")
{
    indexed_triangle_set mesh;
    for (float x : {-50.f, 0.f, 50.f})
        for (float y : {-10.f, 10.f})
            for (float z : {0.f, 5.f})
                mesh.vertices.emplace_back(x, y, z);
    const auto original = mesh;
    const auto original_bounds = calc_mesh_bounds(mesh);
    const BendParams bend{static_cast<float>(std::numbers::pi / 2), 0.f};
    TextBender::bend_mesh(mesh, bend, original_bounds);
    const auto current_bounds = calc_mesh_bounds(mesh);
    TextBender::unbend_mesh(mesh, bend, current_bounds);
    TextBender::bend_mesh(mesh, BendParams{}, current_bounds);
    double worst_error = 0.;
    for (size_t i = 0; i < mesh.vertices.size(); ++i)
        worst_error = std::max(worst_error, static_cast<double>((mesh.vertices[i] - original.vertices[i]).norm()));
    INFO("restored width = " << (calc_mesh_bounds(mesh).max.x() - calc_mesh_bounds(mesh).min.x()));
    REQUIRE(worst_error < 0.001);
}

TEST_CASE("Review: straight glyph edges follow the requested curl", "[review]")
{
    indexed_triangle_set mesh;
    mesh.vertices = {{-1.f, -10.f, 0.f}, {-1.f, 10.f, 0.f}, {1.f, -10.f, 0.f}, {1.f, 10.f, 0.f}};
    mesh.indices = {{0, 2, 1}, {1, 2, 3}};
    const auto bbox = calc_mesh_bounds(mesh);
    const BendParams bend{0.f, static_cast<float>(std::numbers::pi / 2)};
    const Domain::Vec3d expected_midpoint = TextBender::bend_point({-1., 0., 0.}, bend, bbox);
    TextBender::bend_mesh(mesh, bend, bbox);
    const Domain::Vec3d actual_midpoint = ((mesh.vertices[0] + mesh.vertices[1]) * 0.5f).cast<double>();
    REQUIRE((actual_midpoint - expected_midpoint).norm() < 0.1);
}

TEST_CASE("Review: projection preserves the IProjection contracts", "[review]")
{
    const BendedProjection projection(3., 100., 20., {0., 0., 0.}, 0.5f, 0.2f);
    const Domain::Vec2crd original{10, 5};
    const auto [front, back] = projection.create_front_back(original);
    SECTION("project advances from the front to the back") {
        REQUIRE((projection.project(front) - back).norm() < 0.0001);
    }
    SECTION("unproject recovers the coordinates and front depth") {
        double depth = -1.;
        const auto recovered = projection.unproject(front, &depth);
        REQUIRE(recovered.has_value());
        CHECK((recovered.value() - original.cast<double>()).norm() < 0.0001);
        CHECK_THAT(depth, Catch::Matchers::WithinAbs(0., 0.0001));
    }
}

