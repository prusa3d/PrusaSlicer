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
