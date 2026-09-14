#include "Slic3r/App/RenderModuleBase.hpp"

#include "Slic3r/Assert.hpp"
#include "Slic3r/App/Navigator.hpp"
#include "Slic3r/App/ObjectListWindow.hpp"
#include "Slic3r/App/InvalidDataDialog.hpp"
#include "Slic3r/App/Scene/GizmoManager.hpp"

namespace Slic3r::App {

void RenderModuleBase::on_scene_keyboard_event(const Platform::KeyboardEvent& e)
{
    if (!m_render_module_navigator->is_any_modal_dialog_opened()
        && !m_gizmo_manager->on_scene_keyboard_event(e))
    {
        Platform::AbstractRenderModule::on_scene_keyboard_event(e);
    }
}

void RenderModuleBase::set_navigator(Navigator* navigator)
{
    m_render_module_navigator = navigator;
}

const Platform::CommandRegistry::CommandsMap& RenderModuleBase::gizmo_commands() const
{
    ASSERT(m_gizmo_manager);
    return m_gizmo_manager->commands();
}

const Platform::ICommand& RenderModuleBase::command(const char* name) const
{
    if (gizmo_commands().contains(name)) {
        return m_gizmo_manager->command(name);
    }
    return m_command_registry.command(name);
}

bool RenderModuleBase::is_gizmo_manager_completed() const
{
    return m_gizmo_manager != nullptr;
}

void RenderModuleBase::set_opened_dialog(Yoga::Dialog* opened_dialog)
{
    m_dialog_navigation.open_dialog(opened_dialog);
}

void RenderModuleBase::open_invalid_data_dialog()
{
    if (m_invalid_data_dialog.get()) {
        set_opened_dialog(m_invalid_data_dialog.get());
    }
}

void RenderModuleBase::set_object_list_collapsed(bool collapsed)
{
    if (m_object_list.get()) {
        m_object_list->set_collapsed(collapsed);
    }
}

} // namespace Slic3r::App
