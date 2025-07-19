///|/ Copyright (c) Prusa Research 2018 - 2023 Enrico Turri @enricoturri1966, Tomáš Mészáros @tamasmeszaros, Lukáš Matěna @lukasmatena, Oleksandra Iushchenko @YuSanka, Filip Sykala @Jony01, Vojtěch Bubník @bubnikv, Lukáš Hejl @hejllukas, David Kocík @kocikdav, Vojtěch Král @vojtechkral
///|/ Copyright (c) BambuStudio 2023 manch1n @manch1n
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_GLCanvas3D_hpp_
#define slic3r_GLCanvas3D_hpp_

#include <stddef.h>
#include <memory>
#include <chrono>
#include <cstdint>

#include "GLToolbar.hpp"
#include "Event.hpp"
#include "Selection.hpp"
#include "Gizmos/GLGizmosManager.hpp"
#include "GUI_ObjectLayers.hpp"
#include "GLSelectionRectangle.hpp"
#include "MeshUtils.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "GCodeViewer.hpp"
#include "Camera.hpp"
#include "SceneRaycaster.hpp"
#include "GUI_Utils.hpp"

#include <arrange-wrapper/ArrangeSettingsDb_AppCfg.hpp>
#include "ArrangeSettingsDialogImgui.hpp"

#include "libslic3r/Slicing.hpp"

#include <float.h>

#include <wx/timer.h>

class wxSizeEvent;
class wxIdleEvent;
class wxKeyEvent;
class wxMouseEvent;
class wxTimerEvent;
class wxPaintEvent;
class wxGLCanvas;
class wxGLContext;

// Support for Retina OpenGL on Mac OS.
// wxGTK3 seems to simulate OSX behavior in regard to HiDPI scaling support, enable it as well.
#define ENABLE_RETINA_GL (__APPLE__ || __WXGTK3__)

namespace Slic3r {

class BackgroundSlicingProcess;
class BuildVolume;
struct ThumbnailData;
struct ThumbnailsParams;
class ModelObject;
class ModelInstance;
class PrintObject;
class Print;
class SLAPrint;
namespace CustomGCode { struct Item; }

namespace GUI {

class Bed3D;

#if ENABLE_RETINA_GL
class RetinaHelper;
#endif

class Size
{
    int m_width{ 0 };
    int m_height{ 0 };
    float m_scale_factor{ 1.0f };

public:
    Size() = default;
    Size(int width, int height, float scale_factor = 1.0f) : m_width(width), m_height(height), m_scale_factor(scale_factor) {}

    int get_width() const { return m_width; }
    void set_width(int width) { m_width = width; }

    int get_height() const { return m_height; }
    void set_height(int height) { m_height = height; }

    float get_scale_factor() const { return m_scale_factor; }
    void set_scale_factor(float factor) { m_scale_factor = factor; }
};

class RenderTimerEvent : public wxEvent
{
public:
    RenderTimerEvent(wxEventType type, wxTimer& timer)
        : wxEvent(timer.GetId(), type),
        m_timer(&timer)
    {
        SetEventObject(timer.GetOwner());
    }
    int GetInterval() const { return m_timer->GetInterval(); }
    wxTimer& GetTimer() const { return *m_timer; }

    virtual wxEvent* Clone() const { return new RenderTimerEvent(*this); }
    virtual wxEventCategory GetEventCategory() const  { return wxEVT_CATEGORY_TIMER; }
private:
    wxTimer* m_timer;
};

class  ToolbarHighlighterTimerEvent : public wxEvent
{
public:
    ToolbarHighlighterTimerEvent(wxEventType type, wxTimer& timer)
        : wxEvent(timer.GetId(), type),
        m_timer(&timer)
    {
        SetEventObject(timer.GetOwner());
    }
    int GetInterval() const { return m_timer->GetInterval(); }
    wxTimer& GetTimer() const { return *m_timer; }

    virtual wxEvent* Clone() const { return new ToolbarHighlighterTimerEvent(*this); }
    virtual wxEventCategory GetEventCategory() const { return wxEVT_CATEGORY_TIMER; }
private:
    wxTimer* m_timer;
};


class  GizmoHighlighterTimerEvent : public wxEvent
{
public:
    GizmoHighlighterTimerEvent(wxEventType type, wxTimer& timer)
        : wxEvent(timer.GetId(), type),
        m_timer(&timer)
    {
        SetEventObject(timer.GetOwner());
    }
    int GetInterval() const { return m_timer->GetInterval(); }
    wxTimer& GetTimer() const { return *m_timer; }

