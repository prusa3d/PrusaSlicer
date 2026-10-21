#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterInteractor.hpp"

#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/ProjectScoped.hpp"
#include "Slic3r/Biz/UserAccount/UserAccountInteractor.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Biz/Platform/IAppConfigProvider.hpp"
#include "Slic3r/Biz/PrintHost/IPrintHost.hpp"
#include "Slic3r/Biz/PrintHost/PrintHostFactory.hpp"
#include "Slic3r/Biz/PrintHost/PrintHostJobData.hpp"
#include "Slic3r/Biz/Platform/JobManager/JobManager.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Domain/Preset/HwConfig.hpp"
#include "Slic3r/Domain/Workbench.hpp"

#include "Slic3r/Log.hpp"
#include "nlohmann/json.hpp"

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <algorithm>
#include <memory>
#include <optional>
#include <variant>

namespace Slic3r::Biz::PhysicalPrinter {

namespace {

constexpr const char* CONNECTION_TEST_JOB_NAME = "physical_printer_connection_test";
constexpr size_t CONNECT_PRINTERS_OFFSET       = 0;

bool auth_type_supported_by_host(Domain::PrintHostType type, Domain::PrintHostAuthType auth_type)
{
    if (type != Domain::PrusaLink && type != Domain::SL1Host) {
        return true;
    }
    return auth_type != Domain::PrintHostAuthType::None;
}

PhysicalPrinterConfig connect_printer_config(const ConnectPrinter::ConnectPrinter& printer)
{
    ConnectUpload upload;
    upload.printer_uuid = printer.uuid;
    upload.state        = printer.state;
    return {
        std::move(upload),
        {},
        printer.name.empty() ? printer.model : printer.name,
        printer.uuid,
        hw_config_from_connect_json(printer.json)
    };
}

} // namespace

PhysicalPrinterInteractor::PhysicalPrinterInteractor(
    Domain::Workbench& workbench,
    ProjectInteractor& project_interactor,
    Platform::IMainThreadDispatcher& dispatcher,
    Preset::PresetInteractor& preset_interactor,
    UserAccount::UserAccountInteractor& user_account_interactor
) :
    m_workbench(workbench),
    m_project_contexts(std::make_unique<ProjectContexts>(project_interactor)),
    m_dispatcher(dispatcher),
    m_preset_interactor(preset_interactor),
    m_user_account_interactor(user_account_interactor),
    m_connect_printer_service(dispatcher, user_account_interactor),
    m_listener_scope_connect_printer(m_connect_printer_service, *this),
    m_printer_preset_selector(preset_interactor)
{
    read_storage();
    restore_last_used_selection();
}

PhysicalPrinterInteractor::~PhysicalPrinterInteractor()
{
    ASSERT(
        m_dispatcher.is_closed(),
        "There must be no queued events (not even in the future),"
        " because they may remember the address of this instance!"
    );
}

void PhysicalPrinterInteractor::read_storage()
{
    std::vector<PhysicalPrinterConfig> printers;
    printers.reserve(m_storage.all_printers().size());

    for (const auto& [uuid, printer] : m_storage.all_printers()) {
        printers.push_back(printer);
    }

    m_observable_list.reset(std::move(printers));
}

void PhysicalPrinterInteractor::restore_last_used_selection()
{
    auto& services = Platform::PlatformServices::instance();
    if (!services.has_app_config_provider()) {
        return;
    }
    m_last_used_uuid = services.app_config_provider().last_used_physical_printer();

    auto index = find_index(m_last_used_uuid);
    if (!index || !can_be_selected_at(*index)) {
        return;
    }
    m_selected_uuid      = m_last_used_uuid;
    m_explicit_selection = true;
}

void PhysicalPrinterInteractor::set_last_used(const std::string& uuid)
{
    if (uuid == m_last_used_uuid) {
        return;
    }
    m_last_used_uuid = uuid;
    auto& services   = Platform::PlatformServices::instance();
    if (services.has_app_config_provider()) {
        services.app_config_provider().set_last_used_physical_printer(uuid);
    }
}

void PhysicalPrinterInteractor::remember_used_printer(const std::string& uuid)
{
    if (!find_index(uuid)) {
        return;
    }
    set_last_used(uuid);
}

bool PhysicalPrinterInteractor::can_be_selected_at(size_t index) const
{
    const auto& printer = m_observable_list.at(index);
    if (std::holds_alternative<ConnectUpload>(printer.payload)) {
        return m_user_account_interactor.is_logged_in();
    }
    return true;
}

void PhysicalPrinterInteractor::apply_selection(const std::string& uuid, bool apply_hw_config)
{
    m_selected_uuid                              = uuid;
    const PhysicalPrinterConfig physical_printer = printer_config_at(index_of(uuid));
    if (Domain::ConfigContainer* config_container = current_config_container()) {
        config_container->physical_printer_selection() = {
            physical_printer.uuid,
            physical_printer.name
        };
    }

    // Hw config can be missing for Connect printers with incomplete data
    if (apply_hw_config && physical_printer.hw_config.has_value()) {
        if (physical_printer.is_runtime) {
            m_preset_interactor.select_printer_config(physical_printer.hw_config->id);
        } else {
            m_preset_interactor.select_external_printer_config(physical_printer.hw_config.value());
        }
    }

    this->invoke_listeners<IPhysicalPrinterChangedListener>(
        [](auto* listener) { listener->on_selected_physical_printer_changed(); }
    );
}

void PhysicalPrinterInteractor::clear_selection()
{
    if (m_selected_uuid.empty()) {
        return;
    }
    m_selected_uuid.clear();
    this->invoke_listeners<IPhysicalPrinterChangedListener>(
        [](auto* listener) { listener->on_selected_physical_printer_changed(); }
    );
}

void PhysicalPrinterInteractor::select_uuid(const std::string& uuid)
{
    apply_selection(uuid, true);
    m_explicit_selection = true;

    this->invoke_listeners<IPhysicalPrinterChangedListener>(
        [](auto* listener) { listener->on_physical_printer_picked(); }
    );
}

void PhysicalPrinterInteractor::select_uuid_with_presets(const std::string& uuid)
{
    select_uuid(uuid);

    const PhysicalPrinterConfig& printer = *ASSERT_VAL(selected_physical_printer_data());
    if (const auto* upload = std::get_if<ConnectUpload>(&printer.payload)) {
        if (upload->printer_uuid.empty()) {
            return;
        }
        const ConnectPrinter::ConnectPrinter* connect_printer =
            m_connect_printer_service.find(upload->printer_uuid);
        if (!connect_printer) {
            return;
        }
        try {
            m_printer_preset_selector.select_for_connect_printer(
                nlohmann::json::parse(connect_printer->json)
            );
        } catch (const nlohmann::json::exception& e) {
            SPDLOG_ERROR("Failed to parse Connect printer json: {}", e.what());
        }
    } else if (
        std::holds_alternative<PrinterUpload>(printer.payload) && printer.hw_config.has_value()
    )
    {
        m_printer_preset_selector.select_for_hw_config(printer.hw_config.value());
    }
}

void PhysicalPrinterInteractor::select_default()
{
    m_explicit_selection = false;

    const Domain::ConfigContainer* config_container = current_config_container();
    if (config_container == nullptr) {
        if (!find_index(m_selected_uuid)) {
            clear_selection();
        }
        return;
    }

    const Domain::Preset::HwPrinterConfig& hw_config =
        config_container->selected_preset().hw_config;
    const std::string uuid = config_container->physical_printer_selection().uuid;
    if (std::optional<size_t> index = find_index(uuid);
        index && can_be_selected_at(*index) && is_printer_compatible(*index, hw_config))
    {
        apply_selection(uuid, false);
    } else if (std::optional<size_t> index = find_compatible_index(m_current_project_id, hw_config))
    {
        apply_selection(m_observable_list.at(*index).uuid, false);
    } else {
        clear_selection();
    }
}

void PhysicalPrinterInteractor::remove_uuid(const std::string& uuid)
{
    size_t index = index_of(uuid);
    ASSERT(!is_connect_printer_index(index));
    ASSERT(!m_observable_list.at(index).is_runtime);
    bool was_selected = (uuid == m_selected_uuid);

    m_storage.remove_one(uuid);
    m_observable_list.remove({index, index});

    if (was_selected) {
        select_default();
    }

    if (uuid == m_last_used_uuid) {
        set_last_used({});
    }
}

bool PhysicalPrinterInteractor::is_printer_upload_selected() const
{
    const PhysicalPrinterConfig* printer = selected_physical_printer_data();
    return printer != nullptr && std::holds_alternative<PrinterUpload>(printer->payload);
}

bool PhysicalPrinterInteractor::is_connect_printer_selected() const
{
    const PhysicalPrinterConfig* printer = selected_physical_printer_data();
    return printer != nullptr && std::holds_alternative<ConnectUpload>(printer->payload);
}

void PhysicalPrinterInteractor::create_or_save_physical_printer(
    const PhysicalPrinterConfig& printer
)
{
    select_uuid(save_physical_printer(printer));
}

std::string PhysicalPrinterInteractor::save_physical_printer(const PhysicalPrinterConfig& printer)
{
    const PhysicalPrinterConfig& processed_printer = m_storage.save_or_create(printer);

    std::optional<size_t> index = find_index(processed_printer.uuid);
    if (index.has_value()) {
        m_observable_list.set(processed_printer, index.value());
    } else {
        m_observable_list.append(processed_printer);
    }
    return processed_printer.uuid;
}

ObservableList<PhysicalPrinterConfig>& PhysicalPrinterInteractor::observable_list()
{
    return m_observable_list;
}

const ObservableList<PhysicalPrinterConfig>& PhysicalPrinterInteractor::observable_list() const
{
    return m_observable_list;
}

ConnectPrinter::ConnectPrinterService& PhysicalPrinterInteractor::connect_printer_service()
{
    return m_connect_printer_service;
}

Preset::PrinterPresetSelector& PhysicalPrinterInteractor::printer_preset_selector()
{
    return m_printer_preset_selector;
}

bool PhysicalPrinterInteractor::is_connect_printer_index(size_t index) const
{
    return index >= CONNECT_PRINTERS_OFFSET
        && index < CONNECT_PRINTERS_OFFSET + m_connect_printer_count;
}

PhysicalPrinterConfig PhysicalPrinterInteractor::printer_config_at(size_t index) const
{
    // Connect printers are not in the storage, they are owned by the Connect printer service
    if (is_connect_printer_index(index)) {
        return create_connect_printer_config(
            m_connect_printer_service.at(index - CONNECT_PRINTERS_OFFSET)
        );
    }
    const PhysicalPrinterConfig& printer = m_observable_list.at(index);
    return printer.is_runtime ? printer : m_storage.all_printers().at(printer.uuid);
}

Domain::ConfigContainer* PhysicalPrinterInteractor::current_config_container()
{
    Domain::Project* project = m_workbench.find_project_by_id(m_current_project_id);
    return project ? project->find_config_container(m_current_container_id) : nullptr;
}

void PhysicalPrinterInteractor::sync_runtime_printers(Domain::SelectionId project_id)
{
    const size_t offset = CONNECT_PRINTERS_OFFSET + m_connect_printer_count;
    if (m_runtime_printer_count > 0) {
        m_observable_list.remove({offset, offset + m_runtime_printer_count - 1});
    }
    const std::vector<PhysicalPrinterConfig>& runtime_printers =
        m_project_contexts->project(project_id).runtime_printers;
    for (size_t i = 0; i < runtime_printers.size(); ++i) {
        m_observable_list.insert(runtime_printers[i], offset + i);
    }
    m_runtime_printer_count       = runtime_printers.size();
    m_runtime_printers_project_id = project_id;
}

std::optional<size_t> PhysicalPrinterInteractor::find_compatible_index(
    Domain::SelectionId project_id,
    const Domain::Preset::HwPrinterConfig& hw_config
) const
{
    auto is_compatible = [&](size_t index)
    {
        // Runtime printers in the list may belong to another project
        return (!m_observable_list.at(index).is_runtime
                || m_runtime_printers_project_id == project_id)
            && can_be_selected_at(index)
            && is_printer_compatible(index, hw_config);
    };
    if (std::optional<size_t> index = find_index(m_selected_uuid); index && is_compatible(*index)) {
        return index;
    }
    for (size_t i = 0; i < m_observable_list.size(); ++i) {
        if (is_compatible(i)) {
            return i;
        }
    }
    return std::nullopt;
}

const PhysicalPrinterConfig& PhysicalPrinterInteractor::get_or_create_runtime_printer(
    Domain::SelectionId project_id,
    const Domain::PhysicalPrinterSelection& selection,
    const Domain::Preset::HwPrinterConfig& hw_config
)
{
    const std::string& name = selection.name.empty() ? hw_config.name : selection.name;

    std::vector<PhysicalPrinterConfig>& runtime_printers =
        m_project_contexts->project(project_id).runtime_printers;
    auto it = std::ranges::find_if(
        runtime_printers,
        [&](const PhysicalPrinterConfig& printer)
        { return printer.name == name && printer.hw_config->id == hw_config.id; }
    );
    if (it != runtime_printers.end()) {
        return *it;
    }

    // Uuid from 3mf is not reused, it may belong to a printer which is not available yet
    PhysicalPrinterConfig printer;
    printer.payload    = FileSystemExport{false};
    printer.name       = name;
    printer.uuid       = boost::uuids::to_string(boost::uuids::random_generator()());
    printer.hw_config  = hw_config;
    printer.is_runtime = true;
    return runtime_printers.emplace_back(std::move(printer));
}

void PhysicalPrinterInteractor::load_physical_printers_from_3mf(Domain::SelectionId project_id)
{
    Domain::Project* project = m_workbench.find_project_by_id(project_id);
    ASSERT(project != nullptr);

    for (const std::unique_ptr<Domain::ConfigContainer>& config_container :
         project->config_containers())
    {
        Domain::PhysicalPrinterSelection& selection =
            config_container->physical_printer_selection();
        const Domain::Preset::HwPrinterConfig& hw_config =
            config_container->selected_preset().hw_config;

        if (std::optional<size_t> index = find_index(selection.uuid); index
            && !m_observable_list.at(*index).is_runtime
            && is_printer_compatible(*index, hw_config))
        {
            continue;
        }

        if (selection.uuid.empty()) {
            if (std::optional<size_t> index = find_compatible_index(project_id, hw_config)) {
                const PhysicalPrinterConfig& printer = m_observable_list.at(*index);
                selection = {printer.uuid, printer.name};
                continue;
            }
        }

        const PhysicalPrinterConfig& printer =
            get_or_create_runtime_printer(project_id, selection, hw_config);
        selection = {printer.uuid, printer.name};
    }

    m_runtime_printers_project_id.reset();
}

void PhysicalPrinterInteractor::sync_selection()
{
    if (!find_index(m_selected_uuid)) {
        select_default();
    }
}

PhysicalPrinterConfig PhysicalPrinterInteractor::create_connect_printer_config(
    const ConnectPrinter::ConnectPrinter& printer
) const
{
    PhysicalPrinterConfig config = connect_printer_config(printer);
    if (!config.hw_config.has_value()) {
        return config;
    }

    // External hw config from Connect has no names and visual data, take them from the bundle.
    auto full_hw_config = m_preset_interactor.evaluate_external_printer_config(*config.hw_config);
    if (!full_hw_config.has_value()) {
        SPDLOG_WARN(
            "Cannot evaluate hw config of Connect printer '{}': {}",
            printer.name,
            full_hw_config.error()
        );
        return config;
    }
    Domain::Preset::HwPrinterConfig& hw_config = *config.hw_config;
    hw_config.printer_id = full_hw_config->printer_id;
    hw_config.repo_id    = full_hw_config->repo_id;
    hw_config.vendor_id  = full_hw_config->vendor_id;
    hw_config.name       = full_hw_config->name;
    hw_config.short_name = full_hw_config->short_name;
    hw_config.visual     = full_hw_config->visual;
    hw_config.sheet      = full_hw_config->sheet;
    for (size_t i = 0; i < std::min(hw_config.tools.size(), full_hw_config->tools.size()); ++i) {
        hw_config.tools[i].name = full_hw_config->tools[i].name;
    }
    return config;
}

void
PhysicalPrinterInteractor::on_inserted(const ConnectPrinter::ConnectPrinter& printer, size_t index)
{
    auto cfg = create_connect_printer_config(printer);

    m_observable_list.insert(cfg, CONNECT_PRINTERS_OFFSET + index);
    ++m_connect_printer_count;
    sync_selection();

    const Domain::ConfigContainer* config_container = current_config_container();
    if (!m_explicit_selection
        && printer.uuid == m_last_used_uuid
        && m_selected_uuid != printer.uuid
        && config_container != nullptr
        && is_printer_compatible(
            CONNECT_PRINTERS_OFFSET + index,
            config_container->selected_preset().hw_config
        ))
    {
        apply_selection(printer.uuid, false);
    }
}

void PhysicalPrinterInteractor::on_removed(const IndexRange& index_range)
{
    m_observable_list.remove(
        {CONNECT_PRINTERS_OFFSET + index_range.from, CONNECT_PRINTERS_OFFSET + index_range.to}
    );
    m_connect_printer_count -= index_range.to - index_range.from + 1;
    sync_selection();
}

void PhysicalPrinterInteractor::on_updated(const IndexRange& index_range)
{
    for (size_t i = index_range.from; i <= index_range.to; ++i) {
        PhysicalPrinterConfig config =
            create_connect_printer_config(m_connect_printer_service.at(i));
        const PhysicalPrinterConfig& current = m_observable_list.at(CONNECT_PRINTERS_OFFSET + i);
        if (config.name != current.name
            || std::get<ConnectUpload>(config.payload).state
                != std::get<ConnectUpload>(current.payload).state
            || config.hw_config.has_value() != current.hw_config.has_value()
            || (config.hw_config.has_value()
                && config.hw_config->printer_id != current.hw_config->printer_id))
        {
            m_observable_list.set(std::move(config), CONNECT_PRINTERS_OFFSET + i);
        }
    }
}

void PhysicalPrinterInteractor::on_reset()
{
    if (m_connect_printer_count > 0) {
        on_removed({0, m_connect_printer_count - 1});
    }
    for (size_t i = 0; i < m_connect_printer_service.size(); ++i) {
        on_inserted(m_connect_printer_service.at(i), i);
    }
}

size_t PhysicalPrinterInteractor::index_of(const std::string& uuid) const
{
    return *ASSERT_VAL(find_index(uuid));
}

std::optional<size_t> PhysicalPrinterInteractor::find_index(const std::string& uuid) const
{
    for (size_t i = 0; i < m_observable_list.size(); ++i) {
        if (m_observable_list.at(i).uuid == uuid) {
            return i;
        }
    }
    return std::nullopt;
}

const PhysicalPrinterConfig* PhysicalPrinterInteractor::selected_physical_printer_data() const
{
    std::optional<size_t> index = find_index(m_selected_uuid);
    return index ? &m_observable_list.at(*index) : nullptr;
}

bool PhysicalPrinterInteractor::is_printer_compatible(
    size_t index,
    const Domain::Preset::HwPrinterConfig& config
) const
{
    const PhysicalPrinterConfig& printer = m_observable_list.at(index);
    // Connect printer with incomplete data has no hw config to compare
    if (std::holds_alternative<ConnectUpload>(printer.payload) && !printer.hw_config.has_value()) {
        return true;
    }
    return is_physical_printer_compatible(printer, config);
}

void PhysicalPrinterInteractor::on_selected_config_container_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId container_id
)
{
    m_current_project_id   = project_id;
    m_current_container_id = container_id;
    if (m_runtime_printers_project_id != project_id) {
        sync_runtime_printers(project_id);
    }
    select_default();
}

