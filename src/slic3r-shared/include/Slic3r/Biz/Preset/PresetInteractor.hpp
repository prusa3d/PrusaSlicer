#pragma once

#include <unordered_map>

#include <tl/expected.hpp>

#include "Slic3r/Domain/Workbench.hpp"

#include "Slic3r/Biz/ISlicingInputChangedListener.hpp"
#include "Slic3r/InvokeLaterBag.hpp"
#include "Slic3r/Biz/ISelectedConfigContainerChangedListener.hpp"
#include "Slic3r/Biz/Platform/WithListeners.hpp"
#include "Slic3r/Biz/Preset/PresetInteractorProjectContext.hpp"
#include "Slic3r/Biz/Preset/PresetDiffOperation.hpp"
#include "Slic3r/Biz/Preset/IPresetDialogManager.hpp"
#include "Slic3r/Biz/Preset/IO/BundlePaths.hpp"
#include "Slic3r/Biz/ConfigBoxInteractor.hpp"
#include "Slic3r/Biz/PrintToolConfigBoxInteractor.hpp"
#include "Slic3r/Biz/OverridableCBIObservableList.hpp"
#include "Slic3r/Biz/ObjectSettingsInteractor.hpp"
#include "Slic3r/Biz/Preset/IPresetVisualGetter.hpp"
#include "Slic3r/Biz/ObservableListWithSelection.hpp"
#include "Slic3r/Biz/Preset/ProjectPresetView.hpp"
#include "Slic3r/Biz/IConfigBoxSetter.hpp"

namespace Slic3r::Biz {
class BackupStore;
} // namespace Slic3r::Biz

namespace Slic3r::Biz::Preset {

// Minimal interface used by NameValidator to query existing preset names.
class IPresetNameProvider
{
public:
    virtual ~IPresetNameProvider() = default;

    virtual Domain::Preset::PresetNames get_all_vendor_preset_names(
        Domain::Preset::PresetKind kind,
        const std::optional<std::string>& vendor_id = std::nullopt
    ) const = 0;

    virtual boost::filesystem::path
    selected_user_preset_path(Domain::Preset::PresetKind kind, const std::string& preset_name) const = 0;
};

class IPresetChangedListener;

class PresetInteractor;

struct PresetItem
{
    std::string id;
    std::string name;
    std::string hw_printer_config_id;
    std::string hw_printer_config_name;
    Domain::Preset::PresetOrigin origin;
    bool hw_printer_config_runtime_only;

