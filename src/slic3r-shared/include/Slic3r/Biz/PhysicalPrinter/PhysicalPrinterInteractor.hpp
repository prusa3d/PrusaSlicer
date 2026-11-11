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
#include "Slic3r/Biz/Preset/IPresetChangedListener.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Domain/SelectionId.hpp"

#include <string>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace Slic3r::Domain {
class ConfigContainer;
class Workbench;
struct PhysicalPrinterSelection;
} // namespace Slic3r::Domain

namespace Slic3r::Domain::Preset {
struct HwPrinterConfig;
} // namespace Slic3r::Domain::Preset

namespace Slic3r::Biz {
class ProjectInteractor;
template <typename T>
class ProjectScoped;
} // namespace Slic3r::Biz

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
    public Preset::IPresetChangedListener,
    public IListObserver<ConnectPrinter::ConnectPrinter>
{
public:
    PhysicalPrinterInteractor(
        Domain::Workbench& workbench,
        ProjectInteractor& project_interactor,
        Platform::IMainThreadDispatcher& dispatcher,
        Preset::PresetInteractor& preset_interactor,
        UserAccount::UserAccountInteractor& user_account_interactor
    );
    ~PhysicalPrinterInteractor();

    ObservableList<PhysicalPrinterConfig>& observable_list();

    const ObservableList<PhysicalPrinterConfig>& observable_list() const;

    ConnectPrinter::ConnectPrinterService& connect_printer_service();

    Preset::PrinterPresetSelector& printer_preset_selector();

    /// Explicit user selection for this session, fails when no printer config matches.
    bool select_uuid(const std::string& uuid);

    /**
     * @brief Automatic selection which keeps the presets untouched.
     *
     * Selects the printer remembered by the current config container, otherwise the first
     * compatible one, otherwise none.
     */
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

    /**
     * @brief Stores the printer without selecting it.
     * @return Uuid of the stored printer, generated for a new one.
     */
    std::string save_physical_printer(const PhysicalPrinterConfig& printer);

    /// @return Selected printer, nullptr when there is none (e.g. no printer exists).
    const PhysicalPrinterConfig* selected_physical_printer_data() const;

    bool is_connection_testable(const PhysicalPrinterConfig& printer) const;

    bool is_connection_test_in_flight() const;

    void test_printer_connection(
        const PhysicalPrinterConfig& printer,
        std::function<void(PrintHostTestResult)> callback
    );

    /// Discards the running test's result so a new one may start at once. Safe to call repeatedly.
    void cancel_edited_printer_connection_test();

    /**
     * @brief Resolves the physical printers of config containers of a project loaded from 3mf.
     *
     * Keeps the stored printer when it matches the container's hw config, otherwise picks
     * a matching printer (only when 3mf names none) or creates a runtime printer for the project.
     * @param project_id Project which config containers were loaded from 3mf.
     */
    void load_physical_printers_from_3mf(Domain::SelectionId project_id);

    /// Restores the physical printer remembered by the newly active config container.
    void on_selected_config_container_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId container_id
    ) override;
    void on_preset_bundles_initialized() override;
    void on_preset_bundles_reloaded() override;

    void on_inserted(const ConnectPrinter::ConnectPrinter& printer, size_t index) override;
    void on_removed(const IndexRange& index_range) override;
    void on_updated(const IndexRange& index_range) override;
    void on_reset() override;

private:
    void migrate_hw_configs();
    /// Rebuilds the list from the synthetic entries plus the stored printers.
    void read_storage();

    void restore_last_used_selection();
    /// True if the printer may be selected (a Connect printer needs a login).
    bool can_be_selected_at(size_t index) const;
    bool is_printer_compatible(size_t index, const Domain::Preset::HwPrinterConfig& config) const;
    /// Selects the printer and remembers it in the current config container.
    bool apply_selection(const std::string& uuid, bool apply_hw_config);
    void select_connect_materials(const std::string& printer_uuid);
    void clear_selection();
    void set_last_used(const std::string& uuid);

    size_t index_of(const std::string& uuid) const;
    std::optional<size_t> find_index(const std::string& uuid) const;

    void finish_connection_test(size_t generation, PrintHostTestResult result);

    bool is_connect_printer_index(size_t index) const;
    void sync_selection();

    PhysicalPrinterConfig printer_config_at(size_t index) const;
    Domain::ConfigContainer* current_config_container();

    /// Replaces runtime printers in the list with the ones of the given project.
    void sync_runtime_printers(Domain::SelectionId project_id);

    /// First selectable printer of the project compatible with the hw config, current one first.
    std::optional<size_t> find_compatible_index(
        Domain::SelectionId project_id,
        const Domain::Preset::HwPrinterConfig& hw_config
    ) const;

    const PhysicalPrinterConfig& get_or_create_runtime_printer(
        Domain::SelectionId project_id,
        const Domain::PhysicalPrinterSelection& selection,
        const Domain::Preset::HwPrinterConfig& hw_config
    );

    /// Connect printer config with visual data taken from the matching preset bundle printer.
    PhysicalPrinterConfig create_connect_printer_config(
        const ConnectPrinter::ConnectPrinter& printer
    ) const;

private:
    struct ProjectContext
    {
        std::vector<PhysicalPrinterConfig> runtime_printers;
    };
    using ProjectContexts = Biz::ProjectScoped<ProjectContext>;

    Domain::Workbench& m_workbench;
    ProjectInteractor& m_project_interactor;
    std::unique_ptr<ProjectContexts> m_project_contexts;
    Platform::IMainThreadDispatcher& m_dispatcher;
    Preset::PresetInteractor& m_preset_interactor;
    UserAccount::UserAccountInteractor& m_user_account_interactor;
    PhysicalPrinterStorage m_storage;
    ConnectPrinter::ConnectPrinterService m_connect_printer_service;
    ListenerScope<IPresetChangedListener, Preset::PresetInteractor, PhysicalPrinterInteractor>
        m_listener_scope_preset_interactor;
    Biz::ListenerScope<
        IListObserver<ConnectPrinter::ConnectPrinter>,
        ConnectPrinter::ConnectPrinterService,
        PhysicalPrinterInteractor>
        m_listener_scope_connect_printer;
    Preset::PrinterPresetSelector m_printer_preset_selector;
    ObservableList<PhysicalPrinterConfig> m_observable_list;
    size_t m_connect_printer_count{0};
    size_t m_runtime_printer_count{0};
    /// Project which runtime printers are in the list, none when the list must be refreshed.
    std::optional<Domain::SelectionId> m_runtime_printers_project_id;

    Domain::SelectionId m_current_project_id{0};
    Domain::SelectionId m_current_container_id{0};

    std::string m_selected_uuid;
    bool m_explicit_selection{false};
    std::string m_last_used_uuid;

    std::function<void(PrintHostTestResult)> m_connection_test_callback;
    std::string m_connection_test_job_name;
    size_t m_connection_test_generation{0};
    bool m_connection_test_in_flight{false};
};
} // namespace Slic3r::Biz::PhysicalPrinter
