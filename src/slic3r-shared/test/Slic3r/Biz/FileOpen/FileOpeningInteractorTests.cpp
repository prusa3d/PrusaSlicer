#include <catch2/catch_test_macros.hpp>

#include "Slic3r/App/Plater/ThumbnailImageGenerator.hpp"
#include "Slic3r/App/Platform/StdMainThreadDispatcher.hpp"
#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Biz/AppInstance/AppInstanceMessageHandlerFactory.hpp"
#include "Slic3r/Biz/FileOpen/FileOpeningInteractor.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Biz/Preset/IO/BundlePaths.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/SecretStoreDummy.hpp"
#include "Slic3r/Directories.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/Workbench.hpp"
#include "Slic3r/TestUtils/JobManagerScope.hpp"
#include "Slic3r/TestUtils/TestData.hpp"
#include "Slic3r/TestUtils/TestTempDir.hpp"

#include "ArchiveTestUtils.hpp"

#include <boost/filesystem/directory.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/nowide/filesystem.hpp>

#include <algorithm>
#include <chrono>
#include <memory>
#include <thread>

using namespace Slic3r::Biz::FileOpen;
using Slic3r::Biz::FileOpen::Tests::make_stl_bytes;
using Slic3r::Biz::FileOpen::Tests::read_file;
using Slic3r::Biz::FileOpen::Tests::write_file;
using Slic3r::Biz::FileOpen::Tests::write_zip;
namespace fs = boost::filesystem;

namespace {

struct FakeEventListener : IFileOpenEventListener
{
    std::size_t skipped = 0;
    std::vector<std::string> failures;
    std::vector<std::vector<fs::path>> postponed;

    void on_archive_files_skipped(std::size_t count) override
    {
        skipped += count;
    }

    void on_archive_open_failed(const std::string& message) override
    {
        failures.push_back(message);
    }

    void on_archive_postponed(const std::vector<fs::path>& archives, OpenFilesOptions) override
    {
        postponed.push_back(archives);
    }
};

struct FileOpeningInteractorFixture
{
    ::Tests::TestTempDir temp;
    Slic3r::App::Platform::StdMainThreadDispatcher dispatcher;
    ::Tests::JobManagerScope job_manager_scope{dispatcher};
    Slic3r::App::Plater::ThumbnailImageGenerator thumbnail_image_generator;
    Slic3r::Domain::Workbench workbench;
    FakeEventListener events;
    std::unique_ptr<Slic3r::Biz::ProjectInteractor> project_interactor;

    FileOpeningInteractorFixture()
    {
        using namespace Slic3r::Biz;

        boost::nowide::nowide_filesystem();
        Slic3r::set_data_dir(::Tests::get_datadir().string());
        Slic3r::set_temp_dir(temp.path());
        Platform::PlatformServices::instance().set_secret_store(std::make_unique<SecretStoreDummy>());
        Platform::PlatformServices::instance().set_app_instance_message_handler(
            AppInstance::create_app_instance_message_handler(dispatcher)
        );

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
        opener().add_listener<IFileOpenEventListener>(&events);
    }

