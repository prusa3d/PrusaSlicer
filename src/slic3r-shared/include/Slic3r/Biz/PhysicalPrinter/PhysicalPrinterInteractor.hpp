#pragma once

#include "Slic3r/Biz/Platform/IMainThreadDispatcher.hpp"
#include "Slic3r/Biz/Platform/WithListeners.hpp"
#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterConfig.hpp"
#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterStorage.hpp"
#include "Slic3r/Biz/PhysicalPrinter/IPhysicalPrinterChangedListener.hpp"
#include "Slic3r/Biz/ObservableList.hpp"
#include "Slic3r/Biz/ISelectedConfigContainerChangedListener.hpp"
#include "Slic3r/Biz/RemovableDrive/IRemovableDriveStatusListener.hpp"
#include "Slic3r/Domain/SelectionId.hpp"

#include <vector>
#include <string>
#include <map>
#include <functional>
#include <optional>

namespace Slic3r::Domain::Preset {
struct HwPrinterConfig;
} // namespace Slic3r::Domain::Preset

namespace Slic3r::Biz::Preset {
class PresetInteractor;
} // namespace Slic3r::Biz::Preset

namespace Slic3r::Biz::UserAccount {
class UserAccountInteractor;
} // namespace Slic3r::Biz::UserAccount

namespace Slic3r::Biz::RemovableDrive {
class RemovableDriveService;
} // namespace Slic3r::Biz::RemovableDrive

namespace Slic3r::Biz::PhysicalPrinter {

struct PrintHostTestResult
{
    bool ok{false};
    std::string host_name;
    std::string error_message;
};

/// Owns the selectable upload destinations, the current selection, and the last used destination.
class PhysicalPrinterInteractor :
    public WithListeners<IPhysicalPrinterChangedListener>,
    public ISelectedConfigContainerChangedListener,
    public RemovableDrive::IRemovableDriveStatusListener
{
public:
    PhysicalPrinterInteractor(
        Platform::IMainThreadDispatcher& dispatcher,
        Preset::PresetInteractor& preset_interactor,
        UserAccount::UserAccountInteractor& user_account_interactor,
        RemovableDrive::RemovableDriveService& removable_drive_service
    );
    ~PhysicalPrinterInteractor();

    ObservableList<PhysicalPrinterConfig>& observable_list();

    const ObservableList<PhysicalPrinterConfig>& observable_list() const;

    /// True if the destination may be selected (Connect needs a login, Removable Drive needs a drive).
    bool can_be_selected(const std::string& uuid) const;

    /// Explicit user selection for this session.
    void select_uuid(const std::string& uuid);

    /// Automatic fallback to the first entry.
    void select_default();

    /// Explicit selection of Prusa Connect.
    void select_connect_upload();

    /// Switches to Prusa Connect unless the current selection is the user's explicit choice.
    void select_connect_upload_if_default();

    /// Persists the destination restored on the next start.
    void remember_used_destination(const std::string& uuid);

    void remove_uuid(const std::string& uuid);

    std::string selected_uuid() const
    {
        return m_selected_uuid;
    }

    bool is_filesystem_export_selected() const;
    bool is_printer_upload_selected() const;
    bool is_connect_upload_selected() const;

    const PhysicalPrinterConfig& selected_physical_printer_data();

    /// The printer shown in the editor: the selected printer, or the dummy when adding a new one.
    const PhysicalPrinterConfig& edited_printer() const;

    /// Applies editor changes: persists an existing printer, or updates the new-printer dummy.
    void set_edited_printer(const PhysicalPrinterConfig& edited);

    /// Re-links the selected existing printer's hardware config to the currently selected logical printer.
    void update_selected_hw_config();

    /// True when the selected printer's hardware config already matches the current logical printer.
    bool selected_hw_matches_current() const;

    /// Commits the dummy as a new persisted printer and selects it.
    void save_new_printer();

    /// Prepares the editor to add a new printer.
    void on_dialog_button_add_new();

    bool is_connection_testable() const;

    bool is_connection_test_in_flight() const;

    void test_edited_printer_connection(std::function<void(PrintHostTestResult)> callback);

    /// Discards the running test's result so a new one may start at once. Safe to call repeatedly.
    void cancel_edited_printer_connection_test();

    bool is_printer_compatible(
        const std::string& uuid,
        const Domain::Preset::HwPrinterConfig& config
    ) const;

    /// Restores the destination remembered for the newly active config container.
    void on_selected_config_container_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId container_id
    ) override;

    /// Restores a last used Removable Drive once a drive appears, unless the user already chose otherwise.
    void on_removable_drive_status_changed(
        const boost::filesystem::path& drive_path,
        RemovableDrive::RemovableDriveStatus status
    ) override;

private:
    /// Rebuilds the list from the synthetic entries plus the stored printers.
    void read_storage();

    void restore_last_used_selection();
    bool can_be_selected_at(size_t index) const;
    void apply_selection(const std::string& uuid);
    void set_last_used(const std::string& uuid);

    size_t index_of(const std::string& uuid) const;
    std::optional<size_t> find_index(const std::string& uuid) const;

    void finish_connection_test(size_t generation, PrintHostTestResult result);

private:
    Platform::IMainThreadDispatcher& m_dispatcher;
    Preset::PresetInteractor& m_preset_interactor;
    UserAccount::UserAccountInteractor& m_user_account_interactor;
    RemovableDrive::RemovableDriveService& m_removable_drive_service;
    PhysicalPrinterStorage m_storage;
    ObservableList<PhysicalPrinterConfig> m_observable_list;

    using ContainerKey = std::pair<Domain::SelectionId, Domain::SelectionId>;
    ContainerKey m_current_container{0, 0};
    std::map<ContainerKey, std::string> m_container_to_printer_uuid_map;

    std::string m_selected_uuid;
    size_t m_selected_index;
    bool m_explicit_selection{false};
    std::string m_last_used_uuid;

    std::function<void(PrintHostTestResult)> m_connection_test_callback;
    std::string m_connection_test_job_name;
    size_t m_connection_test_generation{0};
    bool m_connection_test_in_flight{false};
};
} // namespace Slic3r::Biz::PhysicalPrinter