    virtual wxEvent* Clone() const { return new GizmoHighlighterTimerEvent(*this); }
    virtual wxEventCategory GetEventCategory() const { return wxEVT_CATEGORY_TIMER; }
private:
    wxTimer* m_timer;
};

wxDECLARE_EVENT(EVT_GLCANVAS_OBJECT_SELECT, SimpleEvent);

using Vec2dEvent = Event<Vec2d>;
// _bool_ value is used as a indicator of selection in the 3DScene
using RBtnEvent = Event<std::pair<Vec2d, bool>>;
template <size_t N> using Vec2dsEvent = ArrayEvent<Vec2d, N>;

using Vec3dEvent = Event<Vec3d>;
template <size_t N> using Vec3dsEvent = ArrayEvent<Vec3d, N>;

using HeightProfileSmoothEvent = Event<HeightProfileSmoothingParams>;

wxDECLARE_EVENT(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_RIGHT_CLICK, RBtnEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_REMOVE_OBJECT, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_ARRANGE, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_ARRANGE_CURRENT_BED, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_SELECT_ALL, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_QUESTION_MARK, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_INCREASE_INSTANCES, Event<int>); // data: +1 => increase, -1 => decrease
wxDECLARE_EVENT(EVT_GLCANVAS_INSTANCE_MOVED, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_FORCE_UPDATE, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_WIPETOWER_TOUCHED, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_INSTANCE_ROTATED, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_RESET_SKEW, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_INSTANCE_SCALED, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_INSTANCE_MIRRORED, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_ENABLE_ACTION_BUTTONS, Event<bool>);
wxDECLARE_EVENT(EVT_GLCANVAS_UPDATE_GEOMETRY, Vec3dsEvent<2>);
wxDECLARE_EVENT(EVT_GLCANVAS_MOUSE_DRAGGING_STARTED, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_MOUSE_DRAGGING_FINISHED, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_UPDATE_BED_SHAPE, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_TAB, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_RESETGIZMOS, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_SLIDERS_MANIPULATION, wxKeyEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_UNDO, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_REDO, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_COLLAPSE_SIDEBAR, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_RESET_LAYER_HEIGHT_PROFILE, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_ADAPTIVE_LAYER_HEIGHT_PROFILE, Event<float>);
wxDECLARE_EVENT(EVT_GLCANVAS_SMOOTH_LAYER_HEIGHT_PROFILE, HeightProfileSmoothEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_RELOAD_FROM_DISK, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_RENDER_TIMER, wxTimerEvent/*RenderTimerEvent*/);
wxDECLARE_EVENT(EVT_GLCANVAS_TOOLBAR_HIGHLIGHTER_TIMER, wxTimerEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_GIZMO_HIGHLIGHTER_TIMER, wxTimerEvent);

class GLCanvas3D
{
    static const double DefaultCameraZoomToBoxMarginFactor;

    class LayersEditing
    {
    public:
        enum EState : unsigned char
        {
            Unknown,
            Editing,
            Completed,
            Paused,
            Num_States
        };

        static const float THICKNESS_BAR_WIDTH;

    private:
        bool                        m_enabled{ false };
        unsigned int                m_z_texture_id{ 0 };
        // Not owned by LayersEditing.
        const DynamicPrintConfig   *m_config{ nullptr };
        // ModelObject for the currently selected object (Model::objects[last_object_id]).
        const ModelObject          *m_model_object{ nullptr };
        // Maximum z of the currently selected object (Model::objects[last_object_id]).
        float                       m_object_max_z{ 0.0f };
        // Owned by LayersEditing.
        SlicingParameters           *m_slicing_parameters{ nullptr };
        std::vector<double>         m_layer_height_profile;
        bool                        m_layer_height_profile_modified{ false };
        // Shrinkage compensation to apply when we need to use object_max_z with Z compensation.
        Vec3d                       m_shrinkage_compensation{ Vec3d::Ones() };

        mutable float               m_adaptive_quality{ 0.5f };
        mutable HeightProfileSmoothingParams m_smooth_params;
        
        static float                s_overlay_window_width;

        struct LayersTexture
        {
            // Texture data
            std::vector<char>   data;
            // Width of the texture, top level.
            size_t              width{ 0 };
            // Height of the texture, top level.
            size_t              height{ 0 };
            // For how many levels of detail is the data allocated?
            size_t              levels{ 0 };
            // Number of texture cells allocated for the height texture.
            size_t              cells{ 0 };
            // Does it need to be refreshed?
            bool                valid{ false };
        };
        LayersTexture   m_layers_texture;

    public:
        EState state{ Unknown };
        float band_width{ 2.0f };
        float strength{ 0.005f };
        int last_object_id{ -1 };
        float last_z{ 0.0f };
        LayerHeightEditActionType last_action{ LAYER_HEIGHT_EDIT_ACTION_INCREASE };

        struct Profile
        {
            GLModel baseline;
            GLModel profile;
            GLModel background;
            struct OldCanvasWidth
            {
                float background{ 0.0f };
                float baseline{ 0.0f };
                float profile{ 0.0f };
            };
            OldCanvasWidth old_canvas_width;
            std::vector<double> old_layer_height_profile;
        };
        Profile m_profile;

        LayersEditing() = default;
        ~LayersEditing();

        void init();

        void set_config(const DynamicPrintConfig* config);
        void select_object(const Model &model, int object_id);

        bool is_allowed() const;

        bool is_enabled() const { return m_enabled; }
        void set_enabled(bool enabled) { m_enabled = is_allowed() && enabled; }

        void render_overlay(const GLCanvas3D& canvas);
        void render_volumes(const GLCanvas3D& canvas, const GLVolumeCollection& volumes);

		void adjust_layer_height_profile();
		void accept_changes(GLCanvas3D& canvas);
        void reset_layer_height_profile(GLCanvas3D& canvas);
        void adaptive_layer_height_profile(GLCanvas3D& canvas, float quality_factor);
        void smooth_layer_height_profile(GLCanvas3D& canvas, const HeightProfileSmoothingParams& smoothing_params);

        static float get_cursor_z_relative(const GLCanvas3D& canvas);
        static bool bar_rect_contains(const GLCanvas3D& canvas, float x, float y);
        static Rect get_bar_rect_screen(const GLCanvas3D& canvas);
        static float get_overlay_window_width() { return LayersEditing::s_overlay_window_width; }

        float object_max_z() const { return m_object_max_z; }

        std::string get_tooltip(const GLCanvas3D& canvas) const;

        std::pair<SlicingParameters, const std::vector<double>> get_layers_height_data();

        void set_shrinkage_compensation(const Vec3d &shrinkage_compensation) { m_shrinkage_compensation = shrinkage_compensation; };

    private:
        bool is_initialized() const;
        void generate_layer_height_texture();
        void render_active_object_annotations(const GLCanvas3D& canvas);
        void render_profile(const GLCanvas3D& canvas);
        void update_slicing_parameters();

        static float thickness_bar_width(const GLCanvas3D &canvas);        
    };

    struct Mouse
    {
        struct Drag
        {
            static const Point Invalid_2D_Point;
            static const Vec3d Invalid_3D_Point;
            static const int MoveThresholdPx;

