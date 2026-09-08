#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/trompeloeil.hpp>

#include "Slic3r/App/Plater/ThumbnailImageGenerator.hpp"
#include "Slic3r/App/Platform/StdMainThreadDispatcher.hpp"
#include "Slic3r/TestUtils/AppInstanceMessageHandlerScope.hpp"
#include "Slic3r/TestUtils/JobManagerScope.hpp"
#include "Slic3r/TestUtils/ScopedThreadDispatcher.hpp"
#include "Slic3r/TestUtils/TestData.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Domain/Types.hpp"

#include "Slic3r/Directories.hpp"

using Slic3r::Domain::BedInstance;
using Slic3r::Domain::BedRef;
using Slic3r::Domain::BedRefs;
using Slic3r::Domain::ConfigContainer;
using Slic3r::Domain::Project;
using Slic3r::Domain::SelectionId;
using Slic3r::Domain::Transform3d;
using Slic3r::Domain::Vec3d;

namespace TriMesh = Slic3r::Biz::Algorithms::TriangleMesh;

struct SlicingInputChangedListener : Slic3r::Biz::ISlicingInputChangedListener
{
    MAKE_MOCK1(on_slicing_input_changed, void(const Slic3r::Domain::BedRef&));
    MAKE_MOCK1(on_slicing_input_removed, void(const Slic3r::Domain::BedRef&));
};

using namespace Slic3r;
using namespace Slic3r::Biz;
using namespace trompeloeil;
namespace fs = boost::filesystem;

struct SceneInteractorFixture
{
    SceneInteractorFixture()
    {
        set_data_dir(Tests::get_datadir().string());

        project_interactor.preset_interactor()
            .load_preset_bundle(Preset::IO::BundlePaths::make_test_runtime(Tests::get_datadir()));

        project_interactor.scene_interactor().add_listener<ISlicingInputChangedListener>(&slicing_input_changed_listener);

        {
            ALLOW_CALL(slicing_input_changed_listener, on_slicing_input_changed(_));
            project_interactor.new_project();
        }
    }

    SlicingInputChangedListener slicing_input_changed_listener;
    Domain::Workbench workbench;

    App::Platform::StdMainThreadDispatcher dispatcher;
    Tests::AppInstanceMessageHandlerScope app_instance_message_handler_scope{dispatcher};
    Tests::JobManagerScope job_manager_scope{dispatcher};
    App::Plater::ThumbnailImageGenerator thumbnail_image_generator;
    ProjectInteractor project_interactor{workbench, dispatcher, thumbnail_image_generator};
    Scene::SceneInteractor& scene_interactor{project_interactor.scene_interactor()};
    Tests::ScopedThreadDispatcher thread_dispatcher{dispatcher};

    fs::path data_dir{Tests::get_datadir()};
    fs::path preset_bundle_dir{data_dir / "presets"};
    fs::path config_dir{data_dir / "configs"};
};

