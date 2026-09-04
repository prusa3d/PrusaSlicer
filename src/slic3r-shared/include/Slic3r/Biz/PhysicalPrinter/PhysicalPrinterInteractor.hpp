#pragma once

#include "Slic3r/Biz/Platform/IMainThreadDispatcher.hpp"
#include "Slic3r/Biz/Platform/WithListeners.hpp"
#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterConfig.hpp"
#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterStorage.hpp"
#include "Slic3r/Biz/PhysicalPrinter/IPhysicalPrinterChangedListener.hpp"
#include "Slic3r/Biz/ObservableList.hpp"
#include "Slic3r/Biz/ISelectedConfigContainerChangedListener.hpp"
#include "Slic3r/Domain/SelectionId.hpp"

#include <vector>
#include <string>
#include <map>

namespace Slic3r::Domain::Preset {
struct HwPrinterConfig;
} // namespace Slic3r::Domain::Preset

namespace Slic3r::Biz::Preset {
class PresetInteractor;
} // namespace Slic3r::Biz::Preset

namespace Slic3r::Biz::UserAccount {
class UserAccountInteractor;
} // namespace Slic3r::Biz::UserAccount

namespace Slic3r::Biz::PhysicalPrinter {

/// Owns the selectable upload destinations, the current selection, and their persistence.
class PhysicalPrinterInteractor :
    public WithListeners<IPhysicalPrinterChangedListener>,
    public ISelectedConfigContainerChangedListener
{
public:
    PhysicalPrinterInteractor(
        Platform::IMainThreadDispatcher& dispatcher,
        Preset::PresetInteractor& preset_interactor,
        UserAccount::UserAccountInteractor& user_account_interactor
    );
    ~PhysicalPrinterInteractor();

    ObservableList<PhysicalPrinterConfig>& observable_list();

    const ObservableList<PhysicalPrinterConfig>& observable_list() const;

    /// True if the destination may be selected (e.g. Connect requires a logged-in user).
    bool can_be_selected(const std::string& uuid) const;
    void select_uuid(const std::string& uuid);
    void select_default();
    void select_connect_upload(bool prefer_physical_printer);
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

    bool is_printer_compatible(
        const std::string& uuid,
        const Domain::Preset::HwPrinterConfig& config
    ) const;

    /// Restores the destination remembered for the newly active config container.
    void on_selected_config_container_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId container_id
    ) override;

private:
    /// Rebuilds the list from the synthetic entries plus the stored printers.
    void read_storage();

    size_t index_of(const std::string& uuid) const;

private:
    Platform::IMainThreadDispatcher& m_dispatcher;
    Preset::PresetInteractor& m_preset_interactor;
    UserAccount::UserAccountInteractor& m_user_account_interactor;
    PhysicalPrinterStorage m_storage;
    ObservableList<PhysicalPrinterConfig> m_observable_list;

    using ContainerKey = std::pair<Domain::SelectionId, Domain::SelectionId>;
    ContainerKey m_current_container{0, 0};
    std::map<ContainerKey, std::string> m_container_to_printer_uuid_map;

    std::string m_selected_uuid;
    size_t m_selected_index;
};
} // namespace Slic3r::Biz::PhysicalPrinter
