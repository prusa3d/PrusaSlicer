#pragma once

#include "Slic3r/App/Yoga/Window.hpp"
#include "Slic3r/App/Yoga/ButtonGroup.hpp"
#include "Slic3r/App/Yoga/ComboBoxListViewSelection.hpp"
#include "Slic3r/Biz/Preset/IMacroOverridesChangedListener.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/App/SidebarToolHeadRow.hpp"
#include "Slic3r/App/Yoga/ListView.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App {

class PrintSettingsDialog;
class ToolToFeatureDialog;
class PrintModeDialog;
class PrintToolFavoritesItem;
class Navigator;

namespace Yoga {
class LayoutButton;
class InputTextField;
class ComboBox;
class ScrollArea;
} // namespace Yoga

class SidebarPrint :
    public Yoga::Window,
    public Biz::Preset::IPresetChangedListener,
    public Biz::Preset::IMacroOverridesChangedListener
{
public:
    explicit SidebarPrint(Biz::ProjectInteractor& project_interactor, Navigator& navigator);

    PrintSettingsDialog& print_settings_dialog();
    ToolToFeatureDialog* tool_to_feature_dialog();
    PrintModeDialog& print_mode_dialog();

    void on_preset_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        Biz::Preset::PresetItemType type
    ) override;

    void on_preset_value_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        const Domain::ConfigItem& item
    ) override;

    void on_preset_bundles_loaded() override;

    void on_config_container_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id
    ) override;

    void on_macro_overrides_changed() override;

private:
    void add_separator();
    void refresh_print_mode_button();

    void create_print_extras();
    void create_favorite_params();

    void update_tools_visibility();
    void update_extruder_assignment_visibility();
    void refresh_print_combobox_label_color();
    void refresh_tools_comboboxes_label_colors();

private:
    using ToolHeadListView = Yoga::ListView<
        SidebarToolHeadRow,
        Biz::Preset::PresetItemObservableList,
        Yoga::ViewFactory<
            SidebarToolHeadRow,
            Biz::Preset::PresetItemObservableList,
            Biz::ProjectInteractor&>>;

    Biz::ListenerScope<
        Biz::Preset::IPresetChangedListener,
        Biz::Preset::PresetInteractor,
        SidebarPrint>
        m_preset_changed_listener_scope;
    Biz::ListenerScope<
        Biz::Preset::IMacroOverridesChangedListener,
        Biz::Preset::PresetInteractor,
        SidebarPrint>
        m_macro_overrides_listener_scope;

    Biz::ProjectInteractor& m_project_interactor;
    Navigator& m_navigator;

    Yoga::LayoutButton* m_settings_set_btn{nullptr};
    Yoga::LayoutButton* m_print_mode_btn{nullptr};
    Yoga::ScrollArea* m_content_area{nullptr};
    ToolToFeatureDialog* m_tool_to_feature_dialog{nullptr};
    Yoga::Item* m_extruder_assignment_item{nullptr};
    PrintToolFavoritesItem* m_favorite_params{nullptr};

    Yoga::ComboBoxListViewSelection<Biz::Preset::PresetItem>* m_combo_print{nullptr};
    ToolHeadListView* m_tool_head_list_view{nullptr};

    PrintSettingsDialog* m_print_settings_dialog{nullptr};
    PrintModeDialog* m_print_mode_dialog{nullptr};

    int m_last_selected_index{-1};
};

} // namespace Slic3r::App
