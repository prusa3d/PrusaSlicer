#include <iostream>
#include <catch2/catch_test_macros.hpp>
#include <catch2/trompeloeil.hpp>

#include "Slic3r/Biz/IProjectsChangedListener.hpp"
#include "Slic3r/Biz/Platform/JobManager/JobManager.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Biz/SecretStoreDummy.hpp"
#include "Slic3r/Biz/AppInstance/AppInstanceMessageHandlerFactory.hpp"

#include "Slic3r/App/Plater/ThumbnailImageGenerator.hpp"
#include "Slic3r/App/Platform/StdMainThreadDispatcher.hpp"

#include "Slic3r/TestUtils/TestData.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Directories.hpp"

#include <chrono>
#include <thread>

#include <boost/filesystem/operations.hpp>
#include <boost/nowide/filesystem.hpp>

using Slic3r::Biz::IProjectsChangedListener;
using Slic3r::Biz::Platform::PlatformServices;
using Slic3r::Biz::Platform::JobManager::JobManager;
using Slic3r::Domain::Project;
using Slic3r::Domain::SelectionId;

namespace fs = boost::filesystem;

namespace Slic3r::Biz::Mock {

struct SelectedProjectChangedListener : public ISelectedProjectChangedListener
{
    MAKE_MOCK1(on_selected_project_changed, void(Domain::SelectionId));
};

struct SelectedConfigContainerChangedListener : public ISelectedConfigContainerChangedListener
{
    MAKE_MOCK2(
        on_selected_config_container_changed,
        void(Domain::SelectionId, Domain::SelectionId)
    );
};

struct SelectedBedInstancesChangedListener : public ISelectedBedInstancesChangedListener
{
    MAKE_MOCK2(
        on_selected_bed_instances_changed,
        void(Domain::SelectionId project_id, const Scene::BedSelection& selection)
    );
};
} // namespace Slic3r::Biz::Mock

void outp()
{
    std::cout << std::endl;
}

template <typename T, typename... ArgsT>
void outp(const T& arg, const ArgsT&... args)
{
    std::cout << arg;
    outp(args...);
}

struct ProjectInteractorFixture
{
    ProjectInteractorFixture()
    {
        using namespace Slic3r::Biz;
        namespace fs = boost::filesystem;

        boost::nowide::nowide_filesystem();

        Platform::PlatformServices::instance().set_secret_store(std::move(store_dummy));
        Platform::PlatformServices::instance().set_app_instance_message_handler(
            AppInstance::create_app_instance_message_handler(dispatcher)
        );
        PlatformServices::instance().set_job_manager(std::make_unique<JobManager>(dispatcher));

        Slic3r::set_data_dir(Tests::get_datadir().string());

        auto data_dir = Tests::get_datadir();

        project_interactor = std::make_unique<Slic3r::Biz::ProjectInteractor>(
            workbench,
            dispatcher,
            thumbnail_image_generator
        );
        project_interactor->preset_interactor().load_preset_bundle(
            Preset::IO::BundlePaths::make_test_runtime(data_dir)
        );
    }

    ~ProjectInteractorFixture()
    {
        // Queue must be clear before ProjectInteractor can be destroyed.
        dispatcher.close();

        project_interactor.reset();

        PlatformServices::instance().set_app_instance_message_handler(nullptr);
        PlatformServices::instance().set_job_manager(nullptr);
    }

    std::unique_ptr<Slic3r::Biz::SecretStoreDummy> store_dummy =
        std::make_unique<Slic3r::Biz::SecretStoreDummy>();
    Slic3r::App::Platform::StdMainThreadDispatcher dispatcher;
    Slic3r::App::Plater::ThumbnailImageGenerator thumbnail_image_generator;
    Slic3r::Domain::Workbench workbench;
    std::unique_ptr<Slic3r::Biz::ProjectInteractor> project_interactor;
};

