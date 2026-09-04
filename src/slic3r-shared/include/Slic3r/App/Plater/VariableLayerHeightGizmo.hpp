#pragma once

#include "Slic3r/App/Plater/LayerHeightGizmoHelper.hpp"
#include "Slic3r/App/Scene/IGizmo.hpp"
#include "Slic3r/App/Scene/ISceneChangedListener.hpp"
#include "Slic3r/App/Scene/IThumbnailRenderListener.hpp"
#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/Biz/Algorithms/LayerHeight.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Domain/ModelInstance.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/ModelVolume.hpp"
#include "Slic3r/Domain/Transformation.hpp"

#include <optional>

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
class VariableLayerHeightDialog;
class PlaterScenePresenter;

class VariableLayerHeightGizmo :
    public Scene::IToolGizmo,
    public Scene::ISceneChangedListener,
    public Scene::IThumbnailRenderListener,
    public Biz::Scene::ISceneChangedListener,
    public Biz::Scene::ISceneSelectionChangedListener
{
public:
    struct SelectedObjectData
    {
        struct Volume
        {
            const Domain::ModelVolume& model_volume;
            const AABBMesh& aabb_mesh;
            Domain::Transform3d world_trafo;
        };

        using Volumes = std::vector<Volume>;

        Domain::ModelObject* model_object           = nullptr;
        const Domain::ModelInstance* model_instance = nullptr;

        Volumes volumes;
    };

    VariableLayerHeightGizmo() = delete;

    VariableLayerHeightGizmo(
        Render::Device& device,
        Biz::ProjectInteractor& project_interactor,
        PlaterScenePresenter& scene_presenter
    );

    ~VariableLayerHeightGizmo() override;

    Scene::ToolType type() const override;
    bool disable_object_selection() const override;
    bool enabled() const override;
    std::unique_ptr<GizmoWindow> release_ui_window() override;
    void provide_gizmo_controller(Scene::IGizmoController& gizmo_controller) override;

    void on_activated() override;
    void on_deactivated() override;

    void on_project_activated(size_t new_project_id) override;
    void on_project_deactivated(size_t old_project_id) override;

    void on_node_added(Scene::Node* node) override;
    void on_node_removed(Scene::Node* node) override;

    void on_thumbnail_render_begin() override;
    void on_thumbnail_render_end() override;

    void on_model_reloaded(Domain::SelectionId project_id) override;

    void on_scene_selection_changed(
        Domain::SelectionId project_id,
        const Biz::Scene::ObjectSelection& selection
    ) override;

    Scene::GizmoActivationState on_mouse(Scene::GizmoEventContext& ctx, bool only_active) override;
    void render_scene(Render::CommandBuffer& cmd_buffer) override;

private:
    struct VolumeHitPoint
    {
        /**
         * Hit position world coordinates.
         */
        Domain::Vec3d world_hit_position = Domain::Vec3d::Zero();
        /**
         * Index into m_selected_object_data.volumes (-1 = no hit).
         */
        int volume_idx = -1;
    };

    enum class Button
    {
        None,
        Left,
        Right
    };

    struct GizmoEvent
    {
        enum class Type
        {
            Moving,
            LeftDown,
            RightDown,
            Dragging,
            LeftUp,
            RightUp,
            Wheel
        };

        Type type = Type::Moving;
        std::optional<float> cursor_z;
        bool shift_down       = false;
        bool ctrl_down        = false;
        bool left_button_down = false;
        float wheel_delta     = 0.f;
    };

    Render::Device& m_device;
    Biz::ProjectInteractor& m_project_interactor;
    Biz::Scene::SceneInteractor& m_scene_interactor;
    PlaterScenePresenter& m_scene_presenter;
    Scene::IGizmoController* m_gizmo_controller = nullptr;

    Yoga::Passthrough<VariableLayerHeightDialog> m_dialog;

    Scene::Node* m_main_node = nullptr;
    Scene::Node* m_mesh_node = nullptr;

    /**
     * Nodes that will be hidden when the gizmo is activated and shown when deactivated.
     */
    Scene::Node::NodeList m_visible_volumes_nodes;
    SelectedObjectData m_selected_object_data;
    LayerHeightParams m_layer_height_params;
    LayerHeightMaterialWrapper m_material_wrapper;

    double m_smart_resolution = 0.5;
    int m_blend_distance      = 5;
    bool m_lock_high_detail   = false;
    double m_band_width       = 2.;

    Button m_mouse_button_down = Button::None;
    bool m_mouse_dragging      = false;

    bool m_last_shift_down = false;
    bool m_last_ctrl_down  = false;
    std::optional<float> m_last_cursor_z;
    Domain::ZHeightPairs m_baseline_layer_height_profile;

    void restore_visible_volumes();
    void hide_visible_volumes();

    void set_dialog_layer_heights_profile_parameters();
    void init_main_nodes();
    void init_mesh_nodes();
    void update_variable_layer_height_texture();
    void refresh_mesh_nodes_material();

    void perform_layer_height_profile_adjustment(
        float layer_z_abs,
        Biz::Algorithms::LayerHeight::AdjustAction action
    );
    void perform_layer_height_profile_smoothing();
    void perform_layer_height_profile_reset();
    void perform_layer_height_profile_clamping();
    void generate_adaptive_layer_height_profile();

    void apply_layer_height_profile_to_model() const;
    void clear_layer_height_profile_on_model() const;

    void update_side_panel_layer_height_profile();
    void update_side_panel_height_ranges();
    void set_cursor_z(std::optional<float> cursor_z);

    float get_layer_height_at_z(float z) const;

    bool process_gizmo_event(const GizmoEvent& event);

    VolumeHitPoint
    perform_raycast(const Domain::Vec2d& mouse_position, const Scene::Camera& camera) const;

    void rebuild_gizmo_state();
};

} // namespace Slic3r::App::Plater
