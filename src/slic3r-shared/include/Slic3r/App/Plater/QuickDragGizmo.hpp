#pragma once

#include "Slic3r/App/Plater/TranslationDialog.hpp"
#include "Slic3r/Domain/Point.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Biz/Platform/TimerQueue.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/App/Scene/Plane.hpp"
#include "Slic3r/App/Scene/IGizmo.hpp"
#include "Slic3r/App/Scene/ISceneProvider.hpp"
#include "Slic3r/App/Scene/MouseDragDetector.hpp" // IMouseDrag
#include "Slic3r/App/Plater/SelectionHandler.hpp"

namespace Slic3r::App::Plater {

using MousePosition = std::array<int, 2>;

class QuickDragGizmo : public Scene::IGizmo, public Scene::IMouseDrag
{
public:
    QuickDragGizmo(
        Biz::ProjectInteractor& project_interactor,
        Scene::ISceneProvider& scene_provider
    );

    Scene::GizmoActivationState on_mouse(Scene::GizmoEventContext& ctx, bool only_active) override;

    // IMouseDrag
    bool on_drag_start(const Scene::GizmoEventContext& ctx) override;
    bool on_dragging(const Scene::GizmoEventContext& ctx) override;
    void on_drag_finish() override;
    void on_drag_cancel() override;
    bool handles_object_selection() const override { return true; }

private:
    bool mouse_pos(float screen_x, float screen_y, Domain::Vec3d& out_pos);

    bool selection_is_off_bed() const;
    void cancel_virtual_bed_timer();

private:
    Biz::ProjectInteractor& m_project_interactor;
    Biz::Scene::SceneInteractor& m_scene_interactor;
    Scene::ISceneProvider& m_scene_provider;
    SelectionHandler m_selection_handler;
    Domain::Vec3d m_initial_world_pos;
    Scene::Plane m_plane{Domain::Vec3d::UnitZ(), 0};
    Biz::Scene::TransformMemento m_xform_memento;

    // Virtual-bed preview state for the current drag.
    Biz::Platform::TimerQueue::TimerID m_virtual_bed_timer_id;
    bool m_was_off_bed{false};
    bool m_dragging{false};
};

} // namespace Slic3r::App::Plater