    ~FileOpeningInteractorFixture()
    {
        // Queue must be clear before ProjectInteractor can be destroyed.
        dispatcher.close();
        project_interactor.reset();
        Slic3r::Biz::Platform::PlatformServices::instance().set_app_instance_message_handler(nullptr);
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

    /**
     * @brief Saves a project with one model through the current writer, legacy test 3MFs don't load cleanly.
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

    fs::path stl_file(const std::string& name)
    {
        const fs::path file = temp.path() / name;
        write_file(file, make_stl_bytes());
        return file;
    }

    template <typename Predicate>
    bool wait_until(Predicate predicate)
    {
        const auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < std::chrono::seconds(10)) {
            dispatcher.dispatch_enqueued();
            if (predicate()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
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
};

} // namespace

TEST_CASE_METHOD(FileOpeningInteractorFixture, "FileOpeningInteractor opens a single 3mf as a project", "[FileOpen][FileOpeningInteractor]")
{
    const fs::path file = project_file("project.3mf");
    opener().open({file});
    REQUIRE(wait_until([&] { return workbench.projects().size() == 2; }));
    CHECK(selected_project().loaded_file_path() == file);
}

TEST_CASE_METHOD(FileOpeningInteractorFixture, "FileOpeningInteractor new_project lets a project replace the empty one", "[FileOpen][FileOpeningInteractor]")
{
    const fs::path file = project_file("project.3mf");
    opener().open({file}, {.new_project = true});
    REQUIRE(wait_until([&] { return selected_project().loaded_file_path() == file; }));
    CHECK(workbench.projects().size() == 1);
}

TEST_CASE_METHOD(FileOpeningInteractorFixture, "FileOpeningInteractor imports models", "[FileOpen][FileOpeningInteractor]")
{
    SECTION("3mf as geometry") {
        const fs::path file = project_file("project.3mf");
        opener().open({file}, {.as_geometry = true});
        CHECK(workbench.projects().size() == 1);
        CHECK(selected_project().loaded_file_path().empty());
        CHECK_FALSE(selected_objects().empty());
    }
    SECTION("several files, 3mf included") {
        const fs::path file = project_file("project.3mf");
        const fs::path stl  = stl_file("part.stl");
        opener().open({file, stl}, {.new_project = true});
        CHECK(workbench.projects().size() == 1);
        const auto& objects = selected_objects();
        CHECK(objects.size() >= 2);
        CHECK(std::any_of(objects.begin(), objects.end(), [&stl](const Slic3r::Domain::ModelObject* object) {
            return fs::path(object->input_file) == stl;
        }));
    }
    SECTION("to a new project when the selected one is not empty") {
        opener().open({stl_file("part.stl")});
        REQUIRE(selected_objects().size() == 1);
        opener().open({stl_file("part2.stl")}, {.new_project = true});
        CHECK(workbench.projects().size() == 2);
        CHECK(selected_objects().size() == 1);
    }
}

TEST_CASE_METHOD(FileOpeningInteractorFixture, "FileOpeningInteractor imports models from an archive", "[FileOpen][FileOpeningInteractor]")
{
    const fs::path archive = write_zip(
        temp.path() / "models.zip",
        {{"a.stl", make_stl_bytes()}, {"readme.txt", "text"}}
    );

    opener().open({archive});
    REQUIRE(wait_until([&] { return !selected_objects().empty(); }));
    REQUIRE(selected_objects().size() == 1);
    CHECK(selected_objects().front()->input_file.empty()); // extracted files are temporary
    CHECK(events.skipped == 1);
    CHECK(workbench.projects().size() == 1);
    REQUIRE(wait_until([&] { return extraction_count() == 0; }));
}

TEST_CASE_METHOD(FileOpeningInteractorFixture, "FileOpeningInteractor opens a 3mf from an archive as a project", "[FileOpen][FileOpeningInteractor]")
{
    const fs::path archive = write_zip(
        temp.path() / "single.zip",
        {{"project.3mf", read_file(project_file("source.3mf"))}}
    );

    opener().open({archive});
    REQUIRE(wait_until([&] { return workbench.projects().size() == 2; }));
    CHECK(selected_project().loaded_file_path().empty());
    CHECK(selected_project().file_name() == "project");
    CHECK_FALSE(project_interactor->backup_store().is_project_unsaved(project_interactor->selected_project_id()));
    REQUIRE(wait_until([&] { return extraction_count() == 0; }));
}

TEST_CASE_METHOD(FileOpeningInteractorFixture, "FileOpeningInteractor opens projects from an archive after models", "[FileOpen][FileOpeningInteractor]")
{
    const fs::path archive = write_zip(
        temp.path() / "mixed.zip",
        {{"project.3mf", read_file(project_file("source.3mf"))},
         {"part.stl", make_stl_bytes()}}
    );

    SECTION("as project") {
        opener().open({archive}, {.new_project = true});
        REQUIRE(wait_until([&] { return workbench.projects().size() == 2; }));
        CHECK(selected_project().loaded_file_path().empty());
        CHECK(selected_project().file_name() == "project");
        // The model went to the project which was selected before the project from the archive.
        std::size_t imported = 0;
        for (const auto& [id, project] : workbench.projects()) {
            if (id != project_interactor->selected_project_id()) {
                imported = project.model().objects.size();
            }
        }
        CHECK(imported == 1);
        REQUIRE(wait_until([&] { return extraction_count() == 0; }));
    }
    SECTION("as geometry") {
        opener().open({archive}, {.as_geometry = true});
        REQUIRE(wait_until([&] { return selected_objects().size() >= 2; }));
        CHECK(workbench.projects().size() == 1);
        REQUIRE(wait_until([&] { return extraction_count() == 0; }));
    }
}

TEST_CASE_METHOD(FileOpeningInteractorFixture, "FileOpeningInteractor lets the dialog choose from an archive", "[FileOpen][FileOpeningInteractor]")
{
    const fs::path archive = write_zip(
        temp.path() / "models.zip",
        {{"a.stl", make_stl_bytes()}, {"b.stl", make_stl_bytes()}}
    );

    SECTION("dialog shown") {
        std::vector<ArchiveFile> offered;
        ArchiveSelectionDone done;
        opener().set_archive_dialog([&](std::vector<ArchiveFile> files, OpenFilesOptions, ArchiveSelectionDone d) {
            offered = std::move(files);
            done    = std::move(d);
            return true;
        });

        opener().open({archive});
        REQUIRE(wait_until([&] { return static_cast<bool>(done); }));
        CHECK(offered.size() == 2);
        CHECK(selected_objects().empty());

        done(ArchiveSelection{.models = {offered[1].path}});
        CHECK(selected_objects().size() == 1);
        CHECK_FALSE(fs::exists(offered[0].path.parent_path()));
    }
    SECTION("dialog cancelled") {
        std::vector<ArchiveFile> offered;
        ArchiveSelectionDone done;
        opener().set_archive_dialog([&](std::vector<ArchiveFile> files, OpenFilesOptions, ArchiveSelectionDone d) {
            offered = std::move(files);
            done    = std::move(d);
            return true;
        });

        opener().open({archive});
        REQUIRE(wait_until([&] { return static_cast<bool>(done); }));
        REQUIRE(fs::exists(offered[0].path));

        done(std::nullopt);
        CHECK(selected_objects().empty());
        CHECK_FALSE(fs::exists(offered[0].path.parent_path()));
    }
    SECTION("dialog can't be shown") {
        opener().set_archive_dialog([](std::vector<ArchiveFile>, OpenFilesOptions, ArchiveSelectionDone) {
            return false;
        });

        opener().open({archive});
        REQUIRE(wait_until([&] { return !events.postponed.empty(); }));
        CHECK(selected_objects().empty());
        REQUIRE(events.postponed.size() == 1);
        CHECK(events.postponed[0] == std::vector<fs::path>{archive});
        CHECK(extraction_count() == 0);
    }
}

TEST_CASE_METHOD(FileOpeningInteractorFixture, "FileOpeningInteractor reports an archive with nothing to open", "[FileOpen][FileOpeningInteractor]")
{
    const fs::path archive = write_zip(temp.path() / "text.zip", {{"readme.txt", "text"}});

    opener().open({archive});
    REQUIRE(wait_until([&] { return !events.failures.empty(); }));
    CHECK(selected_objects().empty());
    CHECK(events.failures.size() == 1);
    CHECK(extraction_count() == 0);
}
