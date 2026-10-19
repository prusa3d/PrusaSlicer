#pragma once

#include "Slic3r/App/Plater/SlaGizmoBase.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::Domain {
class ConfigItem;
struct ConfigValue;
} // namespace Slic3r::Domain

namespace Slic3r::App::Plater {

class SlaHollowDialog;

/**
 * @brief Tool for hollowing one object instance and editing its drain holes.
 *
 * As in the old gizmo, the backend is asked only for the assembled object when the gizmo opens;
 * Apply and a change of the hollowing settings slice it until the holes are drilled, so the
 * result is shown. The hollowing settings are object overrides, set right away; the holes are
 * edited in the cache of SlaGizmoBase.
 */
class SlaHollowGizmo : public SlaGizmoBase
{
public:
    SlaHollowGizmo(
        Render::Device& device,
        Biz::ProjectInteractor& project_interactor,
        PlaterScenePresenter& scene_presenter
    );
    ~SlaHollowGizmo() override;

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
    using Hole = Undo::SlaHollowGizmoState::Hole;

    void set_hole_size(double diameter, double depth);
    // Sets an object override of a hollowing option and shows the result.
    void set_hollowing_option(const std::string& key, const Domain::ConfigValue& value);
    [[nodiscard]] const Domain::ConfigItem* hollowing_option(const std::string& key) const;

    std::unique_ptr<SlaHollowDialog> m_dialog;
    SlaHollowDialog* m_dialog_ptr = nullptr;

    std::vector<Hole> m_holes;
    std::optional<Hole> m_hole_before_drag;
    double m_hole_diameter{4.};
    double m_hole_depth{6.};

    std::unique_ptr<Scene::TriangleMesh> m_cylinder_trimesh;
    std::unique_ptr<Render::Geometry> m_cylinder_geometry;
};

} // namespace Slic3r::App::Plater
