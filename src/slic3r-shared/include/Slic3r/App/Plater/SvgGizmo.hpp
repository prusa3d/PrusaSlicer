#pragma once
#include <optional>
#include "Slic3r/App/Scene/IGizmo.hpp" // IToolGizmo, forward-declaration of Slic3r::App::Yoga::Dialog
#include "Slic3r/App/Scene/IGizmoController.hpp"
#include "Slic3r/App/Scene/MouseDragDetector.hpp"
#include "Slic3r/App/Plater/PlaterScenePresenter.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp" // forward from Yoga::GizmoWindow -> release_ui_window(MacOs,Lin)
#include "Slic3r/Biz/ProjectScoped.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Emboss/SurfaceDrag.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp" // ISceneSelectionChangedListener

namespace Slic3r::App::Plater {
class SvgDialog;

/**
 *  @brief   Tool for emboss scalable vector graphics '*.svg' files
 */
class SvgGizmo :
    public Scene::IToolGizmo,
    public Biz::Scene::ISceneSelectionChangedListener,
    public Biz::Scene::ISceneChangedListener,
    public Scene::IMouseDrag // surface dragging
{
public:
    SvgGizmo(
        PlaterScenePresenter& scene_presenter,
        Biz::ProjectInteractor& project_interactor,
        Scene::IGizmoController& gizmo_controller
    );
    // NOTE: Destructor is defined because Lin&Mac need it for Drag pimpl idiom
    ~SvgGizmo() override;

    /**
     * @name Implementation of IGizmo interface
     */
    std::unique_ptr<GizmoWindow> release_ui_window() override;
    Scene::GizmoActivationState on_mouse(Scene::GizmoEventContext& ctx, bool only_active) override;

    /**
     * @name Implementation of IMouseDrag interface
     */
    bool on_drag_start(const Scene::GizmoEventContext& ctx) override;
    bool on_dragging(const Scene::GizmoEventContext& ctx) override;
    void on_drag_finish() override;
    void on_drag_cancel() override;

    /**
     * @name Implementation of IToolGizmo interface
     */
    void on_activated() override;
    void on_deactivated() override;
    bool enabled() const override;
    void on_project_activated(size_t new_project_id) override;
    void on_project_deactivated(size_t old_project_id) override;

    Scene::ToolType type() const override;
    bool allows_activation_by_double_click(const Scene::GizmoEventContext& ctx) override;
    void render_imgui() override; // Draw crosshair

    /**
     * @name Implementation of ISceneSelectionChangedListener interface
     */
    void on_scene_selection_changed(
        Domain::SelectionId project_id,
        const Biz::Scene::ObjectSelection& selection
    ) override;

    void on_volume_mesh_changed(
        Domain::SelectionId project_id,
        const Domain::ElementRefs& volumes
    ) override;

private:
    // Call every time when param of emboss change
    bool update_volume(std::optional<Domain::ModelVolumeType> volume_type = {});
    void update_from_selected_elements(
        Domain::SelectionId project_id,
        const Domain::ElementRefs& selected_elements
    );
    void close();

    PlaterScenePresenter& m_scene_presenter;
    Biz::ProjectInteractor& m_project_interactor;
    Scene::IGizmoController& m_gizmo_controller;
    Biz::Emboss::SurfaceDrag m_surface_drag;

    struct ProjectContext; // forward declaration
    // m_projects use pimpl to hide ProjectContext into cpp file
    std::unique_ptr<Biz::ProjectScoped<ProjectContext>> m_proj_ctxs;

    SvgDialog& dialog()
    {
        ASSERT(m_dialog != nullptr);
        return *m_dialog;
    }

    SvgDialog* m_dialog = nullptr;
};
} // namespace Slic3r::App::Plater