    bool preset_runtime_only() const;
    std::string ui_preset_name() const;
    std::string ui_hw_config_name() const;
};

using PresetItemObservableList         = ObservableListWithSelection<PresetItem>;
using PresetItemCompoundObservableList = MutableBatchObservableList<PresetItemObservableList>;

using ToolConfigItemObservableList = ObservableListWithSelection<Domain::Preset::HwToolConfigDef>;
using ToolConfigItemCompoundObservableList =
    MutableBatchObservableList<ToolConfigItemObservableList>;

using SheetConfigItemObservableList = ObservableListWithSelection<Domain::Preset::HwSheetConfigDef>;

using PresetsSwitchStates = Biz::Preset::IPresetDialogManager::PresetsSwitchStates;

using KeySet = std::set<std::string>;

struct SelectedPresetIds
{
    std::string hw_config_id;
    std::string repo_id;
    std::string vendor_id;
    std::string printer_id;
    std::string print_id;
    std::vector<std::string> tool_ids;
    std::vector<std::string> material_ids;
};


/**
 * Manipulates presets associated with config containers.
 */
class PresetInteractor final :
    public ISelectedProjectChangedListener,
    public ISelectedConfigContainerChangedListener,
    public WithListeners<
        IPresetChangedListener,
        ISlicingInputChangedListener>,
    public IPresetNameProvider,
    public IConfigBoxSetter,
    public IPresetVisualGetter
{
public:
    PresetInteractor(
        Domain::Workbench& workbench,
        Scene::SceneInteractor& scene_interactor,
        BackupStore& backup_store
    );

    PresetInteractor(PresetInteractor&&) = default;

    void load_preset_bundle(const IO::BundlePaths& paths);
    void save_user_preset(
        Domain::Preset::PresetKind kind,
        size_t slot_index
    );
    void save_user_tool_print_presets();

    const PresetInteractorConfigContainerContext& config_container_context(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id
    ) const;

    /**
     * @brief Load selected_preset from 3MF. This function makes sure the hw_configs and presets
     * stored there are loaded into (runtime) bundle with deduplication and all relevant IDs
     * are unique.
     * @param project_id An ID of the project
     * @param selected_preset Config container preset to load.
     * @note The ID of selected_preset's hw_config may change if it collides with already existing
     * having different values (config changed without changing its ID).
     * @return Success indication or error message bundled in `tl::expected<void, std::string>`
     */
    tl::expected<void, std::string> load_selected_preset_from_3mf(
        Domain::SelectionId project_id,
        Domain::Preset::SelectedPreset& selected_preset
    );

    const PresetInteractorConfigContainerContext& selected_config_container_context() const;

    PresetInteractorConfigContainerContext& initialize_config_container_context(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id
    );
    void initialize_config_container_with_default(Domain::ConfigContainer& cc);
    void initialize_config_container_with_selected(Domain::ConfigContainer& cc);

    const Domain::Preset::HwPrinterConfig& current_printer_config() const;
    const Domain::Preset::EvaluatedPrinterPreset::Preset& current_printer_preset() const;
    const Domain::Preset::SelectedPreset& selected_printer_preset() const;

    PresetItemObservableList& printer_presets()
    {
        return m_printer_presets;
    }

    const PresetItemObservableList& printer_presets() const
    {
        return m_printer_presets;
    }

    PresetItemObservableList& print_presets()
    {
        return m_print_presets;
    }

    const PresetItemObservableList& print_presets() const
    {
        return m_print_presets;
    }

    PresetItemCompoundObservableList& tool_presets()
    {
        return m_tool_print_presets;
    }

    const PresetItemCompoundObservableList& tool_presets() const
    {
        return m_tool_print_presets;
    }

    PresetItemCompoundObservableList& material_presets()
    {
        return m_material_presets;
    }

    const PresetItemCompoundObservableList& material_presets() const
    {
        return m_material_presets;
    }

    ToolConfigItemCompoundObservableList& tool_items()
    {
        return m_tool_items;
    }

    const ToolConfigItemCompoundObservableList& tool_items() const
    {
        return m_tool_items;
    }

    SheetConfigItemObservableList& sheet_items()
    {
        return m_sheet_items;
    }

    const SheetConfigItemObservableList& sheet_items() const
    {
        return m_sheet_items;
    }

    ObjectSettingsInteractor& object_settings_interactor() {
        return m_object_settings_interactor;
    }

    const ObjectSettingsInteractor& object_settings_interactor() const {
        return m_object_settings_interactor;
    }

    /**
     * @param user - set this to true if action came from user (e.g. clicking a button)
     * so this would invalidate Project in a BackupStore
     */
    void select_printer_preset(
        const std::string& printer_hw_config_id,
        const std::string& printer_preset_id,
        bool user = false
    );
    /**
     * @param user - set this to true if action came from user (e.g. clicking a button)
     * so this would invalidate Project in a BackupStore
     */
    void select_print_preset(const std::string& id, bool user = false);
    /**
     * @param user - set this to true if action came from user (e.g. clicking a button)
     * so this would invalidate Project in a BackupStore
     */
    void select_tool_print_preset(size_t tool_index, const std::string& id, bool user = false);
    /**
     * @param user - set this to true if action came from user (e.g. clicking a button)
     * so this would invalidate Project in a BackupStore
     */
    void select_material_preset(size_t material_index, const std::string& id, bool user = false);
    /**
     * @param user - set this to true if action came from user (e.g. clicking a button)
     * so this would invalidate Project in a BackupStore
     */
    bool select_printer_tool_item(size_t tool_index, const std::string& id, bool user = false);
    /**
     * @param user - set this to true if action came from user (e.g. clicking a button)
     * so this would invalidate Project in a BackupStore
     */
    bool select_printer_sheet(const std::string& id, bool user = false);

    using ConfigItemModifyFn = std::function<void(Domain::ConfigItem&)>;

    void set_preset_value(
        Domain::ConfigLocation location,
        int element_idx,
        const std::string& name,
        ConfigItemModifyFn modify_fn
    );

    Domain::Preset::PresetNames get_all_vendor_preset_names(
        Domain::Preset::PresetKind kind,
        const std::optional<std::string>& vendor_id = std::nullopt
    ) const override;

    [[nodiscard]] const Domain::Preset::FeatureDefs* get_vendor_tool_feature_defs(
        const std::optional<std::string>& vendor_id = std::nullopt
    ) const;

    virtual boost::filesystem::path selected_user_preset_path(
        Domain::Preset::PresetKind kind,
        const std::string& preset_name
    ) const override;

    void on_selected_project_changed(size_t index) override;
    void on_selected_config_container_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId container_id
    ) override;

