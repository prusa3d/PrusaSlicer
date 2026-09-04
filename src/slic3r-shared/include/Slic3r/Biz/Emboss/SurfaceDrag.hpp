#pragma once

#include <optional>
#include <tl/expected.hpp>
#include "Slic3r/App/Plater/PlaterScenePresenter.hpp"
#include "Slic3r/App/Scene/GizmoEventContext.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"

namespace Slic3r::App::Scene { class Node; }

namespace Slic3r::Biz::Emboss {

class SurfaceDrag {
public:
    SurfaceDrag(App::Plater::PlaterScenePresenter& scene_presenter, Biz::ProjectInteractor& project_interactor);
    ~SurfaceDrag(); // for pimpl
    bool on_drag_start(const App::Scene::GizmoEventContext& ctx, const std::optional<float>& distance);
    bool on_dragging(const App::Scene::GizmoEventContext& ctx, 
        const std::optional<float>& angle,
        const std::optional<float>& distance,
        const std::optional<double>& up_limit);
    void on_drag_finish();
    void on_drag_cancel();

    bool is_dragging();
    void imgui_draw();

private:
    App::Plater::PlaterScenePresenter& m_scene_presenter;
    Biz::ProjectInteractor& m_project_interactor;
    struct Drag; // pimpl
    std::unique_ptr<Drag> m_drag; // exist only during drag operation
};

Domain::Transform3d get_volume_transformation(
    Domain::Transform3d world, // from volume
    const Domain::Vec3d& world_dir, // wanted new direction
    const Domain::Vec3d& world_position, // wanted new position
    // Invers transformation of text volume instance
    // Help convert world transformation to instance space 
    const Domain::Transform3d& instance_inv,
    // initial rotation in Z axis
    const std::optional<float>& current_angle,
    const std::optional<float>& current_distance,
    const std::optional<double>& up_limit);

/// <summary>
/// Get volume world transformation 
/// </summary>
/// <param name="project">Project containing reference</param>
/// <param name="ref">Reference on model volume</param>
/// <returns>World transformation of the reference volume</returns>
Domain::Transform3d world_tr(const Domain::Project& project, const Domain::ElementRef& ref);

/// <summary>
/// Calculate volume rotation around volume Z axis
/// </summary>
/// <param name="project">Project containing reference</param>
/// <param name="ref">Reference on model volume</param>
/// <returns>Angle in radians when exists</returns>
std::optional<float> calc_rotation(const Domain::Project& project, const Domain::ElementRef& ref);

enum class DistanceIssue {
    NoSurfacePoint,
    ApproxZero
};
/// <summary>
/// Calculate distance of the object from the surface
/// </summary>
/// <param name="project">Project containing reference</param>
/// <param name="ref">Reference on model volume</param>
/// <param name="root">Scene root node to be able cast into scene in direction of the emboss</param>
/// <returns>Distance from surface when exists and not apporx zero</returns>
tl::expected<float, DistanceIssue> calc_distance(const Domain::Project& project,
    const Domain::ElementRef& ref, App::Scene::Node& root);

/// <summary>
/// Applies a relative 3D transformation to the current selection.
/// TODO: move function near SceneInteractor::transform_selection(designed for text and Svg part)
/// </summary>
/// <param name="tr">Relative 3D transformation to apply to the selection.</param>
/// <param name="project_interactor">The project interactor containing the selection to transform.</param>
void transform_selection_relative(const Domain::Transform3d& tr, Biz::ProjectInteractor& project_interactor);

} // Slic3r::Biz::Emboss