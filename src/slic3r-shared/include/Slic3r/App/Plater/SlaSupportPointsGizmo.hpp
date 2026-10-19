#pragma once

#include "Slic3r/App/Plater/SlaGizmoBase.hpp"

#include <memory>
#include <optional>
#include <vector>

namespace Slic3r::App::Plater {

class SlaSupportPointsDialog;

/**
 * @brief Tool for editing the SLA support points of one object instance.
 *
 * The backend is asked to slice the object until the holes are drilled when the gizmo opens, and
 * until the support points step when the points are generated. Generated points are shown only
 * after the user asked for them (Auto-generate), as in the old gizmo. Applied points are user
 * modified: the backend uses them as they are.
 */
class SlaSupportPointsGizmo : public SlaGizmoBase
{
public:
    SlaSupportPointsGizmo(
        Render::Device& device,
        Biz::ProjectInteractor& project_interactor,
        PlaterScenePresenter& scene_presenter
    );
    ~SlaSupportPointsGizmo() override;

    Scene::ToolType type() const override;
    std::unique_ptr<GizmoWindow> release_ui_window() override;
    std::optional<Undo::ToolState> get_tool_state() const override;
    void set_tool_state(const Undo::ToolState& state) override;

protected:
    [[nodiscard]] size_t item_count() const override;
    [[nodiscard]] bool item_selected(size_t index) const override;
    void set_item_selected(size_t index, bool selected) override;
    [[nodiscard]] Domain::Vec3d item_position(size_t index) const override;
    [[nodiscard]] Domain::Vec3d item_out_direction(size_t index) const override;
    void add_item(const MeshHit& hit) override;
    bool move_item(size_t index, const MeshHit& hit) override;
    size_t erase_selected_items(bool force) override;
    void clear_items() override;
    void load_items(const Biz::Slicing::Sla::Object* backend_object) override;
    void write_items(Domain::ModelObject& model_object) const override;
    void remember_item(size_t index) override;
    [[nodiscard]] bool item_changed(size_t index) const override;
    [[nodiscard]] std::vector<ItemMesh> item_meshes() const override;
    [[nodiscard]] std::vector<std::optional<Domain::Transform3d>>
    item_mesh_trafos(size_t index) const override;
    [[nodiscard]] Domain::ColorRGBA item_color(size_t index) const override;
    void on_item_selected(size_t index) override;

    [[nodiscard]] SLAPrintObjectStep step_to_show() const override;
    [[nodiscard]] SLAPrintObjectStep step_after_apply() const override;
    [[nodiscard]] Biz::UndoSnapshotType snapshot_type(Edit edit) const override;
    [[nodiscard]] const char* command_prefix() const override;
    void on_target_activated() override;
    void update_dialog() override;
    void reset_dialog_clipping() override;

private:
    using Point = Undo::SlaSupportPointsGizmoState::Point;

    void set_head_diameter(double head_diameter);
    void set_density(int density);
    void auto_generate();

    std::unique_ptr<SlaSupportPointsDialog> m_dialog;
    SlaSupportPointsDialog* m_dialog_ptr = nullptr;

    std::vector<Point> m_points;
    std::optional<Point> m_point_before_drag;
    bool m_lock_islands = false;
    double m_head_diameter{0.4};

    std::unique_ptr<Scene::TriangleMesh> m_sphere_trimesh;
    std::unique_ptr<Render::Geometry> m_sphere_geometry;
    std::unique_ptr<Scene::TriangleMesh> m_cone_trimesh;
    std::unique_ptr<Render::Geometry> m_cone_geometry;
};

} // namespace Slic3r::App::Plater