    ConfigBoxInteractor& printer_cbi();
    PrintToolConfigBoxInteractor& print_tool_cbi();
    OverridableCBIObservableList& material_cbi_list();
    const OverridableCBIObservableList& material_cbi_list() const;

    const Domain::ConfigValue* get_override_original_value(const Domain::ConfigItem& item, size_t index = 0) const override;

    void set_from_original_value(const Domain::ConfigItem& item, size_t index = 0) override;

    void set_item_value(
        const Domain::ConfigItem& item,
        const Domain::ConfigValue& value,
        const std::vector<size_t>& indexes = {0}
    ) override;

    void set_item_override(const Domain::ConfigItem& item, bool enable, size_t index = 0) override;

    template <typename T>
    using ConstRefBoolPair = std::pair<std::reference_wrapper<const T>, bool>;
    template <typename T>
    using ConstPtrBoolPair = std::pair<const T*, bool>;

    [[nodiscard]] ConstRefBoolPair<Domain::Preset::HwPrinterConfig>
    get_printer_config(Domain::SelectionId project_id, const std::string& hw_config_id) const;
    [[nodiscard]] ConstPtrBoolPair<Domain::Preset::HwPrinterConfig>
    get_printer_config_unsafe(Domain::SelectionId project_id, const std::string& hw_config_id) const;
    [[nodiscard]] ConstRefBoolPair<Domain::Preset::EvaluatedPrinterPreset::Preset>
    get_printer_preset(
        Domain::SelectionId project_id,
        const std::string& hw_config_id,
        const std::string& printer_preset_id
    ) const;
    [[nodiscard]] ConstPtrBoolPair<Domain::Preset::EvaluatedPrinterPreset::Preset>
    get_printer_preset_unsafe(
        Domain::SelectionId project_id,
        const std::string& hw_config_id,
        const std::string& printer_preset_id
    ) const;
    [[nodiscard]] const Domain::Preset::EvaluatedPrinterPreset::Preset*
    get_printer_system_preset(
        Domain::SelectionId project_id,
        const std::string& hw_config_id,
        const std::string& printer_preset_id
    ) const;
    const std::string* get_printer_system_preset_id(
        Domain::SelectionId project_id,
        const std::string& hw_config_id,
        const std::string& printer_preset_id
    ) const;
    [[nodiscard]] ConstRefBoolPair<Domain::Preset::EvaluatedPrintPreset::Preset> get_print_preset(
        Domain::SelectionId project_id,
        const std::string& hw_config_id,
        const std::string& printer_preset_id,
        const std::string& print_id
    ) const;
    [[nodiscard]] ConstPtrBoolPair<Domain::Preset::EvaluatedPrintPreset::Preset> get_print_preset_unsafe(
        Domain::SelectionId project_id,
        const std::string& hw_config_id,
        const std::string& printer_preset_id,
        const std::string& print_id
    ) const;
    [[nodiscard]] const Domain::Preset::EvaluatedPrintPreset::Preset* get_print_system_preset(
        Domain::SelectionId project_id,
        const std::string& hw_config_id,
        const std::string& printer_preset_id,
        const std::string& print_id
    ) const;
    const std::string* get_print_system_preset_id(
        Domain::SelectionId project_id,
        const std::string& hw_config_id,
        const std::string& printer_preset_id,
        const std::string& print_id
    ) const;

    [[nodiscard]] ConstRefBoolPair<Domain::Preset::EvaluatedToolPrintPreset::Preset>
    get_tool_print_preset(
        Domain::SelectionId project_id,
        const std::string& hw_config_id,
        const std::string& printer_preset_id,
        const std::string& print_preset_id,
        size_t tool_index,
        const std::string& tool_print_preset_id
    ) const;
    [[nodiscard]] ConstPtrBoolPair<Domain::Preset::EvaluatedToolPrintPreset::Preset>
    get_tool_print_preset_unsafe(
        Domain::SelectionId project_id,
        const std::string& hw_config_id,
        const std::string& printer_preset_id,
        const std::string& print_preset_id,
        size_t tool_index,
        const std::string& tool_print_preset_id
    ) const;
    [[nodiscard]] const Domain::Preset::EvaluatedToolPrintPreset::Preset*
    get_tool_print_system_preset(
        Domain::SelectionId project_id,
        const std::string& hw_config_id,
        const std::string& printer_preset_id,
        const std::string& print_preset_id,
        size_t tool_index,
        const std::string& tool_print_preset_id
    ) const;
    const std::string* get_tool_print_system_preset_id(
        Domain::SelectionId project_id,
        const std::string& hw_config_id,
        const std::string& printer_preset_id,
        const std::string& print_preset_id,
        size_t tool_index,
        const std::string& tool_print_preset_id
    ) const;

