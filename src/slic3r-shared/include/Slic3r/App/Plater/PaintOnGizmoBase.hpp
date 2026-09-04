#pragma once

#include "Slic3r/App/Plater/MMPaintingUtils.hpp"
#include "Slic3r/App/Plater/PaintOnGizmoHelper.hpp"
#include "Slic3r/App/Scene/Clipper.hpp"
#include "Slic3r/App/Scene/ClipperPresenter.hpp"
#include "Slic3r/App/Scene/GeometryDataFactory.hpp"
#include "Slic3r/App/Scene/IGizmo.hpp"
#include "Slic3r/App/Scene/ISceneChangedListener.hpp"
#include "Slic3r/App/Scene/IThumbnailRenderListener.hpp"
#include "Slic3r/App/Scene/NodeBuilder.hpp"
#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/Biz/Algorithms/Color.hpp"
#include "Slic3r/Biz/Algorithms/TriangleSelector.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/ProjectScoped.hpp"
#include "Slic3r/Domain/FacetsAnnotation.hpp"

namespace Slic3r::App::Plater {
class PaintOnSupportsDialog;
class PlaterScenePresenter;

enum class CursorType
{
    CIRCLE,
    SPHERE,
    POINTER,
    HEIGHT_RANGE
};

class PaintOnGizmoBase :
    public Scene::IToolGizmo,
    public App::Scene::ISceneChangedListener,
    public Biz::Scene::ISceneChangedListener,
    public Biz::Scene::ISceneSelectionChangedListener,
    public Scene::IThumbnailRenderListener
{
public:
    enum class ToolType
    {
        BRUSH,
        BUCKET_FILL,
        SMART_FILL,
        HEIGHT_RANGE,
        COLOR_REPLACE,
    };

    static constexpr float SmartFillAngleMin  = 0.0f;
    static constexpr float SmartFillAngleMax  = 90.f;
    static constexpr float SmartFillAngleStep = 1.f;

    static constexpr float HeightRangeZRangeMin  = 0.1f;
    static constexpr float HeightRangeZRangeMax  = 10.f;
    static constexpr float HeightRangeZRangeStep = 0.1f;

    static constexpr float SmartFillGapArea  = 0.02f;
    static constexpr float BucketFillGapArea = 0.02f;

    static constexpr float CursorRadiusMin  = 0.4f;
    static constexpr float CursorRadiusMax  = 8.f;
    static constexpr float CursorRadiusStep = 0.2f;

    struct PaintableVolume
    {
        const Domain::ModelObject& model_object;
        const Domain::ModelInstance& model_instance;
        Domain::ModelVolume& model_volume;
        const AABBMesh& aabb_mesh;
        Domain::Transform3d world_trafo;
        Domain::Transform3d world_trafo_no_translate;
    };

    using PaintableVolumes = std::vector<PaintableVolume>;

    PaintOnGizmoBase() = delete;

    PaintOnGizmoBase(
        Render::Device& device,
        Scene::GeometryDataFactory& data_factory,
        Biz::ProjectInteractor& project_interactor,
        PlaterScenePresenter& scene_presenter
    );

    virtual float get_cursor_radius_min() const;
    virtual float get_cursor_radius_max() const;
    virtual float get_cursor_radius_step() const;

    virtual float get_cursor_edge_limit() const;

    bool disable_object_selection() const override;

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

    virtual Domain::TriangleSelector::TriangleStateType get_left_button_state_type() const  = 0;
    virtual Domain::TriangleSelector::TriangleStateType get_right_button_state_type() const = 0;

    virtual void on_cursor_radius_changed(float value) {}

    virtual void on_smart_fill_angle_changed(float value) {}

    virtual void on_bucket_fill_angle_changed(float value) {}

    virtual void on_height_range_z_range_changed(float value) {}

    virtual void on_clipping_of_view_changed(double value) {}

    bool enabled() const override;
    void provide_gizmo_controller(Scene::IGizmoController& gizmo_controller) override;

protected:
    Render::Device& m_device;
    Scene::GeometryDataFactory& m_data_factory;
    Biz::ProjectInteractor& m_project_interactor;
    Biz::Scene::SceneInteractor& m_scene_interactor;
    PlaterScenePresenter& m_scene_presenter;
    Scene::Clipper m_clipping_plane_clipper;
    Scene::Clipper m_sinking_plane_clipper;
    Scene::ClipperPresenter m_clipping_plane_presenter;
    Scene::ClipperPresenter m_sinking_plane_presenter;
    Scene::IGizmoController* m_gizmo_controller = nullptr;

    PaintableVolumes m_paintable_volumes;
    std::vector<TriangleSelectorRenderWrapper> m_triangle_selector_wrappers;

    ToolType m_tool_type = ToolType::BRUSH;
    Biz::Algorithms::TriangleSelector::CursorType m_cursor_type =
        Biz::Algorithms::TriangleSelector::CursorType::SPHERE;
    float m_cursor_radius                    = 2.f;
    float m_smart_fill_angle                 = 30.f;
    float m_bucket_fill_angle                = 90.f;
    float m_height_range_z_range             = 1.00f;
    bool m_triangle_splitting_enabled        = true;
    bool m_paint_on_overhangs_only           = false;
    float m_highlight_by_angle_threshold_deg = 0.f;

    PaintingPalette m_painting_colors;

    virtual Domain::FacetsAnnotationKind get_facets_annotation_kind() const = 0;
    virtual const Domain::FacetsAnnotation& get_facets_annotation(
        const Domain::ModelVolume& model_volume
    ) const = 0;
    virtual bool set_facets_annotation(
        Domain::ModelVolume& model_volume,
        const Biz::Algorithms::TriangleSelector& triangle_selector
    ) const = 0;

    /**
     * @brief Called after a finished painting stroke gets applied to the model.
     */
    virtual void on_painting_stroke_applied() {}

    void set_tool_type(ToolType tool_type);
    void set_cursor_type(Biz::Algorithms::TriangleSelector::CursorType cursor_type);

    void update_clipping_plane();
    void update_overhang_detection();
    void clear_all_paintings();

    void apply_painting_to_model() const;

    virtual Domain::ColorRGBA create_default_painting_color(
        const Domain::ModelVolume& model_volume
    ) const;
    virtual PaintingPalette create_painting_colors() const;
    virtual Domain::ColorRGBA get_cursor_sphere_left_button_color() const;
    virtual Domain::ColorRGBA get_cursor_sphere_right_button_color() const;
    virtual Domain::ColorRGBA get_sphere_cursor_color() const;

private:
    struct PaintOnGizmoEvent
    {
        enum class Type
        {
            Moving,
            LeftDown,
            RightDown,
            MouseWheelDown,
            Dragging,
            LeftUp,
            RightUp,
            MouseWheelUp,
        };

        const Type type;
        const Domain::Vec2d mouse_position;
        const bool shift_down;
        const bool alt_down;
        const bool ctrl_down;
    };

    struct VolumeHitPoint
    {
        Domain::Vec3d volume_hit_position =
            Domain::Vec3d::Zero(); // Hit position in local (volume) coordinates.
        int volume_idx   = -1; // Index into m_paintable_volumes (-1 = no hit).
        size_t facet_idx = 0; // Index of the facet hit within the volume.
    };

    using VolumeHitPoints = std::vector<VolumeHitPoint>;

    // Cache structure for raycast results to avoid repeated raycasting when the mouse position hasn't changed.
    struct RaycastCache
    {
        Domain::Vec2d mouse_position = Domain::Vec2d::Zero();
        VolumeHitPoint hit           = {Domain::Vec3d::Zero(), -1, 0};
    };

    enum class Button
    {
        None,
        Left,
        Right
    };

    Scene::Node* m_main_node               = nullptr;
    Scene::Node* m_cursors_node            = nullptr;
    Scene::Node* m_triangle_selectors_node = nullptr;
    Scene::Node* m_clipping_plane_presenter_node = nullptr;
    Scene::Node* m_sinking_plane_presenter_node = nullptr;

    Scene::Node::NodeList
        m_visible_volumes_nodes; // Nodes that will be hidden when the gizmo is activated and shown when deactivated.

    SphereCursorRenderWrapper m_sphere_cursor_render_wrapper;
    CircleCursorRenderWrapper m_circle_cursor_render_wrapper;
    HeightRangeCursorRenderWrapper m_height_range_cursor_render_wrapper;

    Domain::Vec2d m_last_mouse_click = Domain::Vec2d::Zero();
    Button m_button_down             = Button::None;
    bool m_mouse_dragging            = false;

    // It stores the value of the previous mesh_id to which the seed fill was applied.
    // It is used to detect when the mouse has moved from one volume to another one.
    int m_seed_fill_last_mesh_id = -1;

    mutable RaycastCache m_raycast_cache;

    bool
    process_gizmo_event(const PaintOnGizmoEvent& gizmo_event, const Scene::GizmoEventContext& ctx);

    Biz::Algorithms::TriangleSelector::ClippingPlane get_clipping_plane_in_volume_coordinates(
        const Domain::Transform3d& trafo
    ) const;

    std::vector<VolumeHitPoints> get_projected_mouse_positions(
        const Domain::Vec2d& mouse_position,
        const Scene::Camera& camera,
        double resolution
    ) const;

    bool is_mesh_point_clipped(const Domain::Vec3d& point, const Domain::Transform3d& trafo) const;

    // Updates the cached raycast result for the given mouse position.
    void
    update_raycast_cache(const Domain::Vec2d& mouse_position, const Scene::Camera& camera) const;

    // Performs the actual raycast computation (without using cache)
    VolumeHitPoint
    perform_raycast(const Domain::Vec2d& mouse_position, const Scene::Camera& camera) const;

    /**
     * @brief Unselects all triangles selected by seed fill in all volumes
     * and refreshes their painted geometry.
     */
    void seed_fill_unselect_all();

    void restore_visible_volumes();
    void hide_visible_volumes();

    void init_main_nodes();
    void init_clipper_presenters();
    void init_cursors_nodes();
    void update_cursors();
    void rebuild_paintable_geometry();
};

} // namespace Slic3r::App::Plater
