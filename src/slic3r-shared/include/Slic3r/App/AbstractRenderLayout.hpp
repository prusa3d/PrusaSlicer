#pragma once

#include "Slic3r/App/Plater/PlaterRenderModule.hpp"
#include "Slic3r/App/Yoga/RootItem.hpp"
#include "Slic3r/App/TopBar.hpp"
#include "Slic3r/App/ToolBar/ToolBarSwitchButton.hpp"
#include "Slic3r/App/ObjectListWindow.hpp"
#include "Slic3r/App/CubeView.hpp"
#include "Slic3r/App/SidebarBed.hpp"
#include "Slic3r/App/SidebarPrint.hpp"
#include "Slic3r/App/Render/ImguiTypes.hpp"
#include "Slic3r/App/Render/ScreenInfo.hpp"
#include "Slic3r/App/PopNotification/PopNotificationListView.hpp"
#include "Slic3r/App/SidebarObject.hpp"
#include "Slic3r/App/PreferencesDialog.hpp"
#include "Slic3r/App/NumberEntryDialog.hpp"
#include "Slic3r/App/PresetUpdater/PresetUpdaterDialog.hpp"
#include "Slic3r/App/IAppConfigChangedListener.hpp"
#include "Slic3r/App/CrashedProjectsDialog.hpp"

namespace Slic3r::App {

class SidebarStackLayout;
class ToolBar;

namespace Scene {
class IToolGizmo;
} // namespace Scene

namespace Yoga {
class Dialog;
class SplitLayout;
} // namespace Yoga

enum class ToolbarID
{
    ToolLeft = 0,
    ToolRight,
    Mode
};

class AbstractRenderLayout : public IAppConfigChangedListener
{
public:
    using Vec2f = Yoga::Vec2f;

    AbstractRenderLayout(
        Navigator& navigator,
        std::unique_ptr<TopBar> top_bar,
        std::unique_ptr<PreferencesDialog> preferences_dialog,
        std::unique_ptr<ObjectListWindow> object_list,
        std::unique_ptr<CubeView> cube_view,
        std::unique_ptr<PopNotification::PopNotificationListView> pop_notification_list_view,
        std::unique_ptr<SidebarBed> sidebar_bed,
        std::unique_ptr<SidebarPrint> sidebar_print,
        std::unique_ptr<SidebarObject> sidebar_object,
        std::unique_ptr<NumberEntryDialog> numbers_entry_dialog,
        std::unique_ptr<CrashedProjectsDialog> crashed_projects_dialog,
        std::unique_ptr<PresetUpdaterDialog> preset_updater_dialog
    );
    virtual ~AbstractRenderLayout();
    AbstractRenderLayout(const AbstractRenderLayout& other)            = delete;
    AbstractRenderLayout& operator=(const AbstractRenderLayout& other) = delete;

    virtual void init();

    void render();

    void set_size_info_from_screen(const Render::ScreenInfo& screen_info);

    ToolBarButton* add_toolbar_item(ToolbarID id, Render::Icon icon, const std::string& tooltip);
    ToolBarButton* add_toolbar_item_checkable(
        ToolbarID id,
        Render::Icon icon,
        const std::string& tooltip,
        bool checked = false
    );
    ToolBarSwitchButton* add_toolbar_item_switch(
        ToolbarID id,
        Render::Icon icon,
        const std::string& label,
        const std::string& tooltip,
        ToolBarSwitchButton::SwitchPosition switch_position
    );

    ToolBar* tool_left_toolbar() const;
    ToolBar* tool_right_toolbar() const;
    ToolBar* mode_toolbar() const;

    SidebarStackLayout* sidebar_stack_layout() const;

    void set_sidebars_visible(bool visible);

    void save_column_sizes();
    void load_column_sizes();

    void on_app_config_changed(const std::string& key) override;

protected:
    virtual void init_left_column();
    virtual void init_middle_column();
    virtual void init_right_column();

    ToolBar* find_toolbar(ToolbarID id) const;

    void update_cube_view_position();
    void update_left_separator_enable();

private:
    void init_toolbar_row();

protected:
    Navigator& m_navigator;

    Yoga::RootItem m_layout_main;
    Yoga::SplitLayout* m_layout_main_bottom = nullptr;
    Yoga::Item* m_layout_left_column        = nullptr;
    Yoga::Item* m_layout_center_row         = nullptr;
    Yoga::Item* m_layout_right_column       = nullptr;

    Yoga::Item* m_layout_middle_toolbar_row = nullptr;
    Yoga::Item* m_layout_middle_column      = nullptr;
    Yoga::Item* m_layout_scene_row          = nullptr;

    SidebarStackLayout* m_layout_sidebar_stack_layout = nullptr;

    // we are moving CubeView between Horizontal and Vertical layouts, wrapper is needed
    Yoga::Item* m_cube_view_wrapper = nullptr;

    ToolBar* m_tool_left_toolbar  = nullptr;
    ToolBar* m_tool_right_toolbar = nullptr;
    ToolBar* m_mode_toolbar       = nullptr;
    std::vector<ToolBarSwitchButton*> m_switch_buttons;

    bool m_sidebars_visible = true;
    float m_object_list_srcroll_y_delta{-1};
    float m_object_list_srcroll_y_previous_delta{-1};
    Yoga::SizeInfo m_info;

    // Inserted from render module
    Yoga::Passthrough<TopBar> m_top_bar;
    Yoga::Passthrough<ObjectListWindow> m_object_list;
    Yoga::Passthrough<CubeView> m_cube_view;
    Yoga::Passthrough<PopNotification::PopNotificationListView> m_pop_notification_list_view;
    Yoga::Passthrough<SidebarBed> m_sidebar_bed;
    Yoga::Passthrough<SidebarPrint> m_sidebar_print;
    Yoga::Passthrough<SidebarObject> m_sidebar_object;
    Yoga::Passthrough<PreferencesDialog> m_preferences_dialog;
    Yoga::Passthrough<NumberEntryDialog> m_numbers_entry_dialog;
    Yoga::Passthrough<CrashedProjectsDialog> m_crashed_projects_dialog;
    Yoga::Passthrough<PresetUpdaterDialog> m_preset_updater_dialog;
};

} // namespace Slic3r::App