TEST_CASE_METHOD(SceneInteractorFixture, "Scene Interactor Bed Tracking", "[SceneInteractor]")
{
    const auto& p                = project_interactor.selected_project();
    const auto& bed              = *p.bed_container().beds().front();
    const auto& bed_center       = bed.center();
    const auto bed_size          = bed.contour_aabb_extent();
    const auto& object_selection = scene_interactor.object_selection();

    auto& cc                  = p.config_containers().front();
    const auto& bed_instances = cc->bed_instances();
    const auto bi1_id         = bed_instances[0]->id().id;
    const double cube_side    = 100; // mm

    /*
    Legend:
    +-<1>-+
    |     |     Bed (with ID symbol <1> --- i.e. id is stored in bi1_id)
    +-----+
    (1)        Selected instance
    [1]        Unselected instance
    */

    // add object on first bed
    {
        REQUIRE_CALL(slicing_input_changed_listener, on_slicing_input_changed(_)).WITH(_1.instance_id == bi1_id);
        scene_interactor.new_object_from_mesh(Domain::TriangleMesh{TriMesh::make_cube(cube_side, cube_side, cube_side)});
    }

    // center object on first bed
    Transform3d xform = Transform3d::Identity();
    {
        xform.translate(Vec3d{bed_center.x() - cube_side / 2, bed_center.y() - cube_side / 2, 0});
        REQUIRE_CALL(slicing_input_changed_listener, on_slicing_input_changed(_)).WITH(_1.instance_id == bi1_id);
        scene_interactor.transform_selection(xform.matrix());

        /*
        selection: instance mode
        +y A +-<1>-+ 
           | | (1) | 
           | +-----+ 
           o----->
                 +x
        */

        REQUIRE(bed_instances.size() == 1);
        REQUIRE(p.unplaced_model_instances().empty());
        REQUIRE(bed_instances[0]->model_instances.size() == 1);
    }

    const auto first_el_ref = object_selection.elements.front();

    // add second bed instance
    {
        REQUIRE_CALL(slicing_input_changed_listener, on_slicing_input_changed(ANY(const Domain::BedRef&)));
        scene_interactor.add_bed_instance(cc->id().id);

        /*
        selection: instance mode
        +y A +-<1>-+ +-<2>-+
           | | (1) | |     |
           | +-----+ +-----+
           o----->
                 +x
        */

        REQUIRE(bed_instances.size() == 2);
        REQUIRE(p.unplaced_model_instances().empty());
        REQUIRE(bed_instances[0]->model_instances[0]->id().id == first_el_ref.instance_id);
        REQUIRE(bed_instances[0]->model_instances.size() == 1);
        REQUIRE(bed_instances[1]->model_instances.empty());
    };

    Vec3d bed_pitch = bed_instances[1]->transformation.get_offset()
        - bed_instances[0]->transformation.get_offset();
    bed_pitch.y() += bed_size.y() * 2;
    const auto bi2_id = bed_instances[1]->id().id;

    // move object to second bed center
    {
        xform = Transform3d::Identity();
        xform.translate(Vec3d{bed_pitch.x(), 0, 0});

        REQUIRE_CALL(slicing_input_changed_listener, on_slicing_input_changed(_)).WITH(_1.instance_id == bi1_id);
        REQUIRE_CALL(slicing_input_changed_listener, on_slicing_input_changed(_)).WITH(_1.instance_id == bi2_id);
        scene_interactor.transform_selection(xform.matrix());

        /*
        selection: instance mode
        +y A +-<1>-+ +-<2>-+
           | |     | | (1) |
           | +-----+ +-----+
           o----->
                  +x
        */

        REQUIRE(p.unplaced_model_instances().empty());
        REQUIRE(bed_instances[0]->model_instances.empty());
        REQUIRE(bed_instances[1]->model_instances.size() == 1);
        REQUIRE(bed_instances[1]->model_instances[0]->id().id == first_el_ref.instance_id);
    }

    // move object outside second bed
    {
        // Outside of second bed
        xform = Transform3d::Identity();
        xform.translate(Vec3d{0, bed_pitch.y(), 0});

        REQUIRE_CALL(slicing_input_changed_listener, on_slicing_input_changed(_)).WITH(_1.instance_id == bi2_id);
        scene_interactor.transform_selection(xform.matrix());

        /*
        selection: instance mode
                       (1)
        +y A +-<1>-+ +-<2>-+
           | |     | |     |
           | +-----+ +-----+
           o----->
                +x
        */

        REQUIRE(p.unplaced_model_instances().size() == 1);
        REQUIRE(p.unplaced_model_instances()[0]->id().id == first_el_ref.instance_id);
        REQUIRE(bed_instances[0]->model_instances.empty());
        REQUIRE(bed_instances[1]->model_instances.empty());
    }

    // add third bed instance
    {
        // Single object amid of second bed
        REQUIRE_CALL(slicing_input_changed_listener, on_slicing_input_changed(ANY(const Domain::BedRef&)));
        scene_interactor.add_bed_instance(cc->id().id);

        /*
        selection: instance mode
                       (1)
        +y A +-<1>-+ +-<2>-+ +-<3>-+
           | |     | |     | |     |
           | +-----+ +-----+ +-----+
           o----->
                 +x
        */

        REQUIRE(bed_instances.size() == 3);
        REQUIRE(bed_instances[2]->model_instances.empty());
    }

    const auto bi3_id = bed_instances[2]->id().id;

    // move object back to second bed center
    {
        xform = Transform3d::Identity();
        xform.translate(Vec3d{0, -bed_pitch.y(), 0});
        REQUIRE_CALL(slicing_input_changed_listener, on_slicing_input_changed(_)).WITH(_1.instance_id == bi2_id);
        scene_interactor.transform_selection(xform.matrix());

        /*
        selection: instance mode
        +y A +-<1>-+ +-<2>-+ +-<3>-+
           | |     | | (1) | |     |
           | +-----+ +-----+ +-----+
           o----->
                +x
        */

        REQUIRE(p.unplaced_model_instances().empty());
        REQUIRE(bed_instances[1]->model_instances.size() == 1);
        REQUIRE(bed_instances[1]->model_instances[0]->id().id == first_el_ref.instance_id);
    }

    // add object instance on third bed center
    Domain::ElementRef second_el_ref;
    {
        REQUIRE_CALL(slicing_input_changed_listener, on_slicing_input_changed(_)).WITH(_1.instance_id == bi3_id);
        scene_interactor.add_instance(Domain::Vec2d(bed_pitch.x(), 0));
        second_el_ref = scene_interactor.object_selection().elements.front();

        /*
        selection: instance mode
        +y A +-<1>-+ +-<2>-+ +-<3>-+
           | |     | | [1] | | (2) |
           | +-----+ +-----+ +-----+
           o----->
                +x
        */

        REQUIRE(p.unplaced_model_instances().size() == 0);
        REQUIRE(bed_instances[2]->model_instances.size() == 1);
        REQUIRE(bed_instances[2]->model_instances[0]->id().id == second_el_ref.instance_id);
    }

    // delete second bed instance
    {
        Transform3d bed_xform = bed_instances[1]->transformation.get_matrix();
        REQUIRE_CALL(slicing_input_changed_listener, on_slicing_input_changed(_)).WITH(_1.instance_id == bi3_id);
        REQUIRE_CALL(slicing_input_changed_listener, on_slicing_input_removed(_)).WITH(_1.instance_id == bi2_id);
        scene_interactor.remove_bed_instance({cc->id().id, bed_instances[1]->id().id});

        /*
        selection: instance mode
        +y A +-<1>-+ +-<3>-+
           | |     | | (2) |
           | +-----+ +-----+
           o----->
                +x
        */

        REQUIRE(bed_instances.size() == 2);
        REQUIRE(bed_instances[1]->id().id == bi3_id);
        REQUIRE(bed_instances[1]->transformation.get_matrix().isApprox(bed_xform));

        REQUIRE(p.unplaced_model_instances().empty());
        REQUIRE(bed_instances[0]->model_instances.empty());
        REQUIRE(bed_instances[1]->model_instances.size() == 1);
        REQUIRE(bed_instances[1]->model_instances[0]->id().id == second_el_ref.instance_id);
    }

    // delete first bed instance
    {
        Transform3d bed_xform = bed_instances[0]->transformation.get_matrix();
        REQUIRE_CALL(slicing_input_changed_listener, on_slicing_input_changed(_)).WITH(_1.instance_id == bi3_id);
        REQUIRE_CALL(slicing_input_changed_listener, on_slicing_input_removed(_)).WITH(_1.instance_id == bi1_id);
        scene_interactor.remove_bed_instance({cc->id().id, bed_instances[0]->id().id });

        /*
        selection: instance mode
        +y A +-<3>-+
           | | (2) |
           | +-----+
           o----->
                +x
        */

        REQUIRE(bed_instances.size() == 1);
        REQUIRE(bed_instances[0]->id().id == bi3_id);
        REQUIRE(bed_instances[0]->transformation.get_matrix().isApprox(bed_xform));
        REQUIRE(p.unplaced_model_instances().empty());
        REQUIRE(bed_instances[0]->model_instances.size() == 1);
        REQUIRE(bed_instances[0]->model_instances[0]->id().id == second_el_ref.instance_id);
    }

    Domain::ElementRef third_el_ref;

    // add object instance outside bed
    {
        scene_interactor.add_instance(
            Domain::Vec2d(bed_center.x() - cube_side / 2 + bed_pitch.x(), bed_center.y() - cube_side / 2)
        );
        third_el_ref = scene_interactor.object_selection().elements.front();

        /*
        selection: instance mode
        +y A +-<3>-+
           | | [2] |   (3)
           | +-----+
           o----->
                 +x
        */

        REQUIRE(p.unplaced_model_instances().size() == 1);
        REQUIRE(p.unplaced_model_instances()[0]->id().id == third_el_ref.instance_id);
        REQUIRE(bed_instances[0]->model_instances.size() == 1);
        REQUIRE(bed_instances[0]->model_instances[0]->id().id == second_el_ref.instance_id);
    }

    // add bed instance
    {
        REQUIRE_CALL(slicing_input_changed_listener, on_slicing_input_changed(_));
        scene_interactor.add_bed_instance(cc->id().id);

        /*
        selection: instance mode
        +y a +-<3>-+ +-<4>-+
           | | [2] | | (3) |
           | +-----+ +-----+
           o----->
                +x
        */

        REQUIRE(p.unplaced_model_instances().empty());
        REQUIRE(bed_instances[0]->model_instances.size() == 1);
        REQUIRE(bed_instances[0]->model_instances[0]->id().id == second_el_ref.instance_id);
        REQUIRE(bed_instances[1]->model_instances.size() == 1);
        REQUIRE(bed_instances[1]->model_instances[0]->id().id == third_el_ref.instance_id);
    }

    // move second object instance outside if second bed
    const auto bi4_id = bed_instances[1]->id().id;
    {
        xform = Transform3d::Identity();
        xform.translate(Vec3d{bed_pitch.x(), 0, 0});
        REQUIRE_CALL(slicing_input_changed_listener, on_slicing_input_changed(_)).WITH(_1.instance_id == bi4_id);
        scene_interactor.transform_selection(xform.matrix());

        /*
        selection: instance mode
        +y A   +-<3>-+ +-<4>-+
           |   | [2] | |     |  (3)
           |   +-----+ +-----+
           o----->
                +x
        */

        REQUIRE(p.unplaced_model_instances().size() == 1);
        REQUIRE(p.unplaced_model_instances()[0]->id().id == third_el_ref.instance_id);
        REQUIRE(bed_instances[0]->model_instances.size() == 1);
        REQUIRE(bed_instances[0]->model_instances[0]->id().id == second_el_ref.instance_id);
        REQUIRE(bed_instances[1]->model_instances.empty());
    }

    // add volume (instances partly outside beds)
    {
        xform = Transform3d::Identity();
        xform.translate(Vec3d{-bed_size.x(), 0, 0});

        REQUIRE_CALL(slicing_input_changed_listener, on_slicing_input_changed(_)).WITH(_1.instance_id == bi3_id);
        scene_interactor.add_volume_from_mesh(
            Domain::TriangleMesh{TriMesh::make_cube(cube_side, cube_side, cube_side)},
            Domain::ModelVolumeType::MODEL_PART,
            "Test volume",
            xform.matrix()
        );

        /*
        selection: volume mode
        +y A   +-<3>-+ +-<4>-+
           | [ |  2] | |  [  |   3]
           |   +-----+ +-----+
           o----->
                +x
        */

        REQUIRE(p.unplaced_model_instances().size() == 2);
        REQUIRE(p.unplaced_model_instances()[0]->id().id == second_el_ref.instance_id);
        REQUIRE(p.unplaced_model_instances()[1]->id().id == third_el_ref.instance_id);
        REQUIRE(bed_instances[0]->model_instances.size() == 0);
        REQUIRE(bed_instances[1]->model_instances.size() == 0);
    }

    // move volume (instance on bed 3 inside, instance on bed 4 outside)
    {
        xform = Transform3d::Identity();
        xform.translate(Vec3d{bed_pitch.x(), 0, 0});

        REQUIRE_CALL(slicing_input_changed_listener, on_slicing_input_changed(_)).WITH(_1.instance_id == bi3_id);
        scene_interactor.transform_selection(xform.matrix());

        /*
        selection: volume mode
        +y A   +-<3>-+ +-<4>-+
           |   | [2] | |     |  [3]
           |   +-----+ +-----+
           o----->
                +x
        */

        REQUIRE(p.unplaced_model_instances().size() == 1);
        REQUIRE(p.unplaced_model_instances()[0]->id().id == third_el_ref.instance_id);
        REQUIRE(bed_instances[0]->model_instances.size() == 1);
        REQUIRE(bed_instances[0]->model_instances[0]->id().id == second_el_ref.instance_id);
    }

    // Queue must be clear before ProjectInteractor can be destroyed.
    // dispatcher.close();
}

