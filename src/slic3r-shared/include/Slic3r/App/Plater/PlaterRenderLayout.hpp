#pragma once

#include "Slic3r/App/AbstractRenderLayout.hpp"

#include "Slic3r/App/Plater/History.hpp"
#include "Slic3r/App/Plater/SidebarPlaterActionButtons.hpp"

namespace Slic3r::App::Lua {
class PluginDialog;
}

namespace Slic3r::App {
class InvalidDataDialog;
}

namespace Slic3r::App::Plater {

class History;
class SidebarPlaterActionButtons;

class PlaterRenderLayout : public AbstractRenderLayout
{
public:
    PlaterRenderLayout(
        Navigator& navigator,
        std::unique_ptr<TopBar> top_bar,
        std::unique_ptr<PreferencesDialog> preferences_dialog,
        std::unique_ptr<ObjectListWindow> object_list,
        std::unique_ptr<CubeView> cube_view,
        std::unique_ptr<PopNotification::PopNotificationListView> pop_notification_list_view,
        std::unique_ptr<SidebarBed> sidebar_bed,
        std::unique_ptr<SidebarPrint> sidebar_print,
        std::unique_ptr<SidebarObject> sidebar_object,
        std::unique_ptr<SidebarPlaterActionButtons> sidebar_action_buttons,
        std::unique_ptr<History> history,
        std::unique_ptr<NumberEntryDialog> number_entry_dialog,
        std::unique_ptr<WelcomeDialog> welcome_dialog,
        std::unique_ptr<InvalidDataDialog> invalid_data_dialog,
        std::unique_ptr<Lua::PluginDialog> plugin_dialog,
        std::unique_ptr<CrashedProjectsDialog> crashed_projects_dialog,
        std::unique_ptr<PresetUpdaterDialog> preset_updater_dialog
    );

    void init() override;

private:
    void init_left_column() override;
    void init_right_column() override;

private:
    Yoga::Passthrough<SidebarPlaterActionButtons> m_sidebar_action_buttons;
    Yoga::Passthrough<History> m_history;
    Yoga::Passthrough<WelcomeDialog> m_welcome_dialog;
    Yoga::Passthrough<InvalidDataDialog> m_invalid_data_dialog;
    Yoga::Passthrough<Lua::PluginDialog> m_plugin_dialog;
};

} // namespace Slic3r::App::Plater
