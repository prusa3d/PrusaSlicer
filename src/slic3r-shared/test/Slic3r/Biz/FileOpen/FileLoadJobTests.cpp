#include <catch2/catch_test_macros.hpp>

#include "Slic3r/App/Plater/ThumbnailImageGenerator.hpp"
#include "Slic3r/App/Platform/StdMainThreadDispatcher.hpp"
#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Biz/AppInstance/AppInstanceMessageHandlerFactory.hpp"
#include "Slic3r/Biz/FileOpen/FileLoadJob.hpp"
#include "Slic3r/Biz/FileOpen/FileOpeningInteractor.hpp"
#include "Slic3r/Biz/IMessageDialogProvider.hpp"
#include "Slic3r/Biz/Platform/JobManager/IJobManagerStatusChangedListener.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Biz/Preset/IO/BundlePaths.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Biz/SecretStoreDummy.hpp"
#include "Slic3r/Directories.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/Workbench.hpp"
#include "Slic3r/TestUtils/DispatchUtils.hpp"
#include "Slic3r/TestUtils/JobManagerScope.hpp"
#include "Slic3r/TestUtils/TestData.hpp"
#include "Slic3r/TestUtils/TestTempDir.hpp"

#include "ArchiveTestUtils.hpp"

#include <boost/filesystem/directory.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/nowide/filesystem.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <set>

using namespace Slic3r::Biz::FileOpen;
using Slic3r::Biz::FileOpen::Tests::make_stl_bytes;
using Slic3r::Biz::FileOpen::Tests::read_file;
using Slic3r::Biz::FileOpen::Tests::write_file;
using Slic3r::Biz::FileOpen::Tests::write_zip;
namespace fs = boost::filesystem;

namespace {

/**
 * @brief Tetrahedron small enough to make the loading ask whether it is defined in inches.
 */
std::string make_small_stl_bytes()
{
    return "solid tetra\n"
           "facet normal 0 0 -1\n"
           " outer loop\n  vertex 0 0 0\n  vertex 0 2 0\n  vertex 2 0 0\n endloop\n"
           "endfacet\n"
           "facet normal 0 -1 0\n"
           " outer loop\n  vertex 0 0 0\n  vertex 2 0 0\n  vertex 0 0 2\n endloop\n"
           "endfacet\n"
           "facet normal -1 0 0\n"
           " outer loop\n  vertex 0 0 0\n  vertex 0 0 2\n  vertex 0 2 0\n endloop\n"
           "endfacet\n"
           "facet normal 0.577 0.577 0.577\n"
           " outer loop\n  vertex 2 0 0\n  vertex 0 2 0\n  vertex 0 0 2\n endloop\n"
           "endfacet\n"
           "endsolid tetra\n";
}

/**
 * @brief Records the running file loading jobs, the jobs are cancelled through their names.
 */
struct FileLoadJobsListener : Slic3r::Biz::Platform::JobManager::IJobManagerStatusChangedListener
{
    std::set<std::string> running;

    void on_job_manager_status_changed(const Slic3r::Biz::Platform::JobManager::JobManagerStatus& status) override
    {
        running.clear();
        for (const auto& [name, progress] : status) {
            if (name.starts_with(FILE_LOAD_JOB_PREFIX) && progress.status == Slic3r::Domain::JobStatus::Started) {
                running.insert(name);
            }
        }
    }
};

/**
 * @brief Cancels the loading when it asks a question, like the user who presses Cancel while the
 * question is shown. The question stays unanswered.
 */
struct CancellingDialogProvider : Slic3r::Biz::IMessageDialogProvider
{
    FileLoadJobsListener& jobs;
    int questions{0};

    explicit CancellingDialogProvider(FileLoadJobsListener& jobs) : jobs(jobs) {}

    void cancel_loading()
    {
        ++questions;
        for (const std::string& name : jobs.running) {
            Slic3r::Biz::Platform::PlatformServices::instance().job_manager().request_job_stop(name);
        }
    }