            Point start_position_2D{ Invalid_2D_Point };
            Vec3d start_position_3D{ Invalid_3D_Point };
            Vec3d camera_start_target{ Invalid_3D_Point };
            int move_volume_idx{ -1 };
            bool move_requires_threshold{ false };
            Point move_start_threshold_position_2D{ Invalid_2D_Point };
        };

        bool dragging{ false };
        Vec2d position{ DBL_MAX, DBL_MAX };
        Vec3d scene_position{ DBL_MAX, DBL_MAX, DBL_MAX };
        bool ignore_left_up{ false };
        Drag drag;

        void set_start_position_2D_as_invalid() { drag.start_position_2D = Drag::Invalid_2D_Point; }
        void set_start_position_3D_as_invalid() { drag.start_position_3D = Drag::Invalid_3D_Point; }
        void set_camera_start_target_as_invalid() { drag.camera_start_target = Drag::Invalid_3D_Point; }
        void set_move_start_threshold_position_2D_as_invalid() { drag.move_start_threshold_position_2D = Drag::Invalid_2D_Point; }

        bool is_start_position_2D_defined() const { return drag.start_position_2D != Drag::Invalid_2D_Point; }
        bool is_start_position_3D_defined() const { return drag.start_position_3D != Drag::Invalid_3D_Point; }
        bool is_camera_start_target_defined() { return drag.camera_start_target != Drag::Invalid_3D_Point; }

        bool is_move_start_threshold_position_2D_defined() const { return (drag.move_start_threshold_position_2D != Drag::Invalid_2D_Point); }
        bool is_move_threshold_met(const Point& mouse_pos) const {
            return (std::abs(mouse_pos(0) - drag.move_start_threshold_position_2D(0)) > Drag::MoveThresholdPx)
                || (std::abs(mouse_pos(1) - drag.move_start_threshold_position_2D(1)) > Drag::MoveThresholdPx);
        }
    };

    struct SlaCap
    {
        struct Triangles
        {
            GLModel object;
            GLModel supports;
        };
        typedef std::map<unsigned int, Triangles> ObjectIdToModelsMap;
        double z;
        ObjectIdToModelsMap triangles;

        SlaCap() { reset(); }
        void reset() { z = DBL_MAX; triangles.clear(); }
        bool matches(double z) const { return this->z == z; }
    };

    enum class EWarning {
        ObjectOutside,
        ToolpathOutside,
        SlaSupportsOutside,
        SomethingNotShown,
        ObjectClashed,
        GCodeConflict,
        SequentialCollision
    };

    class RenderStats
    {
    private:
        std::chrono::time_point<std::chrono::high_resolution_clock> m_measuring_start;
        int m_fps_out = -1;
        int m_fps_running = 0;
    public:
        void increment_fps_counter() { ++m_fps_running; }
        int get_fps() { return m_fps_out; }
        int get_fps_and_reset_if_needed() {
            auto cur_time = std::chrono::high_resolution_clock::now();
            int elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(cur_time-m_measuring_start).count();
            if (elapsed_ms > 1000  || m_fps_out == -1) {
                m_measuring_start = cur_time;
                m_fps_out = int (1000. * m_fps_running / elapsed_ms);
                m_fps_running = 0;
            }
            return m_fps_out;
        }

    };

    class Labels
    {
        bool m_enabled{ false };
        bool m_shown{ false };
        GLCanvas3D& m_canvas;

    public:
        explicit Labels(GLCanvas3D& canvas) : m_canvas(canvas) {}
        void enable(bool enable) { m_enabled = enable; }
        void show(bool show) { m_shown = m_enabled ? show : false; }
        bool is_shown() const { return m_shown; }
        void render(const std::vector<const ModelInstance*>& sorted_instances) const;
    };

    class Tooltip
    {
        std::string m_text;
        std::chrono::steady_clock::time_point m_start_time;
        // Indicator that the mouse is inside an ImGUI dialog, therefore the tooltip should be suppressed.
        bool m_in_imgui = false;
        float m_cursor_height{ 16.0f };

    public:
        bool is_empty() const { return m_text.empty(); }
        void set_text(const std::string& text);
        void render(const Vec2d& mouse_position, GLCanvas3D& canvas);
        // Indicates that the mouse is inside an ImGUI dialog, therefore the tooltip should be suppressed.
        void set_in_imgui(bool b) { m_in_imgui = b; }
        bool is_in_imgui() const { return m_in_imgui; }
    };

    class Slope
    {
        bool m_enabled{ false };
        GLVolumeCollection& m_volumes;
    public:
        Slope(GLVolumeCollection& volumes) : m_volumes(volumes) {}

        void enable(bool enable) { m_enabled = enable; }
        bool is_enabled() const { return m_enabled; }
        void use(bool use) { m_volumes.set_slope_active(m_enabled ? use : false); }
        bool is_used() const { return m_volumes.is_slope_active(); }
        void set_normal_angle(float angle_in_deg) const {
            m_volumes.set_slope_normal_z(-::cos(Geometry::deg2rad(90.0f - angle_in_deg)));
        }
    };

    class RenderTimer : public wxTimer {
    private:
        virtual void Notify() override;
    };

    class ToolbarHighlighterTimer : public wxTimer {
    private:
        virtual void Notify() override;
    };

    class GizmoHighlighterTimer : public wxTimer {
    private:
        virtual void Notify() override;
    };

public:
    enum ECursorType : unsigned char
    {
        Standard,
        Cross
    };

    struct ArrangeSettings
    {
        float distance           = 6.f;
        float distance_from_bed  = 0.f;
//        float distance_seq_print = 6.;    // Used when sequential print is ON
//        float distance_sla       = 6.;
        float accuracy           = 0.65f; // Unused currently
        bool  enable_rotation    = false;
        int   alignment          = 0;
        int   geometry_handling  = 0;
        int   strategy = 0;
    };

