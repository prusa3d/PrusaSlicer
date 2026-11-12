#pragma once
#include <functional>
#include "Slic3r/App/Plater/PlaterScenePresenter.hpp"
#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/App/Scene/IGizmo.hpp" // IToolGizmo
#include "Slic3r/App/Scene/IGizmoController.hpp" // provide ability to close gizmo
#include "Slic3r/App/Scene/IThumbnailRenderListener.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/ProjectScoped.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp" // ISceneSelectionChangedListener + ISceneChangedListener
#include "Slic3r/Domain/ObjectID.hpp"
#include "Slic3r/App/Yoga/Item.hpp"

namespace Slic3r::App::Plater {
class SimplifyDialog;
class SimplifyNotification;

// Continue development for GLGizmoSimplify permanent link: 
// https://github.com/prusa3d/PrusaSlicer/blob/6fd9846df131c671ac9f944c836536f04d354a53/src/slic3r/GUI/Gizmos/GLGizmoSimplify.hpp
// https://github.com/prusa3d/PrusaSlicer/blob/6fd9846df131c671ac9f944c836536f04d354a53/src/slic3r/GUI/Gizmos/GLGizmoSimplify.cpp
class SimplifyGizmo :
    public Scene::IToolGizmo,
    public Biz::Scene::ISceneSelectionChangedListener,
    public Biz::Scene::ISceneChangedListener,
    public Scene::IThumbnailRenderListener
{
public:
    SimplifyGizmo(
        Render::Device& device,
        PlaterScenePresenter& scene_presenter,
        Biz::ProjectInteractor& project_interactor,
        Scene::IGizmoController& gizmo_controller,
        std::unique_ptr<SimplifyNotification> notification
    );
    ~SimplifyGizmo() override;
    /**
     * @name Implementation of IGizmo interface
     * @{
     */
    Scene::GizmoActivationState on_mouse(Scene::GizmoEventContext& ctx, bool only_active) override;
    /**@}*/

    /**
     * @name Implementation of IToolGizmo interface
     * @{
     */
    void on_activated() override;
    void on_deactivated() override;
    void on_project_activated(size_t new_project_id) override;
    void on_project_deactivated(size_t old_project_id) override;

    void on_thumbnail_render_begin() override;
    void on_thumbnail_render_end() override;

    Scene::ToolType type() const override;
    bool enabled() const override;
    std::unique_ptr<GizmoWindow> release_ui_window() override;
    /**@}*/

    /**
     * @name Implementation of ISceneSelectionChangedListener interface
     * Change simplified selection
     */
    void on_scene_selection_changed(Domain::SelectionId project_id, const Biz::Scene::ObjectSelection& selection) override;

    /**
     * @name Implementation of ISceneChangedListener interface
     * For external(not by SimplifyGizmo) transformation of the simplified mesh
     * Move simplified phantom to current position
     * @{
     */
    void on_volume_transformed(Domain::SelectionId project_id, const Domain::ElementRefs& elements,
        Biz::Scene::TransformState state, const Biz::BedTrackingChanges& bed_tracking_changes) override;
    void on_instance_transformed(Domain::SelectionId project_id, const Domain::ElementRefs& elements,
        Biz::Scene::TransformState state, const Biz::BedTrackingChanges& bed_tracking_changes) override;
    /**@}*/

    struct ProjectContext; // forward declaration
    using ProjectContexts = Biz::ProjectScoped<ProjectContext>;
private:
    SimplifyDialog& dialog() { return *m_dialog.get(); }
    void on_selection_change(Domain::SelectionId project_id, const Biz::Scene::ObjectSelection& selection);

    void deactivate(Domain::SelectionId project_id);
    void close();

    void apply_simplify();
    void process();

    Render::Device& m_device;
    PlaterScenePresenter& m_scene_presenter;
    Biz::ProjectInteractor& m_project_interactor;
    Scene::IGizmoController& m_gizmo_controller;
    // pimpled to not reregistr listeners + remove GizmoManager dependency
    std::unique_ptr<SimplifyNotification> m_notification; 

    // pimpled to hide ProjectContext into cpp file
    std::unique_ptr<ProjectContexts> m_proj_ctxs;
    Yoga::Passthrough<SimplifyDialog> m_dialog;
};
} // namespace Slic3r::App::Plater
