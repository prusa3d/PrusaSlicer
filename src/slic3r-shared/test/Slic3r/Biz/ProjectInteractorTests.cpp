#include <iostream>
#include <catch2/catch_test_macros.hpp>
#include <catch2/trompeloeil.hpp>

#include "Slic3r/Biz/IProjectsChangedListener.hpp"
#include "Slic3r/Biz/Platform/JobManager/JobManager.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Biz/SecretStoreDummy.hpp"
#include "Slic3r/Biz/AppInstance/AppInstanceMessageHandlerFactory.hpp"
#include "Slic3r/Biz/Connect/ConnectMessageHandler.hpp"
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

struct ConnectSelectPrinterFixture : public PhysicalPrinterFromProjectFixture,
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
