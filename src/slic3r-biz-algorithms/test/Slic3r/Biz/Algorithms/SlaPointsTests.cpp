#include "Slic3r/Biz/Algorithms/SlaPoints.hpp"
#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/ModelVolume.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Domain/Model.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

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

namespace {

// Two 10 mm cubes in one mesh, the second one shifted by 20 mm in X.
Domain::TriangleMesh two_cubes()
{
    Domain::TriangleMesh mesh   = TriangleMesh::make_cube(10., 10., 10.);
    Domain::TriangleMesh second = TriangleMesh::make_cube(10., 10., 10.);
    second.translate(Vec3f{20.f, 0.f, 0.f});
    mesh.merge(second);
    return mesh;
}

Domain::ModelObject& add_object(Domain::Model& model, Domain::TriangleMesh mesh)
{
    Domain::ModelObject& object = *model.add_object();
    ModelObject::add_volume(&object, mesh);
    object.add_instance();
    return object;
}

} // namespace

TEST_CASE("Splitting an object gives the SLA points to the new objects", "[algorithms][sla-points]")
{
    using Domain::SLA::PointsStatus;

    Domain::Model        model;
    Domain::ModelObject& object = add_object(model, two_cubes());
    object.volumes.front()->sla_support_points = {
        SupportPoint{Vec3f{5.f, 5.f, 10.f}},
        SupportPoint{Vec3f{25.f, 5.f, 10.f}},
    };
    object.volumes.front()->sla_drain_holes = {DrainHole{Vec3f{25.f, 5.f, 10.f}, Vec3f{0.f, 0.f, -1.f}}};
    object.sla_points_status = PointsStatus::UserModified;

    SECTION("points on both parts")
    {
        Domain::ModelObjectPtrs new_objects;
        ModelObject::split(&object, &new_objects);

        REQUIRE(new_objects.size() == 2);
        for (const Domain::ModelObject* new_object : new_objects) {
            CHECK(new_object->sla_points_status == PointsStatus::UserModified);
            REQUIRE(new_object->volumes.size() == 1);
            REQUIRE(new_object->volumes.front()->sla_support_points.size() == 1);
            // Still on the mesh of the new object.
            const Domain::BoundingBox3d box = new_object->volumes.front()->mesh().bounding_box();
            CHECK(box.contains(new_object->volumes.front()->sla_support_points.front().pos.cast<double>()));
        }
        const size_t with_hole = new_objects.front()->volumes.front()->sla_drain_holes.empty() ? 1 : 0;
        CHECK(new_objects[with_hole]->volumes.front()->sla_drain_holes.size() == 1);
        CHECK(new_objects[1 - with_hole]->volumes.front()->sla_drain_holes.empty());
    }

    SECTION("points on one part only")
    {
        object.volumes.front()->sla_support_points.pop_back();
        Domain::ModelObjectPtrs new_objects;
        ModelObject::split(&object, &new_objects);

        REQUIRE(new_objects.size() == 2);
        size_t user_modified = 0;
        for (const Domain::ModelObject* new_object : new_objects) {
            if (new_object->sla_points_status == PointsStatus::UserModified) {
                ++user_modified;
                CHECK(SlaPoints::has_support_points(*new_object));
            } else {
                // Without points it would get no supports at all.
                CHECK(new_object->sla_points_status == PointsStatus::AutoGenerated);
            }
        }
        CHECK(user_modified == 1);
    }
}

TEST_CASE("Splitting a part gives the SLA points to the new parts", "[algorithms][sla-points]")
{
    Domain::Model        model;
    Domain::ModelObject& object = add_object(model, two_cubes());
    object.volumes.front()->set_offset(Vec3d{1., 2., 3.});
    object.volumes.front()->sla_support_points = {
        SupportPoint{Vec3f{5.f, 5.f, 10.f}},
        SupportPoint{Vec3f{25.f, 5.f, 10.f}},
    };
    object.volumes.front()->sla_drain_holes = {DrainHole{Vec3f{25.f, 5.f, 10.f}, Vec3f{0.f, 0.f, -1.f}}};
    const Domain::SLA::SupportPoints points_before = SlaPoints::support_points_in_object(object);

    REQUIRE(ModelVolume::split(object.volumes.front(), 1) == 2);

    REQUIRE(object.volumes.size() == 2);
    CHECK(object.volumes[0]->sla_support_points.size() == 1);
    CHECK(object.volumes[1]->sla_support_points.size() == 1);
    CHECK(object.volumes[0]->sla_drain_holes.size() + object.volumes[1]->sla_drain_holes.size() == 1);
    // The points stay where they were.
    const Domain::SLA::SupportPoints points_after = SlaPoints::support_points_in_object(object);
    REQUIRE(points_after.size() == 2);
    for (const SupportPoint& before : points_before) {
        CHECK(std::ranges::any_of(points_after, [&](const SupportPoint& after) { return is_approx(after.pos, before.pos); }));
    }
}

