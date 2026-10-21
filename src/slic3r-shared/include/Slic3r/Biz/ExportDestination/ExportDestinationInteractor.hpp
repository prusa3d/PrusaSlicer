#pragma once

#include "Slic3r/Biz/ExportDestination/ExportDestination.hpp"
#include "Slic3r/Biz/ExportDestination/IExportDestinationChangedListener.hpp"
#include "Slic3r/Biz/ISelectedConfigContainerChangedListener.hpp"
#include "Slic3r/Biz/PhysicalPrinter/IPhysicalPrinterChangedListener.hpp"
#include "Slic3r/Biz/Platform/WithListeners.hpp"
#include "Slic3r/Biz/RemovableDrive/IRemovableDriveStatusListener.hpp"
#include "Slic3r/Biz/UserAccount/IUserAccountListener.hpp"
#include "Slic3r/Domain/SelectionId.hpp"

#include <map>
#include <optional>
#include <utility>

namespace Slic3r::Biz::PhysicalPrinter {
class PhysicalPrinterInteractor;
} // namespace Slic3r::Biz::PhysicalPrinter

namespace Slic3r::Biz::UserAccount {
class UserAccountInteractor;
} // namespace Slic3r::Biz::UserAccount

namespace Slic3r::Biz::RemovableDrive {
class RemovableDriveService;
} // namespace Slic3r::Biz::RemovableDrive

namespace Slic3r::Biz::ExportDestination {

class ExportDestinationInteractor :
    public WithListeners<IExportDestinationChangedListener>,
    public ISelectedConfigContainerChangedListener,
    public RemovableDrive::IRemovableDriveStatusListener,
    public PhysicalPrinter::IPhysicalPrinterChangedListener,
    public UserAccount::IUserAccountListener
{
public:
    ExportDestinationInteractor(
        const PhysicalPrinter::PhysicalPrinterInteractor& physical_printer_interactor,
        const UserAccount::UserAccountInteractor& user_account_interactor,
        const RemovableDrive::RemovableDriveService& removable_drive_service
    );

    ExportDestination selected() const
    {
        return m_selected;
    }

    bool can_select(ExportDestination destination) const;

    /// Explicit user selection, remembered for the current config container.
    void select(ExportDestination destination);

    /// Persists the destination restored on the next start.
    void remember_used(ExportDestination destination);

    void on_selected_config_container_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId container_id
    ) override;

    void on_removable_drive_status_changed(
        const boost::filesystem::path& drive_path,
        RemovableDrive::RemovableDriveStatus status
    ) override;

    void on_selected_physical_printer_changed() override;
    void on_physical_printer_picked() override;

    void on_user_account_id_success(bool is_refresh, const std::string& username) override;
    void on_user_account_logged_out() override;

private:
    void restore_last_used_selection();
    void apply(ExportDestination destination);
    void select_default();
    void ensure_valid();

private:
    const PhysicalPrinter::PhysicalPrinterInteractor& m_physical_printer_interactor;
    const UserAccount::UserAccountInteractor& m_user_account_interactor;
    const RemovableDrive::RemovableDriveService& m_removable_drive_service;

    using ContainerKey = std::pair<Domain::SelectionId, Domain::SelectionId>;
    ContainerKey m_current_container{0, 0};
    std::map<ContainerKey, ExportDestination> m_container_to_destination_map;

    ExportDestination m_selected{ExportDestination::LocalDrive};
    bool m_explicit_selection{false};
    std::optional<ExportDestination> m_last_used;
};

} // namespace Slic3r::Biz::ExportDestination
