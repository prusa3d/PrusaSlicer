#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterInteractor.hpp"

#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/UserAccount/UserAccountInteractor.hpp"
#include "Slic3r/Domain/Preset/HwConfig.hpp"

#include "Slic3r/Log.hpp"

#include <algorithm>
#include <variant>

namespace Slic3r::Biz::PhysicalPrinter {

PhysicalPrinterInteractor::PhysicalPrinterInteractor(
    Platform::IMainThreadDispatcher& dispatcher,
    Preset::PresetInteractor& preset_interactor,
    UserAccount::UserAccountInteractor& user_account_interactor
) :
    m_dispatcher(dispatcher),
    m_preset_interactor(preset_interactor),
    m_user_account_interactor(user_account_interactor)
{
    read_storage();
    m_selected_uuid = m_observable_list.at(0).uuid;
    m_selected_index = 0;
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
    printers.reserve(m_storage.all_printers().size() + 3);

    printers.push_back(PhysicalPrinter::filesystem_export_local());
    printers.push_back(PhysicalPrinter::filesystem_export_removable());
    printers.push_back(PhysicalPrinter::connect_upload_generic());

    for (const auto& [uuid, printer] : m_storage.all_printers()) {
        printers.push_back(printer);
    }

    m_observable_list.reset(std::move(printers));
}

bool PhysicalPrinterInteractor::can_be_selected(const std::string& uuid) const
{
    auto index = index_of(uuid);
    const auto& printer = m_observable_list.at(index);
    if (std::holds_alternative<ConnectUpload>(printer.payload)) {
        return m_user_account_interactor.is_logged_in();
    }
    return true;
}

void PhysicalPrinterInteractor::select_uuid(const std::string& uuid)
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

void PhysicalPrinterInteractor::select_default()
{
    if (m_observable_list.size() == 0) {
        return;
    }
    select_uuid(m_observable_list.at(0).uuid);
}

void PhysicalPrinterInteractor::select_connect_upload(bool prefer_physical_printer)
{
    if (m_selected_index != 0 && prefer_physical_printer) {
        return;
    }

    // Note: This implementation works only in case there is single connect upload item
    for (size_t i = 0; i < m_observable_list.size(); ++i) {
        if (std::holds_alternative<ConnectUpload>(m_observable_list.at(i).payload)) {
            select_uuid(m_observable_list.at(i).uuid);
            return;
        }
    }
    DEBUG_ASSERT(false, "ConnectUpload missing in list of physical printers");
}

void PhysicalPrinterInteractor::remove_uuid(const std::string& uuid)
{
    size_t index = index_of(uuid);
    ASSERT(index != 0);
    bool was_selected = (uuid == m_selected_uuid);

    m_storage.remove_one(uuid);
    m_observable_list.remove({index, index});

    // Always call some select method after removal to invoke listeners
    if (was_selected) {
        select_default();
    } else {
        select_uuid(m_selected_uuid);
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
    ASSERT(m_selected_index == 0);
    const Domain::Preset::HwPrinterConfig& current_printer_config = m_preset_interactor.current_printer_config();

    std::string uuid = m_storage.create_from_dummy(current_printer_config);

    m_observable_list.append(m_storage.all_printers().at(uuid));
    select_uuid(uuid); // Reuses the internal logic directly via UUID
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

size_t PhysicalPrinterInteractor::index_of(const std::string& uuid) const
{
    for (size_t i = 0; i < m_observable_list.size(); ++i) {
        if (m_observable_list.at(i).uuid == uuid) {
            return i;
        }
    }
    ASSERT(false);
    return 0;
}

const PhysicalPrinterConfig& PhysicalPrinterInteractor::selected_physical_printer_data()
{
    return m_observable_list.at(m_selected_index);
}

bool PhysicalPrinterInteractor::is_printer_compatible(const std::string& uuid, const Domain::Preset::HwPrinterConfig& config) const
{
    if (uuid == m_observable_list.at(0).uuid) {  // Default is always compatible
        return true;
    }

    const auto& printer = m_observable_list.at(index_of(uuid));

    if (std::holds_alternative<ConnectUpload>(printer.payload)) {
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
            select_uuid(it->second);
            return;
        }
    } else if (can_be_selected(m_selected_uuid) && hw_compatible) {
        m_container_to_printer_uuid_map.emplace(m_current_container, m_selected_uuid);
        return;
    }

    select_default();
}

} // namespace Slic3r::Biz::PhysicalPrinter
