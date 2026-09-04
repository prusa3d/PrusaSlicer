#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>

#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "libslic3r/ModelUtils.hpp"

using namespace Catch;
using namespace Slic3r::Biz::Algorithms;
using Catch::Matchers::Equals;

using Slic3r::Domain::ModelInstanceList;
using Slic3r::Biz::Slicing::with_limited_instances;
using Slic3r::Domain::ModelInstance;


Slic3r::Domain::Model generate_cubes(const int count, const int row_size)
{
    const float size{20};
    Slic3r::Domain::Model model;
    for (int i{}; i < count; ++i) {
        const int row{i / row_size};
        const int column{i % row_size};

        namespace TriMesh = Slic3r::Biz::Algorithms::TriangleMesh;
        Slic3r::Domain::TriangleMesh cube_mesh = TriMesh::make_cube(size, size, size);
        cube_mesh.translate(Slic3r::Domain::Vec3f{column * (size + 5.0f), row * (size + 5.0f), 0.0f});

        Slic3r::Domain::ModelObject* model_object = model.add_object();
        ModelObject::add_volume(model_object, cube_mesh);
        model_object->add_instance();
        ModelObject::ensure_on_bed(*model_object);
    }
    return model;
}

TEST_CASE("With limited instances temporarily removes instances and objects", "[slicing-model-utils]")
{
    Slic3r::Domain::Model model{generate_cubes(5, 5)};

    namespace TriMesh = Slic3r::Biz::Algorithms::TriangleMesh;
    Slic3r::Domain::TriangleMesh cube_mesh = TriMesh::make_cube(10, 10, 10);
    Slic3r::Domain::ModelObject* model_object = model.objects.front();
    ModelObject::add_volume(model_object, cube_mesh);
    model_object->add_instance();
    ModelObject::ensure_on_bed(*model_object);

    ModelInstance* first_object_second_instance{model.objects.front()->instances[1]};
    ModelInstance* third_object_instance{model.objects[2]->instances.front()};
    ModelInstance* fifth_object_instance{model.objects[4]->instances.front()};

    const ModelInstanceList instances_to_keep{
        model.objects.front()->instances[1],
        model.objects[2]->instances.front(),
        model.objects[4]->instances.front()
    };

    with_limited_instances(model, instances_to_keep, [&](){
        REQUIRE(model.objects.size() == 3);
        CHECK_THAT(model.objects[0]->instances, Equals(ModelInstanceList{first_object_second_instance}));
        CHECK_THAT(model.objects[1]->instances, Equals(ModelInstanceList{third_object_instance}));
        CHECK_THAT(model.objects[2]->instances, Equals(ModelInstanceList{fifth_object_instance}));
    });
}
