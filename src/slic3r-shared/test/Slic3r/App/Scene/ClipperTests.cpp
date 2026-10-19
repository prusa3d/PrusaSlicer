#include <catch2/catch_test_macros.hpp>

#include "Slic3r/App/Scene/Clipper.hpp"
#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/ModelInstance.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/ModelVolume.hpp"

using Slic3r::App::Scene::Clipper;
using Slic3r::Domain::Transform3d;
using Slic3r::Domain::Transformation;
using Slic3r::Domain::TriangleMesh;
using Slic3r::Domain::Vec3d;

namespace TriMesh = Slic3r::Biz::Algorithms::TriangleMesh;

TEST_CASE("Clipper cuts the SLA backend mesh instead of the model volumes", "[clipper]")
{
    Slic3r::Domain::Model model;
    Slic3r::Domain::ModelObject* object = model.add_object();
    Slic3r::Biz::Algorithms::ModelObject::add_volume(object, TriMesh::make_cube(20., 20., 20.));
    Slic3r::Biz::Algorithms::ModelObject::add_volume(object, TriMesh::make_cube(5., 5., 5.));
    const Slic3r::Domain::ModelInstance* instance = object->add_instance();

    Clipper clipper;
    clipper.update(object, instance);
    CHECK(clipper.object_clippers.size() == 2);

    // E.g. the hollowed mesh in print object coordinates, with its way back to the model.
    auto sla_mesh = std::make_shared<const TriangleMesh>(TriMesh::make_cube(10., 10., 10.));
    const std::weak_ptr<const TriangleMesh> sla_mesh_alive{sla_mesh};
    Transform3d to_model = Transform3d::Identity();
    to_model.translate(Vec3d{1., 2., 3.});

    clipper.update(object, instance, 0., false, std::move(sla_mesh), Transformation{to_model});
    REQUIRE(clipper.object_clippers.size() == 1);
    CHECK(clipper.object_clippers.front().second.get_matrix().isApprox(to_model));
    // The mesh clipper keeps only a pointer, the clipper owns the mesh.
    CHECK_FALSE(sla_mesh_alive.expired());

    // Without the backend mesh (e.g. it was invalidated) the model volumes are cut again.
    clipper.update(object, instance);
    CHECK(clipper.object_clippers.size() == 2);
    CHECK(sla_mesh_alive.expired());
}