    enum class ESLAViewType
    {
        Original,
        Processed
    };

private:
    wxGLCanvas* m_canvas;
    wxGLContext* m_context;
    SceneRaycaster m_scene_raycaster;
    Bed3D &m_bed;
    int m_last_active_bed_id{ -1 };
#if ENABLE_RETINA_GL
    std::unique_ptr<RetinaHelper> m_retina_helper;
#endif
    bool m_in_render;
    wxTimer m_timer;
    LayersEditing m_layers_editing;
    Mouse m_mouse;
    GLGizmosManager m_gizmos;
    GLToolbar m_main_toolbar;
    GLToolbar m_undoredo_toolbar;
    std::array<ClippingPlane, 2> m_clipping_planes;
    ClippingPlane m_camera_clipping_plane;
    bool m_use_clipping_planes;
    std::array<SlaCap, 2> m_sla_caps;
    int m_layer_slider_index = -1;
    std::string m_sidebar_field;
    // when true renders an extra frame by not resetting m_dirty to false
    // see request_extra_frame()
    bool m_extra_frame_requested;
    bool m_event_handlers_bound{ false };
    float m_bed_selector_current_height = 0.f;

    GLVolumeCollection m_volumes;
#if SLIC3R_OPENGL_ES
    std::vector<TriangleMesh> m_wipe_tower_meshes;
#endif // SLIC3R_OPENGL_ES
    std::array<std::optional<BoundingBoxf>, MAX_NUMBER_OF_BEDS> m_wipe_tower_bounding_boxes;

    GCodeViewer m_gcode_viewer;

    RenderTimer m_render_timer;

    Selection m_selection;
    const DynamicPrintConfig* m_config;
    Model* m_model;
public:
    BackgroundSlicingProcess *m_process;
private:
    bool m_requires_check_outside_state{ false };

    void select_bed(int i, bool triggered_by_user);

    std::array<unsigned int, 2> m_old_size{ 0, 0 };

    // Screen is only refreshed from the OnIdle handler if it is dirty.
    bool m_dirty;
    bool m_initialized;
    bool m_apply_zoom_to_volumes_filter;
    bool m_picking_enabled;
    bool m_moving_enabled;
    bool m_dynamic_background_enabled;
    bool m_multisample_allowed;
    bool m_moving;
    bool m_tab_down;
    ECursorType m_cursor_type;
    GLSelectionRectangle m_rectangle_selection;
    std::vector<int> m_hover_volume_idxs;

    // Following variable is obsolete and it should be safe to remove it.
    // I just don't want to do it now before a release (Lukas Matena 24.3.2019)
    bool m_render_sla_auxiliaries;

    bool m_reload_delayed;

#if ENABLE_RENDER_PICKING_PASS
    bool m_show_picking_texture;
#endif // ENABLE_RENDER_PICKING_PASS

    KeyAutoRepeatFilter m_shift_kar_filter;
    KeyAutoRepeatFilter m_ctrl_kar_filter;

    RenderStats m_render_stats;

    int m_imgui_undo_redo_hovered_pos{ -1 };
    int m_mouse_wheel{ 0 };
    int m_selected_extruder;

    Labels m_labels;
    Tooltip m_tooltip;
    bool m_tooltip_enabled{ true };
    Slope m_slope;

    class SLAView
    {
    public:
        explicit SLAView(GLCanvas3D& parent) : m_parent(parent) {}
        void detect_type_from_volumes(const GLVolumePtrs& volumes);
        void set_type(ESLAViewType type);
        void set_type(const GLVolume::CompositeID& id, ESLAViewType type);
        void update_volumes_visibility(GLVolumePtrs& volumes);
        void update_instances_cache(const std::vector<std::pair<GLVolume::CompositeID, GLVolume::CompositeID>>& new_to_old_ids_map);
        void render_switch_button();

#if ENABLE_SLA_VIEW_DEBUG_WINDOW
        void render_debug_window();
#endif // ENABLE_SLA_VIEW_DEBUG_WINDOW

    private:
        GLCanvas3D& m_parent;
        typedef std::pair<GLVolume::CompositeID, ESLAViewType> InstancesCacheItem;
        std::vector<InstancesCacheItem> m_instances_cache;
        bool m_use_instance_bbox{ true };

        InstancesCacheItem* find_instance_item(const GLVolume::CompositeID& id);
        void select_full_instance(const GLVolume::CompositeID& id);
    };

    SLAView m_sla_view;
    bool m_sla_view_type_detection_active{ false };

    bool is_arrange_alignment_enabled() const;

    ArrangeSettingsDb_AppCfg   m_arrange_settings_db;
    ArrangeSettingsDialogImgui m_arrange_settings_dialog;

    // used to show layers times on the layers slider when pre-gcode view is active
    std::vector<float> m_gcode_layers_times_cache;

public:

    struct ContoursList
    {
        // list of unique contours
        Polygons contours;
        // if defined: list of transforms to apply to contours
        std::optional<std::vector<std::pair<size_t, Transform3d>>> trafos;

        bool empty() const { return contours.empty(); }
    };

private:

    struct ToolbarHighlighter
    {
        void set_timer_owner(wxEvtHandler* owner, int timerid = wxID_ANY) { m_timer.SetOwner(owner, timerid); }
        void init(GLToolbarItem* toolbar_item, GLCanvas3D* canvas);
        void blink();
        void invalidate();
        bool                    m_render_arrow{ false };
        GLToolbarItem*          m_toolbar_item{ nullptr };
    private:
        GLCanvas3D*             m_canvas{ nullptr };
        int				        m_blink_counter{ 0 };
        ToolbarHighlighterTimer m_timer;       
    }
    m_toolbar_highlighter;

