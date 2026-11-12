#pragma once

#include "Slic3r/App/Scene/IGizmo.hpp"
#include "Slic3r/App/Scene/ISceneChangedListener.hpp"
#include "Slic3r/App/Scene/IThumbnailRenderListener.hpp"
#include "Slic3r/App/Scene/AuxiliaryElementId.hpp"
#include "Slic3r/App/Scene/TriangleMeshManager.hpp"
#include "Slic3r/App/Render/GeometryManager.hpp"

#include "Slic3r/Biz/Scene/SceneInteractor.hpp"

#include "Slic3r/Domain/Point.hpp"
#include "Slic3r/Domain/SelectionId.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App::Scene { class NodeBuilder; }
namespace Slic3r::App::Plater {
class PlaterScenePresenter;

class PlaceOnFaceGizmo :
    public Scene::IToolGizmo,
    public Biz::Scene::ISceneSelectionChangedListener,
    public Scene::ISceneChangedListener,
    public Scene::IThumbnailRenderListener
{
    using ModelGeometryManager     = Render::GeometryManager<Scene::AuxiliaryElementId>;
    using ModelTriangleMeshManager = Scene::TriangleMeshManager<Scene::AuxiliaryElementId>;

public:
    PlaceOnFaceGizmo(
        Render::Device& device,
        PlaterScenePresenter& scene_presenter,
        Biz::ProjectInteractor& project_interactor
    );

    /**
     * @name Implementation of IGizmo interface
     * @{
     */
    Scene::GizmoActivationState on_mouse(Scene::GizmoEventContext& ctx, bool only_active) override;
    void on_transient_mouse(Scene::GizmoEventContext& ctx) override;
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

    void on_scene_selection_changed(
        Domain::SelectionId project_id,
        const Biz::Scene::ObjectSelection& selection
    ) override;
    void on_scene_selection_transformed(
        Domain::SelectionId project_id,
        const Biz::Scene::ObjectSelection& selection
    ) override;

    void on_node_added(Scene::Node* node) override {}
    void on_node_changed(Scene::Node* node) override {}
    void on_node_removed(Scene::Node* node) override;

    Scene::ToolType type() const override
    {
        return Scene::ToolType::PlaceOnFace;
    }

    bool enabled() const override;

    /**@}*/
private:
    void build_plane_node(Scene::NodeBuilder& builder, indexed_triangle_set&& its, size_t plane_id);

    void destroy_planes_and_nodes();
    void recreate_planes_and_nodes();

    std::array<Domain::Vec3d, 2> plane_to_world_coordinates(size_t plane_id) const;
    std::array<Domain::Vec3d, 2> plane_to_world_coordinates(const Domain::Vec3d& direction, const Domain::Vec3d& point) const;

    ModelGeometryManager m_model_geometry_manager{"place_on_face_geometry"};
    ModelTriangleMeshManager m_model_triangle_mesh_manager{"place_on_face_mesh"};

    Scene::Node* m_main_node{nullptr};
    bool m_is_active{false};

    Render::Device& m_device;
    PlaterScenePresenter& m_scene_presenter;
    Biz::ProjectInteractor& m_project_interactor;
    Biz::Scene::SceneInteractor& m_scene_interactor;

    void rotate_selection(const Domain::Vec3d& direction, const Domain::Vec3d& point) const;

    std::vector<std::array<Domain::Vec3d, 2>> m_normals_and_points;
};
} // namespace Slic3r::App::Plater
