#pragma once

#include "Slic3r/App/Scene/IGizmo.hpp"
#include "Slic3r/App/ContextMenu.hpp"
#include "Slic3r/Biz/Platform/TimerQueue.hpp"

namespace Slic3r::App::Scene {
class ISceneProvider;
} // namespace Slic3r::App::Scene

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App::Plater {

class ContextMenuGizmo : public Scene::IGizmo, public ContextMenuRequester
{
public:
    ContextMenuGizmo(
        Biz::ProjectInteractor& project_interactor,
        Scene::ISceneProvider& scene_provider
    );

    Scene::GizmoActivationState on_mouse(Scene::GizmoEventContext& ctx, bool only_active) override;
    void on_cycle_prepare() override;

private:
    Biz::ProjectInteractor& m_project_interactor;
    Scene::ISceneProvider& m_scene_provider;

    Biz::Platform::TimerQueue::TimerID m_timer_id;
    bool m_double_click_detected{false};
    bool m_probing{false};
};

} // namespace Slic3r::App::Plater
