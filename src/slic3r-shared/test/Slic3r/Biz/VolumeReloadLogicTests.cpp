#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Slic3r/App/Plater/ThumbnailImageGenerator.hpp"
#include "Slic3r/App/Platform/StdMainThreadDispatcher.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Biz/Format/STL.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Biz/VolumeReloadLogic.hpp"
#include "Slic3r/Directories.hpp"
#include "Slic3r/TestUtils/AppInstanceMessageHandlerScope.hpp"
#include "Slic3r/TestUtils/JobManagerScope.hpp"
#include "Slic3r/TestUtils/ScopedThreadDispatcher.hpp"
#include "Slic3r/TestUtils/TestData.hpp"

#include <boost/filesystem/operations.hpp>

using Slic3r::App::Plater::ThumbnailImageGenerator;
using Slic3r::App::Platform::StdMainThreadDispatcher;
using Slic3r::Biz::ProjectInteractor;
using Slic3r::Biz::Preset::IO::BundlePaths;
using Slic3r::Biz::Scene::SceneInteractor;
using Slic3r::Biz::VolumeReloadLogic::ReloadFromDiskResult;
using Slic3r::Domain::ElementRefs;
using Slic3r::Domain::ModelObject;
using Slic3r::Domain::ModelVolume;
using Slic3r::Domain::TriangleMesh;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::Vec3f;
using Slic3r::Domain::Workbench;

using Slic3r::set_data_dir;
using Slic3r::Biz::store_stl;
using Slic3r::Biz::Algorithms::BoundingBox::sizes;
using Slic3r::Biz::Algorithms::TriangleMesh::make_cube;
using Slic3r::Biz::VolumeReloadLogic::reload_selection;

namespace fs = boost::filesystem;

struct VolumeReloadLogicFixture
{
    VolumeReloadLogicFixture()
    {
        set_data_dir(Tests::get_datadir().string());

        project_interactor.preset_interactor().load_preset_bundle(
            BundlePaths::make_test_runtime(Tests::get_datadir())
        );
        project_interactor.new_project();
    }

    ~VolumeReloadLogicFixture()
    {
        fs::remove(source_file_path);
    }

    Workbench workbench;

    StdMainThreadDispatcher dispatcher;
    Tests::AppInstanceMessageHandlerScope app_instance_message_handler_scope{dispatcher};
    Tests::JobManagerScope job_manager_scope{dispatcher};
    ThumbnailImageGenerator thumbnail_image_generator;
    ProjectInteractor project_interactor{workbench, dispatcher, thumbnail_image_generator};
    SceneInteractor& scene_interactor{project_interactor.scene_interactor()};
    Tests::ScopedThreadDispatcher thread_dispatcher{dispatcher};

    fs::path source_file_path{fs::temp_directory_path() / fs::unique_path("%%%%-%%%%-%%%%.stl")};
};

TEST_CASE_METHOD(
    VolumeReloadLogicFixture,
    "Reload from disk applies the unit conversion of the original import",
    "[VolumeReloadLogic]"
)
{
    ModelVolume::Source
        volume_source{.input_file = source_file_path.string(), .object_idx = 0, .volume_idx = 0};
    double millimeters_per_source_unit = 1.;

    SECTION("meters")
    {
        volume_source.is_converted_from_meters = true;
        millimeters_per_source_unit            = 1000.;
    }

    SECTION("inches")
    {
        volume_source.is_converted_from_inches = true;
        millimeters_per_source_unit            = 25.4;
    }

    const double cube_side_in_source_units = 0.05;
    const TriangleMesh source_mesh{
        make_cube(cube_side_in_source_units, cube_side_in_source_units, cube_side_in_source_units)
    };

    REQUIRE(store_stl(source_file_path.string(), source_mesh, true));

    TriangleMesh imported_mesh = source_mesh;
    imported_mesh.scale(static_cast<float>(millimeters_per_source_unit) * Vec3f::Ones());

    const ElementRefs instances = scene_interactor.new_object_from_mesh(
        std::move(imported_mesh),
        source_file_path.filename().string(),
        volume_source
    );
    REQUIRE(instances.size() == 1);

    const ReloadFromDiskResult result = reload_selection(
        [](const fs::path&) -> std::optional<fs::path> { return std::nullopt; },
        scene_interactor,
        nullptr
    );
    REQUIRE(result.failed_volume_names.empty());
    REQUIRE(result.changed_volume_count == 1);

    const ModelObject* object =
        project_interactor.selected_project().find_object_by_id(instances.front().object_id);
    REQUIRE(object != nullptr);

    const ModelVolume& reloaded_volume = *object->volumes.front();
    const Vec3d reloaded_size          = sizes(reloaded_volume.mesh().bounding_box());
    CHECK(
        reloaded_size.x() == Catch::Approx(cube_side_in_source_units * millimeters_per_source_unit)
    );
    CHECK(
        reloaded_volume.source.is_converted_from_meters == volume_source.is_converted_from_meters
    );
    CHECK(
        reloaded_volume.source.is_converted_from_inches == volume_source.is_converted_from_inches
    );
}
