#include <iostream>
#include <catch2/catch_test_macros.hpp>
#include <catch2/trompeloeil.hpp>

#include "Slic3r/Biz/IProjectsChangedListener.hpp"
#include "Slic3r/Biz/Platform/JobManager/JobManager.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Biz/SecretStoreDummy.hpp"
#include "Slic3r/Biz/AppInstance/AppInstanceMessageHandlerFactory.hpp"
#include "Slic3r/Biz/Algorithms/ZipArchive.hpp"
#include "Slic3r/Biz/Connect/ConnectMessageHandler.hpp"
#include "Slic3r/Biz/Preset/IPresetDialogManager.hpp"
#include "Slic3r/Biz/Preset/PresetSelectionNames.hpp"
#include "Slic3r/Biz/Preset/PrinterPresetSelector.hpp"
#include "Slic3r/Biz/JsonValueJson.hpp"
#include "Slic3r/Biz/Network/MockHttp.hpp"
#include "Slic3r/Biz/Network/MockHttpFactory.hpp"

#include "Slic3r/App/Plater/ThumbnailImageGenerator.hpp"
#include "Slic3r/App/Platform/StdMainThreadDispatcher.hpp"

#include "Slic3r/TestUtils/TestData.hpp"
#include "Slic3r/Domain/ConfigContainer.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Directories.hpp"

#include <chrono>
#include <thread>

