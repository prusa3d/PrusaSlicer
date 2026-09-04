#include "Slic3r/App/CommandBindingManager.hpp"

#include "Slic3r/App/UIItemCommand.hpp"
#include "Slic3r/App/Yoga/MenuItem.hpp"

#include "Slic3r/App/Scene/GizmoCommandRegistry.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"

namespace Slic3r::App {

static auto translator = [](const std::string& s) { return Biz::_u8(s); };

void
CommandBindingManager::bind_menu_item(const UIItemCommand* command, Yoga::MenuItem* ui_item)
{
    ui_item->callbacks().action = [command]() { command->execute(); };

    ui_item->callbacks().checked_changed = [command](bool checked)
    { command->checked_changed(checked); };

    ui_item->set_shortcut(command->keyboard_shortcut_string(translator));

    bind(command->name(), ui_item);
}

void CommandBindingManager::bind_tb_item(const char* command_name, Yoga::AbstractButton* ui_item)
{
    ui_item->callbacks().action = [this, command_name]()
    {
        m_main_command_registry.command(command_name).execute();
        update_ui_items();
    };

    ui_item->set_shortcut(
        m_main_command_registry.command(command_name).keyboard_shortcut_string(translator)
    );

    if (const UIItemCommand* ui_command =
            dynamic_cast<const UIItemCommand*>(&m_main_command_registry.command(command_name)))
    {
        ui_item->callbacks().checked_changed = [ui_command](bool checked)
        { ui_command->checked_changed(checked); };
    }

    bind(command_name, ui_item);
}

void CommandBindingManager::unbind_ui_item(const Yoga::AbstractButton* ui_item)
{
    auto it = m_ui_item_to_command.find(ui_item);
    if (it == m_ui_item_to_command.end()) {
        return;
    }

    auto& cmd_items = m_ui_items.at(it->second);
    auto item_it = std::ranges::find(cmd_items, ui_item);
    cmd_items.erase(item_it);
    m_ui_item_to_command.erase(it);
}


const Platform::ICommand& CommandBindingManager::command(const char* command_name) const
{
    return m_gizmos_command_registry && m_gizmos_command_registry->has_command(command_name) ?
        m_gizmos_command_registry->command(command_name) :
        m_main_command_registry.command(command_name);
}

bool CommandBindingManager::has_command(const char* command_name) const
{
    if (m_gizmos_command_registry && m_gizmos_command_registry->has_command(command_name))
        return true;
    return m_main_command_registry.has_command(command_name);
}

void CommandBindingManager::update_ui_items()
{
    for (auto& [command_name, items] : m_ui_items) {
        const Platform::ICommand& cmd = command(command_name.c_str());
        for (Yoga::AbstractButton* ui_item : items) {
            ui_item->set_enabled(cmd.enabled());
        }
        if (const UIItemCommand* ui_command = dynamic_cast<const UIItemCommand*>(&cmd)) {
            for (Yoga::AbstractButton* ui_item : items) {
                ui_item->set_checked(ui_command->checked());
            }
        }
    }
}

void CommandBindingManager::bind(const char* command_name, Yoga::AbstractButton* ui_item)
{
    if (!m_ui_items.contains(command_name))
        m_ui_items[command_name].reserve(3); // let it be max 3 ui_item per command

    m_ui_items.at(command_name).emplace_back(ui_item);
    m_ui_item_to_command[ui_item] = command_name;
}

void CommandBindingManager::on_user_account_id_success(bool, const std::string&)
{
    update_ui_items();
}

void CommandBindingManager::on_user_account_logged_out()
{
    update_ui_items();
}

void CommandBindingManager::on_selected_bed_instances_changed(
    Domain::SelectionId,
    const Biz::Scene::BedSelection&
)
{
    update_ui_items();
}

void CommandBindingManager::on_status_cache_status_code_changed(const Domain::SlicingId)
{
    update_ui_items();
}

void CommandBindingManager::on_removable_drive_status_changed(
    const boost::filesystem::path&,
    Biz::RemovableDrive::RemovableDriveStatus
)
{
    update_ui_items();
}

void CommandBindingManager::on_scene_selection_changed(Domain::SelectionId project_id, const Biz::Scene::ObjectSelection&)
{
    update_ui_items();
}

} // namespace Slic3r::App