    [[nodiscard]] ConstRefBoolPair<Domain::Preset::EvaluatedMaterialPreset::Preset>
    get_material_preset(
        Domain::SelectionId project_id,
        const std::string& hw_config_id,
        const std::string& printer_preset_id,
        const std::string& print_preset_id,
        size_t slot_index,
        const std::string& material_preset_id
    ) const;
    [[nodiscard]] ConstPtrBoolPair<Domain::Preset::EvaluatedMaterialPreset::Preset>
    get_material_preset_unsafe(
        Domain::SelectionId project_id,
        const std::string& hw_config_id,
        const std::string& printer_preset_id,
        const std::string& print_preset_id,
        size_t slot_index,
        const std::string& material_preset_id
    ) const;
    [[nodiscard]] const Domain::Preset::EvaluatedMaterialPreset::Preset*
    get_material_system_preset(
        Domain::SelectionId project_id,
        const std::string& hw_config_id,
        const std::string& printer_preset_id,
        const std::string& print_preset_id,
        size_t slot_index,
        const std::string& material_preset_id
    ) const;
    const std::string* get_material_system_preset_id(
        Domain::SelectionId project_id,
        const std::string& hw_config_id,
        const std::string& printer_preset_id,
        const std::string& print_preset_id,
        size_t slot_index,
        const std::string& material_preset_id
    ) const;

    [[nodiscard]] ConstRefBoolPair<Domain::Preset::HwPrinterConfig> get_printer_config(
        const std::string& hw_config_id
    ) const
    {
        return get_printer_config(m_selected_project_id, hw_config_id);
    }

    [[nodiscard]] ConstRefBoolPair<Domain::Preset::EvaluatedPrinterPreset::Preset>
    get_printer_preset(const std::string& hw_config_id, const std::string& printer_preset_id) const
    {
        return get_printer_preset(m_selected_project_id, hw_config_id, printer_preset_id);
    }

    [[nodiscard]] ConstRefBoolPair<const Domain::Preset::EvaluatedPrintPreset::Preset>
    get_print_preset(
        const std::string& hw_config_id,
        const std::string& printer_preset_id,
        const std::string& print_id
    ) const
    {
        return get_print_preset(m_selected_project_id, hw_config_id, printer_preset_id, print_id);
    }

    [[nodiscard]] ConstRefBoolPair<Domain::Preset::EvaluatedToolPrintPreset::Preset>
    get_tool_print_preset(
        const std::string& hw_config_id,
        const std::string& printer_preset_id,
        const std::string& print_preset_id,
        size_t tool_index,
        const std::string& tool_print_preset_id
    ) const
    {
        return get_tool_print_preset(
            m_selected_project_id,
            hw_config_id,
            printer_preset_id,
            print_preset_id,
            tool_index,
            tool_print_preset_id
        );
    }

    [[nodiscard]] ConstRefBoolPair<Domain::Preset::EvaluatedMaterialPreset::Preset>
    get_material_preset(
        const std::string& hw_config_id,
        const std::string& printer_preset_id,
        const std::string& print_preset_id,
        size_t slot_index,
        const std::string& material_preset_id
    ) const
    {
        return get_material_preset(
            m_selected_project_id,
            hw_config_id,
            printer_preset_id,
            print_preset_id,
            slot_index,
            material_preset_id
        );
    }

    [[nodiscard]] HwPrinterConfigProjectView get_printer_configs_view(Domain::SelectionId project_id) const;

    [[nodiscard]] PrinterPresetProjectView
    get_printer_presets_view(Domain::SelectionId project_id, const std::string& hw_config_id) const;

    [[nodiscard]] PrintPresetProjectView get_print_presets_view(
        Domain::SelectionId project_id,
        const std::string& hw_config_id,
        const std::string& printer_preset_id
    ) const;

    [[nodiscard]] ToolPrintPresetProjectView get_tool_print_presets_view(
        Domain::SelectionId project_id,
        const std::string& hw_config_id,
        const std::string& printer_preset_id,
        const std::string& print_preset_id,
        size_t tool_index
    ) const;

    [[nodiscard]] MaterialPresetProjectView get_material_presets_view(
        Domain::SelectionId project_id,
        const std::string& hw_config_id,
        const std::string& printer_preset_id,
        const std::string& print_preset_id,
        size_t slot_index
    ) const;