#include <boost/filesystem/operations.hpp>
#include <boost/nowide/filesystem.hpp>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

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
    SelectionId loaded_project_id{0};

    void on_project_loaded(SelectionId project_id) override
    {
        project_loaded    = true;
        loaded_project_id = project_id;
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

namespace {

using Slic3r::Biz::PhysicalPrinter::FileSystemExport;
using Slic3r::Biz::PhysicalPrinter::PhysicalPrinterConfig;
using Slic3r::Biz::PhysicalPrinter::PhysicalPrinterInteractor;

struct PhysicalPrinterFromProjectFixture : public ProjectInteractorFixture
{
    PhysicalPrinterFromProjectFixture()
    {
        fs::create_directories(temp_dir);
        project_interactor->new_project();
    }

    ~PhysicalPrinterFromProjectFixture()
    {
        for (const std::string& uuid : stored_uuids) {
            physical_printers().remove_uuid(uuid);
        }
        boost::system::error_code cleanup_error;
        fs::remove_all(temp_dir, cleanup_error);
    }

    PhysicalPrinterInteractor& physical_printers()
    {
        return project_interactor->physical_printer_interactor();
    }

    Slic3r::Domain::ConfigContainer& selected_config_container()
    {
        return *workbench.project(project_interactor->selected_project_id())
                    .find_config_container(project_interactor->selected_config_container_id());
    }

    const Slic3r::Domain::Preset::HwPrinterConfig& current_hw_config()
    {
        return project_interactor->preset_interactor().current_printer_config();
    }

    std::string create_stored_printer(
        const std::string& name,
        const Slic3r::Domain::Preset::HwPrinterConfig& hw_config
    )
    {
        PhysicalPrinterConfig printer;
        printer.payload   = FileSystemExport{false};
        printer.name      = name;
        printer.hw_config = hw_config;
        stored_uuids.push_back(physical_printers().save_physical_printer(printer));
        physical_printers().select_uuid(stored_uuids.back());
        return stored_uuids.back();
    }

    fs::path store_selected_project()
    {
        const fs::path path = temp_dir / fs::unique_path("project-%%%%-%%%%.3mf");
        Slic3r::store_3mf(
            path.string(),
            workbench.project(project_interactor->selected_project_id())
        );
        return path;
    }

    SelectionId load(const fs::path& path)
    {
        ProjectLoadResultCapture load_result_capture;
        project_interactor->add_listener<IProjectsChangedListener>(&load_result_capture);
        project_interactor->load_project(path);

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

        REQUIRE(load_result_capture.project_loaded);
        return load_result_capture.loaded_project_id;
    }

    size_t runtime_printer_count()
    {
        const auto& list = physical_printers().observable_list();
        size_t count     = 0;
        for (size_t i = 0; i < list.size(); ++i) {
            count += list.at(i).is_runtime ? 1 : 0;
        }
        return count;
    }

    const fs::path temp_dir =
        fs::temp_directory_path() / fs::unique_path("slic3r-pp-test-%%%%-%%%%");
    std::vector<std::string> stored_uuids;
};

} // namespace

TEST_CASE_METHOD(
    PhysicalPrinterFromProjectFixture,
    "3MF without physical printer and no compatible printer gets a runtime printer",
    "[ProjectInteractor][PhysicalPrinter]"
)
{
    const std::string hw_config_name = current_hw_config().name;
    const fs::path path              = store_selected_project();

    load(path);

    REQUIRE(physical_printers().selected_physical_printer_data() != nullptr);
    const PhysicalPrinterConfig& printer = *physical_printers().selected_physical_printer_data();
    CHECK(printer.is_runtime);
    CHECK(printer.name == hw_config_name);
    REQUIRE(printer.hw_config.has_value());
    CHECK(printer.hw_config->id == current_hw_config().id);
    CHECK(current_hw_config().name == hw_config_name);
    CHECK(selected_config_container().physical_printer_selection().uuid == printer.uuid);
    CHECK(runtime_printer_count() == 1);
}

TEST_CASE_METHOD(
    PhysicalPrinterFromProjectFixture,
    "3MF with compatible stored physical printer selects it",
    "[ProjectInteractor][PhysicalPrinter]"
)
{
    const std::string uuid = create_stored_printer("Stored printer", current_hw_config());
    REQUIRE(selected_config_container().physical_printer_selection().uuid == uuid);
    const fs::path path = store_selected_project();

    load(path);

    CHECK(physical_printers().selected_uuid() == uuid);
    REQUIRE(physical_printers().selected_physical_printer_data() != nullptr);
    CHECK_FALSE(physical_printers().selected_physical_printer_data()->is_runtime);
    CHECK(selected_config_container().physical_printer_selection().uuid == uuid);
    CHECK(selected_config_container().physical_printer_selection().name == "Stored printer");
    CHECK(runtime_printer_count() == 0);
}

TEST_CASE_METHOD(
    PhysicalPrinterFromProjectFixture,
    "3MF with incompatible stored physical printer keeps its preset",
    "[ProjectInteractor][PhysicalPrinter]"
)
{
    Slic3r::Domain::Preset::HwPrinterConfig incompatible_hw_config = current_hw_config();
    incompatible_hw_config.model.model += "_incompatible";
    const std::string uuid = create_stored_printer("Office printer", incompatible_hw_config);
    selected_config_container().physical_printer_selection() = {uuid, "Office printer"};

    const std::string hw_config_name = current_hw_config().name;
    const fs::path path              = store_selected_project();

    load(path);

    REQUIRE(physical_printers().selected_physical_printer_data() != nullptr);
    const PhysicalPrinterConfig& printer = *physical_printers().selected_physical_printer_data();
    CHECK(printer.is_runtime);
    CHECK(printer.uuid != uuid);
    CHECK(printer.name == "Office printer");
    CHECK(current_hw_config().name == hw_config_name);
    CHECK(runtime_printer_count() == 1);
}

TEST_CASE_METHOD(
    PhysicalPrinterFromProjectFixture,
    "Runtime physical printers do not leak to other projects",
    "[ProjectInteractor][PhysicalPrinter]"
)
{
    const SelectionId initial_project_id = project_interactor->selected_project_id();
    const fs::path path          = store_selected_project();
    const SelectionId project_id = load(path);
    const std::string uuid       = physical_printers().selected_uuid();
    REQUIRE(runtime_printer_count() == 1);

    const SelectionId other_project_id = project_interactor->new_project();
    CHECK(runtime_printer_count() == 0);
    CHECK(physical_printers().selected_physical_printer_data() == nullptr);
    CHECK(physical_printers().selected_uuid().empty());

    project_interactor->select_project(project_id);
    CHECK(runtime_printer_count() == 1);
    CHECK(physical_printers().selected_uuid() == uuid);

    project_interactor->select_project(other_project_id);
    CHECK(runtime_printer_count() == 0);
    CHECK(physical_printers().selected_uuid().empty());

    project_interactor->select_project(project_id);
    project_interactor->select_project(initial_project_id);
    CHECK(runtime_printer_count() == 0);
    CHECK(physical_printers().selected_uuid().empty());
}

TEST_CASE_METHOD(
    PhysicalPrinterFromProjectFixture,
    "New config container selects compatible local printer",
    "[ProjectInteractor][PhysicalPrinter]"
)
{
    Slic3r::Domain::Preset::HwPrinterConfig incompatible_hw_config = current_hw_config();
    incompatible_hw_config.model.model += "_incompatible";
    const std::string incompatible_uuid =
        create_stored_printer("Office printer", incompatible_hw_config);
    const std::string compatible_uuid = create_stored_printer("Local printer", current_hw_config());
    physical_printers().select_uuid(incompatible_uuid);

    const std::string hw_config_name = current_hw_config().name;
    project_interactor->add_config_container();

    CHECK(physical_printers().selected_uuid() == compatible_uuid);
    CHECK(selected_config_container().physical_printer_selection().uuid == compatible_uuid);
    CHECK(current_hw_config().name == hw_config_name);
}

TEST_CASE_METHOD(
    PhysicalPrinterFromProjectFixture,
    "No physical printer is a valid selection",
    "[ProjectInteractor][PhysicalPrinter]"
)
{
    CHECK(physical_printers().selected_physical_printer_data() == nullptr);
    CHECK_FALSE(physical_printers().is_printer_upload_selected());
    CHECK_FALSE(physical_printers().is_connect_printer_selected());

    const std::string uuid = create_stored_printer("Local printer", current_hw_config());
    REQUIRE(physical_printers().selected_uuid() == uuid);

    physical_printers().remove_uuid(uuid);
    stored_uuids.clear();
    CHECK(physical_printers().selected_physical_printer_data() == nullptr);

    project_interactor->add_config_container();
    CHECK(physical_printers().selected_physical_printer_data() == nullptr);
}

TEST_CASE_METHOD(
    PhysicalPrinterFromProjectFixture,
    "Saved physical printer is selected only when compatible",
    "[ProjectInteractor][PhysicalPrinter]"
)
{
    Slic3r::Domain::Preset::HwPrinterConfig incompatible_hw_config = current_hw_config();
    incompatible_hw_config.model.model += "_incompatible";

    PhysicalPrinterConfig incompatible;
    incompatible.payload   = FileSystemExport{false};
    incompatible.name      = "Office printer";
    incompatible.hw_config = incompatible_hw_config;
    stored_uuids.push_back(physical_printers().save_physical_printer(incompatible));
    CHECK(physical_printers().selected_physical_printer_data() == nullptr);

    physical_printers().select_default();
    CHECK(physical_printers().selected_physical_printer_data() == nullptr);

    PhysicalPrinterConfig compatible = incompatible;
    compatible.name      = "Local printer";
    compatible.hw_config = current_hw_config();
    stored_uuids.push_back(physical_printers().save_physical_printer(compatible));

    const std::string hw_config_name = current_hw_config().name;
    physical_printers().select_default();
    CHECK(physical_printers().selected_uuid() == stored_uuids.back());
    CHECK(current_hw_config().name == hw_config_name);
}

namespace {

// Directory of the material database (MDB) a test opens. It is a base of the fixture that
// comes before the one owning the ProjectInteractor, so that it is removed only after the
// ProjectInteractor, which keeps the MDB open, is gone.
struct TestMatDbDir
{
    ~TestMatDbDir()
    {
        if (!matdb_dir.empty()) {
            boost::system::error_code ec;
            boost::filesystem::remove_all(matdb_dir, ec);
        }
    }

    boost::filesystem::path matdb_dir;
};

struct ConnectSelectPrinterFixture :
    public TestMatDbDir,
    public PhysicalPrinterFromProjectFixture,
    public Slic3r::Biz::Connect::IConnectHandlerListener,
    public Slic3r::Biz::Preset::IPrinterPresetSelectorListener
{
    ConnectSelectPrinterFixture()
    {
        using namespace Slic3r::Biz::Network;
        configure_http_factory_with_mock();
        MockHttpProvider::get().set_create_fn(
            [this](IHttp::RequestMethod, const std::string& url, IHttp::RetryFn retry_fn)
            {
                auto http = std::make_unique<MockHttp>(url, std::move(retry_fn));
                http->set_perform_override_fn(
                    [body = printer_list.dump()](MockHttp* performed, const HttpRetryOpt&)
                    { performed->invoke_complete(body, 200); }
                );
                return http;
            }
        );
        connect_message_handler().add_listener<IConnectHandlerListener>(this);
        physical_printers()
            .printer_preset_selector()
            .add_listener<Slic3r::Biz::Preset::IPrinterPresetSelectorListener>(this);
    }

    ~ConnectSelectPrinterFixture()
    {
        physical_printers()
            .printer_preset_selector()
            .remove_listener<Slic3r::Biz::Preset::IPrinterPresetSelectorListener>(this);
        connect_message_handler().remove_listener<IConnectHandlerListener>(this);
    }

    void on_connect_handler_error(const std::string& msg) override
    {
        errors.push_back(msg);
    }

    void on_printer_preset_selection_warning(const std::string& msg) override
    {
        warnings.push_back(msg);
    }

    Slic3r::Biz::Connect::ConnectMessageHandler& connect_message_handler()
    {
        return project_interactor->connect_message_handler();
    }

    void add_connect_printer(
        const std::string& uuid,
        const Slic3r::Domain::Preset::HwPrinterConfig& hw_config,
        const std::vector<std::string>& material_types = {}
    )
    {
        nlohmann::json tools = nlohmann::json::object();
        for (size_t i = 0; i < hw_config.tool_count; ++i) {
            tools[std::to_string(i)] = {{"class", "FFF"}, {"features", hw_config.tools.at(i).features}};
            if (i < material_types.size()) {
                tools[std::to_string(i)]["material_package_instance"] = {
                    {"package", {{"material", {{"type", material_types[i]}}}}}
                };
            }
        }
        printer_list["printers"].push_back({
            {"uuid", uuid},
            {"name", "Connect printer"},
            {"model", hw_config.model.model},
            {"base_model", hw_config.model.base_model},
            {"tool_count", hw_config.tool_count},
            {"tools", tools},
        });
    }

    template <typename Predicate>
    void select_from_connect(const std::string& uuid, Predicate&& finished)
    {
        connect_message_handler().handle_select_printer_message(
            nlohmann::json{{"uuid", uuid}}.dump()
        );

        const std::chrono::steady_clock::time_point deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!finished() && std::chrono::steady_clock::now() < deadline) {
            dispatcher.dispatch_enqueued();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    const Slic3r::Domain::Preset::HwPrinterConfig* find_other_tool_config()
    {
        const auto& items = project_interactor->preset_interactor().printer_configs().items();
        for (size_t i = 0, n = items.size(); i < n; ++i) {
            const auto& hw_config =
                workbench.preset_bundle().printer_configs.at(items.at(i).hw_printer_config_id);
            if (hw_config.model == current_hw_config().model
                && hw_config.tool_count == 1
                && hw_config.feeders.empty()
                && hw_config.tools.at(0).id != current_hw_config().tools.at(0).id)
            {
                return &hw_config;
            }
        }
        return nullptr;
    }

    const Slic3r::Domain::Preset::HwPrinterConfig* find_config_without_printer_presets()
    {
        auto& preset_interactor = project_interactor->preset_interactor();
        const auto& items       = preset_interactor.printer_configs().items();
        for (size_t i = 0, n = items.size(); i < n; ++i) {
            const auto& hw_config =
                workbench.preset_bundle().printer_configs.at(items.at(i).hw_printer_config_id);
            if (hw_config.feeders.empty()
                && std::ranges::empty(preset_interactor.get_printer_presets(hw_config.id)))
            {
                return &hw_config;
            }
        }
        return nullptr;
    }

    std::string selected_filament_type(size_t slot)
    {
        const Slic3r::Domain::ConfigValue* value =
            project_interactor->preset_interactor().material_cbi_list().at(slot).find("filament_type");
        if (value == nullptr) {
            return {};
        }
        if (value->holds_alternative<std::vector<std::string>>()) {
            const auto values = value->get<std::vector<std::string>>();
            return values.empty() ? std::string{} : values.front();
        }
        return value->holds_alternative<std::string>() ? value->get<std::string>() : std::string{};
    }

    /// Sets what the last added Connect printer reports in a tool, null for an empty tool.
    void set_connect_material(size_t tool, const nlohmann::json& material)
    {
        nlohmann::json& tool_json = printer_list["printers"].back()["tools"][std::to_string(tool)];
        if (material.is_null()) {
            tool_json["material_package_instance"] = nullptr;
        } else {
            tool_json["material_package_instance"] = {{"package", {{"material", material}}}};
        }
    }

    std::string selected_material_id(size_t slot)
    {
        return project_interactor->preset_interactor()
            .selected_printer_preset()
            .materials.at(slot)
            .id;
    }

    std::string selected_matdb_uuid(size_t slot)
    {
        return std::string{
            project_interactor->preset_interactor().selected_printer_preset().material_matdb_uuid(
                slot
            )
        };
    }

    std::vector<std::string> extruder_colours()
    {
        return selected_config_container()
            .project_settings()
            .items.opt("extruder_colour")
            .get<std::vector<std::string>>();
    }

    /// Opens the small material database (MDB) of the test data, as the app opens its own.
    /// A damaged one has only its index, the files of the brands are missing.
    void open_test_matdb(bool damaged = false)
    {
        namespace fs = boost::filesystem;
        matdb_dir    = fs::temp_directory_path() / fs::unique_path();
        fs::create_directories(matdb_dir / "matdb");
        const fs::path source = Tests::get_datadir() / "matdb" / "matdb-mini.zip";
        const fs::path target = matdb_dir / "matdb" / "matdb.zip";
        if (damaged) {
            using Slic3r::Biz::Algorithms::ZipArchive;
            const fs::path index = matdb_dir / "index.json";
            ZipArchive full      = ZipArchive::open(source.string(), ZipArchive::Mode::Read);
            REQUIRE(full.extract_to_file(full.locate_file("index.json"), index.string()));
            ZipArchive index_only = ZipArchive::open(target.string(), ZipArchive::Mode::Write);
            REQUIRE(index_only.add_entry_file("index.json", index.string()));
            REQUIRE(index_only.close());
        } else {
            fs::copy_file(source, target);
        }

        // The data directory is global, it is restored even when opening the MDB throws.
        struct DataDirRestorer
        {
            ~DataDirRestorer()
            {
                Slic3r::set_data_dir(data_dir);
            }

            std::string data_dir = Slic3r::data_dir();
        } restorer;

        Slic3r::set_data_dir(matdb_dir.string());
        project_interactor->matdb_interactor().init_client();
    }

    /// Selects the MDB material in the slot the way the material selection does.
    void select_matdb_material(size_t slot, const std::string& matdb_uuid)
    {
        auto& matdb         = project_interactor->matdb_interactor();
        const auto material = matdb.get_material_by_uuid(matdb_uuid);
        REQUIRE(material.has_value());
        REQUIRE(matdb.select_material_preset(slot, *material, selected_material_id(slot)));
    }

    /// Gives the slot a color of the user, which a reset to the automatic color would change.
    void set_user_colour(size_t slot, const std::string& color)
    {
        project_interactor->project_settings_interactor().set_color_from_user(
            project_interactor->selected_project_id(),
            project_interactor->selected_config_container_id(),
            static_cast<int>(slot),
            color
        );
        REQUIRE(extruder_colours().at(slot) == color);
    }

    bool is_project_unsaved()
    {
        return project_interactor->backup_store().is_project_unsaved(
            project_interactor->selected_project_id()
        );
    }

    nlohmann::json printer_list{{"printers", nlohmann::json::array()}};
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

} // namespace

TEST_CASE_METHOD(
    ConnectSelectPrinterFixture,
    "Set as current in Connect selects the Connect physical printer",
    "[ProjectInteractor][PhysicalPrinter]"
)
{
    const Slic3r::Domain::Preset::HwPrinterConfig* target = find_other_tool_config();
    REQUIRE(target != nullptr);
    const std::string target_tool_id = target->tools.at(0).id;

    const std::string uuid = "c0ffee00-0000-0000-0000-000000000001";
    add_connect_printer(uuid, *target);

    select_from_connect(uuid, [&] { return physical_printers().selected_uuid() == uuid; });

    CHECK(errors.empty());
    CHECK(physical_printers().selected_uuid() == uuid);
    CHECK(physical_printers().is_connect_printer_selected());
    CHECK(selected_config_container().physical_printer_selection().uuid == uuid);
    REQUIRE(current_hw_config().tools.size() == 1);
    CHECK(current_hw_config().tools.at(0).id == target_tool_id);
}

TEST_CASE_METHOD(
    ConnectSelectPrinterFixture,
    "Set as current in Connect with unknown printer keeps the selection",
    "[ProjectInteractor][PhysicalPrinter]"
)
{
    add_connect_printer("c0ffee00-0000-0000-0000-000000000001", current_hw_config());
    const std::string uuid           = create_stored_printer("Local printer", current_hw_config());
    const std::string hw_config_name = current_hw_config().name;

    select_from_connect("c0ffee00-0000-0000-0000-00000000dead", [&] { return !errors.empty(); });

    CHECK(errors.size() == 1);
    CHECK(physical_printers().selected_uuid() == uuid);
    CHECK(current_hw_config().name == hw_config_name);
}

TEST_CASE_METHOD(
    ConnectSelectPrinterFixture,
    "Connect physical printer is compatible with the printer config it selected",
    "[ProjectInteractor][PhysicalPrinter]"
)
{
    const Slic3r::Domain::Preset::HwPrinterConfig* target = find_other_tool_config();
    REQUIRE(target != nullptr);

    const std::string uuid = "c0ffee00-0000-0000-0000-000000000001";
    add_connect_printer(uuid, *target);
    select_from_connect(uuid, [&] { return physical_printers().selected_uuid() == uuid; });

    REQUIRE(physical_printers().selected_physical_printer_data() != nullptr);
    CHECK(Slic3r::Biz::PhysicalPrinter::is_physical_printer_compatible(
        *physical_printers().selected_physical_printer_data(),
        current_hw_config()
    ));
}

TEST_CASE_METHOD(
    ConnectSelectPrinterFixture,
    "Set as current in Connect without matching printer profile keeps the selection",
    "[ProjectInteractor][PhysicalPrinter]"
)
{
    const Slic3r::Domain::Preset::HwPrinterConfig* target = find_config_without_printer_presets();
    REQUIRE(target != nullptr);

    const std::string stored_uuid    = create_stored_printer("Local printer", current_hw_config());
    const std::string hw_config_name = current_hw_config().name;

    const std::string uuid = "c0ffee00-0000-0000-0000-000000000001";
    add_connect_printer(uuid, *target);
    select_from_connect(uuid, [&] { return !warnings.empty(); });

    CHECK(warnings.size() == 1);
    CHECK(physical_printers().selected_uuid() == stored_uuid);
    CHECK(selected_config_container().physical_printer_selection().uuid == stored_uuid);
    CHECK(current_hw_config().name == hw_config_name);
}

TEST_CASE_METHOD(
    ConnectSelectPrinterFixture,
    "Set as current in Connect selects the loaded filament",
    "[ProjectInteractor][PhysicalPrinter]"
)
{
    const std::string filament_type = selected_filament_type(0);
    REQUIRE(!filament_type.empty());

    const std::string uuid = "c0ffee00-0000-0000-0000-000000000001";
    add_connect_printer(uuid, current_hw_config(), {filament_type});
    select_from_connect(uuid, [&] { return physical_printers().selected_uuid() == uuid; });

    CHECK(errors.empty());
    CHECK(warnings.empty());
    CHECK(physical_printers().selected_uuid() == uuid);
    CHECK(selected_filament_type(0) == filament_type);
}

TEST_CASE_METHOD(
    ConnectSelectPrinterFixture,
    "Set as current in Connect warns about loaded filament without profile",
    "[ProjectInteractor][PhysicalPrinter]"
)
{
    const std::string filament_type = selected_filament_type(0);

    const std::string uuid = "c0ffee00-0000-0000-0000-000000000001";
    add_connect_printer(uuid, current_hw_config(), {"UNKNOWN_FILAMENT"});
    select_from_connect(uuid, [&] { return physical_printers().selected_uuid() == uuid; });

    CHECK(errors.empty());
    CHECK(warnings.size() == 1);
    CHECK(physical_printers().selected_uuid() == uuid);
    CHECK(selected_filament_type(0) == filament_type);
}

TEST_CASE_METHOD(
    ConnectSelectPrinterFixture,
    "Set as current in Connect uses the material abbreviation without a type",
    "[ProjectInteractor][PhysicalPrinter]"
)
{
    const std::string filament_type = selected_filament_type(0);
    REQUIRE(!filament_type.empty());

    const std::string uuid = "c0ffee00-0000-0000-0000-000000000001";
    add_connect_printer(uuid, current_hw_config());

    SECTION("abbreviation with a profile")
    {
        // The color is taken only from a filament that was matched, here by the abbreviation.
        set_connect_material(
            0,
            {{"class", "FFF"},
             {"abbreviation", filament_type},
             {"primary_color", {{"color_rgba", "#12ab34ff"}}}}
        );
        select_from_connect(uuid, [&] { return physical_printers().selected_uuid() == uuid; });

        CHECK(errors.empty());
        CHECK(warnings.empty());
        CHECK(selected_filament_type(0) == filament_type);
        const std::vector<std::string> colors = extruder_colours();
        REQUIRE(!colors.empty());
        CHECK(colors.front() == "#12ab34");
    }

    SECTION("abbreviation without a profile")
    {
        set_connect_material(0, {{"class", "FFF"}, {"abbreviation", "UNKNOWN_FILAMENT"}});
        select_from_connect(uuid, [&] { return physical_printers().selected_uuid() == uuid; });

        CHECK(errors.empty());
        REQUIRE(warnings.size() == 1);
        CHECK(warnings.front().find("UNKNOWN_FILAMENT") != std::string::npos);
        CHECK(selected_filament_type(0) == filament_type);
    }
}

TEST_CASE_METHOD(
    ConnectSelectPrinterFixture,
    "Set as current in Connect leaves an empty tool alone",
    "[ProjectInteractor][PhysicalPrinter]"
)
{
    // The slot holds an MDB material and a color of the user, which an empty tool must not
    // take away.
    open_test_matdb();
    select_matdb_material(0, "f7bf0baf-0419-51ee-89ec-f6fc6020f7b6");
    set_user_colour(0, "#12ab34");
    const std::string material_id                  = selected_material_id(0);
    const std::string matdb_uuid                   = selected_matdb_uuid(0);
    const std::vector<std::string> previous_colors = extruder_colours();
    REQUIRE(!matdb_uuid.empty());

    const std::string uuid = "c0ffee00-0000-0000-0000-000000000001";
    add_connect_printer(uuid, current_hw_config());
    set_connect_material(0, nullptr);
    select_from_connect(uuid, [&] { return physical_printers().selected_uuid() == uuid; });

    REQUIRE(physical_printers().selected_uuid() == uuid);
    CHECK(errors.empty());
    CHECK(warnings.empty());
    CHECK(selected_material_id(0) == material_id);
    CHECK(selected_matdb_uuid(0) == matdb_uuid);
    CHECK(extruder_colours() == previous_colors);
}

TEST_CASE_METHOD(
    ConnectSelectPrinterFixture,
    "Set as current in Connect applies the filament color",
    "[ProjectInteractor][PhysicalPrinter]"
)
{
    const std::string filament_type = selected_filament_type(0);
    REQUIRE(!filament_type.empty());

    const std::vector<std::string> previous_colors = extruder_colours();
    REQUIRE(!previous_colors.empty());
    REQUIRE(previous_colors.front() != "#12ab34");

    const std::string uuid = "c0ffee00-0000-0000-0000-000000000001";
    add_connect_printer(uuid, current_hw_config());
    const auto select_with_color = [&](const std::string& color)
    {
        set_connect_material(
            0,
            {{"type", filament_type}, {"primary_color", {{"color_rgba", color}}}}
        );
        select_from_connect(uuid, [&] { return physical_printers().selected_uuid() == uuid; });
        REQUIRE(physical_printers().selected_uuid() == uuid);
        CHECK(errors.empty());
        CHECK(warnings.empty());
    };

    SECTION("with alpha, as Connect reports it")
    {
        select_with_color("#12ab34ff");
        CHECK(extruder_colours().front() == "#12ab34");
    }

    SECTION("without alpha")
    {
        select_with_color("#12ab34");
        CHECK(extruder_colours().front() == "#12ab34");
    }

    SECTION("malformed")
    {
        select_with_color("#12ab34zz");
        CHECK(extruder_colours() == previous_colors);
    }
}

TEST_CASE_METHOD(
    ConnectSelectPrinterFixture,
    "Set as current in Connect keeps the color of a slot it cannot synchronize",
    "[ProjectInteractor][PhysicalPrinter]"
)
{
    const std::string material_id                  = selected_material_id(0);
    const std::vector<std::string> previous_colors = extruder_colours();
    REQUIRE(!previous_colors.empty());

    const std::string uuid = "c0ffee00-0000-0000-0000-000000000001";
    add_connect_printer(uuid, current_hw_config());

    SECTION("filament without a profile")
    {
        set_connect_material(
            0,
            {{"type", "UNKNOWN_FILAMENT"}, {"primary_color", {{"color_rgba", "#12ab34ff"}}}}
        );
        select_from_connect(uuid, [&] { return physical_printers().selected_uuid() == uuid; });

        CHECK(warnings.size() == 1);
    }

    SECTION("filament without a type")
    {
        set_connect_material(
            0,
            {{"class", "FFF"}, {"primary_color", {{"color_rgba", "#12ab34ff"}}}}
        );
        select_from_connect(uuid, [&] { return physical_printers().selected_uuid() == uuid; });

        CHECK(warnings.empty());
    }

    CHECK(physical_printers().selected_uuid() == uuid);
    CHECK(errors.empty());
    CHECK(selected_material_id(0) == material_id);
    CHECK(extruder_colours() == previous_colors);
}

TEST_CASE_METHOD(
    ConnectSelectPrinterFixture,
    "Connect tool keys address tools and feeder slots",
    "[ProjectInteractor][PhysicalPrinter]"
)
{
    bool tested_tools   = false;
    bool tested_feeders = false;
    for (const auto& [id, hw_config] : workbench.preset_bundle().printer_configs) {
        if (hw_config.technology != Slic3r::Domain::PrinterTechnology::FFF) {
            continue;
        }
        // Connect reports a tool as "<tool>" and a slot of a feeder on it as "<tool>.<slot>".
        std::vector<std::string> expected;
        for (uint8_t tool = 0; tool < hw_config.tool_count; ++tool) {
            const auto feeder = hw_config.feeders.find({tool});
            if (feeder == hw_config.feeders.end()) {
                expected.push_back(std::to_string(tool));
                continue;
            }
            for (uint32_t slot = 0; slot < feeder->second.slot_count; ++slot) {
                expected.push_back(fmt::format("{}.{}", tool, slot));
            }
        }
        REQUIRE(hw_config.material_slot_count() == expected.size());

        INFO("printer config " << hw_config.name);
        for (size_t slot = 0; slot < expected.size(); ++slot) {
            CHECK(Slic3r::Biz::Preset::connect_tool_key(hw_config, slot) == expected[slot]);
        }
        tested_tools   = tested_tools || hw_config.tool_count > 1;
        tested_feeders = tested_feeders || !hw_config.feeders.empty();
    }
    CHECK(tested_tools);
    CHECK(tested_feeders);
}

TEST_CASE_METHOD(
    ConnectSelectPrinterFixture,
    "Connect materials of a feeder are read from its slots",
    "[ProjectInteractor][PhysicalPrinter]"
)
{
    using Slic3r::Biz::Preset::ConnectMaterial;
    using Slic3r::Biz::Preset::PrinterPresetSelector;

    // A printer config with a feeder (MMU) on its first tool, e.g. the XL 5T of the test bundle.
    const Slic3r::Domain::Preset::HwPrinterConfig* hw_config = nullptr;
    for (const auto& [id, config] : workbench.preset_bundle().printer_configs) {
        if (config.technology == Slic3r::Domain::PrinterTechnology::FFF
            && config.feeders.find(Slic3r::Domain::Preset::Address{0}) != config.feeders.end())
        {
            hw_config = &config;
            break;
        }
    }
    REQUIRE(hw_config != nullptr);
    const size_t feeder_slots =
        hw_config->feeders.at(Slic3r::Domain::Preset::Address{0}).slot_count;
    REQUIRE(feeder_slots >= 3);

    // As Connect reports it (see hw_config_connect/printer_6.json): the tool with the feeder as
    // "0" without a material, the slots of the feeder as "0.<slot>".
    const auto tool = [](const nlohmann::json& material)
    {
        nlohmann::json package = nullptr;
        if (!material.is_null()) {
            package = {{"package", {{"material", material}}}};
        }
        return nlohmann::json{{"material_package_instance", package}};
    };
    nlohmann::json tools = nlohmann::json::object();
    tools["0"]           = tool(nullptr);
    tools["0.0"]         = tool(
        {{"type", "PLA"},
         {"uuid", "f7bf0baf-0419-51ee-89ec-f6fc6020f7b6"},
         {"primary_color", {{"color_rgba", "#008fbeff"}}}}
    );
    tools["0.1"]                = tool({{"abbreviation", "PETG"}, {"uuid", nullptr}});
    tools["0.2"]                = tool(nullptr);
    tools["1"]                  = tool({{"type", "ASA"}});
    nlohmann::json printer_json = nlohmann::json::object();
    printer_json["tools"]       = tools;

    const std::vector<ConnectMaterial> materials =
        PrinterPresetSelector::connect_materials(printer_json, *hw_config);
    REQUIRE(materials.size() == hw_config->material_slot_count());
    CHECK(materials[0].type == "PLA");
    CHECK(materials[0].matdb_uuid == "f7bf0baf-0419-51ee-89ec-f6fc6020f7b6");
    CHECK(materials[0].color == "#008fbe");
    CHECK(materials[1].type == "PETG");
    CHECK(materials[1].matdb_uuid.empty());
    CHECK(materials[1].color.empty());
    for (size_t slot = 2; slot < materials.size(); ++slot) {
        INFO("slot " << slot);
        // The second tool follows the slots of the feeder.
        CHECK(materials[slot].type == (slot == feeder_slots ? "ASA" : ""));
        CHECK(materials[slot].matdb_uuid.empty());
        CHECK(materials[slot].color.empty());
    }

    // The types are preferred when the printer is switched to the Connect printer.
    std::vector<std::string> expected_types(materials.size());
    expected_types[0] = "PLA";
    expected_types[1] = "PETG";
    if (feeder_slots < expected_types.size()) {
        expected_types[feeder_slots] = "ASA";
    }
    std::vector<std::string> preferred_types;
    for (const auto& preferred : PrinterPresetSelector::material_types(materials)) {
        preferred_types.push_back(preferred.type);
        // The MDB material is looked up by the caller.
        CHECK(!preferred.matdb_material.has_value());
    }
    CHECK(preferred_types == expected_types);
}

namespace {

// Prusament PLA Azure Blue as Connect reports it (see hw_config_connect/printer_4.json);
// the material is in the test MDB (matdb-mini.zip).
const std::string AZURE_BLUE_UUID = "f7bf0baf-0419-51ee-89ec-f6fc6020f7b6";
// The preset of the test bundle ranked highest for it; the type prefers "Prusament PLA".
const std::string AZURE_BLUE_PRESET = "Prusament PLA Azure Blue";
// Prusament PETG Ocean Blue, also in the test MDB. The test bundle has no PETG profile.
const std::string OCEAN_BLUE_UUID = "421e0d10-0ec7-50c9-927c-7a585e8a15c2";

// Answers the unsaved changes dialog by transferring the changed material options.
class TransferMaterialChangesDialog : public Slic3r::Biz::Preset::IPresetDialogManager
{
public:
    PresetsSwitchStates show_unsaved_changes_dialog(
        const std::string& /*dialog_name*/,
        const Slic3r::Domain::ConfigPack& config_original,
        const Slic3r::Domain::ConfigPack& config_selected,
        Slic3r::Domain::ConfigPack* /*config_new_selected*/,
        const Slic3r::Biz::Preset::PresetSelectionNames& /*preset_names*/,
        const Slic3r::Biz::Preset::PresetSelectionNames& preset_names_new,
        const Slic3r::Biz::Preset::PresetInteractor& /*preset_interactor*/,
        bool /*new_printer_has_multiple_extruders*/
    ) override
    {
        ++calls;
        new_material_names.clear();
        for (const auto& name : preset_names_new.materials) {
            new_material_names.push_back(name.name);
        }

        PresetsSwitchStates states;
        const auto& original = std::get<Slic3r::Domain::ConfigPackFDM>(config_original);
        const auto& selected = std::get<Slic3r::Domain::ConfigPackFDM>(config_selected);
        for (size_t slot = 0; slot < selected.filament.size(); ++slot) {
            Slic3r::Biz::Preset::PresetSwitchState state{
                .operation = Slic3r::Biz::Preset::PresetDiffOperation::Transfer
            };
            for (const auto& key : selected.filament[slot].diff_keys(original.filament[slot])) {
                const auto it = selected.filament[slot].find(key);
                if (it.is_override) {
                    state.overrides.insert({key, it.item->value()});
                } else {
                    state.items.insert({key, it.item->value()});
                }
            }
            if (!state.items.empty() || !state.overrides.empty()) {
                states.emplace(
                    Slic3r::Biz::Preset::
                        PresetSwitchKindId{Slic3r::Domain::Preset::PresetKind::FdmMaterial, slot},
                    std::move(state)
                );
            }
        }
        return states;
    }

    std::string show_save_dialog(
        Slic3r::Domain::Preset::PresetKind /*kind*/,
        const std::string& /*original_name*/,
        const Slic3r::Biz::Preset::PresetInteractor& /*preset_interactor*/
    ) override
    {
        return {};
    }

    NamesPerKindMap show_save_print_tool_dialog(
        const NamesPerKindMap& /*original_names_per_kind*/,
        const Slic3r::Biz::Preset::PresetInteractor& /*preset_interactor*/
    ) override
    {
        return {};
    }

    /// Number of the dialogs shown.
    size_t calls{0};
    /// Names of the material presets the last dialog showed as the new ones.
    std::vector<std::string> new_material_names;
};

// Records the MDB uuid of the first material slot whenever the material presets notify
// their observers. The sidebar shows the material of the slot at these points.
class FirstSlotMatDbUuidRecorder :
    public Slic3r::Biz::IListObserver<Slic3r::Biz::Preset::PresetItemObservableList>
{
public:
    explicit FirstSlotMatDbUuidRecorder(Slic3r::Biz::Preset::PresetInteractor& preset_interactor) :
        m_preset_interactor(preset_interactor)
    {
        m_preset_interactor.material_presets()
            .add_listener<IListObserver<Slic3r::Biz::Preset::PresetItemObservableList>>(this);
    }

    ~FirstSlotMatDbUuidRecorder()
    {
        m_preset_interactor.material_presets()
            .remove_listener<IListObserver<Slic3r::Biz::Preset::PresetItemObservableList>>(this);
    }

    void on_updated(const Slic3r::Biz::IndexRange& index_range) override
    {
        if (index_range.from == 0) {
            record();
        }
    }

    void on_reset() override
    {
        record();
    }

    /// The uuid at each notification.
    std::vector<std::string> uuids;

private:
    void record()
    {
        uuids.emplace_back(m_preset_interactor.selected_printer_preset().material_matdb_uuid(0));
    }

    Slic3r::Biz::Preset::PresetInteractor& m_preset_interactor;
};

nlohmann::json azure_blue_material(const std::string& uuid)
{
    return {
        {"abbreviation", "PLA"},
        {"brand", {{"name", "Prusament"}, {"uuid", "ae5ff34e-298e-50c9-8f77-92a97fb30b09"}}},
        {"class", "FFF"},
        {"name", "PLA Azure Blue"},
        {"primary_color", {{"color_rgba", "#008fbeff"}}},
        {"type", "PLA"},
        {"uuid", uuid},
    };
}

} // namespace

TEST_CASE_METHOD(
    ConnectSelectPrinterFixture,
    "Set as current in Connect selects the material from the material database",
    "[ProjectInteractor][PhysicalPrinter]"
)
{
    open_test_matdb();
    REQUIRE(project_interactor->matdb_interactor().get_material_by_uuid(AZURE_BLUE_UUID));
    REQUIRE(selected_matdb_uuid(0).empty());
    REQUIRE(!is_project_unsaved());

    const std::string uuid = "c0ffee00-0000-0000-0000-000000000001";
    add_connect_printer(uuid, current_hw_config());
    std::string expected_color;

    SECTION("with the color Connect reports")
    {
        // Connect reports the color of the spool, which takes precedence over the MDB one.
        nlohmann::json material   = azure_blue_material(AZURE_BLUE_UUID);
        material["primary_color"] = {{"color_rgba", "#12ab34ff"}};
        set_connect_material(0, material);
        expected_color = "#12ab34";
    }

    SECTION("with the color from the material database")
    {
        nlohmann::json material = azure_blue_material(AZURE_BLUE_UUID);
        material.erase("primary_color");
        set_connect_material(0, material);
        expected_color = "#008fbe";
    }

    FirstSlotMatDbUuidRecorder shown(project_interactor->preset_interactor());
    select_from_connect(uuid, [&] { return physical_printers().selected_uuid() == uuid; });

    CHECK(errors.empty());
    CHECK(warnings.empty());
    // The preset the MDB match rules rank highest, not "Prusament PLA" preferred by the type.
    CHECK(selected_material_id(0) == AZURE_BLUE_PRESET);
    CHECK(selected_matdb_uuid(0) == AZURE_BLUE_UUID);
    // The uuid is in place when the slot is last notified about its preset, so the sidebar
    // shows the brand and name of the material.
    REQUIRE(!shown.uuids.empty());
    CHECK(shown.uuids.back() == AZURE_BLUE_UUID);
    const std::vector<std::string> colors = extruder_colours();
    REQUIRE(!colors.empty());
    CHECK(colors.front() == expected_color);
    // Like the rest of what "Set as current" selects, it is not a change made by the user.
    CHECK(!is_project_unsaved());
}

TEST_CASE_METHOD(
    ConnectSelectPrinterFixture,
    "Set as current in Connect selects the best preset again for a material the slot holds",
    "[ProjectInteractor][PhysicalPrinter]"
)
{
    open_test_matdb();
    // Prusament PLA, which is also offered for the material, but ranked lower.
    const std::string other_material_id = selected_material_id(0);
    REQUIRE(other_material_id != AZURE_BLUE_PRESET);

    const std::string uuid = "c0ffee00-0000-0000-0000-000000000001";
    add_connect_printer(uuid, current_hw_config());
    set_connect_material(0, azure_blue_material(AZURE_BLUE_UUID));
    select_from_connect(uuid, [&] { return physical_printers().selected_uuid() == uuid; });
    REQUIRE(selected_material_id(0) == AZURE_BLUE_PRESET);
    REQUIRE(selected_matdb_uuid(0) == AZURE_BLUE_UUID);

    // The slot keeps the uuid with the other preset.
    project_interactor->preset_interactor().select_material_preset(0, other_material_id);
    REQUIRE(selected_matdb_uuid(0) == AZURE_BLUE_UUID);

    select_from_connect(uuid, [&] { return selected_material_id(0) == AZURE_BLUE_PRESET; });

    CHECK(errors.empty());
    CHECK(warnings.empty());
    CHECK(selected_material_id(0) == AZURE_BLUE_PRESET);
    CHECK(selected_matdb_uuid(0) == AZURE_BLUE_UUID);
}

TEST_CASE_METHOD(
    ConnectSelectPrinterFixture,
    "Set as current in Connect transfers changes to the material from the material database",
    "[ProjectInteractor][PhysicalPrinter]"
)
{
    open_test_matdb();
    auto& preset_interactor = project_interactor->preset_interactor();
    TransferMaterialChangesDialog dialog;
    preset_interactor.set_dialog_manager(&dialog);

    struct DialogManagerReset
    {
        ~DialogManagerReset()
        {
            preset_interactor.set_dialog_manager(nullptr);
        }

        Slic3r::Biz::Preset::PresetInteractor& preset_interactor;
    } dialog_manager_reset{preset_interactor};

    const auto spool_weight = [&preset_interactor]
    {
        return std::get<Slic3r::Domain::FilamentSettings>(
                   preset_interactor.selected_printer_preset().materials.at(0).values
        )
            .items.opt("filament_spool_weight")
            .get<double>();
    };
    REQUIRE(selected_material_id(0) != AZURE_BLUE_PRESET);
    REQUIRE(spool_weight() != 250.);
    preset_interactor.set_preset_value(
        Slic3r::Domain::FDMConfigLocation::Filament,
        0,
        "filament_spool_weight",
        [](Slic3r::Domain::ConfigItem& item) { item.set(250.); }
    );
    REQUIRE(spool_weight() == 250.);

    const std::string uuid = "c0ffee00-0000-0000-0000-000000000001";
    add_connect_printer(uuid, current_hw_config());
    set_connect_material(0, azure_blue_material(AZURE_BLUE_UUID));
    select_from_connect(uuid, [&] { return physical_printers().selected_uuid() == uuid; });

    CHECK(errors.empty());
    CHECK(warnings.empty());
    // The dialog, shown once, shows the preset that gets selected, and the change is
    // transferred to it.
    CHECK(dialog.calls == 1);
    REQUIRE(dialog.new_material_names.size() == 1);
    CHECK(dialog.new_material_names.front() == AZURE_BLUE_PRESET);
    CHECK(selected_material_id(0) == AZURE_BLUE_PRESET);
    CHECK(selected_matdb_uuid(0) == AZURE_BLUE_UUID);
    CHECK(spool_weight() == 250.);
}

TEST_CASE_METHOD(
    ConnectSelectPrinterFixture,
    "Set as current in Connect reports a material database material without a profile",
    "[ProjectInteractor][PhysicalPrinter]"
)
{
    open_test_matdb();
    REQUIRE(project_interactor->matdb_interactor().get_material_by_uuid(OCEAN_BLUE_UUID));
    REQUIRE(selected_filament_type(0) == "PLA");
    const std::string material_id                  = selected_material_id(0);
    const std::vector<std::string> previous_colors = extruder_colours();

    const std::string uuid = "c0ffee00-0000-0000-0000-000000000001";
    add_connect_printer(uuid, current_hw_config());
    set_connect_material(
        0,
        {{"abbreviation", "PETG"},
         {"primary_color", {{"color_rgba", "#194c55ff"}}},
         {"type", "PETG"},
         {"uuid", OCEAN_BLUE_UUID}}
    );
    select_from_connect(uuid, [&] { return physical_printers().selected_uuid() == uuid; });

    // The PETG is not taken for selected: it has no profile here, which is reported.
    CHECK(errors.empty());
    REQUIRE(warnings.size() == 1);
    CHECK(warnings.front().find("PETG") != std::string::npos);
    CHECK(selected_material_id(0) == material_id);
    CHECK(selected_matdb_uuid(0).empty());
    CHECK(extruder_colours() == previous_colors);
}

TEST_CASE_METHOD(
    ConnectSelectPrinterFixture,
    "Set as current in Connect matches the type when the material database is damaged",
    "[ProjectInteractor][PhysicalPrinter]"
)
{
    open_test_matdb(true);

    const std::string uuid = "c0ffee00-0000-0000-0000-000000000001";
    add_connect_printer(uuid, current_hw_config());
    set_connect_material(0, azure_blue_material(AZURE_BLUE_UUID));
    select_from_connect(uuid, [&] { return physical_printers().selected_uuid() == uuid; });

    CHECK(errors.empty());
    CHECK(warnings.empty());
    CHECK(physical_printers().selected_uuid() == uuid);
    CHECK(selected_filament_type(0) == "PLA");
    // The preset preferred by the type, not the one ranked highest for the material.
    CHECK(selected_material_id(0) != AZURE_BLUE_PRESET);
    // The uuid of the reported filament, though MDB cannot read it now.
    CHECK(selected_matdb_uuid(0) == AZURE_BLUE_UUID);
    const std::vector<std::string> colors = extruder_colours();
    REQUIRE(!colors.empty());
    CHECK(colors.front() == "#008fbe");
}

TEST_CASE_METHOD(
    ConnectSelectPrinterFixture,
    "Set as current in Connect matches the type of a material missing in the material database",
    "[ProjectInteractor][PhysicalPrinter]"
)
{
    open_test_matdb();
    const std::string unknown_uuid = "00000000-0000-0000-0000-00000000beef";
    REQUIRE(!project_interactor->matdb_interactor().get_material_by_uuid(unknown_uuid));

    // The slot holds an MDB material, which the filament matched by its type replaces.
    const std::vector<std::string> auto_colors = extruder_colours();
    select_matdb_material(0, AZURE_BLUE_UUID);
    REQUIRE(selected_matdb_uuid(0) == AZURE_BLUE_UUID);
    REQUIRE(extruder_colours().front() == "#008fbe");
    REQUIRE(auto_colors.front() != "#008fbe");

    const std::string uuid = "c0ffee00-0000-0000-0000-000000000001";
    add_connect_printer(uuid, current_hw_config());
    // The uuid the slot gets with the preset of the type.
    std::string expected_matdb_uuid;

    SECTION("with a color")
    {
        nlohmann::json material   = azure_blue_material(unknown_uuid);
        material["primary_color"] = {{"color_rgba", "#12ab34ff"}};
        set_connect_material(0, material);
        select_from_connect(uuid, [&] { return physical_printers().selected_uuid() == uuid; });

        CHECK(extruder_colours().front() == "#12ab34");
        expected_matdb_uuid = unknown_uuid;
    }

    SECTION("without a color")
    {
        // The slot must not keep the color of the MDB material it lost.
        nlohmann::json material = azure_blue_material(unknown_uuid);
        material.erase("primary_color");
        set_connect_material(0, material);
        select_from_connect(uuid, [&] { return physical_printers().selected_uuid() == uuid; });

        CHECK(extruder_colours() == auto_colors);
        expected_matdb_uuid = unknown_uuid;
    }

    SECTION("without a uuid")
    {
        set_connect_material(0, {{"class", "FFF"}, {"type", "PLA"}});
        select_from_connect(uuid, [&] { return physical_printers().selected_uuid() == uuid; });

        CHECK(extruder_colours() == auto_colors);
    }

    CHECK(physical_printers().selected_uuid() == uuid);
    CHECK(errors.empty());
    CHECK(warnings.empty());
    CHECK(selected_matdb_uuid(0) == expected_matdb_uuid);
    CHECK(selected_filament_type(0) == "PLA");
}

TEST_CASE_METHOD(
    ConnectSelectPrinterFixture,
    "Set as current in Connect keeps a uuid the material database cannot read",
    "[ProjectInteractor][PhysicalPrinter]"
)
{
    // E.g. from a project, or newer than the MDB; the slot has a color of the user.
    open_test_matdb();
    const std::string unknown_uuid = "00000000-0000-0000-0000-00000000beef";
    REQUIRE(!project_interactor->matdb_interactor().get_material_by_uuid(unknown_uuid));
    project_interactor->preset_interactor().set_material_matdb_uuid(0, unknown_uuid);
    set_user_colour(0, "#12ab34");

    const std::string uuid = "c0ffee00-0000-0000-0000-000000000001";
    add_connect_printer(uuid, current_hw_config());
    set_connect_material(0, {{"class", "FFF"}, {"type", "PLA"}, {"uuid", unknown_uuid}});
    select_from_connect(uuid, [&] { return physical_printers().selected_uuid() == uuid; });

    REQUIRE(physical_printers().selected_uuid() == uuid);
    CHECK(errors.empty());
    CHECK(warnings.empty());
    CHECK(selected_matdb_uuid(0) == unknown_uuid);
    CHECK(extruder_colours().front() == "#12ab34");
}

TEST_CASE_METHOD(
    ConnectSelectPrinterFixture,
    "Set as current in Connect selects the preset the material selection offers first",
    "[ProjectInteractor][PhysicalPrinter]"
)
{
    // Prusament PLA Army Green of the test MDB, which "Prusament PLA" and "Prusament PLA Azure
    // Blue" match equally.
    open_test_matdb();
    const std::string army_green_uuid = "6d8798d1-03c7-5981-8e18-dc282a43679c";
    REQUIRE(project_interactor->matdb_interactor().get_material_by_uuid(army_green_uuid));

    const std::string uuid = "c0ffee00-0000-0000-0000-000000000001";
    add_connect_printer(uuid, current_hw_config());
    set_connect_material(
        0,
        {{"class", "FFF"}, {"type", "PLA"}, {"abbreviation", "PLA"}, {"uuid", army_green_uuid}}
    );
    select_from_connect(uuid, [&] { return physical_printers().selected_uuid() == uuid; });

    REQUIRE(physical_printers().selected_uuid() == uuid);
    CHECK(errors.empty());
    CHECK(warnings.empty());
    REQUIRE(selected_matdb_uuid(0) == army_green_uuid);

    // The material selection lists the material of the slot first, with the presets it offers
    // for it, the best match first.
    auto& matdb = project_interactor->matdb_interactor();
    matdb.query(0, "", std::nullopt, false);
    REQUIRE(matdb.result().size() > 0);
    const auto& item = matdb.result().at(0);
    REQUIRE(item.material.uuid == army_green_uuid);
    REQUIRE(item.preset_matches.size() >= 2);
    CHECK(item.preset_matches[0].rank == item.preset_matches[1].rank);
    CHECK(item.preset_matches.front().preset_id == selected_material_id(0));
}