TEST_CASE("Merging objects keeps the SLA points only if all of them were user modified", "[algorithms][sla-points]")
{
    using Domain::SLA::PointsStatus;

    Domain::Model        model;
    Domain::ModelObject& first  = add_object(model, TriangleMesh::make_cube(10., 10., 10.));
    Domain::ModelObject& second = add_object(model, TriangleMesh::make_cube(10., 10., 10.));
    first.instances.front()->set_offset(Vec3d{100., 0., 0.});
    first.instances.front()->set_rotation(Vec3d{0., 0., 0.5});
    first.volumes.front()->sla_support_points = {SupportPoint{Vec3f{5.f, 5.f, 10.f}}};
    first.volumes.front()->sla_drain_holes    = {DrainHole{Vec3f{5.f, 5.f, 10.f}, Vec3f{0.f, 0.f, -1.f}}};
    first.sla_points_status                   = PointsStatus::UserModified;
    const Vec3f world_point =
        (first.instances.front()->get_matrix() * Vec3d{5., 5., 10.}).cast<float>();

    SECTION("all user modified")
    {
        second.volumes.front()->sla_support_points = {SupportPoint{Vec3f{1.f, 1.f, 10.f}}};
        second.sla_points_status                   = PointsStatus::UserModified;

        const Domain::ModelObject& merged = *ModelObject::merge({first.instances.front(), second.instances.front()});

        CHECK(merged.sla_points_status == PointsStatus::UserModified);
        const Domain::SLA::SupportPoints points = SlaPoints::support_points_in_object(merged);
        REQUIRE(points.size() == 2);
        // The merged object has an identity instance, the points keep their place.
        CHECK(std::ranges::any_of(points, [&](const SupportPoint& point) { return is_approx(point.pos, world_point); }));
    }

    SECTION("one generated")
    {
        second.sla_points_status = PointsStatus::AutoGenerated;

        const Domain::ModelObject& merged = *ModelObject::merge({first.instances.front(), second.instances.front()});

        CHECK(merged.sla_points_status == PointsStatus::AutoGenerated);
        CHECK(SlaPoints::support_points_in_object(merged).empty());
        // The drain holes don't depend on the status.
        CHECK(SlaPoints::drain_holes_in_object(merged).size() == 1);
    }

    SECTION("no points")
    {
        CHECK(SlaPoints::merged_points_status({PointsStatus::NoPoints, PointsStatus::NoPoints}) == PointsStatus::NoPoints);
    }
}

TEST_CASE("Baking the instance rotation into the meshes moves the SLA points with the mesh", "[algorithms][sla-points]")
{
    Domain::Model        model;
    Domain::ModelObject& object = add_object(model, TriangleMesh::make_cube(10., 10., 10.));
    object.instances.front()->set_rotation(Vec3d{0.3, 0.2, 0.1});
    Domain::ModelVolume& volume = *object.volumes.front();
    // On mesh vertices, which are transformed in place.
    volume.sla_support_points = {SupportPoint{volume.mesh().its.vertices[3]}};
    volume.sla_drain_holes    = {DrainHole{volume.mesh().its.vertices[5], Vec3f{-1.f, 0.f, 0.f}}};

    ModelObject::bake_xy_rotation_into_meshes(object, 0);

    const Domain::ModelVolume& baked = *object.volumes.front();
    CHECK(is_approx(baked.sla_support_points.front().pos, baked.mesh().its.vertices[3]));
    CHECK(is_approx(baked.sla_drain_holes.front().pos, baked.mesh().its.vertices[5]));
    CHECK_FALSE(is_approx(baked.sla_drain_holes.front().normal, Vec3f{-1.f, 0.f, 0.f}));
}
