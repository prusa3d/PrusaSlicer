#pragma once

#include "Slic3r/App/Scene/IGizmo.hpp"
#include "Slic3r/Biz/Platform/TimerQueue.hpp"

namespace Slic3r::App::Scene {
class ISceneProvider;
} // namespace Slic3r::App::Scene

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App::Plater {

enum class ContextMenuType
{
    Scene, //???
    Bed,
    Object,
    MultiObjects,
    SvgOrText,
    Volume
};

class IShowContextMenuListener
{
public:
    virtual ~IShowContextMenuListener()                                                   = default;
    virtual void on_show_context_menu(ContextMenuType type, Domain::Vec2f mouse_position) = 0;
};

class ContextMenuGizmo : public Scene::IGizmo, public WithListeners<IShowContextMenuListener>
{
public:
    ContextMenuGizmo(
        Biz::ProjectInteractor& project_interactor,
        Scene::ISceneProvider& scene_provider
    );

    Scene::GizmoActivationState on_mouse(Scene::GizmoEventContext& ctx, bool only_active) override;

private:
    void invoke_show_context_menu(ContextMenuType type, Domain::Vec2f mouse_position);

private:
    Biz::ProjectInteractor& m_project_interactor;
    Scene::ISceneProvider& m_scene_provider;

    Biz::Platform::TimerQueue::TimerID m_timer_id;
    bool m_double_click_detected{false};
};

} // namespace Slic3r::App::Plater
