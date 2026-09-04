#pragma once

#include "Slic3r/App/Plater/ScaleWidget.hpp"
#include "Slic3r/App/Scene/GizmoManager.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Biz/ObservableListSortFilter.hpp"
#include "Slic3r/Biz/Platform/ListenerScope.hpp"

#include "Slic3r/App/Yoga/Window.hpp"
#include "Slic3r/App/Yoga/ListView.hpp"
#include "Slic3r/App/ObjectConfigItem.hpp"
#include "Slic3r/App/OverrideOptionGroup.hpp"
#include "Slic3r/App/Config/ObservableOverrideCategorizer.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
class ObjectSettingsObservableList;
} // namespace Slic3r::Biz

namespace Slic3r::App::Yoga {
class Text;
class LayoutButton;
class ComboBox;
class ScrollArea;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

class OverrideSettingsDialog;
class WipeTowerSettings;

class SidebarObject :
    public Yoga::Window,
    public Biz::Scene::ISceneSelectionChangedListener,
    public Scene::IGizmoActiveToolListener,
    public Biz::IListObserver<Biz::OverrideItem>,
    public Biz::Preset::IPresetChangedListener
{
public:
    explicit SidebarObject(Biz::ProjectInteractor& project_interactor);

    void on_scene_selection_changed(
        Domain::SelectionId project_id,
        const Biz::Scene::ObjectSelection& selection
    ) override;

    void on_reset() override;

    void active_tool_changed(Scene::IToolGizmo* active_tool) override;

protected:
    void visible_updated_internal() override;

private:
    void add_volume_type_selector();
    void update_volume_type_selector();
    void update_object_name();
    void update_enable_modifiers();

private:
    using OverrideGroupListViewFactory = Yoga::ViewFactory<
        OverrideOptionGroup,
        Biz::OverrideItem,
        Biz::ProjectInteractor&,
        OverrideOptionGroup::SelectCategoryFn&>;
    using OverrideGroupListView =
        Yoga::ListView<OverrideOptionGroup, Biz::OverrideItem, OverrideGroupListViewFactory>;

    using ConfigItemFilter = Biz::ObservableListSortFilter<Biz::OverrideItem>;

    Biz::ProjectInteractor& m_project_interactor;

    Biz::ListenerScope<
        Biz::Scene::ISceneSelectionChangedListener,
        Biz::Scene::SceneInteractor,
        SidebarObject>
        m_scene_selection_changed_listener_scope;

    Biz::ListenerScope<
        Biz::Preset::IPresetChangedListener,
        Biz::Preset::PresetInteractor,
        SidebarObject>
        m_preset_changed_listener_scope;

    Biz::ListenerScope<
        Biz::IListObserver<Biz::OverrideItem>,
        Biz::ObjectSettingsObservableList,
        SidebarObject>
        m_osi_observer_scope;

    Item* m_extruder_picker{nullptr};
    Item* m_scale_section{nullptr};
    OverrideGroupListView* m_override_group_list_view{nullptr};

    Biz::UnsharedPointer<ConfigItemFilter> m_config_item_filter;
    Biz::UnsharedPointer<ObservableOverrideCategorizer> m_override_group_filter;

    Yoga::Text* m_text_object_name{nullptr};
    Yoga::ComboBox* m_volume_type_selector{nullptr};
    Yoga::Text* m_volume_type_selector_warning{nullptr};
    Plater::ScaleWidget* m_scale_widget{nullptr};
    Yoga::LayoutButton* m_add_settings_button{nullptr};
    Yoga::Text* m_no_overrides_label{nullptr};
    Yoga::ScrollArea* m_scroll_area{nullptr};

    Biz::Scene::ObjectSelection m_selection;

    OverrideSettingsDialog* m_override_settings_dialog{nullptr};

    WipeTowerSettings* m_wipe_tower_settings{nullptr};

    OverrideOptionGroup::SelectCategoryFn m_open_override_settings_dialog_for_category{nullptr};
};

} // namespace Slic3r::App
