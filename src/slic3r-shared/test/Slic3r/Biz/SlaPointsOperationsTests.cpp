#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/SlaPoints.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Biz/Utils/CutUtils.hpp"
#include "Slic3r/Domain/Model.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace Slic3r;
using Domain::SLA::DrainHole;
using Domain::SLA::PointsStatus;
using Domain::SLA::SupportPoint;
using Domain::Vec3d;
using Domain::Vec3f;

namespace SlaPoints = Biz::Algorithms::SlaPoints;
namespace TriMesh   = Biz::Algorithms::TriangleMesh;

TEST_CASE("Cutting an object drops its SLA points and holes", "[sla-points]")
{
    Domain::Model        model;
    Domain::ModelObject& object = *model.add_object();
    Biz::Algorithms::ModelObject::add_volume(&object, TriMesh::make_cube(20., 20., 20.));
    // A negative volume in the upper half is copied by the cut, not cut.
    Domain::ModelVolume& negative = *Biz::Algorithms::ModelObject::add_volume(
        &object, TriMesh::make_cube(4., 4., 4.), Domain::ModelVolumeType::NEGATIVE_VOLUME
    );
    negative.set_offset(Vec3d{8., 8., 14.});
    object.add_instance();
    object.volumes.front()->sla_support_points = {SupportPoint{Vec3f{10.f, 10.f, 0.f}}};
    negative.sla_support_points                = {SupportPoint{Vec3f{2.f, 2.f, 0.f}}};
    negative.sla_drain_holes                   = {DrainHole{Vec3f{2.f, 2.f, 4.f}, Vec3f{0.f, 0.f, -1.f}}};
    object.sla_points_status                   = PointsStatus::UserModified;

    Biz::ModelObjectCutAttributes attributes;
    attributes.keep_as_parts = false;
    Biz::Cut cut(&object, 0, Domain::Transform3d{Eigen::Translation3d{0., 0., 10.}}, attributes);
    const Domain::ModelObjectPtrs& result = cut.perform_with_plane();

    REQUIRE(result.size() == 2);
    for (const Domain::ModelObject* part : result) {
        CHECK(part->sla_points_status == PointsStatus::NoPoints);
        for (const Domain::ModelVolume* volume : part->volumes) {
            CHECK(volume->sla_support_points.empty());
            CHECK(volume->sla_drain_holes.empty());
        }
    }
}