    void show_yesno_dialog(const std::string&, const std::string&, const YesNoCallback&) override { cancel_loading(); }
    void show_rich_yesno_dialog(
        const std::string&,
        const std::string&,
        const std::string&,
        const YesNoCallback&,
        const CheckBoxCheckedCallback&
    ) override
    {
        cancel_loading();
    }
    void show_yesnocancel_dialog(const std::string&, const std::string&, const Button&, const Button&, const Button&)
        override
    {
        cancel_loading();
    }
    void show_info_dialog(const std::string&, const std::string&, bool) override {}
    void show_warning_dialog(const std::string&, const std::string&) override {}
    void show_error_dialog(const std::string&, const std::string&) override {}
    std::optional<Slic3r::Biz::StepLoadDialogResult> show_load_step_dialog(const std::string&, double, double, bool)
        override
    {
        cancel_loading();
        return std::nullopt;
    }
};

struct FileLoadJobFixture
{
    ::Tests::TestTempDir temp;
    Slic3r::App::Platform::StdMainThreadDispatcher dispatcher;
    ::Tests::JobManagerScope job_manager_scope{dispatcher};
    Slic3r::App::Plater::ThumbnailImageGenerator thumbnail_image_generator;
    Slic3r::Domain::Workbench workbench;
    FileLoadJobsListener jobs;
    CancellingDialogProvider dialog_provider{jobs};
    std::unique_ptr<Slic3r::Biz::ProjectInteractor> project_interactor;

    FileLoadJobFixture()
    {
        using namespace Slic3r::Biz;

        boost::nowide::nowide_filesystem();
        Slic3r::set_data_dir(::Tests::get_datadir().string());
        Slic3r::set_temp_dir(temp.path());
        Platform::PlatformServices::instance().set_secret_store(std::make_unique<SecretStoreDummy>());
        Platform::PlatformServices::instance().set_app_instance_message_handler(
            AppInstance::create_app_instance_message_handler(dispatcher)
        );
        Platform::PlatformServices::instance().job_manager().add_listener<Platform::JobManager::IJobManagerStatusChangedListener>(&jobs);

        project_interactor = std::make_unique<ProjectInteractor>(
            workbench,
            dispatcher,
            thumbnail_image_generator
        );
        project_interactor->preset_interactor().load_preset_bundle(
            Preset::IO::BundlePaths::make_test_runtime(::Tests::get_datadir())
        );
        project_interactor->set_undo_provider(std::make_unique<NoopUndoProvider>());
        project_interactor->new_project();
    }

    ~FileLoadJobFixture()
    {
        using namespace Slic3r::Biz;

        // Queue must be clear before ProjectInteractor can be destroyed.
        dispatcher.close();
        project_interactor.reset();
        Platform::PlatformServices::instance().job_manager().remove_listener<Platform::JobManager::IJobManagerStatusChangedListener>(&jobs);
        Platform::PlatformServices::instance().set_app_instance_message_handler(nullptr);
        Slic3r::set_temp_dir({});
    }

    FileOpeningInteractor& opener()
    {
        return project_interactor->file_opening_interactor();
    }

    Slic3r::Domain::Project& selected_project()
    {
        return project_interactor->selected_project();
    }

    const std::vector<Slic3r::Domain::ModelObject*>& selected_objects()
    {
        return selected_project().model().objects;
    }

    void enable_cancelling_dialogs()
    {
        project_interactor->set_dialog_provider(&dialog_provider);
    }

    /**
     * @brief Saves a project with one model through the current writer.
     */
    fs::path project_file(const std::string& name)
    {
        const fs::path file = temp.path() / name;
        Slic3r::Domain::Project project;
        project.config_containers().emplace_back(std::make_unique<Slic3r::Domain::ConfigContainer>());
        project_interactor->preset_interactor().initialize_config_container_with_default(
            *project.config_containers().front()
        );
        Slic3r::Domain::ModelObject* object = project.model().add_object();
        object->name = "cube";
        Slic3r::Biz::Algorithms::ModelObject::add_volume(
            object,
            Slic3r::Biz::Algorithms::TriangleMesh::make_cube(10.0, 10.0, 10.0)
        );
        object->volumes.front()->name = object->name;
        object->add_instance();
        Slic3r::store_3mf(file.string(), project);
        return file;
    }

    fs::path stl_file(const std::string& name, const std::string& bytes = make_stl_bytes())
    {
        const fs::path file = temp.path() / name;
        write_file(file, bytes);
        return file;
    }

    template <typename Predicate>
    bool wait_until(Predicate predicate)
    {
        return ::Tests::dispatch_until(dispatcher, predicate);
    }

    void dispatch_for(std::chrono::milliseconds duration)
    {
        ::Tests::dispatch_for(dispatcher, duration);
    }

    std::size_t extraction_count() const
    {
        std::size_t count = 0;
        for (const fs::directory_entry& entry : fs::directory_iterator(temp.path())) {
            if (entry.path().filename().string().starts_with("PrusaSlicer-archive-")) {
                ++count;
            }
        }
        return count;
    }

    /**
     * @brief Selects the instance of the only object in the selected project.
     */
    Slic3r::Biz::Scene::ObjectSelection select_the_object()
    {
        const Slic3r::Domain::ModelObject& object{*selected_objects().front()};
        const Slic3r::Biz::Scene::ObjectSelection selection{
            Slic3r::Biz::Scene::SelectionMode::Instance,
            {Slic3r::Domain::ElementRef{object.id().id, object.instances.front()->id().id}}
        };
        project_interactor->scene_interactor().set_object_selection(selection);
        return selection;
    }
};

} // namespace

