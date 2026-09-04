#include "Slic3r/App/Plater/PlaterRenderLayout.hpp"
#include "Slic3r/App/Lua/PluginDialog.hpp"
#include "Slic3r/App/InvalidDataDialog.hpp"
#include <Yoga.h>

namespace Slic3r::App::Plater {

PlaterRenderLayout::PlaterRenderLayout(
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
) :
    AbstractRenderLayout(
        navigator,
        std::move(top_bar),
        std::move(preferences_dialog),
        std::move(object_list),
        std::move(cube_view),
        std::move(pop_notification_list_view),
        std::move(sidebar_bed),
        std::move(sidebar_print),
        std::move(sidebar_object),
        std::move(number_entry_dialog),
        std::move(crashed_projects_dialog),
        std::move(preset_updater_dialog)
    ),
    m_sidebar_action_buttons(std::move(sidebar_action_buttons)),
    m_history(std::move(history)),
    m_welcome_dialog(std::move(welcome_dialog)),
    m_invalid_data_dialog(std::move(invalid_data_dialog)),
    m_plugin_dialog(std::move(plugin_dialog))
{}

void PlaterRenderLayout::init()
{
    AbstractRenderLayout::init();

    m_layout_main.append(m_welcome_dialog.release());
    m_welcome_dialog->attach_to_center();
    m_layout_main.append(m_invalid_data_dialog.release());
    m_invalid_data_dialog->attach_to_center();
    m_layout_main.append(m_plugin_dialog.release());
    m_plugin_dialog->attach_to_center();
}

void PlaterRenderLayout::init_left_column()
{
    AbstractRenderLayout::init_left_column();
    m_layout_left_column->append(m_history.release());
}

void PlaterRenderLayout::init_right_column()
{
    AbstractRenderLayout::init_right_column();

    m_layout_right_column->append(m_sidebar_action_buttons.release());
}

} // namespace Slic3r::App::Plater