TEST_CASE_METHOD(SceneInteractorFixture, "Bed selection", "[SceneInteractor]")
{
    const Project& project{project_interactor.selected_project()};
    const SelectionId project_id{project_interactor.selected_project_id()};
    const Domain::ConfigContainer& config_container{*project.config_containers().front()};

    REQUIRE(config_container.bed_instances().size() == 1);

    const BedRef initialy_selected_instance{
        config_container.id().id,
        config_container.bed_instances().front()->id().id
    };
    BedRefs beds;
    {
        ALLOW_CALL(slicing_input_changed_listener, on_slicing_input_changed(_));
        for (std::size_t count{}; count < 4; ++count) {
            const BedInstance instance{scene_interactor.add_bed_instance(config_container.id().id)};
            beds.push_back(BedRef{config_container.id().id, instance.id().id});
        }
    }

    REQUIRE(!scene_interactor.bed_selection().empty());
    CHECK(scene_interactor.bed_selection().is_selected(initialy_selected_instance));

    CHECK(scene_interactor.bed_selection().select_one(beds[2]));
    CHECK(scene_interactor.bed_selection().is_selected(beds[2]));
    CHECK(scene_interactor.bed_selection().toggle(beds[3]));

    CHECK(scene_interactor.bed_selection().is_selected(beds[2]));
    CHECK(scene_interactor.bed_selection().is_selected(beds[3]));
    CHECK(scene_interactor.bed_selection().last_selected_bed() == beds[3]);

    CHECK(scene_interactor.bed_selection().toggle(beds[2]));
    CHECK(!scene_interactor.bed_selection().is_selected(beds[2]));
    CHECK(!scene_interactor.bed_selection().toggle(beds[3]));
    CHECK(!scene_interactor.bed_selection().select_one(beds[3]));

    // After this the selection should be 0, 3.
    CHECK(scene_interactor.bed_selection().toggle(beds[0]));

    {
        ALLOW_CALL(slicing_input_changed_listener, on_slicing_input_changed(_));
        project_interactor.new_project();
    }

    const Domain::Project& another_project{project_interactor.selected_project()};
    const Domain::ConfigContainer& another_config_container{*another_project.config_containers().front()};

    REQUIRE(another_config_container.bed_instances().size() == 1);

    const BedRef another_project_instance{
        another_config_container.id().id,
        another_config_container.bed_instances().front()->id().id
    };

    REQUIRE(!scene_interactor.bed_selection().empty());
    CHECK(scene_interactor.bed_selection().is_selected(another_project_instance));

    project_interactor.select_project(project_id);

    // The original project selection is remembered.
    CHECK(scene_interactor.bed_selection().is_selected(beds[3]));
    CHECK(scene_interactor.bed_selection().is_selected(beds[0]));
    CHECK(scene_interactor.bed_selection().last_selected_bed() == beds[0]);
}