    /**
     * @brief Get iterator-like view over hw printer configs.
     * @return For-range iterable of `std::pair<std::ref_wrapper<HwPrinterConfig>, bool>`
     * where the bool indicates if it is a runtime hw config.
     *
     * Intended usage
     * @code{.cpp}
     * for (const auto [ref, is_runtime] : preset_interactor.get_printer_configs(...)) {
     *     const auto& hw_config = ref.get();
     *     // do stuff with hw_config and is_runtime
     * }
     * @endcode
     */
    [[nodiscard]] auto get_printer_configs(Domain::SelectionId project_id) const
    {
        return get_printer_configs_view(project_id).items();
    }

    /**
     * @brief Get iterator-like view over printer presets.
     * @return For-range iterable of `std::pair<std::ref_wrapper<EvaluatedPrinterPreset::Preset&>, bool>`
     * where the bool indicates if it is a runtime preset.
     *
     * Intended usage
     * @code{.cpp}
     * for (const auto [ref, is_runtime] : preset_interactor.get_printer_presets(...)) {
     *     const auto& printer_preset = ref.get();
     *     // do stuff with printer_preset and is_runtime
     * }
     * @endcode
     */
    [[nodiscard]] auto
    get_printer_presets(Domain::SelectionId project_id, const std::string& hw_config_id) const
    {
        return get_printer_presets_view(project_id, hw_config_id).items();
    }

    /**
     * @brief Get iterator-like view over print presets.
     * @return For-range iterable of `std::pair<std::ref_wrapper<EvaluatedPrintPreset::Preset&>, bool>`
     * where the bool indicates if it is a runtime preset.
     *
     * Intended usage
     * @code{.cpp}
     * for (const auto [ref, is_runtime] : preset_interactor.get_print_presets(...)) {
     *     const auto& print_preset = ref.get();
     *     // do stuff with print_preset and is_runtime
     * }
     * @endcode
     */
    [[nodiscard]] auto get_print_presets(
        Domain::SelectionId project_id,
        const std::string& hw_config_id,
        const std::string& printer_preset_id
    ) const
    {
        return get_print_presets_view(project_id, hw_config_id, printer_preset_id).items();
    }

    /**
     * @brief Get iterator-like view over tool-print presets.
     * @return For-range iterable of `std::pair<std::ref_wrapper<EvaluatedToolPrintPreset::Preset&>, bool>`
     * where the bool indicates if it is a runtime preset.
     *
     * Intended usage
     * @code{.cpp}
     * for (const auto [ref, is_runtime] : preset_interactor.get_tool_print_presets(...)) {
     *     const auto& tool_print_preset = ref.get();
     *     // do stuff with tool_print_preset and is_runtime
     * }
     * @endcode
     */
    [[nodiscard]] auto get_tool_print_presets(
        Domain::SelectionId project_id,
        const std::string& hw_config_id,
        const std::string& printer_preset_id,
        const std::string& print_preset_id,
        size_t tool_index
    ) const
    {
        return get_tool_print_presets_view(
                   project_id,
                   hw_config_id,
                   printer_preset_id,
                   print_preset_id,
                   tool_index
        )
            .items();
    }

    /**
     * @brief Get iterator-like view over material presets.
     * @return For-range iterable of `std::pair<std::ref_wrapper<EvaluatedMaterialPreset::Preset&>, bool>`
     * where the bool indicates if it is a runtime preset.
     *
     * Intended usage
     * @code{.cpp}
     * for (const auto [ref, is_runtime] : preset_interactor.get_material_presets(...)) {
     *     const auto& material_preset = ref.get();
     *     // do stuff with material_preset and is_runtime
     * }
     * @endcode
     */
    [[nodiscard]] auto get_material_presets(
        Domain::SelectionId project_id,
        const std::string& hw_config_id,
        const std::string& printer_preset_id,
        const std::string& print_preset_id,
        size_t slot_index
    ) const
    {
        return get_material_presets_view(
                   project_id,
                   hw_config_id,
                   printer_preset_id,
                   print_preset_id,
                   slot_index
        )
            .items();
    }

    /**
     * @brief Get iterator-like view over hw printer configs for selected project.
     * @return For-range iterable of `std::pair<std::ref_wrapper<HwPrinterConfig>, bool>`
     * where the bool indicates if it is a runtime hw config.
     *
     * Intended usage
     * @code{.cpp}
     * for (const auto [ref, is_runtime] : preset_interactor.get_printer_configs(...)) {
     *     const auto& hw_config = ref.get();
     *     // do stuff with hw_config and is_runtime
     * }
     * @endcode
     */
    [[nodiscard]] auto get_printer_configs() const
    {
        return get_printer_configs(m_selected_project_id);
    }

