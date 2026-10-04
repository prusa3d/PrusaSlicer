#pragma once

#include "Slic3r/App/Render/Material.hpp"
#include "Slic3r/App/Scene/Clipper.hpp"
#include "Slic3r/App/Scene/ClipperPresenter.hpp"
#include "Slic3r/App/Scene/IGizmo.hpp"
#include "Slic3r/App/Scene/ISceneChangedListener.hpp"
#include "Slic3r/App/Scene/IThumbnailRenderListener.hpp"
#include "Slic3r/App/Scene/Node.hpp"
#include "Slic3r/App/Undo/ToolState.hpp"
#include "Slic3r/Biz/IUndoProvider.hpp"
#include "Slic3r/Biz/SLAObjectCache.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Domain/SlicingId.hpp"

#include "libslic3r/PrintSteps.hpp"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::App::Render {
class Device;
class Geometry;
} // namespace Slic3r::App::Render

namespace Slic3r::App::Scene {
class TriangleMesh;
} // namespace Slic3r::App::Scene

namespace Slic3r::Biz {
class ProjectInteractor;
class SLAObjectRequest;
} // namespace Slic3r::Biz

namespace Slic3r::Biz::Slicing::Sla {
struct Object;
} // namespace Slic3r::Biz::Slicing::Sla