    struct GizmoHighlighter
    {
        void set_timer_owner(wxEvtHandler* owner, int timerid = wxID_ANY) { m_timer.SetOwner(owner, timerid); }
        void init(GLGizmosManager* manager, GLGizmosManager::EType gizmo, GLCanvas3D* canvas);
        void blink();
        void invalidate();
        bool                    m_render_arrow{ false };
        GLGizmosManager::EType  m_gizmo_type;
    private:
        GLGizmosManager*        m_gizmo_manager{ nullptr };
        GLCanvas3D*             m_canvas{ nullptr };
        int				        m_blink_counter{ 0 };
        GizmoHighlighterTimer   m_timer;

    }
    m_gizmo_highlighter;

#if ENABLE_SHOW_CAMERA_TARGET
    struct CameraTarget
    {
        std::array<GLModel, 3> axis;
        Vec3d target{ Vec3d::Zero() };
    };

    CameraTarget m_camera_target;
    GLModel m_target_validation_box;
#endif // ENABLE_SHOW_CAMERA_TARGET
    GLModel m_background;

public:
    GLCanvas3D(wxGLCanvas* canvas, Bed3D& bed);
    ~GLCanvas3D();

    bool is_initialized() const { return m_initialized; }

    void set_context(wxGLContext* context) { m_context = context; }

    wxGLCanvas* get_wxglcanvas() { return m_canvas; }
	const wxGLCanvas* get_wxglcanvas() const { return m_canvas; }

    wxWindow* get_wxglcanvas_parent();

    bool init();
    void post_event(wxEvent &&event);

    std::shared_ptr<SceneRaycasterItem> add_raycaster_for_picking(SceneRaycaster::EType type, int id, const MeshRaycaster& raycaster,
        const Transform3d& trafo = Transform3d::Identity(), bool use_back_faces = false) {
        return m_scene_raycaster.add_raycaster(type, id, raycaster, trafo, use_back_faces);
    }
    void remove_raycasters_for_picking(SceneRaycaster::EType type, int id) {
        m_scene_raycaster.remove_raycasters(type, id);
    }
    void remove_raycasters_for_picking(SceneRaycaster::EType type) {
        m_scene_raycaster.remove_raycasters(type);
    }

    std::vector<std::shared_ptr<SceneRaycasterItem>>* get_raycasters_for_picking(SceneRaycaster::EType type) {
        return m_scene_raycaster.get_raycasters(type);
    }

    void set_raycaster_gizmos_on_top(bool value) {
        m_scene_raycaster.set_gizmos_on_top(value);
    }

    void set_as_dirty() { m_dirty = true; }
    void requires_check_outside_state() { m_requires_check_outside_state = true; }

    unsigned int get_volumes_count() const { return (unsigned int)m_volumes.volumes.size(); }
    const GLVolumeCollection& get_volumes() const { return m_volumes; }
    void reset_volumes();
    ModelInstanceEPrintVolumeState check_volumes_outside_state(bool selection_only = true) const;
    // update the is_outside state of all the volumes contained in the given collection
    void check_volumes_outside_state(GLVolumeCollection& volumes) const { check_volumes_outside_state(volumes, nullptr, false); }

private:
    // returns true if all the volumes are completely contained in the print volume
    // returns the containment state in the given out_state, if non-null
    bool check_volumes_outside_state(GLVolumeCollection& volumes, ModelInstanceEPrintVolumeState* out_state, bool selection_only = true) const;

public:
    void init_gcode_viewer() { m_gcode_viewer.init(); }
    void reset_gcode_toolpaths() { m_gcode_viewer.reset(); }
    const GCodeViewer::SequentialView& get_gcode_sequential_view() const { return m_gcode_viewer.get_sequential_view(); }
    void update_gcode_sequential_view_current(unsigned int first, unsigned int last) { m_gcode_viewer.update_sequential_view_current(first, last); }
    const libvgcode::Interval& get_gcode_view_full_range() const { return m_gcode_viewer.get_gcode_view_full_range(); }
    const libvgcode::Interval& get_gcode_view_enabled_range() const { return m_gcode_viewer.get_gcode_view_enabled_range(); }
    const libvgcode::Interval& get_gcode_view_visible_range() const { return m_gcode_viewer.get_gcode_view_visible_range(); }
    const libvgcode::PathVertex& get_gcode_vertex_at(size_t id) const { return m_gcode_viewer.get_gcode_vertex_at(id); }

    std::pair<std::optional<std::unique_ptr<GLModel>>, bool> get_current_marker_model() const;

    void toggle_sla_auxiliaries_visibility(bool visible, const ModelObject* mo = nullptr, int instance_idx = -1);
    void toggle_model_objects_visibility(bool visible, const ModelObject* mo = nullptr, int instance_idx = -1, const ModelVolume* mv = nullptr);
    void update_instance_printable_state_for_object(size_t obj_idx);
    void update_instance_printable_state_for_objects(const std::vector<size_t>& object_idxs);

    void set_config(const DynamicPrintConfig* config);
    const DynamicPrintConfig *config() const { return m_config; }
    void set_process(BackgroundSlicingProcess* process) { m_process = process; }
    void set_model(Model* model);
    const Model* get_model() const { return m_model; }

    const arr2::ArrangeSettingsView * get_arrange_settings_view() const { return &m_arrange_settings_dialog; }

    const Selection& get_selection() const { return m_selection; }
    Selection& get_selection() { return m_selection; }

    const GLGizmosManager& get_gizmos_manager() const { return m_gizmos; }
    GLGizmosManager& get_gizmos_manager() { return m_gizmos; }

    void bed_shape_changed();

    void set_layer_slider_index(int i) { m_layer_slider_index = i; }

    void set_clipping_plane(unsigned int id, const ClippingPlane& plane) {
        if (id < 2) {
            m_clipping_planes[id] = plane;
            m_sla_caps[id].reset();
        }
    }
    void reset_clipping_planes_cache() { m_sla_caps[0].triangles.clear(); m_sla_caps[1].triangles.clear(); }
    void set_use_clipping_planes(bool use) { m_use_clipping_planes = use; }

