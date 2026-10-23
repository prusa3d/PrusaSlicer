#include "Slic3r/App/Plater/ContextMenuGizmo.hpp"
#include "Slic3r/App/Plater/GizmoTimeouts.hpp"
#include "Slic3r/App/Scene/BedNodeTag.hpp"
#include "Slic3r/App/Scene/SceneNodeTag.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/App/Scene/ISceneProvider.hpp"

#include <tracy/Tracy.hpp>

namespace Slic3r::App::Plater {

ContextMenuGizmo::ContextMenuGizmo(
    Biz::ProjectInteractor& project_interactor,
    Scene::ISceneProvider& scene_provider
) :
    m_project_interactor(project_interactor),
    m_scene_provider(scene_provider)
{}

Scene::GizmoActivationState
ContextMenuGizmo::on_mouse(Scene::GizmoEventContext& ctx, bool only_active)
{
    ZoneScoped;

    // ignore bed picking when the camera is below the bed
    const Scene::Camera& camera{m_scene_provider.scene().camera()};
    const Scene::CameraProjectionType camera_type{camera.cam_projection().type()};
    const bool pick_disabled{
        (camera_type == Scene::CameraProjectionType::Perspective) ? camera.position().z() < 0.0 :
                                                                    camera.forward().z() >= 0.0
    };

    if (pick_disabled)
        return Scene::GizmoActivationState::Inactive;

    const Platform::MouseEvent& evt{ctx.mouse_event()};
    const Platform::MouseEvent::Type type{evt.type()};

    if (evt.button() != Platform::MouseButton::Right) {
        return m_probing ?
            Scene::GizmoActivationState::Probing :
            Scene::GizmoActivationState::Inactive;
    }

    if (type == Platform::MouseEvent::Type::ButtonDown) {
        m_probing = true;
        return Scene::GizmoActivationState::Probing;
    }
    auto& timer_queue = Biz::Platform::PlatformServices::instance().timer_queue();

    if (type == Platform::MouseEvent::Type::DoubleClick) {
        if (timer_queue.is_timer_running(m_timer_id)) {
            timer_queue.cancel_timer(m_timer_id);
        }
        m_double_click_detected = true;
        return Scene::GizmoActivationState::Done;
    }

    if (type == Platform::MouseEvent::Type::ButtonUp) {
        if (m_double_click_detected) {
            m_double_click_detected = false;
            return Scene::GizmoActivationState::Inactive;
        }

        Domain::Vec2f pos{evt.x(), evt.y()};

        if (ctx.pick_results().empty()) {
            request_context_menu(ContextMenuType::Scene, pos);
            return Scene::GizmoActivationState::Done;
        }

        Scene::Node* node = ctx.pick_results().front().node;
        if (const Scene::BedNodeTag* bed_tag{node->tag_of_type<Scene::BedNodeTag>()}) {
            const Domain::BedRef instance{bed_tag->config_container_id, bed_tag->instance_id};

            if (m_project_interactor.scene_interactor().bed_selection().is_selected(instance)) {
                request_context_menu(ContextMenuType::Bed, pos);
                return Scene::GizmoActivationState::Done;
            }
        } else if (const Scene::SceneNodeTag* tag{node->tag_of_type<Scene::SceneNodeTag>()};
                   tag && !tag->is_wipe_tower())
        {
            // ButtonUp id postponed a bit from QuickSelectGizmo, so
            // we need to postpone its processing here slightly too to correct process selection before
            m_timer_id = timer_queue.set_timer(
                std::chrono::milliseconds(GIZMO_CONTEXT_MENU_TIMEOUT),
                [this, pos]()
                {
                    request_context_menu(
                        context_menu_type(
                            m_project_interactor.scene_interactor().object_selection(),
                            m_project_interactor.selected_project()
                        ),
                        pos
                    );
                }
            );
            return Scene::GizmoActivationState::Inactive;
        }
    }

    return Scene::GizmoActivationState::Inactive;
}

void ContextMenuGizmo::on_cycle_prepare()
{
    m_probing = false;
}

} // namespace Slic3r::App::Plater
