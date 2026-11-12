#pragma once

#include "Slic3r/App/Plater/HeightRangeNodeTag.hpp"
#include "Slic3r/App/Plater/LayerHeightGizmoHelper.hpp"
#include "Slic3r/App/Scene/IGizmo.hpp"
#include "Slic3r/App/Scene/ISceneChangedListener.hpp"
#include "Slic3r/App/Scene/IThumbnailRenderListener.hpp"
#include "Slic3r/App/Scene/Ray.hpp"
#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Domain/LayerHeightProfile.hpp"
#include "Slic3r/Domain/ModelObject.hpp"

#include <optional>

namespace Slic3r::Domain {
struct ConfigBox;
} // namespace Slic3r::Domain

namespace Slic3r::App::Render {
class Device;
} // namespace Slic3r::App::Render

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::Biz::Scene {
class SceneInteractor;
} // namespace Slic3r::Biz::Scene

namespace Slic3r::App::Plater {
class HeightRangeDialog;
class PlaterScenePresenter;

class HeightRangeGizmo :
    public Scene::IToolGizmo,
    public Scene::ISceneChangedListener,
    public Scene::IThumbnailRenderListener,
    public Biz::Scene::ISceneChangedListener,
    public Biz::Scene::ISceneSelectionChangedListener
{
public:
    struct SelectedObjectData
    {
        Domain::ModelObject* model_object           = nullptr;
        const Domain::ModelInstance* model_instance = nullptr;
        Domain::Vec3d model_object_center           = Domain::Vec3d::Zero();
    };

    HeightRangeGizmo() = delete;

    HeightRangeGizmo(
        Render::Device& device,
        Biz::ProjectInteractor& project_interactor,
        PlaterScenePresenter& scene_presenter
    );

    ~HeightRangeGizmo() override;

    Scene::ToolType type() const override;
    bool disable_object_selection() const override;
    bool enabled() const override;
    std::unique_ptr<Plater::GizmoWindow> release_ui_window() override;

    void on_activated() override;
    void on_deactivated() override;

    void on_project_activated(size_t new_project_id) override;
    void on_project_deactivated(size_t old_project_id) override;

    void on_scene_selection_changed(
        Domain::SelectionId project_id,
        const Biz::Scene::ObjectSelection& selection
    ) override;

    void on_node_added(Scene::Node* node) override;
    void on_node_removed(Scene::Node* node) override;

    void on_thumbnail_render_begin() override;
    void on_thumbnail_render_end() override;

    void on_model_reloaded(Domain::SelectionId project_id) override;

    Scene::GizmoActivationState on_mouse(Scene::GizmoEventContext& ctx, bool only_active) override;
    void on_transient_mouse(Scene::GizmoEventContext& ctx) override;
    void register_commands(Platform::CommandRegistry& registry) override;

    std::optional<App::Undo::ToolState> get_tool_state() const override;
    void set_tool_state(const App::Undo::ToolState& state) override;

private:
    struct ConfigBoxSetterImpl;

    struct GizmoEvent
    {
        enum class Type
        {
            LeftDown,
            Dragging,
            LeftUp
        };

        Type type;
        Scene::Ray pick_ray;
        HeightRangePlaneNodeTag::PlaneType picked_plane = HeightRangePlaneNodeTag::PlaneType::Min;
    };

    /** State of an active plane drag.
     *  Mouse position is projected onto a vertical axis through the object center
     *  and converted to a Z delta.
     */
    struct DragState
    {
        enum class DragType
        {
            MinPlane,
            MaxPlane
        };

        DragType drag_type;
        double initial_min_z       = 0.;
        double initial_max_z       = 0.;
        double initial_projected_z = 0.;

        double initial_plane_z() const
        {
            return (drag_type == DragType::MinPlane) ? initial_min_z : initial_max_z;
        }
    };

    Render::Device& m_device;
    Biz::ProjectInteractor& m_project_interactor;
    Biz::Scene::SceneInteractor& m_scene_interactor;
    PlaterScenePresenter& m_scene_presenter;

    Yoga::Passthrough<HeightRangeDialog> m_dialog;

    HeightRangePlanesWrapper m_planes_wrapper;
    SelectedObjectData m_selected_object_data;
    LayerHeightParams m_layer_height_params;
    Scene::Node::NodeList m_non_selected_volumes_nodes;
    /** Whether a variable layer height profile existed on the model when the gizmo was activated. */
    bool m_has_variable_layer_height_profile{false};

    Domain::LayerConfigRanges m_layer_config_ranges;
    std::optional<Domain::LayerHeightRange> m_selected_layer_height_range;
    std::optional<Domain::VolumeSettings> m_clipboard_height_range_settings;
    std::optional<Domain::LayerConfigRanges> m_clipboard_layer_config_ranges;

    std::optional<DragState> m_drag_state;
    std::optional<HeightRangePlaneNodeTag::PlaneType> m_hovered_plane;

    std::unique_ptr<ConfigBoxSetterImpl> m_config_setter;

    void restore_non_selected_volumes();
    void hide_non_selected_volumes();

    double get_range_layer_height(const Domain::VolumeSettings& settings) const;
    Domain::ConfigBox* selected_height_range_config_box();

    bool process_gizmo_event(const GizmoEvent& event);

    void init_main_nodes();

    void set_dialog_layer_heights_profile_parameters();
    void update_side_panel_layer_height_profile();
    void update_side_panel_height_ranges();
    void update_layer_height_profile();
    void update_layer_height_title(
        const std::optional<Domain::LayerHeightRange>& range_for_title = std::nullopt
    );

    void perform_height_range_addition();
    void perform_height_range_deletion(const Domain::LayerHeightRange& range_to_delete);
    void perform_height_range_selection(const Domain::LayerHeightRange& range_to_select);
    void perform_height_range_deselection();
    void
    perform_height_range_value_change(std::optional<double> min_z, std::optional<double> max_z);
    void perform_override_change();
    void perform_height_ranges_restart();
    void perform_single_height_range_restart(const Domain::LayerHeightRange& range_to_clear);

    void add_layer_height_override();
    void copy_height_range_overrides();
    void paste_height_range_overrides();

    void apply_layer_config_ranges_to_model();

    void rebuild_gizmo_state();
};

} // namespace Slic3r::App::Plater
