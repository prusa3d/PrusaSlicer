#include "Slic3r/App/Plater/ContextMenuGizmo.hpp"
#include "Slic3r/App/Scene/BedNodeTag.hpp"
#include "Slic3r/App/Scene/SceneNodeTag.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Algorithms/Point.hpp"
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
        return Scene::GizmoActivationState::Inactive;
    }

    if (type == Platform::MouseEvent::Type::ButtonDown) {
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
            invoke_show_context_menu(ContextMenuType::Scene, pos);
            return Scene::GizmoActivationState::Inactive;
        }

        Scene::Node* node = ctx.pick_results().front().node;
        if (const Scene::BedNodeTag* bed_tag{node->tag_of_type<Scene::BedNodeTag>()}) {
            const Domain::BedRef instance{bed_tag->config_container_id, bed_tag->instance_id};

            if (m_project_interactor.scene_interactor().bed_selection().is_selected(instance)) {
                invoke_show_context_menu(ContextMenuType::Bed, pos);
            }
        } else if (const Scene::SceneNodeTag* tag{node->tag_of_type<Scene::SceneNodeTag>()};
                   tag && !tag->is_wipe_tower())
        {
            // ButtonUp id postponed a bit from QuickSelectGizmo, so
            // we need to postpone its processing here slightly too to correct process selection before
            m_timer_id = timer_queue.set_timer(
                std::chrono::milliseconds(150),
                [this, pos]()
                {
                    const Biz::Scene::ObjectSelection& selection =
                        m_project_interactor.scene_interactor().object_selection();
                    if (!selection.empty()) {
                        Domain::Project& project = m_project_interactor.selected_project();
                        if (selection.mode == Slic3r::Biz::Scene::SelectionMode::Volume) {
                            bool is_svg_or_text = selection.elements.size() == 1;
                            if (is_svg_or_text) {
                                Domain::ModelVolume* volume = project.find_volume_by_id(
                                    selection.elements.front().object_id,
                                    selection.elements.front().volume_id
                                );
                                is_svg_or_text = volume->is_text() || volume->is_svg();
                            }

                            invoke_show_context_menu(
                                is_svg_or_text ? ContextMenuType::SvgOrText :
                                                 ContextMenuType::Volume,
                                pos
                            );
                        } else {
                            invoke_show_context_menu(
                                selection.only_single_object() ? ContextMenuType::Object :
                                                                 ContextMenuType::MultiObjects,
                                pos
                            );
                        }
                    }
                }
            );
            return Scene::GizmoActivationState::Inactive;
        }
    }

    return Scene::GizmoActivationState::Inactive;
}

void ContextMenuGizmo::invoke_show_context_menu(ContextMenuType type, Domain::Vec2f mouse_position)
{
    invoke_listeners<IShowContextMenuListener>([&](IShowContextMenuListener* l)
                                               { l->on_show_context_menu(type, mouse_position); });
}

} // namespace Slic3r::App::Plater