namespace Slic3r::App::Plater {

class PlaterScenePresenter;

/**
 * @brief Node tag of the nodes of one item (point, hole) of an SLA gizmo.
 */
struct SlaGizmoItemNodeTag
{
    size_t index;
};

/**
 * @brief Base of the SLA gizmos editing items on the surface of one object instance (support
 * points, drain holes).
 *
 * The gizmo shows the object as the backend has it (hollowed, drilled, elevated) and works on
 * that mesh: it asks the backend to slice the object until the step the derived gizmo needs.
 * Until the mesh is there, the model is shown greyed out and input is disabled.
 *
 * The items are edited in a cache in model object coordinates, each edit takes an undo snapshot
 * carrying the cache as the tool state. The cache is written into the model (and so sent to
 * slicing) on Apply and whenever the gizmo closes, except when it closes because of undo/redo
 * (the restored snapshot is already the right state). Items not applied when another project
 * gets selected wait for its return.
 *
 * Mouse: left click adds an item on the mesh, dragging moves it, right click deletes it,
 * Shift/Alt click or drag selects/deselects (by rectangle). Delete, Ctrl+A and Enter are commands.
 */
class SlaGizmoBase :
    public Scene::IToolGizmo,
    public Scene::ISceneChangedListener,
    public Scene::IThumbnailRenderListener,
    public Biz::Scene::ISceneChangedListener,
    public Biz::Scene::ISceneSelectionChangedListener,
    public Biz::ISLAObjectCacheChangedListener
{
public:
    SlaGizmoBase(
        Render::Device& device,
        Biz::ProjectInteractor& project_interactor,
        PlaterScenePresenter& scene_presenter
    );
    ~SlaGizmoBase() override;

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
    bool supports_printer(Domain::PrinterTechnology pt) const override;
    bool enabled() const override;
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

protected:
    struct MeshHit
    {
        Domain::Vec3d position; // model object coordinates
        Domain::Vec3d normal; // model object coordinates
        Domain::Vec3d print_normal; // print object coordinates, as the backend mesh has it
        double distance;
    };

    // Edits taking an undo snapshot, the derived gizmo names them.
    enum class Edit
    {
        Add,
        Delete,
        Move,
        RemoveAll,
        Apply
    };

    // A mesh every item is rendered with (e.g. sphere and cone of a point).
    struct ItemMesh
    {
        const Render::Geometry* geometry;
        const Scene::TriangleMesh* trimesh;
    };

    /**
     * @name Items, implemented by the derived gizmo
     * @{
     */
    [[nodiscard]] virtual size_t item_count() const                 = 0;
    [[nodiscard]] virtual bool item_selected(size_t index) const    = 0;
    virtual void set_item_selected(size_t index, bool selected)     = 0;
    [[nodiscard]] virtual Domain::Vec3d item_position(size_t index) const = 0;
    // Direction from the item out of the surface in world coordinates, zero if unknown.
    [[nodiscard]] virtual Domain::Vec3d item_out_direction(size_t index) const = 0;
    virtual void add_item(const MeshHit& hit) = 0;
    // Returns false if the item can't be moved.
    virtual bool move_item(size_t index, const MeshHit& hit) = 0;
    // Erases the selected items (unless locked and not forced), returns the number erased.
    virtual size_t erase_selected_items(bool force) = 0;
    virtual void clear_items()                      = 0;
    // Takes the items from the model or the backend object (null if there is none).
    virtual void load_items(const Biz::Slicing::Sla::Object* backend_object) = 0;
    // Writes the cache into the model object.
    virtual void write_items(Domain::ModelObject& model_object) const = 0;
    // The state of an item before it is dragged, to tell whether it moved.
    virtual void remember_item(size_t index)               = 0;
    [[nodiscard]] virtual bool item_changed(size_t index) const = 0;

    [[nodiscard]] virtual std::vector<ItemMesh> item_meshes() const = 0;
    // Transformations of the item meshes in world coordinates, nullopt hides the mesh.
    [[nodiscard]] virtual std::vector<std::optional<Domain::Transform3d>>
    item_mesh_trafos(size_t index) const = 0;
    [[nodiscard]] virtual Domain::ColorRGBA item_color(size_t index) const = 0;
    virtual void on_item_selected(size_t index) {}
    /**@}*/

    /**
     * @name Gizmo specifics, implemented by the derived gizmo
     * @{
     */
    // The step the backend has to reach when the gizmo shows the object.
    [[nodiscard]] virtual SLAPrintObjectStep step_to_show() const = 0;
    // The step the backend has to reach after Apply.
    [[nodiscard]] virtual SLAPrintObjectStep step_after_apply() const = 0;
    [[nodiscard]] virtual Biz::UndoSnapshotType snapshot_type(Edit edit) const = 0;
    [[nodiscard]] virtual const char* command_prefix() const = 0;
    virtual void on_target_activated() {}
    virtual void update_dialog()             = 0;
    virtual void reset_dialog_clipping()     = 0;
    /**@}*/

    void request_backend(SLAPrintObjectStep step, bool use_cache = true);
    void load_backend_object();
    void rebuild_item_nodes();
    void update_item_node(size_t index);
    void update_item_colors();
    void select_all(bool select);
    void delete_selected(bool force);
    void remove_all();
    void apply();
    void flush();
    void edited(Edit edit);
    // An edit of the cache specific to the derived gizmo.
    void edited(Biz::UndoSnapshotType snapshot_type);
    // Drops the unapplied edits (the derived gizmo reloads the items).
    void discard_edits() { m_dirty = false; }
    void set_clipping_position(double position);
    void reset_clipping_direction();

    // Restores the state saved by an undo snapshot or a project switch. With unapplied items the
    // derived gizmo puts them into the cache first, otherwise they are reloaded.
    void restore_items(bool dirty);

    [[nodiscard]] const Domain::ModelObject* model_object() const;
    [[nodiscard]] const Domain::ModelInstance* model_instance() const;
    [[nodiscard]] bool input_enabled() const;
    [[nodiscard]] bool active() const { return m_active; }
    [[nodiscard]] bool dirty() const { return m_dirty; }
    [[nodiscard]] size_t object_id() const { return m_object_id; }
    [[nodiscard]] bool has_slicing_target() const { return m_slicing_id.has_value(); }
    [[nodiscard]] const std::optional<SLAPrintObjectStep>& requested_step() const
    {
        return m_requested_step;
    }
    [[nodiscard]] std::optional<size_t> hovered() const { return m_hovered; }
    // The status line for the states common to all SLA gizmos, empty if none applies.
    [[nodiscard]] std::string common_status() const;

    [[nodiscard]] Domain::Transform3d items_to_world() const;
    // Normal of the backend mesh closest to the point (model object coordinates), in world.
    [[nodiscard]] Domain::Vec3d surface_world_normal(const Domain::Vec3d& model_position) const;
    // A direction in model object coordinates in world, as the backend transforms it.
    [[nodiscard]] Domain::Vec3d model_direction_to_world(const Domain::Vec3d& direction) const;
    // A direction in print object coordinates in model object coordinates (inverse of above).
    [[nodiscard]] Domain::Vec3d print_direction_to_model(const Domain::Vec3d& direction) const;
    [[nodiscard]] bool is_clipped(const Domain::Vec3d& world_position) const;

    Render::Device& m_device;
    Biz::ProjectInteractor& m_project_interactor;
    Biz::Scene::SceneInteractor& m_scene_interactor;
    PlaterScenePresenter& m_scene_presenter;

private:
    struct Rectangle
    {
        Domain::Vec2d start;
        Domain::Vec2d end;
        bool select;
    };

    // Unapplied items of a project kept while another project is selected.
    struct SavedCache
    {
        size_t object_id;
        size_t instance_id;
        Undo::ToolState state;
    };

    bool find_target();
    void init_scene();
    void release_scene();
    void reset_target();
    void activate_clipper();
    void rebuild_mesh_nodes();
    void update_clipping();
    void hide_plater_nodes();
    void restore_plater_nodes();

    [[nodiscard]] std::optional<MeshHit> raycast(const Scene::Ray& ray) const;
    [[nodiscard]] std::optional<size_t> hovered_item(const Scene::GizmoEventContext& ctx) const;
    [[nodiscard]] bool is_visible(const Domain::Vec3d& world_position) const;

    void select_item(size_t index, bool exclusive);
    void unselect_item(size_t index);
    void select_by_rectangle(const Rectangle& rectangle);
    void render_rectangle(Render::CommandBuffer& cmd_buffer);

    Scene::IGizmoController* m_gizmo_controller = nullptr;
    std::unique_ptr<Biz::SLAObjectRequest> m_request;
    std::optional<SLAPrintObjectStep> m_requested_step;

    // Target of the gizmo, valid while active.
    bool m_active        = false;
    size_t m_object_id   = 0;
    size_t m_instance_id = 0;
    std::optional<Domain::SlicingId> m_slicing_id;
    Domain::Transform3d m_bed_trafo{Domain::Transform3d::Identity()};

    // Items edited, but not written into the model yet.
    bool m_dirty = false;

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

    Scene::Node* m_main_node    = nullptr;
    Scene::Node* m_mesh_node    = nullptr;
    Scene::Node* m_items_node   = nullptr;
    Scene::Node* m_clipper_node = nullptr;
    Scene::Scene* m_scene       = nullptr;
    Scene::Node::NodeList m_hidden_nodes;
    // The nodes of each item, one per item mesh.
    std::vector<std::vector<Scene::Node*>> m_item_nodes;

    Scene::Clipper m_clipper;
    Scene::ClipperPresenter m_clipper_presenter;

    // Mouse interaction.
    std::optional<size_t> m_hovered;
    std::optional<size_t> m_dragged;
    bool m_wait_for_up = false;
    std::optional<Rectangle> m_rectangle;
    std::unique_ptr<Render::Geometry> m_rectangle_geometry;
    Render::Material m_rectangle_material;

    std::map<Domain::SelectionId, SavedCache> m_saved_caches;
};

} // namespace Slic3r::App::Plater