struct TransformInProgressFixture : SceneInteractorFixture
{
    TransformInProgressFixture()
    {
        const auto cube{
            []()
            { return Domain::TriangleMesh{TriMesh::make_cube(cube_side, cube_side, cube_side)}; }};

        ALLOW_CALL(slicing_input_changed_listener, on_slicing_input_changed(_));

        const Domain::Project& project{project_interactor.selected_project()};

        scene_interactor.new_object_from_mesh(cube());
        const Domain::ModelObject* first_object{project.model().objects.front()};
        first_instance_ref = {first_object->id().id, first_object->instances.front()->id().id};

        // Two volumes, selecting a single volume would otherwise be promoted to Instance mode.
        scene_interactor.new_object_from_mesh(cube());
        scene_interactor.add_volume_from_mesh(cube(), Domain::ModelVolumeType::MODEL_PART);
        const Domain::ModelObject* second_object{project.model().objects.back()};
        second_instance_ref = {second_object->id().id, second_object->instances.front()->id().id};
        second_volume_ref   = {
            second_object->id().id,
            second_object->instances.front()->id().id,
            second_object->volumes.front()->id().id};

        const Domain::ConfigContainer* config_container{project.config_containers().front().get()};
        scene_interactor.add_bed_instance(config_container->id().id);
        const Domain::ConfigContainer::BedInstanceList& bed_instances{
            config_container->bed_instances()};
        second_bed_ref = Domain::BedRef{config_container->id().id, bed_instances[1]->id().id};
        bed_pitch      = bed_instances[1]->transformation.get_offset()
            - bed_instances[0]->transformation.get_offset();
    }

