#pragma once

#include "Slic3r/App/Platform/AbstractRenderModule.hpp"
#include "Slic3r/App/DialogNavigation.hpp"
#include "Slic3r/App/MenuManager.hpp"
#include "Slic3r/App/CommandBindingManager.hpp"
#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/ModalDialog.hpp"

#include <memory>

namespace Slic3r::App {

class Navigator;
class ObjectListWindow;
class InvalidDataDialog;
class MsgBoxHandler;

namespace Scene {
class GizmoManager;
class Scene;
} // namespace Scene

/**
 * @brief Base class shared by the project-backed render modules (PlaterRenderModule,
 *        PreviewRenderModule). Holds the gizmo/menu/dialog/navigation infrastructure that is
 *        identical between them but can't live in Platform::AbstractRenderModule itself, since
 *        that class sits in slic3r-platform and must not depend on slic3r-shared types
 *        (MenuManager, GizmoManager, DialogNavigation, ...).
 */
class RenderModuleBase : public Platform::AbstractRenderModule
{
public:
    void on_scene_keyboard_event(const Platform::KeyboardEvent& e) override;

    void set_navigator(Navigator* navigator) override;

    MenuManager& menu_manager() override { return m_menu_manager; }
    CommandBindingManager& command_binding_manager() override { return m_command_binding_manager; }

    const Platform::CommandRegistry::CommandsMap& gizmo_commands() const override;
    const Platform::ICommand& command(const char* name) const override;
    bool is_gizmo_manager_completed() const override;

    void set_opened_dialog(Yoga::Dialog* opened_dialog);
    void set_object_list_collapsed(bool collapsed);

    virtual void set_modal_dialog(ModalDialog dialog);

    virtual MsgBoxHandler* msg_box_handler();

protected:
    void apply_mouse_3d_input(Scene::Scene& scene);

    Navigator* m_render_module_navigator{nullptr};
    std::unique_ptr<Scene::GizmoManager> m_gizmo_manager;
    DialogNavigation m_dialog_navigation;
    Yoga::Passthrough<ObjectListWindow> m_object_list;
    Yoga::Passthrough<InvalidDataDialog> m_invalid_data_dialog;
    MenuManager m_menu_manager{m_command_registry};
    CommandBindingManager m_command_binding_manager{m_command_registry};
};

} // namespace Slic3r::App
