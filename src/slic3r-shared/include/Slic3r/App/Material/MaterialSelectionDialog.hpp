#pragma once

#include "Slic3r/App/Yoga/Dialog.hpp"
#include "Slic3r/App/Yoga/ListView.hpp"
#include "Slic3r/App/Yoga/ScrollArea.hpp"
#include "Slic3r/App/Yoga/Validator.hpp"
#include "Slic3r/App/Yoga/ButtonGroup.hpp"
#include "Slic3r/App/IAppConfigChangedListener.hpp"
#include "Slic3r/App/Material/MaterialSelectionRow.hpp"
#include "Slic3r/App/Material/MaterialSpoolSelectionRow.hpp"

#include "Slic3r/Biz/ISelectedConfigContainerChangedListener.hpp"
#include "Slic3r/Biz/ObservableListSortFilter.hpp"
#include "Slic3r/Biz/ObservableListSearcher.hpp"
#include "Slic3r/Biz/ProjectScoped.hpp"
#include "Slic3r/Biz/Preset/IPresetChangedListener.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/Platform/ListenerScope.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App::Yoga {
class InputText;
class LayoutButton;
class ScrollArea;
class StackLayout;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

class Navigator;
class MaterialSettingsDialog;

class MaterialSelectionDialog :
    public Yoga::Dialog,
    public Biz::IListSelectionChangedListener,
    public Biz::IListObserver<Biz::Preset::PresetItemObservableList>,
    public IAppConfigChangedListener,
    public Biz::Preset::IPresetChangedListener,
    public Biz::ISelectedConfigContainerChangedListener
{
public:
    struct Callbacks
    {
        std::function<void(size_t tab_index)> advanced_settings_tab_opened;
    };

    MaterialSelectionDialog(Biz::ProjectInteractor& project_interactor, Navigator& navigator);
    ~MaterialSelectionDialog();

    Callbacks& material_selection_callbacks();

    void set_material_index(size_t material_index);

    void on_list_selection_changed(Domain::SelectionId new_selection) override;

    void on_will_be_reset(std::optional<size_t> new_size = std::nullopt) override;
    void on_reset() override;

    MaterialSettingsDialog& material_settings_dialog();

    void on_app_config_changed(const std::string& key) override;

    void on_preset_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        Biz::Preset::PresetItemType type
    ) override;

    void on_selected_config_container_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id
    ) override;

protected:
    void close_action() override;
    void on_about_to_show() override;

    void on_tab_selected(int current_index) override;

    void update_preset_list();

    void create_page_presets();
    void create_page_materials();

private:
    struct ProjectContext;
    ProjectContext& context();
    const ProjectContext& context() const;
    void update_current_context();
    void update_material_filter();
    void update_materials_tab_enabled();

private:
    using SelectionRowListViewFactory = Yoga::ViewFactory<
        MaterialSelectionRow,
        Biz::Preset::PresetItem,
        MaterialSelectionRow::FnClicked,
        MaterialSelectionRow::FnIndexClicked,
        MaterialSelectionRow::FnChecked,
        MaterialSelectionRow::FnClicked,
        size_t&,
        Biz::Preset::PresetInteractor&>;
    using SelectionRowListView = Yoga::ListView<
        MaterialSelectionRow,
        Biz::Preset::PresetItem,
        SelectionRowListViewFactory,
        Yoga::ScrollArea>;

    using SpoolSelectionRowListViewFactory = Yoga::ViewFactory<
        MaterialSpoolSelectionRow,
        Biz::MatDb::MatDbItem,
        Biz::MatDb::MatDbInteractor&,
        size_t&,
        MaterialSpoolSelectionRow::CheckedFn,
        MaterialSpoolSelectionRow::CloseFn>;
    using SpoolSelectionRowListView = Yoga::ListView<
        MaterialSpoolSelectionRow,
        Biz::MatDb::MatDbItem,
        SpoolSelectionRowListViewFactory,
        Yoga::ScrollArea>;

    Callbacks m_callbacks;

    Biz::ListenerScope<
        Biz::Preset::IPresetChangedListener,
        Biz::Preset::PresetInteractor,
        MaterialSelectionDialog>
        m_preset_changed_listener_scope;

    Biz::ListenerScope<
        Biz::ISelectedConfigContainerChangedListener,
        Biz::ProjectInteractor,
        MaterialSelectionDialog>
        m_selected_config_container_changed_listener_scope;

    Biz::ProjectInteractor& m_project_interactor;
    Navigator& m_navigator;

    // ----------- Page presets ------------
    Biz::Preset::PresetItemCompoundObservableList& m_material_presets;
    Biz::UnsharedPointer<Biz::ObservableListSortFilter<Biz::Preset::PresetItem>> m_material_filter;
    Biz::UnsharedPointer<Biz::ObservableListSearcher<Biz::Preset::PresetItem>> m_material_searcher;
    size_t m_material_index = Domain::INVALID_ID;
    Yoga::ButtonGroup m_material_type_button_group;
    std::map<std::string, Yoga::LayoutButton*> m_type_filter_buttons;
    Yoga::Item* m_type_filters = nullptr;

    Yoga::StackLayout* m_stack_layout = nullptr;

    Yoga::Item* m_page_presets                           = nullptr;
    Yoga::InputText* m_input_text_search_presets         = nullptr;
    Yoga::LayoutButton* m_only_favorites_button          = nullptr;
    SelectionRowListView* m_selection_row_list_view      = nullptr;
    Biz::Preset::PresetItemObservableList* m_preset_list = nullptr;
    MaterialSettingsDialog* m_material_settings_dialog   = nullptr;

    // ----------- Page materials ----------
    Yoga::Item* m_page_materials                               = nullptr;
    Yoga::InputText* m_input_text_search_materials             = nullptr;
    Yoga::LayoutButton* m_only_favorite_materials_button       = nullptr;
    SpoolSelectionRowListView* m_spool_selection_row_list_view = nullptr;
    Yoga::ComboBox* m_combobox_material_type                   = nullptr;
    bool m_materials_tab_enabled                               = true;

    struct ProjectContext
    {
        std::string type_filter;

        bool operator==(const ProjectContext& other) const = default;
    } m_current_context;

    std::unique_ptr<Biz::ProjectScoped<ProjectContext>> m_project_contexts;
};

} // namespace Slic3r::App