    /**
     * @brief Get iterator-like view over printer presets for selected project.
     * @return For-range iterable of `std::pair<std::ref_wrapper<EvaluatedPrinterPreset::Preset&>, bool>`
     * where the bool indicates if it is a runtime preset.
     *
     * Intended usage
     * @code{.cpp}
     * for (const auto [ref, is_runtime] : preset_interactor.get_printer_presets(...)) {
     *     const auto& printer_preset = ref.get();
     *     // do stuff with printer_preset and is_runtime
     * }
     * @endcode
     */
    [[nodiscard]] auto get_printer_presets(const std::string& hw_config_id) const
    {
        return get_printer_presets(m_selected_project_id, hw_config_id);
    }

    /**
     * @brief Get iterator-like view over print presets for selected project.
     * @return For-range iterable of `std::pair<std::ref_wrapper<EvaluatedPrintPreset::Preset&>, bool>`
     * where the bool indicates if it is a runtime preset.
     *
     * Intended usage
     * @code{.cpp}
     * for (const auto [ref, is_runtime] : preset_interactor.get_print_presets(...)) {
     *     const auto& print_preset = ref.get();
     *     // do stuff with print_preset and is_runtime
     * }
     * @endcode
     */
    [[nodiscard]] auto
    get_print_presets(const std::string& hw_config_id, const std::string& printer_preset_id) const
    {
        return get_print_presets(m_selected_project_id, hw_config_id, printer_preset_id);
    }

    /**
     * @brief Get iterator-like view over tool-print presets for selected project.
     * @return For-range iterable of `std::pair<std::ref_wrapper<EvaluatedToolPrintPreset::Preset&>, bool>`
     * where the bool indicates if it is a runtime preset.
     *
     * Intended usage
     * @code{.cpp}
     * for (const auto [ref, is_runtime] : preset_interactor.get_tool_print_presets(...)) {
     *     const auto& tool_print_preset = ref.get();
     *     // do stuff with tool_print_preset and is_runtime
     * }
     * @endcode
     */
    [[nodiscard]] auto get_tool_print_presets(
        const std::string& hw_config_id,
        const std::string& printer_preset_id,
        const std::string& print_preset_id,
        size_t tool_index
    ) const
    {
        return get_tool_print_presets(
            m_selected_project_id,
            hw_config_id,
            printer_preset_id,
            print_preset_id,
            tool_index
        );
    }

    /**
     * @brief Get iterator-like view over material presets for selected project.
     * @return For-range iterable of `std::pair<std::ref_wrapper<EvaluatedMaterialPreset::Preset&>, bool>`
     * where the bool indicates if it is a runtime preset.
     *
     * Intended usage
     * @code{.cpp}
     * for (const auto [ref, is_runtime] : preset_interactor.get_material_presets(...)) {
     *     const auto& material_preset = ref.get();
     *     // do stuff with material_preset and is_runtime
     * }
     * @endcode
     */
    [[nodiscard]] auto get_material_presets(
        const std::string& hw_config_id,
        const std::string& printer_preset_id,
        const std::string& print_preset_id,
        size_t slot_index
    ) const
    {
        return get_material_presets(
            m_selected_project_id,
            hw_config_id,
            printer_preset_id,
            print_preset_id,
            slot_index
        );
    }

    bool is_printer_preset_selected_and_dirty(
        const std::string& hw_config_id,
        const std::string& printer_preset_id) const;

    bool is_print_preset_selected_and_dirty(
        const std::string& hw_config_id,
        const std::string& printer_preset_id,
        const std::string& print_preset_id) const;

    bool is_tool_print_preset_selected_and_dirty(
        const std::string& hw_config_id,
        const std::string& printer_preset_id,
        const std::string& print_preset_id,
        const std::string& tool_print_preset_id,
        size_t tool_index) const;

    bool is_tool_material_preset_selected_and_dirty(
        const std::string& hw_config_id,
        const std::string& printer_preset_id,
        const std::string& print_preset_id,
        const std::string& tool_material_preset_id,
        size_t slot_index) const;

    void discard_selected_printer_preset_changes();
    void discard_selected_print_preset_changes();
    void discard_selected_tool_print_preset_changes(size_t tool_index);
    void discard_selected_tool_material_preset_changes(size_t slot_index);

    void set_dialog_manager(IPresetDialogManager* dialog_manager) {
        m_dialog_manager = dialog_manager;
    }

    IPresetDialogManager* dialog_manager() {
        return m_dialog_manager;
    }

    void set_unsaved_changes(PresetsSwitchStates&& unsaved_changes);

