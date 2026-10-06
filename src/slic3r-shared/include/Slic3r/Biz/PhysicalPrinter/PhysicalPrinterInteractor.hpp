#pragma once

#include "Slic3r/Biz/Platform/IMainThreadDispatcher.hpp"
#include "Slic3r/Biz/Platform/WithListeners.hpp"
#include "Slic3r/Biz/Platform/ListenerScope.hpp"
#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterConfig.hpp"
#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterStorage.hpp"
#include "Slic3r/Biz/PhysicalPrinter/IPhysicalPrinterChangedListener.hpp"
#include "Slic3r/Biz/ObservableList.hpp"
#include "Slic3r/Biz/IListObserver.hpp"
#include "Slic3r/Biz/ConnectPrinter/ConnectPrinterService.hpp"
#include "Slic3r/Biz/Preset/PrinterPresetSelector.hpp"
#include "Slic3r/Biz/ISelectedConfigContainerChangedListener.hpp"
#include "Slic3r/Domain/SelectionId.hpp"

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

namespace Slic3r::Biz::PhysicalPrinter {

struct PrintHostTestResult
{
    bool ok{false};
    std::string host_name;
    std::string error_message;
};

/// Owns the selectable printers, the current selection, and the last used printer.
class PhysicalPrinterInteractor :
    public WithListeners<IPhysicalPrinterChangedListener>,
    public ISelectedConfigContainerChangedListener,
    public IListObserver<ConnectPrinter::ConnectPrinter>
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

    ConnectPrinter::ConnectPrinterService& connect_printer_service();

    Preset::PrinterPresetSelector& printer_preset_selector();

    /// True if the printer may be selected (a Connect printer needs a login).
    bool can_be_selected(const std::string& uuid) const;

    /// Explicit user selection for this session.
    void select_uuid(const std::string& uuid);

    /// Explicit user selection that also selects the presets matching the printer.
    void select_uuid_with_presets(const std::string& uuid);

    /// Automatic fallback to the first entry.
    void select_default();

    /// Persists the printer restored on the next start.
    void remember_used_printer(const std::string& uuid);

    void remove_uuid(const std::string& uuid);

    std::string selected_uuid() const
    {
        return m_selected_uuid;
    }

    bool is_printer_upload_selected() const;
    bool is_connect_printer_selected() const;

    /**
     * @note Also selects this printer
     */
    void create_or_save_physical_printer(const PhysicalPrinterConfig& printer);

    const PhysicalPrinterConfig& selected_physical_printer_data() const;

    bool is_connection_testable(const PhysicalPrinterConfig& printer) const;

    bool is_connection_test_in_flight() const;

    void test_printer_connection(
        const PhysicalPrinterConfig& printer,
        std::function<void(PrintHostTestResult)> callback
    );

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

    void on_inserted(const ConnectPrinter::ConnectPrinter& printer, size_t index) override;
    void on_removed(const IndexRange& index_range) override;
    void on_updated(const IndexRange& index_range) override;
    void on_reset() override;

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

    bool is_connect_printer_index(size_t index) const;
    void sync_selection();

private:
    Platform::IMainThreadDispatcher& m_dispatcher;
    Preset::PresetInteractor& m_preset_interactor;
    UserAccount::UserAccountInteractor& m_user_account_interactor;
    PhysicalPrinterStorage m_storage;
    ConnectPrinter::ConnectPrinterService m_connect_printer_service;
    Biz::ListenerScope<
        IListObserver<ConnectPrinter::ConnectPrinter>,
        ConnectPrinter::ConnectPrinterService,
        PhysicalPrinterInteractor>
        m_listener_scope_connect_printer;
    Preset::PrinterPresetSelector m_printer_preset_selector;
    ObservableList<PhysicalPrinterConfig> m_observable_list;
    size_t m_connect_printer_count{0};

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
