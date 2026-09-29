#include "Slic3r/App/RenderModuleBase.hpp"

#include "Slic3r/Assert.hpp"
#include "Slic3r/App/Navigator.hpp"
#include "Slic3r/App/ObjectListWindow.hpp"
#include "Slic3r/App/InvalidDataDialog.hpp"
#include "Slic3r/App/EffectiveValuesDialog.hpp"
#include "Slic3r/App/Scene/GizmoManager.hpp"
#include "Slic3r/App/Scene/Scene.hpp"
#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/Mouse3D/Mouse3DController.hpp"

namespace Slic3r::App {

void RenderModuleBase::on_scene_keyboard_event(const Platform::KeyboardEvent& e)
{
    if (!m_render_module_navigator->is_any_modal_dialog_opened()
        && !m_gizmo_manager->on_scene_keyboard_event(e))
    {
        Platform::AbstractRenderModule::on_scene_keyboard_event(e);
    }
}

void RenderModuleBase::apply_mouse_3d_input(Scene::Scene& scene)
{
    const AppServices& app_services = AppServices::instance();
    if (app_services.has_mouse_3d_controller()
        && app_services.mouse_3d_controller().apply(scene.camera(), scene.camera_trackball()))
    {
        this->request_render();
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

void RenderModuleBase::set_effective_values_key(std::optional<std::string> key)
{
    if (m_effective_values_dialog.get()) {
        m_effective_values_dialog->set_key(key);
    }
}
void RenderModuleBase::set_object_list_collapsed(bool collapsed)
{
    if (m_object_list.get()) {
        m_object_list->set_collapsed(collapsed);
    }
}

MsgBoxHandler* RenderModuleBase::msg_box_handler()
{
    return nullptr;
}

void RenderModuleBase::set_modal_dialog(ModalDialog dialog) {}

} // namespace Slic3r::App
