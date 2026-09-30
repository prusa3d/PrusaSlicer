#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterInteractor.hpp"

#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/UserAccount/UserAccountInteractor.hpp"
#include "Slic3r/Biz/RemovableDrive/RemovableDriveService.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Biz/Platform/IAppConfigProvider.hpp"
#include "Slic3r/Biz/PrintHost/IPrintHost.hpp"
#include "Slic3r/Biz/PrintHost/PrintHostFactory.hpp"
#include "Slic3r/Biz/PrintHost/PrintHostJobData.hpp"
#include "Slic3r/Biz/Platform/JobManager/JobManager.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Domain/Preset/HwConfig.hpp"

#include "Slic3r/Log.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <variant>

namespace Slic3r::Biz::PhysicalPrinter {

namespace {

constexpr const char* CONNECTION_TEST_JOB_NAME = "physical_printer_connection_test";
constexpr size_t CONNECT_PRINTERS_OFFSET = 3;

bool auth_type_supported_by_host(Domain::PrintHostType type, Domain::PrintHostAuthType auth_type)
{
    if (type != Domain::PrusaLink && type != Domain::SL1Host) {
        return true;
    }
    return auth_type != Domain::PrintHostAuthType::None;
}

} // namespace

PhysicalPrinterInteractor::PhysicalPrinterInteractor(
    Platform::IMainThreadDispatcher& dispatcher,
    Preset::PresetInteractor& preset_interactor,
    UserAccount::UserAccountInteractor& user_account_interactor,
    RemovableDrive::RemovableDriveService& removable_drive_service
) :
    m_dispatcher(dispatcher),
    m_preset_interactor(preset_interactor),
    m_user_account_interactor(user_account_interactor),
    m_removable_drive_service(removable_drive_service),
    m_connect_printer_service(dispatcher, user_account_interactor)
{
    read_storage();
    m_selected_uuid = m_observable_list.at(0).uuid;
    m_selected_index = 0;
    restore_last_used_selection();
    m_connect_printer_service.add_printer_list_observer(this);
}

PhysicalPrinterInteractor::~PhysicalPrinterInteractor()
{
    m_connect_printer_service.remove_printer_list_observer(this);
    ASSERT(
        m_dispatcher.is_closed(),
        "There must be no queued events (not even in the future),"
        " because they may remember the address of this instance!"
    );
}

void PhysicalPrinterInteractor::read_storage()
{
    std::vector<PhysicalPrinterConfig> printers;
    printers.reserve(m_storage.all_printers().size() + 3);

    printers.push_back(PhysicalPrinter::filesystem_export_local());
    printers.push_back(PhysicalPrinter::filesystem_export_removable());
    printers.push_back(PhysicalPrinter::connect_upload_generic());

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
    m_selected_uuid     = m_last_used_uuid;
    m_selected_index    = *index;
    m_explicit_selection = true;
}

void PhysicalPrinterInteractor::set_last_used(const std::string& uuid)
{
    if (uuid == m_last_used_uuid) {
        return;
    }
    m_last_used_uuid = uuid;
    auto& services = Platform::PlatformServices::instance();
    if (services.has_app_config_provider()) {
        services.app_config_provider().set_last_used_physical_printer(uuid);
    }
}

void PhysicalPrinterInteractor::remember_used_destination(const std::string& uuid)
{
    if (!find_index(uuid)) {
        return;
    }
    set_last_used(uuid);
}

bool PhysicalPrinterInteractor::can_be_selected(const std::string& uuid) const
{
    return can_be_selected_at(index_of(uuid));
}

bool PhysicalPrinterInteractor::can_be_selected_at(size_t index) const
{
    const auto& printer = m_observable_list.at(index);
    if (std::holds_alternative<ConnectUpload>(printer.payload)) {
        return m_user_account_interactor.is_logged_in();
    }
    if (const auto* fs = std::get_if<FileSystemExport>(&printer.payload); fs && fs->prefer_removable) {
        return m_removable_drive_service.has_removable_drives();
    }
    return true;
}

void PhysicalPrinterInteractor::apply_selection(const std::string& uuid)
{
    m_selected_uuid = uuid;
    m_selected_index = index_of(uuid);
    m_container_to_printer_uuid_map[m_current_container] = m_selected_uuid;

    this->invoke_listeners<IPhysicalPrinterChangedListener>(
        [](auto* listener) {
            listener->on_selected_physical_printer_changed();
        }
    );
}

void PhysicalPrinterInteractor::select_uuid(const std::string& uuid)
{
    apply_selection(uuid);
    m_explicit_selection = true;
}

void PhysicalPrinterInteractor::select_default()
{
    if (m_observable_list.size() == 0) {
        return;
    }
    apply_selection(m_observable_list.at(0).uuid);
    m_explicit_selection = false;
}

void PhysicalPrinterInteractor::select_connect_upload()
{
    select_uuid(std::string(PRUSA_CONNECT_UUID));
}

void PhysicalPrinterInteractor::select_connect_upload_if_default()
{
    if (m_selected_index != 0 || m_explicit_selection) {
        return;
    }
    apply_selection(std::string(PRUSA_CONNECT_UUID));
}

void PhysicalPrinterInteractor::on_removable_drive_status_changed(
    const boost::filesystem::path& /* drive_path */,
    RemovableDrive::RemovableDriveStatus status
)
{
    if (status != RemovableDrive::RemovableDriveStatus::Inserted) {
        return;
    }
    if (m_explicit_selection || m_last_used_uuid != REMOVABLE_DRIVE_UUID || m_selected_uuid == REMOVABLE_DRIVE_UUID) {
        return;
    }
    apply_selection(m_last_used_uuid);
}

void PhysicalPrinterInteractor::remove_uuid(const std::string& uuid)
{
    ASSERT(!is_reserved_uuid(uuid));
    size_t index = index_of(uuid);
    ASSERT(!is_connect_printer_index(index));
    bool was_selected = (uuid == m_selected_uuid);

    m_storage.remove_one(uuid);
    m_observable_list.remove({index, index});
    std::erase_if(m_container_to_printer_uuid_map, [&uuid](const auto& entry) { return entry.second == uuid; });

    // Always call some select method after removal to invoke listeners
    if (was_selected) {
        select_default();
    } else {
        apply_selection(m_selected_uuid);
    }

    if (uuid == m_last_used_uuid) {
        set_last_used({});
    }
}

const PhysicalPrinterConfig& PhysicalPrinterInteractor::edited_printer() const
{
    const auto& selected = m_observable_list.at(m_selected_index);
    if (std::holds_alternative<PrinterUpload>(selected.payload)) {
        return m_storage.all_printers().at(selected.uuid);
    }
    return m_storage.dummy();
}

void PhysicalPrinterInteractor::set_edited_printer(const PhysicalPrinterConfig& edited)
{
    const auto& selected = m_observable_list.at(m_selected_index);

    if (!std::holds_alternative<PrinterUpload>(selected.payload)) {
        // Composing a new printer: keep the changes in the scratch copy. uuid
        // and hw_config are assigned when it is committed (save_new_printer).
        m_storage.dummy() = edited;
        return;
    }

    // Existing printer: uuid and hardware config are immutable.
    const std::string uuid = selected.uuid;
    PhysicalPrinterConfig updated = edited;
    updated.uuid      = uuid;
    updated.hw_config = m_storage.all_printers().at(uuid).hw_config;

    m_storage.all_printers().at(uuid) = updated;
    m_storage.save_one(uuid);
    m_observable_list.set(updated, m_selected_index);

    invoke_listeners<IPhysicalPrinterChangedListener>([](auto* l) { l->on_printer_data_changed(); });
}

void PhysicalPrinterInteractor::update_selected_hw_config()
{
    const auto& selected = m_observable_list.at(m_selected_index);
    if (!std::holds_alternative<PrinterUpload>(selected.payload)) {
        return; // Only saved host-upload printers carry an editable hardware config.
    }

    const std::string uuid = selected.uuid;
    PhysicalPrinterConfig& stored = m_storage.all_printers().at(uuid);
    stored.hw_config = m_preset_interactor.current_printer_config();

    m_storage.save_one(uuid);
    m_observable_list.set(stored, m_selected_index);

    invoke_listeners<IPhysicalPrinterChangedListener>([](auto* l) { l->on_printer_data_changed(); });
}

bool PhysicalPrinterInteractor::selected_hw_matches_current() const
{
    const auto& selected = m_observable_list.at(m_selected_index);
    if (!std::holds_alternative<PrinterUpload>(selected.payload)) {
        return false;
    }
    return selected.hw_config.has_same_values(m_preset_interactor.current_printer_config());
}

void PhysicalPrinterInteractor::save_new_printer()
{
    ASSERT(is_filesystem_export_selected());
    const Domain::Preset::HwPrinterConfig& current_printer_config = m_preset_interactor.current_printer_config();

    std::string uuid = m_storage.create_from_dummy(current_printer_config);

    m_observable_list.append(m_storage.all_printers().at(uuid));
    select_uuid(uuid);
    set_last_used(uuid);
}

void PhysicalPrinterInteractor::on_dialog_button_add_new()
{
    select_default();
}

bool PhysicalPrinterInteractor::is_filesystem_export_selected() const
{
    return std::holds_alternative<FileSystemExport>(m_observable_list.at(m_selected_index).payload);
}

bool PhysicalPrinterInteractor::is_printer_upload_selected() const
{
    return std::holds_alternative<PrinterUpload>(m_observable_list.at(m_selected_index).payload);
}

bool PhysicalPrinterInteractor::is_connect_upload_selected() const
{
    return std::holds_alternative<ConnectUpload>(m_observable_list.at(m_selected_index).payload);
}

ObservableList<PhysicalPrinterConfig>&  PhysicalPrinterInteractor::observable_list()
{
    return m_observable_list;
}

const ObservableList<PhysicalPrinterConfig>&  PhysicalPrinterInteractor::observable_list() const
{
    return m_observable_list;
}

ConnectPrinter::ConnectPrinterService& PhysicalPrinterInteractor::connect_printer_service()
{
    return m_connect_printer_service;
}

bool PhysicalPrinterInteractor::is_connect_printer_index(size_t index) const
{
    return index >= CONNECT_PRINTERS_OFFSET && index < CONNECT_PRINTERS_OFFSET + m_connect_printer_count;
}

void PhysicalPrinterInteractor::sync_selection()
{
    if (std::optional<size_t> index = find_index(m_selected_uuid)) {
        m_selected_index = *index;
        return;
    }
    select_default();
}

void PhysicalPrinterInteractor::on_inserted(const ConnectPrinter::ConnectPrinter& printer, size_t index)
{
    m_observable_list.insert(connect_printer_config(printer), CONNECT_PRINTERS_OFFSET + index);
    ++m_connect_printer_count;
    sync_selection();

    if (!m_explicit_selection && printer.uuid == m_last_used_uuid && m_selected_uuid != printer.uuid) {
        apply_selection(printer.uuid);
    }
}

void PhysicalPrinterInteractor::on_removed(const IndexRange& index_range)
{
    for (size_t i = index_range.from; i <= index_range.to; ++i) {
        const std::string& uuid = m_observable_list.at(CONNECT_PRINTERS_OFFSET + i).uuid;
        std::erase_if(m_container_to_printer_uuid_map, [&uuid](const auto& entry) { return entry.second == uuid; });
    }
    m_observable_list.remove({CONNECT_PRINTERS_OFFSET + index_range.from, CONNECT_PRINTERS_OFFSET + index_range.to});
    m_connect_printer_count -= index_range.to - index_range.from + 1;
    sync_selection();
}

void PhysicalPrinterInteractor::on_updated(const IndexRange& index_range)
{
    const auto& connect_printers = m_connect_printer_service.observable_list();
    for (size_t i = index_range.from; i <= index_range.to; ++i) {
        PhysicalPrinterConfig config = connect_printer_config(connect_printers.at(i));
        const PhysicalPrinterConfig& current = m_observable_list.at(CONNECT_PRINTERS_OFFSET + i);
        if (config.name != current.name
            || std::get<ConnectUpload>(config.payload).state != std::get<ConnectUpload>(current.payload).state)
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
    const auto& connect_printers = m_connect_printer_service.observable_list();
    for (size_t i = 0; i < connect_printers.size(); ++i) {
        on_inserted(connect_printers.at(i), i);
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

const PhysicalPrinterConfig& PhysicalPrinterInteractor::selected_physical_printer_data()
{
    return m_observable_list.at(m_selected_index);
}

bool PhysicalPrinterInteractor::is_printer_compatible(const std::string& uuid, const Domain::Preset::HwPrinterConfig& config) const
{
    const auto& printer = m_observable_list.at(index_of(uuid));

    if (std::holds_alternative<FileSystemExport>(printer.payload)
        || std::holds_alternative<ConnectUpload>(printer.payload)) {
        return true;
    }

    if (std::holds_alternative<PrinterUpload>(printer.payload)) {
        return is_physical_printer_compatible(printer, config);
    }

    return false;
}

void PhysicalPrinterInteractor::on_selected_config_container_changed(Domain::SelectionId project_id, Domain::SelectionId container_id)
{
    m_current_container = ContainerKey{project_id, container_id};

    bool hw_compatible = is_printer_compatible(m_selected_uuid, m_preset_interactor.current_printer_config());
    if (auto it = m_container_to_printer_uuid_map.find(m_current_container); it != m_container_to_printer_uuid_map.end()) {
        if (can_be_selected(it->second)) {
            apply_selection(it->second);
            return;
        }
    } else if (can_be_selected(m_selected_uuid) && hw_compatible) {
        m_container_to_printer_uuid_map.emplace(m_current_container, m_selected_uuid);
        return;
    }

    select_default();
}

bool PhysicalPrinterInteractor::is_connection_testable() const
{
    const PhysicalPrinterConfig& printer = edited_printer();
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

void PhysicalPrinterInteractor::test_edited_printer_connection(
    std::function<void(PrintHostTestResult)> callback
)
{
    if (m_connection_test_in_flight || !is_connection_testable()) {
        return;
    }

    m_connection_test_callback  = std::move(callback);
    m_connection_test_in_flight = true;

    const size_t generation = ++m_connection_test_generation;
    m_connection_test_job_name = CONNECTION_TEST_JOB_NAME + std::to_string(generation);

    PhysicalPrinterConfig config = edited_printer();

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
        .on_result(
            [this, generation](PrintHostTestResult result)
            { finish_connection_test(generation, std::move(result)); }
        )
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
        Platform::PlatformServices::instance().job_manager().request_job_stop(m_connection_test_job_name);
    }
    m_connection_test_job_name.clear();
}

void PhysicalPrinterInteractor::finish_connection_test(size_t generation, PrintHostTestResult result)
{
    if (generation != m_connection_test_generation) {
        return;
    }

    m_connection_test_in_flight = false;
    m_connection_test_job_name.clear();

    std::function<void(PrintHostTestResult)> callback = std::move(m_connection_test_callback);
    m_connection_test_callback = nullptr;

    if (callback) {
        callback(std::move(result));
    }
}

} // namespace Slic3r::Biz::PhysicalPrinter