    static constexpr double cube_side{20.0}; // mm

    static Domain::SquareMatrix4d translation(double offset)
    {
        Transform3d xform{Transform3d::Identity()};
        xform.translate(Vec3d{offset, 0, 0});
        return xform.matrix();
    }

    Domain::ElementRef first_instance_ref;
    Domain::ElementRef second_instance_ref;
    Domain::ElementRef second_volume_ref;
    Domain::BedRef second_bed_ref;
    Vec3d bed_pitch;
};

TEST_CASE_METHOD(
    TransformInProgressFixture,
    "Selection change during object drag",
    "[SceneInteractor]")
{
    const Project& project{project_interactor.selected_project()};
    Scene::TransformMemento memento;

    scene_interactor.set_object_selection(
        Scene::ObjectSelection{Scene::SelectionMode::Instance, {second_instance_ref}});
    const Domain::ModelInstance& second_instance{*project.find_instance_by_id(
        second_instance_ref.object_id,
        second_instance_ref.instance_id)};
    const double second_original_x{second_instance.get_matrix().translation().x()};
    scene_interactor.transform_selection(translation(10), memento);

    scene_interactor.set_object_selection(Scene::ObjectSelection{
        Scene::SelectionMode::Instance,
        {first_instance_ref, second_instance_ref}});
    const Domain::ModelInstance& first_instance{
        *project.find_instance_by_id(first_instance_ref.object_id, first_instance_ref.instance_id)};
    const double first_original_x{first_instance.get_matrix().translation().x()};
    scene_interactor.transform_selection(translation(20), memento);

    CHECK(first_instance.get_matrix().translation().x()
          == Catch::Approx(first_original_x + 20.0).margin(1e-6));
    CHECK(second_instance.get_matrix().translation().x()
          == Catch::Approx(second_original_x + 20.0).margin(1e-6));
}

