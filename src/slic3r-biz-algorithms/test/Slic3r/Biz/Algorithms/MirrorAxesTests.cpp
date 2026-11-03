#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/ModelVolume.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace Slic3r;
using Domain::MirrorAxes;
using Domain::Transform3d;
using Domain::Vec3d;

static Domain::ModelVolume& cube_volume(Domain::Model& model)
{
    Domain::ModelObject& object = *model.add_object();
    Biz::Algorithms::ModelObject::add_volume(&object, Biz::Algorithms::TriangleMesh::make_cube(10., 10., 10.));
    object.add_instance();
    return *object.volumes.front();
}

TEST_CASE("MirrorAxes matrix and parity", "[algorithms][mirror]")
{
    MirrorAxes none;
    CHECK_FALSE(none.any());
    CHECK_FALSE(none.is_odd());
    CHECK(none.matrix().matrix().isApprox(Transform3d::Identity().matrix()));

    MirrorAxes xz{{true, false, true}};
    CHECK(xz.any());
    CHECK_FALSE(xz.is_odd());
    CHECK(xz.matrix().linear().isApprox(Vec3d{-1., 1., -1.}.asDiagonal().toDenseMatrix()));

    xz.flip(Domain::Y);
    CHECK(xz.is_odd());
    CHECK(xz.matrix().linear().determinant() < 0.);
}

TEST_CASE("Volume mirror axes are validated against the matrix", "[algorithms][mirror]")
{
    Domain::Model         model;
    Domain::ModelVolume&  volume = cube_volume(model);

    SECTION("flags matching a left-handed matrix are kept")
    {
        volume.set_transformation(Transform3d(Eigen::Scaling(1., -1., 1.)));
        volume.set_mirror_axes(MirrorAxes{{false, true, false}});
        CHECK(volume.mirror_axes() == MirrorAxes{{false, true, false}});
        CHECK(volume.right_handed_matrix().matrix().isApprox(Transform3d::Identity().matrix()));
    }

    SECTION("left-handed matrix without flags falls back to X")
    {
        volume.set_transformation(Transform3d(Eigen::Scaling(1., -1., 1.)));
        CHECK(volume.mirror_axes() == MirrorAxes{{true, false, false}});
        CHECK(volume.right_handed_matrix().linear().determinant() > 0.);
    }

    SECTION("right-handed matrix with odd flags reports none")
    {
        volume.set_mirror_axes(MirrorAxes{{true, false, false}});
        CHECK_FALSE(volume.mirror_axes().any());
        CHECK(volume.right_handed_matrix().matrix().isApprox(volume.get_matrix().matrix()));
    }

    SECTION("X then Y mirror is a right-handed matrix with two flags")
    {
        volume.set_transformation(Transform3d(Eigen::Scaling(-1., -1., 1.)));
        volume.set_mirror_axes(MirrorAxes{{true, true, false}});
        CHECK(volume.mirror_axes() == MirrorAxes{{true, true, false}});
        CHECK(volume.right_handed_matrix().matrix().isApprox(Transform3d::Identity().matrix()));
    }
}

TEST_CASE("Left-handed instances are normalized into the volumes", "[algorithms][mirror]")
{
    Domain::Model        model;
    Domain::ModelVolume& volume = cube_volume(model);
    Domain::ModelObject& object = *model.objects.front();
    Domain::ModelInstance& instance = *object.instances.front();

    const Transform3d instance_trafo{
        Eigen::Translation3d(10., 20., 0.) * Eigen::AngleAxisd(0.4, Vec3d::UnitZ()) * Eigen::Scaling(1., -1., 1.)
    };
    const Transform3d volume_trafo{Eigen::Translation3d(1., 2., 3.) * Eigen::AngleAxisd(0.3, Vec3d::UnitX())};
    instance.set_transformation(Domain::Transformation{instance_trafo});
    volume.set_transformation(Domain::Transformation{volume_trafo});
    REQUIRE(instance.is_left_handed());

    Biz::Algorithms::ModelObject::normalize_instance_handedness(object);

    CHECK_FALSE(instance.is_left_handed());
    CHECK(volume.is_left_handed());
    CHECK(volume.mirror_axes() == MirrorAxes{{true, false, false}});
    CHECK((instance.get_matrix() * volume.get_matrix()).matrix().isApprox((instance_trafo * volume_trafo).matrix()));

    SECTION("a right-handed instance is left alone")
    {
        Biz::Algorithms::ModelObject::normalize_instance_handedness(object);
        CHECK(volume.mirror_axes() == MirrorAxes{{true, false, false}});
    }
}