    bool                                get_use_clipping_planes() const { return m_use_clipping_planes; }
    const std::array<ClippingPlane, 2> &get_clipping_planes() const { return m_clipping_planes; };

    void set_use_color_clip_plane(bool use) { m_volumes.set_use_color_clip_plane(use); }
    void set_color_clip_plane(const Vec3d& cp_normal, double offset) { m_volumes.set_color_clip_plane(cp_normal, offset); }
    void set_color_clip_plane_colors(const std::array<ColorRGBA, 2>& colors) { m_volumes.set_color_clip_plane_colors(colors); }

    void refresh_camera_scene_box();

    BoundingBoxf3 volumes_bounding_box() const;
    BoundingBoxf3 scene_bounding_box() const;

    bool is_layers_editing_enabled() const { return m_layers_editing.is_enabled(); }
    bool is_layers_editing_allowed() const { return m_layers_editing.is_allowed(); }

    void reset_layer_height_profile();
    void adaptive_layer_height_profile(float quality_factor);
    void smooth_layer_height_profile(const HeightProfileSmoothingParams& smoothing_params);

    bool is_reload_delayed() const { return m_reload_delayed; }

    void enable_layers_editing(bool enable);
    void enable_picking(bool enable) { m_picking_enabled = enable; }
    void enable_moving(bool enable) { m_moving_enabled = enable; }
    void enable_gizmos(bool enable) { m_gizmos.set_enabled(enable); }
    void enable_selection(bool enable) { m_selection.set_enabled(enable); }
    void enable_main_toolbar(bool enable) { m_main_toolbar.set_enabled(enable); }
    void enable_undoredo_toolbar(bool enable) { m_undoredo_toolbar.set_enabled(enable); }
    void enable_dynamic_background(bool enable) { m_dynamic_background_enabled = enable; }
    void enable_labels(bool enable) { m_labels.enable(enable); }
    void enable_slope(bool enable) { m_slope.enable(enable); }
    void allow_multisample(bool allow) { m_multisample_allowed = allow; }

    void zoom_to_bed();
    void zoom_to_volumes();
    void zoom_to_selection();
    void zoom_to_gcode();
    void select_view(const std::string& direction);

    PrinterTechnology current_printer_technology() const;

    void update_volumes_colors_by_extruder();

    bool is_dragging() const { return m_gizmos.is_dragging() || (m_moving && !m_mouse.scene_position.isApprox(m_mouse.drag.start_position_3D)); }

    void render();
    // printable_only == false -> render also non printable volumes as grayed
    // parts_only == false -> render also sla support and pad
    void render_thumbnail(ThumbnailData& thumbnail_data, unsigned int w, unsigned int h, const ThumbnailsParams& thumbnail_params, Camera::EType camera_type);
    void render_thumbnail(ThumbnailData& thumbnail_data, unsigned int w, unsigned int h, const ThumbnailsParams& thumbnail_params, const GLVolumeCollection& volumes, Camera::EType camera_type);

    void select_all();
    void deselect_all();
    void delete_selected() { m_selection.erase(); }
    void ensure_on_bed(unsigned int object_idx, bool allow_negative_z);

    std::vector<double> get_gcode_layers_zs() const { return m_gcode_viewer.get_layers_zs(); }
    std::vector<float> get_gcode_layers_times() const { return m_gcode_viewer.get_layers_times(); }
    const std::vector<float>& get_gcode_layers_times_cache() const { return m_gcode_layers_times_cache; }
    void reset_gcode_layers_times_cache() { m_gcode_layers_times_cache.clear(); }
    void set_volumes_z_range(const std::array<double, 2>& range) { m_volumes.set_range(range[0] - 1e-6, range[1] + 1e-6); }
    void set_toolpaths_z_range(const std::array<unsigned int, 2>& range);
    size_t get_gcode_extruders_count() { return m_gcode_viewer.get_extruders_count(); }

    std::vector<int> load_object(const ModelObject& model_object, int obj_idx, std::vector<int> instance_idxs);
    std::vector<int> load_object(const Model& model, int obj_idx);

    void mirror_selection(Axis axis);

    void reload_scene(bool refresh_immediately, bool force_full_scene_refresh = false);

    void load_gcode_shells();
    void load_gcode_preview(const GCodeProcessorResult& gcode_result, const std::vector<std::string>& str_tool_colors,
        const std::vector<std::string>& str_color_print_colors);
    void set_gcode_view_type(libvgcode::EViewType type) { return m_gcode_viewer.set_view_type(type); }
    libvgcode::EViewType get_gcode_view_type() const { return m_gcode_viewer.get_view_type(); }
    void enable_gcode_view_type_cache_load(bool enable) { m_gcode_viewer.enable_view_type_cache_load(enable); }
    void enable_gcode_view_type_cache_write(bool enable) { m_gcode_viewer.enable_view_type_cache_write(enable); }
    bool is_gcode_view_type_cache_load_enabled() const { return m_gcode_viewer.is_view_type_cache_load_enabled(); }
    bool is_gcode_view_type_cache_write_enabled() const { return m_gcode_viewer.is_view_type_cache_write_enabled(); }

    void load_preview(const std::vector<std::string>& str_tool_colors, const std::vector<std::string>& str_color_print_colors,
        const std::vector<CustomGCode::Item>& color_print_values);
    void load_sla_preview();
    void bind_event_handlers();
    void unbind_event_handlers();

