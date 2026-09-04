#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <limits>
#include <map>
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

TEST_CASE("Subdivided glyph edges follow the bend without moving original vertices", "[TextBender]")
{
    const float horizontal = GENERATE(0.f, 0.8f, -2.7f);
    const float vertical = GENERATE(0.6f, -1.8f, 3.0f);
    const float arc = GENERATE(0.f, 1.4f, -3.0f);
    indexed_triangle_set mesh;
    // Offset coordinates deliberately avoid any special origin or vertex order.
    mesh.vertices = {{12.f, 5.f, 0.f}, {14.f, 5.f, 0.f}, {14.f, 25.f, 0.f}, {12.f, 25.f, 0.f}};
    mesh.indices = {{0, 1, 2}, {0, 2, 3}};
    const auto original = mesh.vertices;
    const auto bbox = calc_mesh_bounds(mesh);
    const BendParams params{horizontal, vertical, arc};
    TextBender::bend_mesh(mesh, params, bbox);
    REQUIRE(mesh.vertices.size() > original.size());
    for (size_t i = 0; i < original.size(); ++i)
        CHECK((mesh.vertices[i].cast<double>() - TextBender::bend_point(original[i].cast<double>(), params, bbox)).norm() < 1e-5);
    // Check actual resulting edges, rather than assuming fixed vertex indices
    // still describe an edge after subdivision.
    for (const auto& tri : mesh.indices) {
        for (int edge = 0; edge < 3; ++edge) {
            const Domain::Vec3d a = mesh.vertices[tri[edge]].cast<double>();
            const Domain::Vec3d b = mesh.vertices[tri[(edge + 1) % 3]].cast<double>();
            const Domain::Vec3d flat_midpoint = 0.5 * (TextBender::unbend_point(a, params, bbox)
                + TextBender::unbend_point(b, params, bbox));
            const auto expected = TextBender::bend_point(flat_midpoint, params, bbox);
            REQUIRE((0.5 * (a + b) - expected).norm() <= TextBender::MAX_CHORD_ERROR + 1e-4);
        }
    }
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

static indexed_triangle_set rectangular_solid()
{
    indexed_triangle_set mesh;
    mesh.vertices = {{-50.f, -10.f, 0.f}, {50.f, -10.f, 0.f}, {50.f, 10.f, 0.f}, {-50.f, 10.f, 0.f},
        {-50.f, -10.f, 2.f}, {50.f, -10.f, 2.f}, {50.f, 10.f, 2.f}, {-50.f, 10.f, 2.f}};
    mesh.indices = {{0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7}, {0, 1, 5}, {0, 5, 4},
        {1, 2, 6}, {1, 6, 5}, {2, 3, 7}, {2, 7, 6}, {3, 0, 4}, {3, 4, 7}};
    return mesh;
}

TEST_CASE("Bend subdivision keeps a closed consistently oriented mesh", "[TextBender]")
{
    auto mesh = rectangular_solid();
    const float arc = GENERATE(0.f, 2.2f, -2.8f);
    TextBender::bend_mesh(mesh, {1.7f, -2.8f, arc}, calc_mesh_bounds(mesh));
    std::map<std::pair<int, int>, std::pair<int, int>> edges;
    std::vector<bool> used(mesh.vertices.size(), false);
    for (const auto& tri : mesh.indices) {
        const Domain::Vec3d a = mesh.vertices[tri[0]].cast<double>();
        const Domain::Vec3d b = mesh.vertices[tri[1]].cast<double>();
        const Domain::Vec3d c = mesh.vertices[tri[2]].cast<double>();
        REQUIRE((b - a).cross(c - a).norm() > 1e-10);
        for (int i = 0; i < 3; ++i) {
            const int from = tri[i], to = tri[(i + 1) % 3];
            used[from] = true;
            const std::pair<int, int> key = std::minmax(from, to);
            auto& [count, winding] = edges[key];
            ++count;
            winding += from < to ? 1 : -1;
        }
    }
    for (const auto& [edge, incidence] : edges) {
        REQUIRE(incidence.first == 2);
        REQUIRE(incidence.second == 0);
    }
    REQUIRE(std::all_of(used.begin(), used.end(), [](bool value) { return value; }));
}

TEST_CASE("Repeated combined bends and reset preserve the baseline dimensions", "[TextBender]")
{
    auto mesh = rectangular_solid();
    const auto original = mesh.vertices;
    const auto bounds = calc_mesh_bounds(mesh);
    const Domain::BoundingBox3d reference{bounds.min.cast<double>(), bounds.max.cast<double>()};
    BendParams previous;
    const float limit = static_cast<float>(std::numbers::pi);
    const std::vector<BendParams> edits{{1.57f, -0.6f, 1.2f}, {-2.1f, 1.1f, -2.5f},
        {limit, -limit, limit}, {0.f, 0.f, -limit}, {0.2f, 0.4f, -0.5f}, {}};
    for (int repeat = 0; repeat < 3; ++repeat) {
        for (const auto& next : edits) {
            TextBender::restore_mesh(mesh, previous, reference);
            for (size_t i = 0; i < original.size(); ++i) {
                CAPTURE(repeat, i, previous.horizontal_bend, previous.vertical_curl, previous.vertical_arc);
                CAPTURE(mesh.vertices[i].x(), mesh.vertices[i].y(), mesh.vertices[i].z());
                REQUIRE((mesh.vertices[i] - original[i]).norm() < 0.003f);
            }
            TextBender::bend_mesh(mesh, next, reference);
            previous = next;
        }
    }
    for (size_t i = 0; i < original.size(); ++i) {
        REQUIRE(mesh.vertices[i].allFinite());
        REQUIRE((mesh.vertices[i] - original[i]).norm() < 0.003f);
    }
}

TEST_CASE("Prepared drag baselines have stable topology over the full angle range", "[TextBender]")
{
    auto base = rectangular_solid();
    const auto bbox = calc_mesh_bounds(base);
    TextBender::prepare_mesh(base, {bbox.min.cast<double>(), bbox.max.cast<double>()});
    const float angle = GENERATE(-3.1415927f, -1.0f, 0.f, 1.0f, 3.1415927f);
    auto mesh = base;
    TextBender::bend_mesh(mesh, {angle, -angle, angle}, bbox);
    REQUIRE(mesh.indices == base.indices);
    REQUIRE(mesh.vertices.size() == base.vertices.size());
}

TEST_CASE("Bend limits and nonfinite input cannot fold or poison the mesh", "[TextBender]")
{
    const auto bbox = calc_mesh_bounds(rectangular_solid());
    const Domain::Vec3d point{30., 8., 1.};
    const float limit = static_cast<float>(std::numbers::pi);
    const auto maximum = TextBender::bend_point(point, {limit, -limit, limit}, bbox);
    const auto excessive = TextBender::bend_point(point, {20.f, -20.f, 20.f}, bbox);
    REQUIRE((maximum - excessive).norm() < 1e-8);
    const auto invalid = TextBender::bend_point(point,
        {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::infinity()}, bbox);
    REQUIRE(invalid.allFinite());
    REQUIRE((invalid - point).norm() < 1e-8);
}

TEST_CASE("Projection roundtrips signed bends and both extrusion surfaces", "[BendedProjection]")
{
    const float angle = GENERATE(-3.1415927f, -0.5f, 0.f, 0.5f, 3.1415927f);
    const BendedProjection projection(3., 100., 20., {0., 0., 0.}, angle, -angle, angle);
    const Domain::Vec2crd point{25, 5};
    const auto [front, back] = projection.create_front_back(point);
    CHECK((projection.project(front) - back).norm() < 1e-7);
    for (const auto& surface : {std::pair{front, 0.}, std::pair{back, 3.}}) {
        double depth = -1.;
        const auto original = projection.unproject(surface.first, &depth);
        REQUIRE(original.has_value());
        CHECK((original.value() - point.cast<double>()).norm() < 1e-7);
        CHECK_THAT(depth, Catch::Matchers::WithinAbs(surface.second, 1e-7));
    }
    REQUIRE_FALSE(projection.unproject({std::numeric_limits<double>::quiet_NaN(), 0., 0.}).has_value());
}

TEST_CASE("Z arc raises or lowers both ends around a fixed middle", "[TextBender]")
{
    const float angle = GENERATE(-3.1415927f, -1.5f, 0.f, 1.5f, 3.1415927f);
    const Domain::BoundingBox3f bbox{{10.f, 20.f, 2.f}, {110.f, 40.f, 7.f}};
    const BendParams params{0.f, 0.f, angle};
    const Domain::Vec3d middle{60., 30., 4.};
    const auto center = TextBender::bend_point(middle, params, bbox);
    const auto left = TextBender::bend_point({10., 30., 4.}, params, bbox);
    const auto right = TextBender::bend_point({110., 30., 4.}, params, bbox);
    CHECK((center - middle).norm() < 1e-8);
    CHECK_THAT(left.x() + right.x(), Catch::Matchers::WithinAbs(120., 1e-6));
    CHECK_THAT(left.y(), Catch::Matchers::WithinAbs(right.y(), 1e-6));
    CHECK(left.z() == 4.);
    CHECK(right.z() == 4.);
    if (angle == 0.f) {
        CHECK(left.x() == 10.);
        CHECK(left.y() == 30.);
    } else {
        CHECK((left.y() - middle.y()) * angle > 0.);
        // The midline follows a circle, not a V or a tilted straight line.
        const double radius = 100. / TextBender::clamp_angle(angle);
        const Domain::Vec2d circle_center{middle.x(), middle.y() + radius};
        CHECK_THAT((left.head<2>() - circle_center).norm(), Catch::Matchers::WithinAbs(std::abs(radius), 1e-6));
        CHECK_THAT((right.head<2>() - circle_center).norm(), Catch::Matchers::WithinAbs(std::abs(radius), 1e-6));
    }
}

TEST_CASE("Z arc can be removed while retaining the two depth bends", "[TextBender]")
{
    auto mesh = rectangular_solid();
    for (auto& point : mesh.vertices)
        point += Domain::Vec3f{12.f, 37.f, 8.f};
    const auto original = mesh.vertices;
    const auto bounds = calc_mesh_bounds(mesh);
    const float arc = GENERATE(-3.1415927f, -1.3f, 1.3f, 3.1415927f);
    const BendParams combined{1.2f, -0.8f, arc};
    TextBender::bend_mesh(mesh, combined, bounds);
    TextBender::restore_mesh(mesh, combined, {bounds.min.cast<double>(), bounds.max.cast<double>()});
    const BendParams depth_only{1.2f, -0.8f, 0.f};
    TextBender::bend_mesh(mesh, depth_only, calc_mesh_bounds(mesh));
    for (size_t i = 0; i < original.size(); ++i) {
        const auto expected = TextBender::bend_point(original[i].cast<double>(), depth_only, bounds);
        CHECK((mesh.vertices[i].cast<double>() - expected).norm() < 0.003);
    }
}

TEST_CASE("Older two-axis bends can still recover dimensions without a saved reference", "[TextBender]")
{
    auto mesh = rectangular_solid();
    const auto original = mesh.vertices;
    const BendParams params{2.7f, -3.1415927f};
    TextBender::bend_mesh(mesh, params, calc_mesh_bounds(mesh));
    TextBender::unbend_mesh(mesh, params, calc_mesh_bounds(mesh));
    for (size_t i = 0; i < original.size(); ++i)
        REQUIRE((mesh.vertices[i] - original[i]).norm() < 0.003f);
}
