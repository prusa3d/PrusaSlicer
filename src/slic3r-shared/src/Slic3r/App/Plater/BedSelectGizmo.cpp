#include "Slic3r/App/Plater/BedSelectGizmo.hpp"
#include "Slic3r/App/Scene/BedNodeTag.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Algorithms/Point.hpp"
#include "Slic3r/App/Scene/ISceneProvider.hpp"

#include <tracy/Tracy.hpp>

namespace Slic3r::App::Plater {

BedSelectGizmo::BedSelectGizmo(
    Biz::ProjectInteractor& project_interactor,
    Scene::ISceneProvider& scene_provider
) :
    m_project_interactor(project_interactor),
    m_scene_interactor(project_interactor.scene_interactor()),
    m_scene_provider(scene_provider)
{ }

Scene::GizmoActivationState BedSelectGizmo::on_mouse(Scene::GizmoEventContext& ctx, bool only_active)
{
    ZoneScoped;

    // ignore bed picking when the camera is below the bed
    const Scene::Camera& camera{m_scene_provider.scene().camera()};
    const Scene::CameraProjectionType camera_type{camera.cam_projection().type()};
    const bool pick_disabled{
        (camera_type == Scene::CameraProjectionType::Perspective) ? camera.position().z() < 0.0 :
                                                                    camera.forward().z() >= 0.0
    };

    if (pick_disabled) {
        return Scene::GizmoActivationState::Inactive;
    }

    const Platform::MouseEvent& evt{ctx.mouse_event()};
    const Platform::MouseEvent::Type type{evt.type()};

    if (type == Platform::MouseEvent::Type::ButtonDown && evt.button() != Platform::MouseButton::Left)
    {
        return Scene::GizmoActivationState::Inactive;
    }

    switch (m_click_detector.on_mouse(evt)) {
    case Scene::ClickState::Inactive:
        return Scene::GizmoActivationState::Inactive;
    case Scene::ClickState::Probing:
        return Scene::GizmoActivationState::Probing;
    case Scene::ClickState::Activated:
        break;
    }

    const Scene::BedNodeTag* tag{
        ctx.pick_results().empty() ? nullptr :
                                     ctx.pick_results().front().node->tag_of_type<Scene::BedNodeTag>()
    };

    auto& selection = m_scene_interactor.bed_selection();
    if (tag == nullptr) {
        if (selection.select_one(selection.last_selected_bed())) {
            return Scene::GizmoActivationState::Done;
        }
        return Scene::GizmoActivationState::Inactive;
    }
    const Domain::BedRef instance{tag->config_container_id, tag->instance_id};
    if (selection.is_selected(instance)) {
        return Scene::GizmoActivationState::Inactive;
    }

    const bool shift_down{
        (evt.key_modifiers() & Platform::KeyModifiers(Platform::KeyModifier::Shift)) != 0
    };

    if (shift_down) {
        if (selection.toggle(instance, Biz::Scene::CameraActionOnBedSelection::CenterOnBed)) {
            return Scene::GizmoActivationState::Done;
        }
    } else {
        if (selection.select_one(instance, Biz::Scene::CameraActionOnBedSelection::CenterOnBed)) {
            return Scene::GizmoActivationState::Done;
        }
    }

    return Scene::GizmoActivationState::Inactive;
}

} // namespace Slic3r::App::Plater
