#pragma once

#include "Slic3r/App/Scene/IGizmo.hpp"
#include "Slic3r/App/Scene/ClickDetector.hpp"

namespace Slic3r::App::Scene {
class ISceneProvider;
} // namespace Slic3r::App::Scene

namespace Slic3r::Biz::Scene {
class SceneInteractor;
} // namespace Slic3r::Biz::Scene

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App::Plater {

class BedSelectGizmo : public Scene::IGizmo
{
public:
    BedSelectGizmo(Biz::ProjectInteractor& project_interactor, Scene::ISceneProvider& scene_provider);

    Scene::GizmoActivationState on_mouse(Scene::GizmoEventContext& ctx, bool only_active) override;

private:
    Biz::ProjectInteractor& m_project_interactor;
    Biz::Scene::SceneInteractor& m_scene_interactor;
    Scene::ISceneProvider& m_scene_provider;
    Scene::ClickDetector m_click_detector;
};

} // namespace Slic3r::App::Plater