TEST_CASE_METHOD(ProjectInteractorFixture, "Project Interactor Listeners", "[ProjectInteractor]")
{
    using namespace Slic3r::Biz;
    using namespace trompeloeil;

    Mock::SelectedProjectChangedListener selected_project_changed_listener;
    Mock::SelectedConfigContainerChangedListener selected_config_container_listener;
    project_interactor->add_listener<ISelectedProjectChangedListener>(
        &selected_project_changed_listener
    );
    project_interactor->add_listener<ISelectedConfigContainerChangedListener>(
        &selected_config_container_listener
    );

    Scene::SceneInteractor& scene_interactor = project_interactor->scene_interactor();
    Mock::SelectedBedInstancesChangedListener selected_bed_instances_changed_listener;
    scene_interactor.add_listener<ISelectedBedInstancesChangedListener>(
        &selected_bed_instances_changed_listener
    );

    const auto is_selection_valid{[](const Scene::BedSelection& selection)
                                  {
                                      const auto bed_ref{selection.last_selected_bed()};
                                      return bed_ref.config_container_id > 0
                                          && bed_ref.instance_id > 0;
                                  }};

    {
        REQUIRE_CALL(selected_project_changed_listener, on_selected_project_changed(0));
        REQUIRE_CALL(
            selected_config_container_listener,
            on_selected_config_container_changed(0, gt(0))
        );
        REQUIRE_CALL(
            selected_bed_instances_changed_listener,
            on_selected_bed_instances_changed(0, _)
        )
            .WITH(is_selection_valid(_2))
            .TIMES(AT_LEAST(1));
        project_interactor->new_project();
    }

    {
        REQUIRE_CALL(selected_project_changed_listener, on_selected_project_changed(1));
        REQUIRE_CALL(
            selected_config_container_listener,
            on_selected_config_container_changed(1, gt(0))
        )
            .SIDE_EFFECT(
                auto capStr =
                    std::string("selected_config_container( cc: ") + std::to_string(_2) + " )";
                UNSCOPED_INFO(capStr);
            );
        REQUIRE_CALL(
            selected_bed_instances_changed_listener,
            on_selected_bed_instances_changed(1, _)
        )
            .WITH(is_selection_valid(_2))
            .TIMES(AT_LEAST(1))
            .SIDE_EFFECT(
                auto capStr = std::string("selected_bed_instance( cc: ")
                    + std::to_string(_2.last_selected_bed().config_container_id)
                    + " )";
                UNSCOPED_INFO(capStr);
            );
        project_interactor->new_project();
    }

    {
        REQUIRE_CALL(selected_project_changed_listener, on_selected_project_changed(0))
            .SIDE_EFFECT(
                auto capStr = std::string("selected_project: ") + std::to_string(_1);
                UNSCOPED_INFO(capStr);
            );
        // TODO: this is temporary behavior, will be removed
        REQUIRE_CALL(
            selected_config_container_listener,
            on_selected_config_container_changed(0, gt(0))
        )
            .SIDE_EFFECT(
                auto capStr =
                    std::string("selected_config_container( cc: ") + std::to_string(_2) + " )";
                UNSCOPED_INFO(capStr);
            );
        // on_selected_bed_instances_changed is not called as it is selected during new_project()
        project_interactor->select_project(0);
    }
}

TEST_CASE_METHOD(
    ProjectInteractorFixture,
    "Project Interactor Config Container",
    "[ProjectInteractor]"
)
{
    project_interactor->new_project();
    const auto& p0 = project_interactor->selected_project();

    REQUIRE(p0.config_containers().size() == 1);
    const auto& cc0   = *p0.config_containers().at(0);
    const auto cc0_id = cc0.id().id;
    REQUIRE(project_interactor->selected_config_container_id() == cc0_id);

    const auto cc1_id = project_interactor->add_config_container();
    REQUIRE(project_interactor->selected_config_container_id() == cc1_id);

    project_interactor->select_config_container(cc0_id);
    REQUIRE(project_interactor->selected_config_container_id() == cc0_id);

    project_interactor->remove_config_container(cc0_id);
    REQUIRE(project_interactor->selected_config_container_id() == cc1_id);
    REQUIRE(p0.config_containers().size() == 1);

    const auto& cc2_id = project_interactor->add_config_container();
    REQUIRE(project_interactor->selected_config_container_id() == cc2_id);
    REQUIRE(p0.config_containers().size() == 2);

    const auto& p1_id = project_interactor->new_project();
}

namespace {

struct ProjectLoadResultCapture final : public IProjectsChangedListener
{
    bool project_loaded{false};
    bool load_failed{false};

    void on_project_loaded(SelectionId) override
    {
        project_loaded = true;
    }

    void on_project_load_failed(const std::string&) override
    {
        load_failed = true;
    }
};

boost::filesystem::path write_empty_project_3mf(const boost::filesystem::path& directory)
{
    const fs::path empty_3mf_path = directory / "empty_project.3mf";
    const Project empty_project;
    Slic3r::store_3mf(empty_3mf_path.string(), empty_project);

    return empty_3mf_path;
}

} // namespace

TEST_CASE_METHOD(
    ProjectInteractorFixture,
    "Load project reports failure for a 3MF without a loadable project",
    "[ProjectInteractor]"
)
{
    const fs::path temp_dir =
        fs::temp_directory_path() / fs::unique_path("slic3r-pi-test-%%%%-%%%%");
    fs::create_directories(temp_dir);

    const fs::path empty_3mf_path = write_empty_project_3mf(temp_dir);
    REQUIRE(fs::exists(empty_3mf_path));

    ProjectLoadResultCapture load_result_capture;
    project_interactor->add_listener<IProjectsChangedListener>(&load_result_capture);
    project_interactor->load_project(empty_3mf_path);

    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!load_result_capture.project_loaded
           && !load_result_capture.load_failed
           && std::chrono::steady_clock::now() < deadline)
    {
        dispatcher.dispatch_enqueued();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    project_interactor->remove_listener<IProjectsChangedListener>(&load_result_capture);

    boost::system::error_code cleanup_error;
    fs::remove_all(temp_dir, cleanup_error);

    CHECK_FALSE(load_result_capture.project_loaded);
    REQUIRE(load_result_capture.load_failed);
}
