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
#include "Slic3r/Biz/Scene/BedPlacement.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Domain/Types.hpp"

#include "Slic3r/Directories.hpp"

using Slic3r::Domain::Transform3d;
using Slic3r::Domain::Vec2d;
using Slic3r::Domain::Vec3d;

namespace TriMesh = Slic3r::Biz::Algorithms::TriangleMesh;

namespace {

using namespace Slic3r;

struct SlicingInputChangedListener : Biz::ISlicingInputChangedListener
{
    MAKE_MOCK1(on_slicing_input_changed, void(const Domain::BedRef&));
    MAKE_MOCK1(on_slicing_input_removed, void(const Domain::BedRef&));
};

struct VirtualBedFixture
{
    VirtualBedFixture()
    {
        set_data_dir(Tests::get_datadir().string());

        project_interactor.preset_interactor()
            .load_preset_bundle(Biz::Preset::IO::BundlePaths::make_test_runtime(Tests::get_datadir()));

        project_interactor.scene_interactor()
            .add_listener<Biz::ISlicingInputChangedListener>(&slicing_input_changed_listener);

        m_allow_slicing_changed = NAMED_ALLOW_CALL(slicing_input_changed_listener, on_slicing_input_changed(trompeloeil::_));
        m_allow_slicing_removed = NAMED_ALLOW_CALL(slicing_input_changed_listener, on_slicing_input_removed(trompeloeil::_));
        project_interactor.new_project();
    }

    SlicingInputChangedListener slicing_input_changed_listener;
    std::unique_ptr<trompeloeil::expectation> m_allow_slicing_changed;
    std::unique_ptr<trompeloeil::expectation> m_allow_slicing_removed;

    Domain::Workbench workbench;

    App::Platform::StdMainThreadDispatcher dispatcher;
    Tests::AppInstanceMessageHandlerScope app_instance_message_handler_scope{dispatcher};
    Tests::JobManagerScope job_manager_scope{dispatcher};
    App::Plater::ThumbnailImageGenerator thumbnail_image_generator;
    Biz::ProjectInteractor project_interactor{workbench, dispatcher, thumbnail_image_generator};
    Biz::Scene::SceneInteractor& scene_interactor{project_interactor.scene_interactor()};
    Tests::ScopedThreadDispatcher thread_dispatcher{dispatcher};
};

} // namespace

TEST_CASE_METHOD(VirtualBedFixture, "next_bed_placement predicts the same position layout assigns", "[VirtualBedPreview]")
{
    const auto& p = project_interactor.selected_project();
    auto& cc = p.config_containers().front();
    const Vec2d gap{20.0, 20.0};

    Biz::Scene::BedPlacement placement;
    const auto predicted = placement.next_bed_placement(p, cc->id().id, gap);
    REQUIRE(predicted.has_value());

    // Materialize: add a second bed — this internally calls layout() with the same gap.
    Domain::BedInstance& new_inst = scene_interactor.add_bed_instance(cc->id().id);

    // The prediction must match what layout() actually computed.
    REQUIRE(new_inst.matrix().isApprox(*predicted, 1e-6));
}

TEST_CASE_METHOD(VirtualBedFixture, "next_bed_placement returns nullopt for unknown cc", "[VirtualBedPreview]")
{
    const auto& p = project_interactor.selected_project();
    Biz::Scene::BedPlacement placement;
    auto next = placement.next_bed_placement(p, Domain::INVALID_ID, Vec2d{20.0, 20.0});
    REQUIRE_FALSE(next.has_value());
}

TEST_CASE_METHOD(VirtualBedFixture, "virtual bed preview accepts a selection that would land inside", "[VirtualBedPreview]")
{
    const auto& p = project_interactor.selected_project();
    auto& cc = p.config_containers().front();
    const auto& bed = cc->bed();
    const double cube_side = 50.0;

    // Add a cube and center it on the first bed.
    scene_interactor.new_object_from_mesh(Domain::TriangleMesh{TriMesh::make_cube(cube_side, cube_side, cube_side)});
    const Vec2d bed_center = bed.center();
    {
        Transform3d xform = Transform3d::Identity();
        xform.translate(Vec3d{bed_center.x() - cube_side / 2, bed_center.y() - cube_side / 2, 0});
        scene_interactor.transform_selection(xform.matrix());
    }
    REQUIRE(p.unplaced_model_instances().empty());

    // Where would the next bed go?
    Biz::Scene::BedPlacement placement;
    auto next = placement.next_bed_placement(p, cc->id().id, Vec2d{20.0, 20.0});
    REQUIRE(next.has_value());

    // Move the cube so its center ends up at the would-be bed's center.
    // Cube's current bbox-center in world = first bed's center.
    // Virtual bed center in world = (*next) * bed.center().
    const Vec3d virtual_bed_center_world = *next * Vec3d{bed_center.x(), bed_center.y(), 0};
    {
        Transform3d xform = Transform3d::Identity();
        xform.translate(virtual_bed_center_world - Vec3d{bed_center.x(), bed_center.y(), 0});
        scene_interactor.transform_selection(xform.matrix());
    }
    // The cube should now be unplaced (no real bed there).
    REQUIRE(p.unplaced_model_instances().size() == 1);

    scene_interactor.show_virtual_bed_preview(cc->id().id);
    REQUIRE(scene_interactor.virtual_bed_preview().has_value());
    REQUIRE(scene_interactor.virtual_bed_preview_accepts_selection());

    scene_interactor.hide_virtual_bed_preview();
    REQUIRE_FALSE(scene_interactor.virtual_bed_preview().has_value());
}

TEST_CASE_METHOD(VirtualBedFixture, "virtual bed preview rejects a selection that would not land inside", "[VirtualBedPreview]")
{
    const auto& p = project_interactor.selected_project();
    auto& cc = p.config_containers().front();
    const auto& bed = cc->bed();
    const double cube_side = 50.0;

    scene_interactor.new_object_from_mesh(Domain::TriangleMesh{TriMesh::make_cube(cube_side, cube_side, cube_side)});
    {
        Transform3d xform = Transform3d::Identity();
        xform.translate(Vec3d{bed.center().x() - cube_side / 2, bed.center().y() - cube_side / 2, 0});
        scene_interactor.transform_selection(xform.matrix());
    }

    // Move the cube far off any real or would-be bed.
    {
        Transform3d xform = Transform3d::Identity();
        xform.translate(Vec3d{0, -2000.0, 0});
        scene_interactor.transform_selection(xform.matrix());
    }
    REQUIRE(p.unplaced_model_instances().size() == 1);

    scene_interactor.show_virtual_bed_preview(cc->id().id);
    REQUIRE(scene_interactor.virtual_bed_preview().has_value());
    REQUIRE_FALSE(scene_interactor.virtual_bed_preview_accepts_selection());
}