    bool has_invalid_hw_config() const {
        auto& p = get_or_fail_project_context(m_selected_project_id);
        const auto& cc = m_workbench.project(m_selected_project_id).find_config_container(selected_config_container_context().config_container_id);
        return p.invalid_hw_config.has_value() && p.invalid_hw_config->id == cc->selected_preset().hw_config.id;
    }

    /**
     * @name Implementation of Biz::Preset::IPresetVisualGetter public interface
     * @{
     */
    Domain::Vec2ds system_preset_bed_shape(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id
    ) const override;

    Domain::Vec2ds system_preset_bed_shape(
        Domain::SelectionId project_id,
        const Domain::Preset::SelectedPreset& selected_preset
    ) const;
    /**@}*/

    bool use_hw_config_short_name() const
    {
        return m_use_hw_config_short_name;
    }

    void set_use_hw_config_short_name(bool use_short_name)
    {
        m_use_hw_config_short_name = use_short_name;
    }

    struct ToolItems {
        std::vector<Domain::Preset::HwToolConfigDef> tool_defs;
        std::vector<std::size_t> selected_tools;
    };

    [[nodiscard]] ToolItems
    get_tool_items(const Domain::Preset::HwPrinterConfig& hw_config);

    struct SheetItems {
        std::vector<Domain::Preset::HwSheetConfigDef> sheet_defs;
        std::size_t selected_sheet{};
    };

    [[nodiscard]] SheetItems
    get_sheet_items(const Domain::Preset::HwPrinterConfig& hw_config);

    bool update_changed_printer_configs(const std::vector<Domain::Preset::HwPrinterConfig>& hw_configs);

private:
    using ProjectContexts = std::unordered_map<Domain::SelectionId, PresetInteractorProjectContext>;

    enum class ListenerType
    {
        IPresetChangedListener,
        ISlicingInputChangedListener
    };

    //using ListenerInvokeLaterBag = DeduplicatingInvokeLaterBag<ListenerType, PresetItemType, int>;
    using ListenerInvokeLaterBag = InvokeLaterBag;

    PresetInteractorConfigContainerContext& mutable_selected_config_container_context();

    Domain::Preset::SelectedPreset& mutable_selected_printer_preset();

    ProjectContexts::const_iterator get_project_context(Domain::SelectionId project_id) const
    {
        return m_project_contexts.find(project_id);
    }

    ProjectContexts::iterator get_project_context(Domain::SelectionId project_id)
    {
        return m_project_contexts.find(project_id);
    }

    const PresetInteractorProjectContext& get_or_fail_project_context(
        Domain::SelectionId project_id
    ) const
    {
        auto it = m_project_contexts.find(project_id);
        ASSERT(it != m_project_contexts.end());
        return it->second;
    }

    PresetInteractorProjectContext& get_or_fail_project_context(Domain::SelectionId project_id)
    {
        auto it = m_project_contexts.find(project_id);
        ASSERT(it != m_project_contexts.end());
        return it->second;
    }

    PresetInteractorProjectContext& get_or_create_project_context(Domain::SelectionId project_id);
    PresetInteractorConfigContainerContext& get_or_create_config_container_context(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id
    );
    tl::expected<void, std::string> update_hw_config_features(
        const Domain::Preset::Bundle& preset_bundle,
        Domain::Preset::HwPrinterConfig& hw_config
    );

    /**
     * @brief Updates presets for changed hw configuration
     * @return Returns true if the updated presets are valid, otherwise false---the caller is then
     * responsible to revert changes to changed hw_config of selected config container.
     */
    bool update_changed_selected_preset_hw_config(Domain::Preset::HwPrinterConfig& hw_config);

    const std::string& selected_hw_config_id() const;

    void save_user_preset_internal(Domain::Preset::PresetKind kind, size_t slot_index, const KeySet& item_names_to_omit, std::string new_name, InvokeLaterBag& bag);
    void update_vendor_presets(std::mutex& mut, Domain::Preset::Bundle& preset_bundle, const std::string& vendor_id);
    void reload_vendor_presets(const std::string& vendor_id);

    void fill_config_container_with_selected_preset(
        Domain::ConfigContainer& cc,
        const std::string& printer_hw_config_id,
        const std::string& printer_preset_id,
        bool printer_only,
        ListenerInvokeLaterBag& bag
    );