    void on_size(wxSizeEvent& evt) { m_dirty = true; }
    void on_idle(wxIdleEvent& evt);
    void on_char(wxKeyEvent& evt);
    void on_key(wxKeyEvent& evt);
    void on_mouse_wheel(wxMouseEvent& evt);
    void on_timer(wxTimerEvent& evt);
    void on_render_timer(wxTimerEvent& evt);
    void on_mouse(wxMouseEvent& evt);
    void on_paint(wxPaintEvent& evt);
    void on_set_focus(wxFocusEvent& evt);

    Size get_canvas_size() const;
    Vec2d get_local_mouse_position() const;

    // store opening position of menu
    std::optional<Vec2d> m_popup_menu_positon; // position of mouse right click
    void  set_popup_menu_position(const Vec2d &position) { m_popup_menu_positon = position; }
    const std::optional<Vec2d>& get_popup_menu_position() const { return m_popup_menu_positon; }
    void clear_popup_menu_position() { m_popup_menu_positon.reset(); }

    void set_tooltip(const std::string& tooltip);

    // the following methods add a snapshot to the undo/redo stack, unless the given string is empty
    void do_move(const std::string& snapshot_type);
    void do_rotate(const std::string& snapshot_type);
    void do_scale(const std::string& snapshot_type);
    void do_mirror(const std::string& snapshot_type);
    void do_reset_skew(const std::string& snapshot_type);

    void update_gizmos_on_off_state();
    void reset_all_gizmos() { m_gizmos.reset_all_states(); }

    void handle_sidebar_focus_event(const std::string& opt_key, bool focus_on);
    void handle_layers_data_focus_event(const t_layer_height_range range, const EditorType type);

    void update_ui_from_settings();

    int get_move_volume_id() const { return m_mouse.drag.move_volume_idx; }
    int get_first_hover_volume_idx() const { return m_hover_volume_idxs.empty() ? -1 : m_hover_volume_idxs.front(); }
    void set_selected_extruder(int extruder) { m_selected_extruder = extruder;}

    class WipeTowerInfo {
    protected:
        Vec2d m_pos = {NaNd, NaNd};
        double m_rotation = 0.;
        BoundingBoxf m_bb;
        int m_bed_index{0};
        friend class GLCanvas3D;

    public:
        inline operator bool() const {
            return !std::isnan(m_pos.x()) && !std::isnan(m_pos.y());
        }

        inline const Vec2d& pos() const { return m_pos; }
        inline double rotation() const { return m_rotation; }
        inline const Vec2d bb_size() const { return m_bb.size(); }
        inline const BoundingBoxf& bounding_box() const { return m_bb; }
        inline const int bed_index() const { return m_bed_index; }

        static void apply_wipe_tower(Vec2d pos, double rot, int bed_index);
    };

    std::vector<WipeTowerInfo> get_wipe_tower_infos() const;

    // Returns the view ray line, in world coordinate, at the given mouse position.
    Linef3 mouse_ray(const Point& mouse_pos);

    bool is_mouse_dragging() const { return m_mouse.dragging; }

    double get_size_proportional_to_max_bed_size(double factor) const;

    void set_cursor(ECursorType type);
    void msw_rescale() { m_gcode_viewer.invalidate_legend(); }

    void request_extra_frame() { m_extra_frame_requested = true; }
    
    void schedule_extra_frame(int miliseconds);

    float get_main_toolbar_height() { return m_main_toolbar.get_height(); }
    int get_main_toolbar_item_id(const std::string& name) const { return m_main_toolbar.get_item_id(name); }
    void force_main_toolbar_left_action(int item_id) { m_main_toolbar.force_left_action(item_id, *this); }
    void force_main_toolbar_right_action(int item_id) { m_main_toolbar.force_right_action(item_id, *this); }
    void update_tooltip_for_settings_item_in_main_toolbar();

    bool has_toolpaths_to_export() const { return m_gcode_viewer.can_export_toolpaths(); }
    void export_toolpaths_to_obj(const char* filename) const { m_gcode_viewer.export_toolpaths_to_obj(filename); }

    void mouse_up_cleanup();

    bool are_labels_shown() const { return m_labels.is_shown(); }
    void show_labels(bool show) { m_labels.show(show); }

    bool is_legend_shown() const { return m_gcode_viewer.is_legend_shown(); }
    void show_legend(bool show) { m_gcode_viewer.show_legend(show); m_dirty = true; }

    bool is_using_slope() const { return m_slope.is_used(); }
    void use_slope(bool use) { m_slope.use(use); }
    void set_slope_normal_angle(float angle_in_deg) { m_slope.set_normal_angle(angle_in_deg); }

    void highlight_toolbar_item(const std::string& item_name);
    void highlight_gizmo(const std::string& gizmo_name);

    // Timestamp for FPS calculation and notification fade-outs.
    static int64_t timestamp_now() {
#ifdef _WIN32
        // Cheaper on Windows, calls GetSystemTimeAsFileTime()
        return wxGetUTCTimeMillis().GetValue();
#else
        // calls clock()
        return wxGetLocalTimeMillis().GetValue();
#endif
    }

    const Print* fff_print() const;
    const SLAPrint* sla_print() const;

    void reset_old_size() { m_old_size = { 0, 0 }; }

    bool is_object_sinking(int object_idx) const;

    void apply_retina_scale(Vec2d &screen_coordinate) const;

    std::pair<SlicingParameters, const std::vector<double>> get_layers_height_data(int object_id);

    void detect_sla_view_type();
    void set_sla_view_type(ESLAViewType type);
    void set_sla_view_type(const GLVolume::CompositeID& id, ESLAViewType type);
    void enable_sla_view_type_detection() { m_sla_view_type_detection_active = true; }

private:
    bool _is_shown_on_screen() const;

    bool _init_toolbars();
    bool _init_main_toolbar();
    bool _init_undoredo_toolbar();
    bool _init_view_toolbar();
    bool _init_collapse_toolbar();

    bool _set_current();
    void _resize(unsigned int w, unsigned int h);

    BoundingBoxf3 _max_bounding_box(bool include_bed_model) const;