TEST_CASE_METHOD(
    TransformInProgressFixture,
    "Bed instance removed during object drag",
    "[SceneInteractor]")
{
    const Project& project{project_interactor.selected_project()};
    Scene::TransformMemento memento;

    scene_interactor.set_object_selection(
        Scene::ObjectSelection{Scene::SelectionMode::Instance, {first_instance_ref}});
    scene_interactor.transform_selection(translation(bed_pitch.x()), memento);
    REQUIRE(project.config_containers().front()->bed_instances()[1]->model_instances.size() == 1);
    REQUIRE(memento.changes.updated_beds.contains(second_bed_ref));

    {
        ALLOW_CALL(slicing_input_changed_listener, on_slicing_input_changed(_));
        ALLOW_CALL(slicing_input_changed_listener, on_slicing_input_removed(_));
        scene_interactor.remove_bed_instance(second_bed_ref);
    }

    {
        ALLOW_CALL(slicing_input_changed_listener, on_slicing_input_changed(_));
        scene_interactor.finalize_transform_selection(memento, false);
    }

    CHECK(project.config_containers().front()->bed_instances().size() == 1);
    CHECK(project.model().objects.size() == 1);
    CHECK(memento.elements.empty());
}

TEST_CASE_METHOD(
    TransformInProgressFixture,
    "Selection emptied during volume drag",
    "[SceneInteractor]")
{
    const Project& project{project_interactor.selected_project()};
    Scene::TransformMemento memento;

    scene_interactor.set_object_selection(
        Scene::ObjectSelection{Scene::SelectionMode::Volume, {second_volume_ref}});
    const Domain::ModelInstance& second_instance{
        *project.find_instance_by_id(second_volume_ref.object_id, second_volume_ref.instance_id)};
    const Domain::ModelVolume& second_volume{
        *project.find_volume_by_id(second_volume_ref.object_id, second_volume_ref.volume_id)};
    const auto world_x = [&]
    { return (second_instance.get_matrix() * second_volume.get_matrix()).translation().x(); };
    const double world_original_x{world_x()};

    scene_interactor.transform_selection(translation(10), memento);
    REQUIRE(world_x() == Catch::Approx(world_original_x + 10.0).margin(1e-6));

    scene_interactor.set_object_selection(Scene::ObjectSelection{Scene::SelectionMode::Volume, {}});
    scene_interactor.transform_selection(translation(20), memento);

    {
        ALLOW_CALL(slicing_input_changed_listener, on_slicing_input_changed(_));
        scene_interactor.finalize_transform_selection(memento, false);
    }

    CHECK(world_x() == Catch::Approx(world_original_x + 10.0).margin(1e-6));
}