    void select_printer_preset_internal(
        const std::string& printer_hw_config_id,
        const std::string printer_preset_id,
        bool no_data_update,
        ListenerInvokeLaterBag& bag
    );
    void select_print_preset_internal(
        const std::string id,
        bool no_data_update,
        ListenerInvokeLaterBag& bag
    );
    void select_tool_print_preset_internal(
        size_t tool_index,
        const std::string id,
        bool no_data_update,
        ListenerInvokeLaterBag& bag
    );
    void select_material_preset_internal(
        size_t material_index,
        const std::string id,
        bool no_data_update,
        ListenerInvokeLaterBag& bag
    );

    bool print_has_unsaved_changes() const;
    bool tool_print_has_unsaved_changes(size_t tool_index) const;
    bool material_has_unsaved_changes(size_t tool_index) const;

    void fill_printer_presets(bool no_data_update, ListenerInvokeLaterBag& bag);
    void fill_print_presets(
        const Domain::Preset::SelectedPreset& selected_preset,
        bool no_data_update,
        ListenerInvokeLaterBag& bag
    );
    void fill_tools_presets(
        const Domain::Preset::SelectedPreset& selected_preset,
        bool no_data_update,
        ListenerInvokeLaterBag& bag
    );
    void fill_materials_presets(
        Domain::Preset::SelectedPreset& selected_preset,
        bool no_data_update,
        ListenerInvokeLaterBag& bag
    );
    void fill_tool_items(const Domain::Preset::HwPrinterConfig& hw_config);

    void fill_sheet_items(const Domain::Preset::HwPrinterConfig& hw_config);

    void fill_selected_material_cbis(
        Domain::Preset::SelectedPreset& selected_preset,
        bool force_fill_originals=false
    );
    void update_print_tool_cbi(
        Domain::Preset::SelectedPreset& selected_preset,
        Domain::SelectionId config_container_id
    );

    void duplicate_hw_config_if_needed_and_update(Domain::Preset::HwPrinterConfig& hw_config, ListenerInvokeLaterBag& bag);

    void invoke_slicing_input_changed();
    void invoke_on_preset_value_changed(const Domain::ConfigItem& config_item);
    PresetsSwitchStates::iterator find_unsaved_change(
        PresetDiffOperation operation,
        Domain::Preset::PresetKind kind,
        std::optional<size_t> tool_id = std::nullopt);
    void
    process_all_save_changes(Domain::Preset::SelectedPreset& selected_preset, InvokeLaterBag& bag);
    void process_operation_from_unsaved_changes(
        Domain::Preset::SelectedPreset& selected_preset,
        PresetDiffOperation operation,
        ListenerInvokeLaterBag& bag,
        std::optional<Domain::Preset::PresetKind> kind = std::nullopt,
        std::optional<size_t> tool_id = std::nullopt
    );

    void delete_preset(Domain::Preset::PresetKind kind, const std::string& preset_ids);

private:
    using SetAccessorMap = std::map<const OverridableConfigBoxInteractor*, OverridableConfigBoxInteractor::SetAccessor>;

    Domain::Workbench& m_workbench;
    BackupStore& m_backup_store;
    IO::BundlePaths m_bundle_paths;

    PresetItemObservableList m_printer_presets;
    PresetItemObservableList m_print_presets;
    PresetItemCompoundObservableList::WriteAccessor m_tool_print_presets_writer;
    PresetItemCompoundObservableList m_tool_print_presets{m_tool_print_presets_writer};
    PresetItemCompoundObservableList::WriteAccessor m_material_presets_writer;
    PresetItemCompoundObservableList m_material_presets{m_material_presets_writer};
    ToolConfigItemCompoundObservableList::WriteAccessor m_tool_items_writer;
    ToolConfigItemCompoundObservableList m_tool_items{m_tool_items_writer};
    SheetConfigItemObservableList m_sheet_items;

    ObjectSettingsInteractor::SetAccessor m_object_settings_interactor_accessor;
    ObjectSettingsInteractor m_object_settings_interactor;

    ProjectContexts m_project_contexts;

    ConfigBoxInteractor::SetAccessor m_printer_cbi_accessor;
    ConfigBoxInteractor m_printer_cbi;
    PrintToolConfigBoxInteractor::SetAccessor m_print_tool_cbi_accessor;
    PrintToolConfigBoxInteractor m_print_tool_cbi;
    OverridableCBIObservableList m_material_cbi_list;
    SetAccessorMap m_material_accessors; ///< Contains All SetAccessors currently in use

    Domain::SelectionId m_selected_project_id{Domain::INVALID_ID};

    IPresetDialogManager* m_dialog_manager{ nullptr };
    PresetsSwitchStates m_unsaved_changes;
    SelectedPresetIds m_unsaved_changes_selected_ids;

    bool m_use_hw_config_short_name{true};
};
} // namespace Slic3r::Biz::Preset