TEST_CASE_METHOD(FileLoadJobFixture, "Cancelled model loading reports Cancelled and adds nothing", "[FileOpen][FileLoadJob]")
{
    enable_cancelling_dialogs();
    std::optional<FileLoadResult> result;

    opener().open({stl_file("small.stl", make_small_stl_bytes())}, {}, [&](FileLoadResult r) { result = r; });

    REQUIRE(wait_until([&] { return result.has_value(); }));
    CHECK(*result == FileLoadResult::Cancelled);
    CHECK(dialog_provider.questions == 1);
    CHECK(selected_objects().empty());
}

TEST_CASE_METHOD(FileLoadJobFixture, "Cancelled models of an archive skip its projects", "[FileOpen][FileLoadJob]")
{
    enable_cancelling_dialogs();
    const fs::path archive = write_zip(
        temp.path() / "mixed.zip",
        {{"project.3mf", read_file(project_file("source.3mf"))},
         {"small.stl", make_small_stl_bytes()}}
    );

    opener().open({archive});

    REQUIRE(wait_until([&] { return dialog_provider.questions == 1; }));
    // The extracted files are removed when the loading of the archive ends.
    REQUIRE(wait_until([&] { return extraction_count() == 0; }));
    // A project which was wrongly opened would load meanwhile.
    dispatch_for(std::chrono::milliseconds(200));
    CHECK(workbench.projects().size() == 1);
    CHECK(selected_objects().empty());
}

TEST_CASE_METHOD(FileLoadJobFixture, "Loaded volumes are added into the selected object", "[FileOpen][FileLoadJob]")
{
    opener().open({stl_file("part.stl")});
    REQUIRE(wait_until([&] { return selected_objects().size() == 1; }));
    select_the_object();

    std::optional<FileLoadResult> result;
    project_interactor->load_volumes_to_selected_object(
        {stl_file("modifier.stl")},
        Slic3r::Domain::ModelVolumeType::PARAMETER_MODIFIER,
        [&](FileLoadResult r) { result = r; }
    );

    REQUIRE(wait_until([&] { return result.has_value(); }));
    CHECK(*result == FileLoadResult::Loaded);
    REQUIRE(selected_objects().size() == 1);
    CHECK(selected_objects().front()->volumes.size() == 2);
}

TEST_CASE_METHOD(FileLoadJobFixture, "Loaded volumes are dropped when the selection changed during the loading", "[FileOpen][FileLoadJob]")
{
    opener().open({stl_file("part.stl")});
    REQUIRE(wait_until([&] { return selected_objects().size() == 1; }));
    select_the_object();

    std::optional<FileLoadResult> result;
    project_interactor->load_volumes_to_selected_object(
        {stl_file("modifier.stl")},
        Slic3r::Domain::ModelVolumeType::PARAMETER_MODIFIER,
        [&](FileLoadResult r) { result = r; }
    );
    // The result is applied on the main thread, so the selection always changes before it.
    project_interactor->scene_interactor().clear_object_selection();

    REQUIRE(wait_until([&] { return result.has_value(); }));
    CHECK(*result == FileLoadResult::Failed);
    REQUIRE(selected_objects().size() == 1);
    CHECK(selected_objects().front()->volumes.size() == 1);
}

TEST_CASE_METHOD(FileLoadJobFixture, "Model loading which adds nothing reports Failed", "[FileOpen][FileLoadJob]")
{
    std::optional<FileLoadResult> result;

    opener().open({stl_file("broken.stl", "not an stl")}, {}, [&](FileLoadResult r) { result = r; });

    REQUIRE(wait_until([&] { return result.has_value(); }));
    CHECK(*result == FileLoadResult::Failed);
    CHECK(selected_objects().empty());
}

TEST_CASE_METHOD(FileLoadJobFixture, "Loading of empty paths reports Loaded and adds nothing", "[FileOpen][FileLoadJob]")
{
    std::vector<FileLoadResult> results;
    project_interactor->load_models_to_project({}, false, [&](FileLoadResult r) { results.push_back(r); });
    project_interactor->load_volumes_to_selected_object(
        {},
        Slic3r::Domain::ModelVolumeType::MODEL_PART,
        [&](FileLoadResult r) { results.push_back(r); }
    );
    CHECK(results == std::vector<FileLoadResult>{FileLoadResult::Loaded, FileLoadResult::Loaded});
    CHECK(selected_objects().empty());
}
