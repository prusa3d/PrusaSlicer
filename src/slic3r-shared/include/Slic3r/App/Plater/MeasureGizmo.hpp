#pragma once

#include "Slic3r/App/Scene/IGizmo.hpp"
#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Plater/MeasureGizmoHelper.hpp"
#include "Slic3r/Biz/ProjectScoped.hpp"

#define MEASURE_GIZMO_DEBUG 0

namespace Slic3r::Biz::Scene {
struct ObjectSelection;
class SceneInteractor;
} // namespace Slic3r::Biz::Scene

namespace Slic3r::App::Scene {
class NodeBuilder;
} // namespace Slic3r::App::Scene

namespace Slic3r::App::Render {
class Device;
} // namespace Slic3r::App::Render

namespace Slic3r::App::Plater {
class MeasureDialog;
class PlaterScenePresenter;

class MeasureGizmo : public Scene::IToolGizmo, public Biz::Scene::ISceneSelectionChangedListener
{
public:
    MeasureGizmo(
        Render::Device& device,
        Biz::ProjectInteractor& project_interactor,
        PlaterScenePresenter& scene_presenter
    );

    void on_activated() override;
    void on_deactivated() override;

    Scene::ToolType type() const override;
    bool enabled() const override;
    std::unique_ptr<GizmoWindow> release_ui_window() override;

    Scene::GizmoActivationState on_mouse(Scene::GizmoEventContext& ctx, bool only_active) override;
    void on_transient_mouse(Scene::GizmoEventContext& ctx) override;
    void on_keyboard(Scene::GizmoKeyEventContext& ctx) override;

    void on_project_activated(size_t new_project_id) override;
    void on_project_deactivated(size_t old_project_id) override;

    void render_scene(Render::CommandBuffer& cmd_buffer) override;

    void register_commands(Platform::CommandRegistry& registry) override;

    bool disable_object_selection() const override { return true; }

    void on_scene_selection_changed(
        Domain::SelectionId project_id,
        const Biz::Scene::ObjectSelection& selection
    ) override;
    void on_scene_selection_transformed(
        Domain::SelectionId project_id,
        const Biz::Scene::ObjectSelection& selection
    ) override;

    // DEBUG ONLY
    void render_imgui() override;

private:
    void reset();
    void update_scene_selection_cache_state(
        const Domain::ElementRefs& removed_volumes,
        const Domain::ElementRefs& added_volumes
    );
    void update_scene_selection_cache_measuring_geometry();
    void update_feature_detection_data(const Scene::Node* scene_node, const Scene::GizmoEventContext& ctx);
    std::optional<Measure::FeatureItem> detect_current_feature();
    void update_current_feature_on_scene();
    void update_measurement();
    void update_measurement_result();
    void update_dimensioning();
    void update_linear_dimensioning();
    void update_angular_dimensioning();
    void update_arc_edge_edge_dimensioning(
        const Measure::SurfaceFeature& f1,
        const Measure::SurfaceFeature& f2
    );
    void update_arc_edge_plane_dimensioning(
        const Measure::SurfaceFeature& f1,
        const Measure::SurfaceFeature& f2
    );
    void update_arc_plane_plane_dimensioning(
        const Measure::SurfaceFeature& f1,
        const Measure::SurfaceFeature& f2
    );
    void update_ui_dialog();
    void update_highlight();
    void clear_scene();
    void clear_features();

    void handle_left_click_on_current_feature(Scene::Node& feature_node);
    void handle_left_click_on_first_selected_feature(Scene::Node& feature_node);
    void handle_left_click_on_second_selected_feature(Scene::Node& feature_node);
    void handle_left_click_on_first_circle_center_feature(Scene::Node& feature_node);
    void handle_left_click_on_second_circle_center_feature(Scene::Node& feature_node);

    void handle_hover_first_selected_feature(Scene::Node& feature_node);
    void handle_hover_second_selected_feature(Scene::Node& feature_node);
    void handle_hover_first_circle_center_feature(Scene::Node& feature_node);
    void handle_hover_second_circle_center_feature(Scene::Node& feature_node);

    void add_feature_to_scene(
        const Measure::SurfaceFeature& feature,
        Measure::MeasureGizmoElementType type,
        const Domain::ElementRef& ref,
        const std::string& debug_name,
        const Domain::ColorRGBA& color,
        const Measure::Measuring& measuring,
        Scene::Node& parent_node
    );
    void remove_feature_from_scene(Measure::MeasureGizmoElementType type);

    void build_point_feature(
        Scene::NodeBuilder& builder,
        const Measure::SurfaceFeature& feature,
        const Domain::ColorRGBA& color
    );
    void build_edge_feature(
        Scene::NodeBuilder& builder,
        const Measure::SurfaceFeature& feature,
        const Domain::ColorRGBA& color
    );
    void build_circle_feature(
        Scene::NodeBuilder& builder,
        const Measure::SurfaceFeature& feature,
        const Domain::ColorRGBA& color
    );
    void build_plane_feature(
        Scene::NodeBuilder& builder,
        Domain::ElementRef ref,
        const Measure::SurfaceFeature& feature,
        const Measure::Measuring& measuring,
        const Domain::ColorRGBA& color
    );

    void build_dimensioning_node();
    void build_linear_dimensioning_node(Scene::NodeBuilder& builder);
    void build_angular_dimensioning_node(Scene::NodeBuilder& builder);

    Render::Material dimensioning_material();

private:
    enum class SelectionMode : uint8_t
    {
        Feature,
        Point
    };

    Render::Device& m_device;
    Biz::ProjectInteractor& m_project_interactor;
    Biz::Scene::SceneInteractor& m_scene_interactor;
    PlaterScenePresenter& m_scene_presenter;

    SelectionMode m_selection_mode{SelectionMode::Feature};
    bool m_mouse_left_down{false};
    std::optional<Measure::FeatureDetectionData> m_feature_detection_data;

    struct DimensioningNodes
    {
        Scene::Node* main{nullptr};
        Scene::Node* linear{nullptr};
        Scene::Node* angular{nullptr};

        void reset()
        {
            main    = nullptr;
            linear  = nullptr;
            angular = nullptr;
        }
    };

    using GeometryManager     = Render::GeometryManager<std::string>;
    using TriangleMeshManager = Scene::TriangleMeshManager<std::string>;

    struct ProjectContext
    {
        size_t id{Domain::INVALID_ID};
        bool activated{false};
        Scene::Node* main_node{nullptr};
        Scene::Node* features_node{nullptr};
        DimensioningNodes dimensioning_nodes;
        GeometryManager geometry_manager{"measure_gizmo_geometry"};
        TriangleMeshManager triangle_mesh_manager{"measure_gizmo_mesh"};
        Measure::SceneSelectionCache scene_selection_cache;
        Measure::FeatureCache feature_cache;
    };

    using ProjectContexts = Biz::ProjectScoped<ProjectContext>;
    ProjectContexts m_projects;
    ProjectContext* m_current_project{nullptr};

    Measure::MeasurementResult m_measurement_result;

    Yoga::Passthrough<MeasureDialog> m_dialog;
};

} // namespace Slic3r::App::Plater
