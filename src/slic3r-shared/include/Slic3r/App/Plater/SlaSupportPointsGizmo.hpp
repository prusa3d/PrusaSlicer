#pragma once

#include "Slic3r/App/Render/Material.hpp"
#include "Slic3r/App/Scene/Clipper.hpp"
#include "Slic3r/App/Scene/ClipperPresenter.hpp"
#include "Slic3r/App/Scene/IGizmo.hpp"
#include "Slic3r/App/Scene/ISceneChangedListener.hpp"
#include "Slic3r/App/Scene/IThumbnailRenderListener.hpp"
#include "Slic3r/App/Scene/Node.hpp"
#include "Slic3r/App/Undo/ToolState.hpp"
#include "Slic3r/Biz/SLAObjectCache.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Domain/SlicingId.hpp"

#include "libslic3r/PrintSteps.hpp"

#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace Slic3r::App::Render {
class Device;
class Geometry;
class ScreenInfo;
} // namespace Slic3r::App::Render

namespace Slic3r::App::Scene {
class TriangleMesh;
} // namespace Slic3r::App::Scene

namespace Slic3r::Biz {
class ProjectInteractor;
class SLAObjectRequest;
} // namespace Slic3r::Biz

namespace Slic3r::App::Plater {

class PlaterScenePresenter;
class SlaSupportPointsDialog;

/**
 * @brief Node tag of one support point (its sphere and cone) of the SLA support points gizmo.
 */
struct SlaSupportPointNodeTag
{
    size_t index;
};

/**
 * @brief Tool for editing the SLA support points of one object instance.
 *
 * The gizmo shows the object as the backend sees it (hollowed, drilled, elevated) and works on
 * that mesh. The backend is asked to slice the object until the holes are drilled when the gizmo
 * opens, and until the support points step when the points get auto-generated.
 *
 * The points are edited in a cache, each edit takes an undo snapshot carrying the cache. The cache
 * is written into the model (and so sent to slicing) on Apply and whenever the gizmo closes,
 * except when it closes because of undo/redo (the restored snapshot is already the right state).
 * The points in the cache are in model object coordinates.
 */
class SlaSupportPointsGizmo :
    public Scene::IToolGizmo,
    public Scene::ISceneChangedListener,
    public Scene::IThumbnailRenderListener,
    public Biz::Scene::ISceneChangedListener,
    public Biz::Scene::ISceneSelectionChangedListener,
    public Biz::ISLAObjectCacheChangedListener
{
public:
    SlaSupportPointsGizmo(
        Render::Device& device,
        Biz::ProjectInteractor& project_interactor,
        PlaterScenePresenter& scene_presenter
    );
    ~SlaSupportPointsGizmo() override;

    /**
     * @name Implementation of IGizmo interface
     * @{
     */
    Scene::GizmoActivationState on_mouse(Scene::GizmoEventContext& ctx, bool only_active) override;
    void register_commands(Platform::CommandRegistry& registry) override;
    void render_scene(Render::CommandBuffer& cmd_buffer) override;
    bool disable_object_selection() const override;
    void provide_gizmo_controller(Scene::IGizmoController& controller) override;
    /**@}*/

    /**
     * @name Implementation of IToolGizmo interface
     * @{
     */
    void on_activated() override;
    void on_deactivated() override;
    void on_project_activated(size_t new_project_id) override;
    void on_project_deactivated(size_t old_project_id) override;
    Scene::ToolType type() const override;
    bool supports_printer(Domain::PrinterTechnology pt) const override;
    bool enabled() const override;
    std::unique_ptr<GizmoWindow> release_ui_window() override;
    std::optional<Undo::ToolState> get_tool_state() const override;
    void set_tool_state(const Undo::ToolState& state) override;
    /**@}*/

    void on_node_added(Scene::Node* node) override;
    void on_node_removed(Scene::Node* node) override;

    void on_thumbnail_render_begin() override;
    void on_thumbnail_render_end() override;

    void on_model_reloaded(Domain::SelectionId project_id) override;
    void on_scene_selection_changed(
        Domain::SelectionId project_id,
        const Biz::Scene::ObjectSelection& selection
    ) override;

    void on_sla_object_cache_changed(const Domain::SlicingId& id, Domain::ObjectID object_id)
        override;

private:
    using Point = Undo::SlaSupportPointsGizmoState::Point;

    struct MeshHit
    {
        Domain::Vec3d position; // model object coordinates
        Domain::Vec3d normal; // model object coordinates
        double distance;
    };

    struct Rectangle
    {
        Domain::Vec2d start;
        Domain::Vec2d end;
        bool select;
    };

    // Points of a project kept while another project is selected.
    struct SavedCache
    {
        size_t object_id;
        size_t instance_id;
        std::vector<Point> points;
    };

    bool find_target();
    void init_scene();
    void release_scene();
    void reset_target();
    void activate_clipper();

    void request_backend(SLAPrintObjectStep step, bool use_cache = true);
    // Requests the steps the points of the model need: the support points if they are generated.
    void request_backend_for_model(bool use_cache);
    void load_backend_object();
    [[nodiscard]] const Domain::ModelObject* model_object() const;
    [[nodiscard]] const Domain::ModelInstance* model_instance() const;
    [[nodiscard]] bool input_enabled() const;

    void rebuild_mesh_nodes();
    void rebuild_point_nodes();
    void update_point_node(size_t index);
    void update_point_colors();
    void update_clipping();
    void update_dialog();
    void hide_plater_nodes();
    void restore_plater_nodes();

    [[nodiscard]] Domain::Transform3d points_to_world() const;
    [[nodiscard]] Domain::Vec3d point_world_normal(size_t index) const;
    [[nodiscard]] bool is_clipped(const Domain::Vec3d& world_position) const;
    [[nodiscard]] std::optional<MeshHit> raycast(const Scene::Ray& ray) const;
    [[nodiscard]] std::optional<size_t> hovered_point(const Scene::GizmoEventContext& ctx) const;
    [[nodiscard]] bool is_visible(const Domain::Vec3d& world_position) const;

    void add_point(const MeshHit& hit);
    void select_point(size_t index, bool exclusive);
    void unselect_point(size_t index);
    void select_all(bool select);
    void delete_selected(bool force);
    void select_by_rectangle(const Rectangle& rectangle, const Render::ScreenInfo& screen_info);
    void set_head_diameter(double head_diameter);
    void set_density(int density);
    void remove_all();
    void auto_generate();
    void apply();
    void flush();
    void edited(Biz::UndoSnapshotType snapshot_type);

    void render_rectangle(Render::CommandBuffer& cmd_buffer);

    Render::Device& m_device;
    Biz::ProjectInteractor& m_project_interactor;
    Biz::Scene::SceneInteractor& m_scene_interactor;
    PlaterScenePresenter& m_scene_presenter;
    Scene::IGizmoController* m_gizmo_controller = nullptr;

    std::unique_ptr<SlaSupportPointsDialog> m_dialog;
    SlaSupportPointsDialog* m_dialog_ptr = nullptr;
    std::unique_ptr<Biz::SLAObjectRequest> m_request;
    std::optional<SLAPrintObjectStep> m_requested_step;

    // Target of the gizmo, valid while active.
    bool m_active        = false;
    size_t m_object_id   = 0;
    size_t m_instance_id = 0;
    std::optional<Domain::SlicingId> m_slicing_id;
    Domain::Transform3d m_bed_trafo{Domain::Transform3d::Identity()};

    // Edit cache.
    std::vector<Point> m_points;
    bool m_dirty        = false;
    bool m_lock_islands = false;
    double m_head_diameter{0.4};

    // What the backend sent about the object (print object coordinates).
    std::shared_ptr<const Domain::TriangleMesh> m_backend_mesh;
    Domain::Transform3d m_object_trafo{Domain::Transform3d::Identity()};
    Domain::Transform3d m_instance_trafo{Domain::Transform3d::Identity()};
    double m_elevation{0.};
    Render::Material m_mesh_material;
    std::unique_ptr<Scene::TriangleMesh> m_backend_trimesh;
    std::unique_ptr<Render::Geometry> m_backend_geometry;
    // Model meshes shown (disabled) until the backend mesh is there.
    std::vector<std::unique_ptr<Render::Geometry>> m_model_geometries;

    std::unique_ptr<Scene::TriangleMesh> m_sphere_trimesh;
    std::unique_ptr<Render::Geometry> m_sphere_geometry;
    std::unique_ptr<Scene::TriangleMesh> m_cone_trimesh;
    std::unique_ptr<Render::Geometry> m_cone_geometry;

    Scene::Node* m_main_node     = nullptr;
    Scene::Node* m_mesh_node     = nullptr;
    Scene::Node* m_points_node   = nullptr;
    Scene::Node* m_clipper_node  = nullptr;
    Scene::Scene* m_scene        = nullptr;
    Scene::Node::NodeList m_hidden_nodes;
    // Sphere and cone of each point.
    std::vector<std::pair<Scene::Node*, Scene::Node*>> m_point_nodes;

    Scene::Clipper m_clipper;
    Scene::ClipperPresenter m_clipper_presenter;

    // Mouse interaction.
    std::optional<size_t> m_hovered;
    std::optional<size_t> m_dragged;
    std::optional<Point> m_point_before_drag;
    bool m_wait_for_up = false;
    std::optional<Rectangle> m_rectangle;
    std::unique_ptr<Render::Geometry> m_rectangle_geometry;
    Render::Material m_rectangle_material;

    std::map<Domain::SelectionId, SavedCache> m_saved_caches;
};

} // namespace Slic3r::App::Plater
