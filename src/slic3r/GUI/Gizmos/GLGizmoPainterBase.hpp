///|/ Copyright (c) Prusa Research 2019 - 2023 Pavel Mikuš @Godrak, Lukáš Matěna @lukasmatena, Enrico Turri @enricoturri1966, Vojtěch Bubník @bubnikv, Lukáš Hejl @hejllukas, Filip Sykala @Jony01
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_GLGizmoPainterBase_hpp_
#define slic3r_GLGizmoPainterBase_hpp_

#include "GLGizmoBase.hpp"

#include "slic3r/GUI/GLModel.hpp"

#include "libslic3r/ObjectID.hpp"
#include "libslic3r/TriangleSelector.hpp"
#include "libslic3r/Model.hpp"

#include <cereal/types/vector.hpp>
#include <GL/glew.h>

#include <memory>
#include <wx/string.h>


namespace Slic3r::GUI {

enum class SLAGizmoEventType : unsigned char;
class ClippingPlane;
struct Camera;
class GLGizmoMmuSegmentation;
class Selection;

enum class PainterGizmoType {
    FDM_SUPPORTS,
    SEAM,
    MM_SEGMENTATION,
    FUZZY_SKIN
};

class TriangleSelectorGUI : public TriangleSelector {
public:
    explicit TriangleSelectorGUI(const TriangleMesh& mesh)
        : TriangleSelector(mesh) {}
    virtual ~TriangleSelectorGUI() = default;

    virtual void render(ImGuiWrapper* imgui, const Transform3d& matrix);
    void         render(const Transform3d& matrix) { this->render(nullptr, matrix); }

    void request_update_render_data() { m_update_render_data = true; }

#ifdef PRUSASLICER_TRIANGLE_SELECTOR_DEBUG
    void render_debug(ImGuiWrapper* imgui);
    bool m_show_triangles{false};
    bool m_show_invalid{false};
#endif // PRUSASLICER_TRIANGLE_SELECTOR_DEBUG

protected:
    bool m_update_render_data = false;

    static ColorRGBA get_seed_fill_color(const ColorRGBA& base_color);

private:
    void update_render_data();

    GLModel                m_iva_enforcers;
    GLModel                m_iva_blockers;
    std::array<GLModel, 3> m_iva_seed_fills;
#ifdef PRUSASLICER_TRIANGLE_SELECTOR_DEBUG
    std::array<GLModel, 3> m_varrays;
#endif // PRUSASLICER_TRIANGLE_SELECTOR_DEBUG

protected:
    GLModel m_paint_contour;

    void update_paint_contour();
    void render_paint_contour(const Transform3d& matrix);
};


// Following class is a base class for a gizmo with ability to paint on mesh
// using circular blush (such as FDM supports gizmo and seam painting gizmo).
// The purpose is not to duplicate code related to mesh painting.
class GLGizmoPainterBase : public GLGizmoBase
{
private:
    ObjectID m_old_mo_id;
    size_t m_old_volumes_size = 0;
    void on_render() override {}

public:
    GLGizmoPainterBase(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
    ~GLGizmoPainterBase() override;
    void data_changed(bool is_serializing) override;
    virtual bool gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down);

    // Following function renders the triangles and cursor. Having this separated
    // from usual on_render method allows to render them before transparent
    // objects, so they can be seen inside them. The usual on_render is called
    // after all volumes (including transparent ones) are rendered.
    virtual void render_painter_gizmo() = 0;

    virtual float get_cursor_radius_min() const { return CursorRadiusMin; }
    virtual float get_cursor_radius_max() const { return CursorRadiusMax; }
    virtual float get_cursor_radius_step() const { return CursorRadiusStep; }

    /// <summary>
    /// Implement when want to process mouse events in gizmo
    /// Click, Right click, move, drag, ...
    /// </summary>
    /// <param name="mouse_event">Keep information about mouse click</param>
    /// <returns>Return True when use the information and don't want to
    /// propagate it otherwise False.</returns>
    bool on_mouse(const wxMouseEvent &mouse_event) override;

protected:
    virtual void render_triangles(const Selection& selection) const;

    void render_cursor();
    void render_cursor_circle();
    void render_cursor_sphere(const Transform3d &trafo) const;
    void render_cursor_height_range(const Transform3d &trafo) const;

    virtual void update_model_object() const = 0;
    virtual void update_from_model_object() = 0;

    virtual ColorRGBA get_cursor_sphere_left_button_color() const  { return { 0.0f, 0.0f, 1.0f, 0.25f }; }
    virtual ColorRGBA get_cursor_sphere_right_button_color() const { return { 1.0f, 0.0f, 0.0f, 0.25f }; }

