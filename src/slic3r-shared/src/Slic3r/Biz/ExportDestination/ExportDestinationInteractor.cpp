#include "Slic3r/Biz/ExportDestination/ExportDestinationInteractor.hpp"

#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterInteractor.hpp"
#include "Slic3r/Biz/Platform/IAppConfigProvider.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Biz/RemovableDrive/RemovableDriveService.hpp"
#include "Slic3r/Biz/UserAccount/UserAccountInteractor.hpp"

namespace Slic3r::Biz::ExportDestination {

ExportDestinationInteractor::ExportDestinationInteractor(
    const PhysicalPrinter::PhysicalPrinterInteractor& physical_printer_interactor,
    const UserAccount::UserAccountInteractor& user_account_interactor,
    const RemovableDrive::RemovableDriveService& removable_drive_service
) :
    m_physical_printer_interactor(physical_printer_interactor),
    m_user_account_interactor(user_account_interactor),
    m_removable_drive_service(removable_drive_service)
{
    restore_last_used_selection();
}

void ExportDestinationInteractor::restore_last_used_selection()
{
    auto& services = Platform::PlatformServices::instance();
    if (!services.has_app_config_provider()) {
        return;
    }
    m_last_used = export_destination_from_string(services.app_config_provider().last_used_export_destination());
    if (!m_last_used || !can_select(*m_last_used)) {
        return;
    }
    m_selected           = *m_last_used;
    m_explicit_selection = true;
}

bool ExportDestinationInteractor::can_select(ExportDestination destination) const
{
    switch (destination) {
    case ExportDestination::LocalDrive: return true;
    case ExportDestination::RemovableDrive: return m_removable_drive_service.has_removable_drives();
    case ExportDestination::Connect: return m_user_account_interactor.is_logged_in();
    case ExportDestination::PrintHost: return m_physical_printer_interactor.is_printer_upload_selected();
    }
    return false;
}

void ExportDestinationInteractor::apply(ExportDestination destination)
{
    m_selected = destination;
    invoke_listeners<IExportDestinationChangedListener>(
        [](auto* listener) {
            listener->on_export_destination_changed();
        }
    );
}

void ExportDestinationInteractor::select(ExportDestination destination)
{
    m_container_to_destination_map[m_current_container] = destination;
    m_explicit_selection = true;
    apply(destination);
}

void ExportDestinationInteractor::select_default()
{
    m_explicit_selection = false;
    apply(ExportDestination::LocalDrive);
}

void ExportDestinationInteractor::ensure_valid()
{
    if (!can_select(m_selected)) {
        select_default();
    }
}

void ExportDestinationInteractor::remember_used(ExportDestination destination)
{
    if (m_last_used == destination) {
        return;
    }
    m_last_used = destination;
    auto& services = Platform::PlatformServices::instance();
    if (services.has_app_config_provider()) {
        services.app_config_provider().set_last_used_export_destination(
            std::string(export_destination_to_string(destination))
        );
    }
}

void ExportDestinationInteractor::on_selected_config_container_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId container_id
)
{
    m_current_container = ContainerKey{project_id, container_id};

    if (auto it = m_container_to_destination_map.find(m_current_container); it != m_container_to_destination_map.end()) {
        if (can_select(it->second)) {
            apply(it->second);
            return;
        }
    } else if (can_select(m_selected)) {
        m_container_to_destination_map.emplace(m_current_container, m_selected);
        return;
    }

    select_default();
}

void ExportDestinationInteractor::on_removable_drive_status_changed(
    const boost::filesystem::path& /* drive_path */,
    RemovableDrive::RemovableDriveStatus status
)
{
    if (status != RemovableDrive::RemovableDriveStatus::Inserted) {
        ensure_valid();
        return;
    }
    if (m_explicit_selection || m_last_used != ExportDestination::RemovableDrive
        || m_selected == ExportDestination::RemovableDrive) {
        return;
    }
    apply(ExportDestination::RemovableDrive);
}

void ExportDestinationInteractor::on_selected_physical_printer_changed()
{
    ensure_valid();
}

void ExportDestinationInteractor::on_physical_printer_picked()
{
    if (m_physical_printer_interactor.is_printer_upload_selected()) {
        select(ExportDestination::PrintHost);
    } else if (m_physical_printer_interactor.is_connect_printer_selected() && can_select(ExportDestination::Connect)) {
        select(ExportDestination::Connect);
    }
}

void ExportDestinationInteractor::on_user_account_id_success(bool is_refresh, const std::string& /* username */)
{
    if (is_refresh || m_explicit_selection || m_selected != ExportDestination::LocalDrive) {
        return;
    }
    apply(ExportDestination::Connect);
}

void ExportDestinationInteractor::on_user_account_logged_out()
{
    ensure_valid();
}

} // namespace Slic3r::Biz::ExportDestination