TEST_CASE_METHOD(
    TransformInProgressFixture,
    "Dragged volume deleted during drag",
    "[SceneInteractor]")
{
    const Project& project{project_interactor.selected_project()};
    Scene::TransformMemento memento;

    scene_interactor.set_object_selection(
        Scene::ObjectSelection{Scene::SelectionMode::Volume, {second_volume_ref}});
    scene_interactor.transform_selection(translation(10), memento);
    REQUIRE(memento.elements.contains(second_volume_ref));

    scene_interactor.set_object_selection(Scene::ObjectSelection{
        Scene::SelectionMode::Instance,
        {{second_volume_ref.object_id, second_volume_ref.instance_id}}});
    {
        ALLOW_CALL(slicing_input_changed_listener, on_slicing_input_changed(_));
        scene_interactor.delete_selected_elements();
    }
    REQUIRE(project.model().objects.size() == 1);

    scene_interactor.set_object_selection(Scene::ObjectSelection{Scene::SelectionMode::Volume, {}});
    scene_interactor.finalize_transform_selection(memento, true);

    CHECK(memento.elements.empty());
}

TEST_CASE_METHOD(TransformInProgressFixture, "Wipe tower bed removed during drag", "[SceneInteractor]")
{
    const Project& project{project_interactor.selected_project()};
    Scene::TransformMemento memento;

    const Domain::ElementRef wipe_tower_ref{
        Domain::SlicingId{project_interactor.selected_project_id(), second_bed_ref.instance_id}
    };
    scene_interactor.set_object_selection(
        Scene::ObjectSelection{Scene::SelectionMode::Instance, {wipe_tower_ref}}
    );

    scene_interactor.transform_selection(translation(10), memento);
    REQUIRE(memento.elements.contains(wipe_tower_ref));

    {
        ALLOW_CALL(slicing_input_changed_listener, on_slicing_input_changed(_));
        ALLOW_CALL(slicing_input_changed_listener, on_slicing_input_removed(_));
        scene_interactor.remove_bed_instance(second_bed_ref);
    }

    scene_interactor.transform_selection(translation(20), memento);

    {
        ALLOW_CALL(slicing_input_changed_listener, on_slicing_input_changed(_));
        scene_interactor.finalize_transform_selection(memento, false);
    }

    CHECK(project.config_containers().front()->bed_instances().size() == 1);
    CHECK_FALSE(scene_interactor.object_selection().is_selected(wipe_tower_ref));
    CHECK(memento.elements.empty());
}
