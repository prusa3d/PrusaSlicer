#include "Slic3r/Biz/Algorithms/SlaPoints.hpp"
#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Domain/Model.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace Slic3r;
using namespace Slic3r::Biz::Algorithms;
using Domain::SLA::DrainHole;
using Domain::SLA::SupportPoint;
using Domain::Vec3d;
using Domain::Vec3f;

namespace {

bool is_approx(const Vec3f& a, const Vec3f& b) { return a.isApprox(b, 1e-5f); }

// Two parts: a 10 mm cube at the origin and the same cube scaled 2x and shifted by 20 mm in X,
// so it spans [20, 40] x [0, 20] x [0, 20] in object coordinates.
struct TwoParts
{
    Domain::Model        model;
    Domain::ModelObject* object = model.add_object();
    Domain::ModelVolume* a      = ModelObject::add_volume(object, TriangleMesh::make_cube(10., 10., 10.));
    Domain::ModelVolume* b      = ModelObject::add_volume(object, TriangleMesh::make_cube(10., 10., 10.));

    TwoParts()
    {
        b->set_scaling_factor(Vec3d{2., 2., 2.});
        b->set_offset(Vec3d{20., 0., 0.});
    }
};

} // namespace

TEST_CASE("SLA support points go to the nearest part", "[algorithms][sla-points]")
{
    TwoParts parts;
    const Domain::SLA::SupportPoints points{
        SupportPoint{Vec3f{5.f, 5.f, 10.f}, 0.2f},
        SupportPoint{Vec3f{40.f, 10.f, 10.f}, 0.3f},
    };

    SlaPoints::set_support_points_in_object(*parts.object, points);

    REQUIRE(parts.a->sla_support_points.size() == 1);
    REQUIRE(parts.b->sla_support_points.size() == 1);
    CHECK(is_approx(parts.a->sla_support_points.front().pos, Vec3f{5.f, 5.f, 10.f}));
    // In the mesh coordinates of the scaled part.
    CHECK(is_approx(parts.b->sla_support_points.front().pos, Vec3f{10.f, 5.f, 5.f}));
    // The head radius is a physical size, it doesn't scale with the part.
    CHECK(parts.b->sla_support_points.front().head_front_radius == 0.3f);

    const Domain::SLA::SupportPoints in_object = SlaPoints::support_points_in_object(*parts.object);
    REQUIRE(in_object.size() == 2);
    CHECK(is_approx(in_object[0].pos, points[0].pos));
    CHECK(is_approx(in_object[1].pos, points[1].pos));
    CHECK(SlaPoints::has_support_points(*parts.object));
}

TEST_CASE("SLA support points follow the transformation of their part", "[algorithms][sla-points]")
{
    TwoParts parts;
    SlaPoints::set_support_points_in_object(*parts.object, {SupportPoint{Vec3f{40.f, 10.f, 10.f}}});

    parts.b->set_scaling_factor(Vec3d{3., 3., 3.});

    const Domain::SLA::SupportPoints in_object = SlaPoints::support_points_in_object(*parts.object);
    REQUIRE(in_object.size() == 1);
    CHECK(is_approx(in_object.front().pos, Vec3f{50.f, 15.f, 15.f}));
}

TEST_CASE("SLA drain holes keep their size and direction", "[algorithms][sla-points]")
{
    TwoParts parts;
    parts.b->set_scaling_factor(Vec3d{2., 1., 1.});
    const Vec3f normal = Vec3f{1.f, 1.f, 0.f}.normalized();
    const DrainHole hole{Vec3f{40.f, 10.f, 5.f}, normal, 2.f, 4.f};

    SlaPoints::set_drain_holes_in_object(*parts.object, {hole});

    REQUIRE(parts.a->sla_drain_holes.empty());
    REQUIRE(parts.b->sla_drain_holes.size() == 1);
    const DrainHole& stored = parts.b->sla_drain_holes.front();
    CHECK(is_approx(stored.normal, Vec3f{0.5f, 1.f, 0.f}.normalized()));
    CHECK(stored.radius == 2.f);
    CHECK(stored.height == 4.f);

    const Domain::SLA::DrainHoles in_object = SlaPoints::drain_holes_in_object(*parts.object);
    REQUIRE(in_object.size() == 1);
    CHECK(is_approx(in_object.front().pos, hole.pos));
    CHECK(is_approx(in_object.front().normal, normal));
    CHECK(SlaPoints::has_drain_holes(*parts.object));
}

TEST_CASE("SLA points of volumes outside the SLA geometry are kept but not used", "[algorithms][sla-points]")
{
    TwoParts parts;
    parts.b->set_type(Domain::ModelVolumeType::PARAMETER_MODIFIER);
    parts.b->sla_support_points = {SupportPoint{Vec3f{1.f, 2.f, 3.f}}};

    SlaPoints::set_support_points_in_object(*parts.object, {SupportPoint{Vec3f{40.f, 10.f, 10.f}}});

    // The point near the modifier goes to the only part.
    CHECK(parts.a->sla_support_points.size() == 1);
    CHECK(parts.b->sla_support_points.size() == 1);
    CHECK(SlaPoints::support_points_in_object(*parts.object).size() == 1);

    parts.a->sla_support_points.clear();
    CHECK_FALSE(SlaPoints::has_support_points(*parts.object));

    SlaPoints::clear_support_points(*parts.object);
    CHECK(parts.b->sla_support_points.empty());
}

TEST_CASE("SLA points move with the mesh of their volume", "[algorithms][sla-points]")
{
    TwoParts parts;
    parts.a->sla_support_points = {SupportPoint{Vec3f{5.f, 5.f, 10.f}}};
    parts.a->sla_drain_holes    = {DrainHole{Vec3f{10.f, 5.f, 5.f}, Vec3f{1.f, 0.f, 0.f}}};

    parts.a->scale_geometry_after_creation(Vec3f{2.f, 1.f, 1.f});

    CHECK(is_approx(parts.a->sla_support_points.front().pos, Vec3f{10.f, 5.f, 10.f}));
    CHECK(is_approx(parts.a->sla_drain_holes.front().pos, Vec3f{20.f, 5.f, 5.f}));
    CHECK(is_approx(parts.a->sla_drain_holes.front().normal, Vec3f{1.f, 0.f, 0.f}));
}