    void _zoom_to_box(const BoundingBoxf3& box, double margin_factor = DefaultCameraZoomToBoxMarginFactor);
    void _update_camera_zoom(double zoom);

    void _refresh_if_shown_on_screen();

    void _picking_pass();
    void _rectangular_selection_picking_pass();
    void _render_background();
    void _render_bed(const Transform3d& view_matrix, const Transform3d& projection_matrix, bool bottom);
    void _render_bed_axes();
    void _render_bed_for_picking(const Transform3d& view_matrix, const Transform3d& projection_matrix, bool bottom);
    void _render_objects(GLVolumeCollection::ERenderType type);
    void _render_gcode() { m_gcode_viewer.render(); }
    void _render_gcode_cog() { m_gcode_viewer.render_cog(); }
    void _render_selection();
    bool check_toolbar_icon_size(float init_scale, float& new_scale_to_save, bool is_custom, int counter = 3);
#if ENABLE_RENDER_SELECTION_CENTER
    void _render_selection_center() { m_selection.render_center(m_gizmos.is_dragging()); }
#endif // ENABLE_RENDER_SELECTION_CENTER
    void _check_and_update_toolbar_icon_scale();
    void _render_overlays();
    void _render_bed_selector();
    void _render_volumes_for_picking(const Camera& camera) const;
    void _render_current_gizmo() const { m_gizmos.render_current_gizmo(); }
    void _render_gizmos_overlay();
    void _render_main_toolbar();
    void _render_undoredo_toolbar();
    void _render_collapse_toolbar() const;
    void _render_view_toolbar() const;
#if ENABLE_SHOW_CAMERA_TARGET
    void _render_camera_target();
    void _render_camera_target_validation_box();
#endif // ENABLE_SHOW_CAMERA_TARGET
    void _render_sla_slices();
    void _render_selection_sidebar_hints() { m_selection.render_sidebar_hints(m_sidebar_field); }
    bool _render_undo_redo_stack(const bool is_undo, float pos_x);
    bool _render_arrange_menu(float pos_x, bool current_bed);
    void _render_thumbnail_internal(ThumbnailData& thumbnail_data, const ThumbnailsParams& thumbnail_params, const GLVolumeCollection& volumes, Camera::EType camera_type);
    // render thumbnail using an off-screen framebuffer
    void _render_thumbnail_framebuffer(ThumbnailData& thumbnail_data, unsigned int w, unsigned int h, const ThumbnailsParams& thumbnail_params, const GLVolumeCollection& volumes, Camera::EType camera_type);
    // render thumbnail using an off-screen framebuffer when GLEW_EXT_framebuffer_object is supported
    void _render_thumbnail_framebuffer_ext(ThumbnailData& thumbnail_data, unsigned int w, unsigned int h, const ThumbnailsParams& thumbnail_params, const GLVolumeCollection& volumes, Camera::EType camera_type);
    // render thumbnail using the default framebuffer
    void _render_thumbnail_legacy(ThumbnailData& thumbnail_data, unsigned int w, unsigned int h, const ThumbnailsParams& thumbnail_params, const GLVolumeCollection& volumes, Camera::EType camera_type);

    void _update_volumes_hover_state();

    void _perform_layer_editing_action(wxMouseEvent* evt = nullptr);

    // Convert the screen space coordinate to an object space coordinate.
    // If the Z screen space coordinate is not provided, a depth buffer value is substituted.
    Vec3d _mouse_to_3d(const Point& mouse_pos, const float* z = nullptr, bool use_ortho = false);

    // Convert the screen space coordinate to world coordinate on the bed.
    Vec3d _mouse_to_bed_3d(const Point& mouse_pos);

    void _start_timer() { m_timer.Start(100, wxTIMER_CONTINUOUS); }
    void _stop_timer() { m_timer.Stop(); }

    // Load SLA objects and support structures for objects, for which the slaposSliceSupports step has been finished.
  	void _load_sla_shells();
    void _update_sla_shells_outside_state() { check_volumes_outside_state(); }
    void _set_warning_notification_if_needed(EWarning warning);

    // generates a warning notification containing the given message
    void _set_warning_notification(EWarning warning, bool state);

    std::pair<bool, const GLVolume*> _is_any_volume_outside() const;
    bool _is_sequential_print_enabled() const;

    // updates the selection from the content of m_hover_volume_idxs
    void _update_selection_from_hover();

    bool _deactivate_undo_redo_toolbar_items();
    bool _deactivate_collapse_toolbar_items();
    bool _deactivate_arrange_menu();

    float get_overlay_window_width() { return LayersEditing::get_overlay_window_width(); }

#if ENABLE_BINARIZED_GCODE_DEBUG_WINDOW
    void show_binary_gcode_debug_window();
#endif // ENABLE_BINARIZED_GCODE_DEBUG_WINDOW
};

const ModelVolume *get_model_volume(const GLVolume &v, const Model &model);
ModelVolume *get_model_volume(const ObjectID &volume_id, const ModelObjectPtrs &objects);
ModelVolume *get_model_volume(const GLVolume &v, const ModelObjectPtrs &objects);
ModelVolume *get_model_volume(const GLVolume &v, const ModelObject &object);

GLVolume *get_first_hovered_gl_volume(const GLCanvas3D &canvas);
GLVolume *get_selected_gl_volume(const GLCanvas3D &canvas);

ModelObject *get_model_object(const GLVolume &gl_volume, const Model &model);
ModelObject *get_model_object(const GLVolume &gl_volume, const ModelObjectPtrs &objects);

ModelInstance *get_model_instance(const GLVolume &gl_volume, const Model &model);
ModelInstance *get_model_instance(const GLVolume &gl_volume, const ModelObjectPtrs &objects);
ModelInstance *get_model_instance(const GLVolume &gl_volume, const ModelObject &object);

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLCanvas3D_hpp_
