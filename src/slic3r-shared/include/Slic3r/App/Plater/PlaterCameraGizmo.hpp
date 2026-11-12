#pragma once

#include "Slic3r/App/Scene/AbstractCameraGizmo.hpp"

namespace Slic3r::App::Plater {

class PlaterCameraGizmo : public Scene::AbstractCameraGizmo
{
public:
    PlaterCameraGizmo(const Domain::Workbench& workbench, Biz::ProjectInteractor& project_interactor,
        Scene::ISceneProvider& scene_provider, Platform::AnimationManager& animation_manager)
        : Scene::AbstractCameraGizmo(workbench, project_interactor, scene_provider, animation_manager) {}

private:
    virtual bool any_draggable(Scene::GizmoEventContext& ctx) const override;
};

} // namespace Slic3r::App::Plater