bool PhysicalPrinterInteractor::is_connection_testable(const PhysicalPrinterConfig& printer) const
{
    if (printer.host.empty()) {
        return false;
    }

    const PrinterUpload* upload = std::get_if<PrinterUpload>(&printer.payload);
    if (upload == nullptr) {
        return false;
    }

    return auth_type_supported_by_host(upload->type, upload->auth_type);
}

bool PhysicalPrinterInteractor::is_connection_test_in_flight() const
{
    return m_connection_test_in_flight;
}

void PhysicalPrinterInteractor::test_printer_connection(
    const PhysicalPrinterConfig& printer,
    std::function<void(PrintHostTestResult)> callback
)
{
    if (m_connection_test_in_flight || !is_connection_testable(printer)) {
        return;
    }

    m_connection_test_callback  = std::move(callback);
    m_connection_test_in_flight = true;

    const size_t generation    = ++m_connection_test_generation;
    m_connection_test_job_name = CONNECTION_TEST_JOB_NAME + std::to_string(generation);

    PhysicalPrinterConfig config = printer;

    std::function job_func = [](JThread::StopToken stop_token,
                                PhysicalPrinterConfig printer) -> PrintHostTestResult
    {
        std::unique_ptr<PrintHost::IPrintHost> host = PrintHost::create_print_host(
            std::move(printer),
            PrintHost::PrintHostJobData{std::monostate{}, {}}
        );
        if (!host) {
            // TRN Error shown when no print host implementation matches the printer settings.
            return {false, {}, _u8L("Could not get a valid printer host reference.")};
        }

        std::string msg;
        const bool ok = host->test(
            msg,
            [stop_token](Network::IHttp::Progress, bool& cancel)
            {
                if (stop_token.stop_requested()) {
                    cancel = true;
                }
            },
            [stop_token](Network::IHttp::Retry, bool& cancel)
            {
                if (stop_token.stop_requested()) {
                    cancel = true;
                }
            }
        );

        if (stop_token.stop_requested()) {
            return {false, {}, {}};
        }

        return {ok, std::string{host->get_name()}, ok ? std::string{} : std::move(msg)};
    };

    Platform::PlatformServices::instance()
        .job_manager()
        .create_job(m_connection_test_job_name, std::move(job_func), std::move(config))
        .on_result([this, generation](PrintHostTestResult result)
                   { finish_connection_test(generation, std::move(result)); })
        .on_exception(
            [this, generation](const std::exception_ptr&)
            {
                // TRN Error shown when the print host connection test itself failed unexpectedly.
                finish_connection_test(generation, {false, {}, _u8L("Connection test failed.")});
            }
        )
        .start();
}

void PhysicalPrinterInteractor::cancel_edited_printer_connection_test()
{
    if (!m_connection_test_in_flight) {
        return;
    }

    ++m_connection_test_generation;
    m_connection_test_in_flight = false;
    m_connection_test_callback  = nullptr;

    if (Platform::PlatformServices::instance().has_job_manager()) {
        Platform::PlatformServices::instance().job_manager().request_job_stop(
            m_connection_test_job_name
        );
    }
    m_connection_test_job_name.clear();
}

void
PhysicalPrinterInteractor::finish_connection_test(size_t generation, PrintHostTestResult result)
{
    if (generation != m_connection_test_generation) {
        return;
    }

    m_connection_test_in_flight = false;
    m_connection_test_job_name.clear();

    std::function<void(PrintHostTestResult)> callback = std::move(m_connection_test_callback);
    m_connection_test_callback                        = nullptr;

    if (callback) {
        callback(std::move(result));
    }
}

} // namespace Slic3r::Biz::PhysicalPrinter