    virtual TriangleStateType get_left_button_state_type() const { return TriangleStateType::ENFORCER; }
    virtual TriangleStateType get_right_button_state_type() const { return TriangleStateType::BLOCKER; }

    float m_cursor_radius = 2.f;
    static constexpr float CursorRadiusMin  = 0.4f; // cannot be zero
    static constexpr float CursorRadiusMax  = 8.f;
    static constexpr float CursorRadiusStep = 0.2f;

    // For each model-part volume, store status and division of the triangles.
    std::vector<std::unique_ptr<TriangleSelectorGUI>> m_triangle_selectors;

    TriangleSelector::CursorType m_cursor_type = TriangleSelector::CursorType::SPHERE;

    enum class ToolType {
        BRUSH,
        BUCKET_FILL,
        SMART_FILL,
        HEIGHT_RANGE
    };

    struct ProjectedMousePosition
    {
        Vec3f  mesh_hit;
        int    mesh_idx;
        size_t facet_idx;
    };

    bool     m_triangle_splitting_enabled = true;
    ToolType m_tool_type                  = ToolType::BRUSH;
    float    m_smart_fill_angle           = 30.f;
    float    m_bucket_fill_angle          = 90.f;
    float    m_height_range_z_range       = 1.00f;

    bool     m_paint_on_overhangs_only          = false;
    float    m_highlight_by_angle_threshold_deg = 0.f;

    GLModel m_circle;
    Vec2d m_old_center{ Vec2d::Zero() };
    float m_old_cursor_radius{ 0.0f };

    static constexpr float SmartFillAngleMin  = 0.0f;
    static constexpr float SmartFillAngleMax  = 90.f;
    static constexpr float SmartFillAngleStep = 1.f;

    static constexpr float HeightRangeZRangeMin  = 0.1f;
    static constexpr float HeightRangeZRangeMax  = 10.f;
    static constexpr float HeightRangeZRangeStep = 0.1f;

    static constexpr float SmartFillGapArea  = 0.02f;
    static constexpr float BucketFillGapArea = 0.02f;

    // It stores the value of the previous mesh_id to which the seed fill was applied.
    // It is used to detect when the mouse has moved from one volume to another one.
    int      m_seed_fill_last_mesh_id     = -1;

    enum class Button {
        None,
        Left,
        Right
    };

    struct ClippingPlaneDataWrapper
    {
        std::array<float, 4> clp_dataf;
        std::array<float, 2> z_range;
    };

    ClippingPlaneDataWrapper get_clipping_plane_data() const;

    TriangleSelector::ClippingPlane get_clipping_plane_in_volume_coordinates(const Transform3d &trafo) const;

private:
    std::vector<std::vector<ProjectedMousePosition>> get_projected_mouse_positions(const Vec2d &mouse_position, double resolution, const std::vector<Transform3d> &trafo_matrices) const;

    bool is_mesh_point_clipped(const Vec3d& point, const Transform3d& trafo) const;
    void update_raycast_cache(const Vec2d& mouse_position,
                              const Camera& camera,
                              const std::vector<Transform3d>& trafo_matrices) const;

    static std::shared_ptr<GLModel> s_sphere;

    bool m_schedule_update = false;
    Vec2d m_last_mouse_click = Vec2d::Zero();

    Button m_button_down = Button::None;
    EState m_old_state = Off; // to be able to see that the gizmo has just been closed (see on_set_state)

    // Following cache holds result of a raycast query. The queries are asked
    // during rendering the sphere cursor and painting, this saves repeated
    // raycasts when the mouse position is the same as before.
    struct RaycastResult {
        Vec2d mouse_position;
        int mesh_id;
        Vec3f hit;
        size_t facet;
    };
    mutable RaycastResult m_rr = {Vec2d::Zero(), -1, Vec3f::Zero(), 0};

protected:
    void on_set_state() override;
    virtual void on_opening() = 0;
    virtual void on_shutdown() = 0;
    virtual PainterGizmoType get_painter_type() const = 0;

    bool on_is_activable() const override;
    bool on_is_selectable() const override;
    void on_load(cereal::BinaryInputArchive& ar) override;
    void on_save(cereal::BinaryOutputArchive& ar) const override {}
    CommonGizmosDataID on_get_requirements() const override;
    bool wants_enter_leave_snapshots() const override { return true; }

    virtual wxString handle_snapshot_action_name(bool shift_down, Button button_down) const = 0;

    friend class ::Slic3r::GUI::GLGizmoMmuSegmentation;
};


} // namespace Slic3r::GUI

#endif // slic3r_GLGizmoPainterBase_hpp_
