///|/ Copyright (c) Prusa Research 2018 - 2023 Enrico Turri @enricoturri1966, Oleksandra Iushchenko @YuSanka, Tomáš Mészáros @tamasmeszaros, Lukáš Matěna @lukasmatena, Vojtěch Bubník @bubnikv, David Kocík @kocikdav, Filip Sykala @Jony01, Lukáš Hejl @hejllukas, Vojtěch Král @vojtechkral
///|/ Copyright (c) BambuStudio 2023 manch1n @manch1n
///|/ Copyright (c) SuperSlicer 2023 Remi Durand @supermerill
///|/ Copyright (c) 2020 Benjamin Greiner
///|/ Copyright (c) 2019 John Drake @foxox
///|/ Copyright (c) 2019 BeldrothTheGold @BeldrothTheGold
///|/ Copyright (c) 2019 Thomas Moore
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "libslic3r/libslic3r.h"
#include "GLCanvas3D.hpp"

#include <igl/unproject.h> // IWYU pragma: keep
#include <LocalesUtils.hpp>
#include <nanosvgrast.h>

#include "libslic3r/BuildVolume.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/GCode/ThumbnailData.hpp"
#include "libslic3r/Geometry/ConvexHull.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/MultipleBeds.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/Technologies.hpp"
#include "libslic3r/Tesselate.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "3DBed.hpp"
#include "3DScene.hpp"
#include "BackgroundSlicingProcess.hpp"
#include "GLShader.hpp"
#include "GUI.hpp"
#include "Tab.hpp"
#include "GUI_Preview.hpp"
#include "OpenGLManager.hpp"
#include "Plater.hpp"
#include "MainFrame.hpp"
#include "GUI_App.hpp"
#include "GUI_ObjectList.hpp"
#include "GUI_ObjectManipulation.hpp"
#include "Mouse3DController.hpp"
#include "I18N.hpp"
#include "NotificationManager.hpp"
#include "format.hpp"

#include "slic3r/GUI/BitmapCache.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoPainterBase.hpp"
#include "slic3r/Utils/UndoRedo.hpp"

#if ENABLE_RETINA_GL
#include "slic3r/Utils/RetinaHelper.hpp"
#endif

#include <GL/glew.h>

#include <wx/glcanvas.h>
#include <wx/bitmap.h>
#include <wx/dcmemory.h>
#include <wx/image.h>
#include <wx/settings.h>
#include <wx/tooltip.h>
#include <wx/debug.h>
#include <wx/fontutil.h>

// Print now includes tbb, and tbb includes Windows. This breaks compilation of wxWidgets if included before wx.
#include "libslic3r/Print.hpp"
#include "libslic3r/SLAPrint.hpp"

#include "wxExtensions.hpp"

#include <tbb/parallel_for.h>
#include <tbb/spin_mutex.h>

#include <boost/log/trivial.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/crc.hpp>

#include <iostream>
#include <float.h>
#include <algorithm>
#include <cmath>

#include <map>

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui/imgui_internal.h>
#include <slic3r/GUI/Gizmos/GLGizmoMmuSegmentation.hpp>

extern std::vector<GLuint> s_th_tex_id;

static constexpr const float TRACKBALLSIZE = 0.8f;

static const Slic3r::ColorRGBA DEFAULT_BG_DARK_COLOR  = { 0.478f, 0.478f, 0.478f, 1.0f };
static const Slic3r::ColorRGBA DEFAULT_BG_LIGHT_COLOR = { 0.753f, 0.753f, 0.753f, 1.0f };
static const Slic3r::ColorRGBA ERROR_BG_DARK_COLOR    = { 0.478f, 0.192f, 0.039f, 1.0f };
static const Slic3r::ColorRGBA ERROR_BG_LIGHT_COLOR   = { 0.753f, 0.192f, 0.039f, 1.0f };

#define SHOW_IMGUI_DEMO_WINDOW
#ifdef SHOW_IMGUI_DEMO_WINDOW
static bool show_imgui_demo_window = false;
#endif // SHOW_IMGUI_DEMO_WINDOW

namespace Slic3r {
namespace GUI {

void GLCanvas3D::select_bed(int i, bool triggered_by_user)
{
    int old_bed = s_multiple_beds.get_active_bed();
    if ((i == old_bed  && !s_multiple_beds.is_autoslicing()) || i == -1)
        return;

    if (current_printer_technology() == ptSLA) {
        // Close SlaSupports or Hollow gizmos before switching beds. They rely on having access to SLAPrintObject to work.
        if (GLGizmosManager::EType cur_giz = get_gizmos_manager().get_current_type();
            cur_giz == GLGizmosManager::EType::SlaSupports || cur_giz == GLGizmosManager::EType::Hollow) {
            if (! get_gizmos_manager().open_gizmo(get_gizmos_manager().get_current_type()))
                return;
        }
    }
    wxGetApp().plater()->canvas3D()->m_process->stop();
    m_sequential_print_clearance.m_evaluating = true;
    reset_sequential_print_clearance();

    post_event(Event<bool>(EVT_GLCANVAS_ENABLE_ACTION_BUTTONS, is_sliceable(s_print_statuses[i])));

    // The stop call above schedules some events that would be processed after the switch.
    // Among else, on_process_completed would be called, which would stop slicing of
    // the new bed. We need to stop the process, pump all the events out of the queue
    // and then switch the beds.
    wxGetApp().CallAfter([i, old_bed, triggered_by_user]() {
        wxYield();
        s_multiple_beds.set_active_bed(i);
        s_beds_just_switched = true;
        s_beds_switched_since_last_gcode_load = true;
        if (wxGetApp().plater()->is_preview_shown()) {
            s_reload_preview_after_switching_beds = true;
            wxPostEvent(wxGetApp().plater(), SimpleEvent(EVT_GLVIEWTOOLBAR_PREVIEW));
            wxGetApp().plater()->get_camera().translate_world(
                s_multiple_beds.get_bed_translation(i)
                - s_multiple_beds.get_bed_translation(old_bed)
            );
        }
        wxGetApp().plater()->schedule_background_process();
        wxGetApp().plater()->object_list_changed(); // Updates Slice Now / Export buttons.
        if (s_multiple_beds.is_autoslicing() && triggered_by_user) {
            s_multiple_beds.stop_autoslice(false);
            wxGetApp().sidebar().switch_from_autoslicing_mode();
        }
    });
}

#ifdef __WXGTK3__
// wxGTK3 seems to simulate OSX behavior in regard to HiDPI scaling support.
RetinaHelper::RetinaHelper(wxWindow* window) : m_window(window), m_self(nullptr) {}
RetinaHelper::~RetinaHelper() {}
float RetinaHelper::get_scale_factor() { return float(m_window->GetContentScaleFactor()); }
#endif // __WXGTK3__

// Fixed the collision between BuildVolume::Type::Convex and macro Convex defined inside /usr/include/X11/X.h that is included by WxWidgets 3.0.
#if defined(__linux__) && defined(Convex)
#undef Convex
#endif

GLCanvas3D::LayersEditing::~LayersEditing()
{
    if (m_z_texture_id != 0) {
        glsafe(::glDeleteTextures(1, &m_z_texture_id));
        m_z_texture_id = 0;
    }
    delete m_slicing_parameters;
}

const float GLCanvas3D::LayersEditing::THICKNESS_BAR_WIDTH = 70.0f;

void GLCanvas3D::LayersEditing::init()
{
    glsafe(::glGenTextures(1, (GLuint*)&m_z_texture_id));
    glsafe(::glBindTexture(GL_TEXTURE_2D, m_z_texture_id));
    if (!OpenGLManager::get_gl_info().is_core_profile() || !OpenGLManager::get_gl_info().is_mesa()) {
        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
    }
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST));
    glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1));
    glsafe(::glBindTexture(GL_TEXTURE_2D, 0));
}

void GLCanvas3D::LayersEditing::set_config(const DynamicPrintConfig* config)
{
    m_config = config;
    delete m_slicing_parameters;
    m_slicing_parameters = nullptr;
    m_layers_texture.valid = false;

    m_layer_height_profile.clear();
    m_layer_height_profile_modified = false;
}

void GLCanvas3D::LayersEditing::select_object(const Model &model, int object_id)
{
    const ModelObject *model_object_new = (object_id >= 0) ? model.objects[object_id] : nullptr;
    // Maximum height of an object changes when the object gets rotated or scaled.
    // Changing maximum height of an object will invalidate the layer heigth editing profile.
    // m_model_object->bounding_box() is cached, therefore it is cheap even if this method is called frequently.
    const float new_max_z = (model_object_new == nullptr) ? 0.0f : static_cast<float>(model_object_new->max_z());

    if (m_model_object != model_object_new || this->last_object_id != object_id || m_object_max_z != new_max_z ||
        (model_object_new != nullptr && m_model_object->id() != model_object_new->id())) {
        m_layer_height_profile.clear();
        m_layer_height_profile_modified = false;
        delete m_slicing_parameters;
        m_slicing_parameters   = nullptr;
        m_layers_texture.valid = false;
        this->last_object_id   = object_id;
        m_model_object         = model_object_new;
        m_object_max_z         = new_max_z;
    }
}

bool GLCanvas3D::LayersEditing::is_allowed() const
{
    return wxGetApp().get_shader("variable_layer_height") != nullptr && m_z_texture_id > 0;
}

float GLCanvas3D::LayersEditing::s_overlay_window_width;

void GLCanvas3D::LayersEditing::render_overlay(const GLCanvas3D& canvas)
{
    if (!m_enabled)
        return;

    const Size& cnv_size = canvas.get_canvas_size();

    ImGuiPureWrap::set_next_window_pos(static_cast<float>(cnv_size.get_width()) - wxGetApp().imgui()->get_style_scaling() * THICKNESS_BAR_WIDTH,
        static_cast<float>(cnv_size.get_height()), ImGuiCond_Always, 1.0f, 1.0f);

    ImGuiPureWrap::begin(_u8L("Variable layer height"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, _u8L("Left mouse button:"));
    ImGui::SameLine();
    ImGuiPureWrap::text(_u8L("Add detail"));

    ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, _u8L("Right mouse button:"));
    ImGui::SameLine();
    ImGuiPureWrap::text(_u8L("Remove detail"));

    ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, _u8L("Shift + Left mouse button:"));
    ImGui::SameLine();
    ImGuiPureWrap::text(_u8L("Reset to base"));

    ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, _u8L("Shift + Right mouse button:"));
    ImGui::SameLine();
    ImGuiPureWrap::text(_u8L("Smoothing"));

    ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, _u8L("Mouse wheel:"));
    ImGui::SameLine();
    ImGuiPureWrap::text(_u8L("Increase/decrease edit area"));
    
    ImGui::Separator();
    if (ImGuiPureWrap::button(_u8L("Adaptive")))
        wxPostEvent((wxEvtHandler*)canvas.get_wxglcanvas(), Event<float>(EVT_GLCANVAS_ADAPTIVE_LAYER_HEIGHT_PROFILE, m_adaptive_quality));

    ImGui::SameLine();
    float text_align = ImGui::GetCursorPosX();
    ImGui::AlignTextToFramePadding();
    ImGuiPureWrap::text(_u8L("Quality / Speed"));
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(_u8L("Higher print quality versus higher print speed.").c_str());
        ImGui::EndTooltip();
    }

    ImGui::SameLine();
    float widget_align = ImGui::GetCursorPosX();
    const float style_scaling = wxGetApp().imgui()->get_style_scaling();
    ImGui::PushItemWidth(style_scaling * 120.0f);
    m_adaptive_quality = std::clamp(m_adaptive_quality, 0.0f, 1.f);
    wxGetApp().imgui()->slider_float("", &m_adaptive_quality, 0.0f, 1.f, "%.2f");

    ImGui::Separator();
    if (ImGuiPureWrap::button(_u8L("Smooth")))
        wxPostEvent((wxEvtHandler*)canvas.get_wxglcanvas(), HeightProfileSmoothEvent(EVT_GLCANVAS_SMOOTH_LAYER_HEIGHT_PROFILE, m_smooth_params));

    ImGui::SameLine();
    ImGui::SetCursorPosX(text_align);
    ImGui::AlignTextToFramePadding();
    ImGuiPureWrap::text(_u8L("Radius"));
    ImGui::SameLine();
    ImGui::SetCursorPosX(widget_align);
    ImGui::PushItemWidth(style_scaling * 120.0f);
    int radius = (int)m_smooth_params.radius;
    if (ImGui::SliderInt("##1", &radius, 1, 10)) {
        radius = std::clamp(radius, 1, 10);
        m_smooth_params.radius = (unsigned int)radius;
    }

    ImGui::SetCursorPosX(text_align);
    ImGui::AlignTextToFramePadding();
    ImGuiPureWrap::text(_u8L("Keep min"));
    ImGui::SameLine();
    if (ImGui::GetCursorPosX() < widget_align)  // because of line lenght after localization
        ImGui::SetCursorPosX(widget_align);

    ImGui::PushItemWidth(style_scaling * 120.0f);
    ImGuiPureWrap::checkbox("##2", m_smooth_params.keep_min);

    ImGui::Separator();
    if (ImGuiPureWrap::button(_u8L("Reset")))
        wxPostEvent((wxEvtHandler*)canvas.get_wxglcanvas(), SimpleEvent(EVT_GLCANVAS_RESET_LAYER_HEIGHT_PROFILE));

    GLCanvas3D::LayersEditing::s_overlay_window_width = ImGui::GetWindowSize().x /*+ (float)m_layers_texture.width/4*/;
    ImGuiPureWrap::end();

    render_active_object_annotations(canvas);
    render_profile(canvas);
}

float GLCanvas3D::LayersEditing::get_cursor_z_relative(const GLCanvas3D& canvas)
{
    const Vec2d mouse_pos = canvas.get_local_mouse_position();
    const Rect& rect = get_bar_rect_screen(canvas);
    float x = (float)mouse_pos.x();
    float y = (float)mouse_pos.y();
    float t = rect.get_top();
    float b = rect.get_bottom();

    return (rect.get_left() <= x && x <= rect.get_right() && t <= y && y <= b) ?
        // Inside the bar.
        (b - y - 1.0f) / (b - t - 1.0f) :
        // Outside the bar.
        -1000.0f;
}

bool GLCanvas3D::LayersEditing::bar_rect_contains(const GLCanvas3D& canvas, float x, float y)
{
    const Rect& rect = get_bar_rect_screen(canvas);
    return rect.get_left() <= x && x <= rect.get_right() && rect.get_top() <= y && y <= rect.get_bottom();
}

Rect GLCanvas3D::LayersEditing::get_bar_rect_screen(const GLCanvas3D& canvas)
{
    const Size& cnv_size = canvas.get_canvas_size();
    float w = (float)cnv_size.get_width();
    float h = (float)cnv_size.get_height();

    return { w - thickness_bar_width(canvas), 0.0f, w, h };
}

std::pair<SlicingParameters, const std::vector<double>> GLCanvas3D::LayersEditing::get_layers_height_data()
{
    if (m_slicing_parameters != nullptr)
        return { *m_slicing_parameters, m_layer_height_profile };

    assert(m_model_object != nullptr);
    this->update_slicing_parameters();
    PrintObject::update_layer_height_profile(*m_model_object, *m_slicing_parameters, m_layer_height_profile);
    std::pair<SlicingParameters, const std::vector<double>> ret = { *m_slicing_parameters, m_layer_height_profile };
    delete m_slicing_parameters;
    m_slicing_parameters = nullptr;
    return ret;
}

bool GLCanvas3D::LayersEditing::is_initialized() const
{
    return wxGetApp().get_shader("variable_layer_height") != nullptr;
}

std::string GLCanvas3D::LayersEditing::get_tooltip(const GLCanvas3D& canvas) const
{
    std::string ret;
    if (m_enabled && m_layer_height_profile.size() >= 4) {
        float z = get_cursor_z_relative(canvas);
        if (z != -1000.0f) {
            z *= m_object_max_z;

            float h = 0.0f;
            for (size_t i = m_layer_height_profile.size() - 2; i >= 2; i -= 2) {
                const float zi = static_cast<float>(m_layer_height_profile[i]);
                const float zi_1 = static_cast<float>(m_layer_height_profile[i - 2]);
                if (zi_1 <= z && z <= zi) {
                    float dz = zi - zi_1;
                    h = (dz != 0.0f) ? static_cast<float>(lerp(m_layer_height_profile[i - 1], m_layer_height_profile[i + 1], (z - zi_1) / dz)) :
                        static_cast<float>(m_layer_height_profile[i + 1]);
                    break;
                }
            }
            if (h > 0.0f)
                ret = format("%.3f", h);
        }
    }
    return ret;
}

void GLCanvas3D::LayersEditing::render_active_object_annotations(const GLCanvas3D& canvas)
{
    const Size cnv_size = canvas.get_canvas_size();
    const float cnv_width = (float)cnv_size.get_width();
    const float cnv_height = (float)cnv_size.get_height();
    if (cnv_width == 0.0f || cnv_height == 0.0f)
        return;

    const float cnv_inv_width = 1.0f / cnv_width;
    GLShaderProgram* shader = wxGetApp().get_shader("variable_layer_height");
    if (shader == nullptr)
        return;

    shader->start_using();

    shader->set_uniform("z_to_texture_row", float(m_layers_texture.cells - 1) / (float(m_layers_texture.width) * m_object_max_z));
    shader->set_uniform("z_texture_row_to_normalized", 1.0f / (float)m_layers_texture.height);
    shader->set_uniform("z_cursor", m_object_max_z * this->get_cursor_z_relative(canvas));
    shader->set_uniform("z_cursor_band_width", band_width);
    shader->set_uniform("object_max_z", m_object_max_z);
    shader->set_uniform("view_model_matrix", Transform3d::Identity());
    shader->set_uniform("projection_matrix", Transform3d::Identity());
    shader->set_uniform("view_normal_matrix", (Matrix3d)Matrix3d::Identity());

    glsafe(::glPixelStorei(GL_UNPACK_ALIGNMENT, 1));
    glsafe(::glBindTexture(GL_TEXTURE_2D, m_z_texture_id));

    // Render the color bar
    if (!m_profile.background.is_initialized() || m_profile.old_canvas_width.background != cnv_width) {
        m_profile.old_canvas_width.background = cnv_width;
        m_profile.background.reset();

        GLModel::Geometry init_data;
        init_data.format = { GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3N3T2 };
        init_data.reserve_vertices(4);
        init_data.reserve_indices(6);

        // vertices
        const float l = 1.0f - 2.0f * THICKNESS_BAR_WIDTH * cnv_inv_width;
        const float r = 1.0f;
        const float t = 1.0f;
        const float b = -1.0f;
        init_data.add_vertex(Vec3f(l, b, 0.0f), Vec3f::UnitZ(), Vec2f(0.0f, 0.0f));
        init_data.add_vertex(Vec3f(r, b, 0.0f), Vec3f::UnitZ(), Vec2f(1.0f, 0.0f));
        init_data.add_vertex(Vec3f(r, t, 0.0f), Vec3f::UnitZ(), Vec2f(1.0f, 1.0f));
        init_data.add_vertex(Vec3f(l, t, 0.0f), Vec3f::UnitZ(), Vec2f(0.0f, 1.0f));

        // indices
        init_data.add_triangle(0, 1, 2);
        init_data.add_triangle(2, 3, 0);

        m_profile.background.init_from(std::move(init_data));
    }

    m_profile.background.render();

    glsafe(::glBindTexture(GL_TEXTURE_2D, 0));

    shader->stop_using();
}

void GLCanvas3D::LayersEditing::render_profile(const GLCanvas3D& canvas)
{
    //FIXME show some kind of legend.

    if (!m_slicing_parameters)
        return;

    const Size cnv_size = canvas.get_canvas_size();
    const float cnv_width  = (float)cnv_size.get_width();
    const float cnv_height = (float)cnv_size.get_height();
    if (cnv_width == 0.0f || cnv_height == 0.0f)
        return;

    // Make the vertical bar a bit wider so the layer height curve does not touch the edge of the bar region.
    const float scale_x = THICKNESS_BAR_WIDTH / float(1.12 * m_slicing_parameters->max_layer_height);
    const float scale_y = cnv_height / m_object_max_z;

    const float cnv_inv_width  = 1.0f / cnv_width;
    const float cnv_inv_height = 1.0f / cnv_height;
    const float left = 1.0f - 2.0f * THICKNESS_BAR_WIDTH * cnv_inv_width;

    // Baseline
    if (!m_profile.baseline.is_initialized() || m_profile.old_layer_height_profile != m_layer_height_profile || m_profile.old_canvas_width.baseline != cnv_width) {
        m_profile.old_canvas_width.baseline = cnv_width;
        m_profile.baseline.reset();

        GLModel::Geometry init_data;
        init_data.format = { GLModel::Geometry::EPrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P2 };
        init_data.color = ColorRGBA::BLACK();
        init_data.reserve_vertices(2);
        init_data.reserve_indices(2);

        // vertices
        const float axis_x = left + 2.0f * float(m_slicing_parameters->layer_height) * scale_x * cnv_inv_width;
        init_data.add_vertex(Vec2f(axis_x, -1.0f));
        init_data.add_vertex(Vec2f(axis_x, 1.0f));

        // indices
        init_data.add_line(0, 1);

        m_profile.baseline.init_from(std::move(init_data));
    }

    if (!m_profile.profile.is_initialized() || m_profile.old_layer_height_profile != m_layer_height_profile || m_profile.old_canvas_width.profile != cnv_width) {
        m_profile.old_canvas_width.profile = cnv_width;
        m_profile.old_layer_height_profile = m_layer_height_profile;
        m_profile.profile.reset();

        GLModel::Geometry init_data;
        init_data.format = { GLModel::Geometry::EPrimitiveType::LineStrip, GLModel::Geometry::EVertexLayout::P2 };
        init_data.color = ColorRGBA::BLUE();
        init_data.reserve_vertices(m_layer_height_profile.size() / 2);
        init_data.reserve_indices(m_layer_height_profile.size() / 2);

        // vertices + indices
        for (unsigned int i = 0; i < (unsigned int)m_layer_height_profile.size(); i += 2) {
            init_data.add_vertex(Vec2f(left + 2.0f * float(m_layer_height_profile[i + 1]) * scale_x * cnv_inv_width,
                2.0f * (float(m_layer_height_profile[i]) * scale_y * cnv_inv_height - 0.5)));
            init_data.add_index(i / 2);
        }

        m_profile.profile.init_from(std::move(init_data));
    }

#if SLIC3R_OPENGL_ES
    GLShaderProgram* shader = wxGetApp().get_shader("dashed_lines");
#else
    GLShaderProgram* shader = OpenGLManager::get_gl_info().is_core_profile() ? wxGetApp().get_shader("dashed_thick_lines") : wxGetApp().get_shader("flat");
#endif // SLIC3R_OPENGL_ES
    if (shader != nullptr) {
        shader->start_using();
        shader->set_uniform("view_model_matrix", Transform3d::Identity());
        shader->set_uniform("projection_matrix", Transform3d::Identity());
#if !SLIC3R_OPENGL_ES
        if (OpenGLManager::get_gl_info().is_core_profile()) {
#endif // !SLIC3R_OPENGL_ES
            const std::array<int, 4>& viewport = wxGetApp().plater()->get_camera().get_viewport();
            shader->set_uniform("viewport_size", Vec2d(double(viewport[2]), double(viewport[3])));
            shader->set_uniform("width", 0.25f);
            shader->set_uniform("gap_size", 0.0f);
#if !SLIC3R_OPENGL_ES
        }
#endif // !SLIC3R_OPENGL_ES
        m_profile.baseline.render();
        m_profile.profile.render();
        shader->stop_using();
    }
}

void GLCanvas3D::LayersEditing::render_volumes(const GLCanvas3D& canvas, const GLVolumeCollection& volumes)
{
    assert(this->is_allowed());
    assert(this->last_object_id != -1);

    GLShaderProgram* current_shader = wxGetApp().get_current_shader();
    ScopeGuard guard([current_shader]() { if (current_shader != nullptr) current_shader->start_using(); });
    if (current_shader != nullptr)
        current_shader->stop_using();

    GLShaderProgram* shader = wxGetApp().get_shader("variable_layer_height");
    if (shader == nullptr)
        return;

    shader->start_using();

    generate_layer_height_texture();

    // Uniforms were resolved, go ahead using the layer editing shader.
    shader->set_uniform("z_to_texture_row", float(m_layers_texture.cells - 1) / (float(m_layers_texture.width) * float(m_object_max_z)));
    shader->set_uniform("z_texture_row_to_normalized", 1.0f / float(m_layers_texture.height));
    shader->set_uniform("z_cursor", float(m_object_max_z) * float(this->get_cursor_z_relative(canvas)));
    shader->set_uniform("z_cursor_band_width", float(this->band_width));

    const Camera& camera = wxGetApp().plater()->get_camera();
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());

    // Initialize the layer height texture mapping.
    const GLsizei w = (GLsizei)m_layers_texture.width;
    const GLsizei h = (GLsizei)m_layers_texture.height;
    const GLsizei half_w = w / 2;
    const GLsizei half_h = h / 2;
    glsafe(::glPixelStorei(GL_UNPACK_ALIGNMENT, 1));
    glsafe(::glBindTexture(GL_TEXTURE_2D, m_z_texture_id));
    glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0));
    glsafe(::glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA, half_w, half_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0));
    glsafe(::glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, m_layers_texture.data.data()));
    glsafe(::glTexSubImage2D(GL_TEXTURE_2D, 1, 0, 0, half_w, half_h, GL_RGBA, GL_UNSIGNED_BYTE, m_layers_texture.data.data() + m_layers_texture.width * m_layers_texture.height * 4));
    for (GLVolume* glvolume : volumes.volumes) {
        // Render the object using the layer editing shader and texture.
        if (!glvolume->is_active || glvolume->composite_id.object_id != this->last_object_id || glvolume->is_modifier)
            continue;

        shader->set_uniform("volume_world_matrix", glvolume->world_matrix());
        shader->set_uniform("object_max_z", 0.0f);
        const Transform3d& view_matrix = camera.get_view_matrix();
        const Transform3d model_matrix = glvolume->world_matrix();
        shader->set_uniform("view_model_matrix", view_matrix * model_matrix);
        const Matrix3d view_normal_matrix = view_matrix.matrix().block(0, 0, 3, 3) * model_matrix.matrix().block(0, 0, 3, 3).inverse().transpose();
        shader->set_uniform("view_normal_matrix", view_normal_matrix);

        glvolume->render();
    }
    // Revert back to the previous shader.
    glsafe(::glBindTexture(GL_TEXTURE_2D, 0));
}

void GLCanvas3D::LayersEditing::adjust_layer_height_profile()
{
    this->update_slicing_parameters();
    PrintObject::update_layer_height_profile(*m_model_object, *m_slicing_parameters, m_layer_height_profile);
    Slic3r::adjust_layer_height_profile(*m_slicing_parameters, m_layer_height_profile, this->last_z, this->strength, this->band_width, this->last_action);
    m_layer_height_profile_modified = true;
    m_layers_texture.valid = false;
}

void GLCanvas3D::LayersEditing::reset_layer_height_profile(GLCanvas3D& canvas)
{
    const_cast<ModelObject*>(m_model_object)->layer_height_profile.clear();
    m_layer_height_profile.clear();
    m_layers_texture.valid = false;
    canvas.post_event(SimpleEvent(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS));
    wxGetApp().obj_list()->update_info_items(last_object_id);
}

void GLCanvas3D::LayersEditing::adaptive_layer_height_profile(GLCanvas3D& canvas, float quality_factor)
{
    this->update_slicing_parameters();
    m_layer_height_profile = layer_height_profile_adaptive(*m_slicing_parameters, *m_model_object, quality_factor);
    const_cast<ModelObject*>(m_model_object)->layer_height_profile.set(m_layer_height_profile);
    m_layers_texture.valid = false;
    canvas.post_event(SimpleEvent(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS));
    wxGetApp().obj_list()->update_info_items(last_object_id);
}

void GLCanvas3D::LayersEditing::smooth_layer_height_profile(GLCanvas3D& canvas, const HeightProfileSmoothingParams& smoothing_params)
{
    this->update_slicing_parameters();
    m_layer_height_profile = smooth_height_profile(m_layer_height_profile, *m_slicing_parameters, smoothing_params);
    const_cast<ModelObject*>(m_model_object)->layer_height_profile.set(m_layer_height_profile);
    m_layers_texture.valid = false;
    canvas.post_event(SimpleEvent(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS));
    wxGetApp().obj_list()->update_info_items(last_object_id);
}

void GLCanvas3D::LayersEditing::generate_layer_height_texture()
{
    this->update_slicing_parameters();
    // Always try to update the layer height profile.
    bool update = ! m_layers_texture.valid;
    if (PrintObject::update_layer_height_profile(*m_model_object, *m_slicing_parameters, m_layer_height_profile)) {
        // Initialized to the default value.
        m_layer_height_profile_modified = false;
        update = true;
    }

    // Update if the layer height profile was changed, or when the texture is not valid.
    if (! update && ! m_layers_texture.data.empty() && m_layers_texture.cells > 0)
        // Texture is valid, don't update.
        return; 

    if (m_layers_texture.data.empty()) {
        m_layers_texture.width  = 1024;
        m_layers_texture.height = 1024;
        m_layers_texture.levels = 2;
        m_layers_texture.data.assign(m_layers_texture.width * m_layers_texture.height * 5, 0);
    }

    bool level_of_detail_2nd_level = true;
    m_layers_texture.cells = Slic3r::generate_layer_height_texture(
        *m_slicing_parameters, 
        Slic3r::generate_object_layers(*m_slicing_parameters, m_layer_height_profile), 
		m_layers_texture.data.data(), m_layers_texture.height, m_layers_texture.width, level_of_detail_2nd_level);
    m_layers_texture.valid = true;
}

void GLCanvas3D::LayersEditing::accept_changes(GLCanvas3D& canvas)
{
    if (last_object_id >= 0) {
        if (m_layer_height_profile_modified) {
            wxGetApp().plater()->take_snapshot(_L("Variable layer height - Manual edit"));
            const_cast<ModelObject*>(m_model_object)->layer_height_profile.set(m_layer_height_profile);
            wxGetApp().obj_list()->update_info_items(last_object_id);
            wxGetApp().plater()->schedule_background_process();
        }
    }
    m_layer_height_profile_modified = false;
}

void GLCanvas3D::LayersEditing::update_slicing_parameters() {
	if (m_slicing_parameters == nullptr) {
		m_slicing_parameters = new SlicingParameters();
        *m_slicing_parameters = PrintObject::slicing_parameters(*m_config, *m_model_object, m_object_max_z, m_shrinkage_compensation);
    }
}

float GLCanvas3D::LayersEditing::thickness_bar_width(const GLCanvas3D &canvas)
{
    return
#if ENABLE_RETINA_GL
        canvas.get_canvas_size().get_scale_factor()
#else
        canvas.get_wxglcanvas()->GetContentScaleFactor()
#endif
         * THICKNESS_BAR_WIDTH;
}


const Point GLCanvas3D::Mouse::Drag::Invalid_2D_Point(INT_MAX, INT_MAX);
const Vec3d GLCanvas3D::Mouse::Drag::Invalid_3D_Point(DBL_MAX, DBL_MAX, DBL_MAX);
const int GLCanvas3D::Mouse::Drag::MoveThresholdPx = 5;

void GLCanvas3D::Labels::render(const std::vector<const ModelInstance*>& sorted_instances) const
{
    if (!m_enabled || !is_shown())
        return;

    const Camera& camera = wxGetApp().plater()->get_camera();
    const Model* model = m_canvas.get_model();
    if (model == nullptr)
        return;

    Transform3d world_to_eye = camera.get_view_matrix();
    Transform3d world_to_screen = camera.get_projection_matrix() * world_to_eye;
    const std::array<int, 4>& viewport = camera.get_viewport();

    struct Owner
    {
        int obj_idx;
        int inst_idx;
        size_t model_instance_id;
        BoundingBoxf3 world_box;
        double eye_center_z;
        std::string title;
        std::string label;
        std::string print_order;
        bool selected;
    };

    // collect owners world bounding boxes and data from volumes
    std::vector<Owner> owners;
    const GLVolumeCollection& volumes = m_canvas.get_volumes();
    for (const GLVolume* volume : volumes.volumes) {
        int obj_idx = volume->object_idx();
        if (0 <= obj_idx && obj_idx < (int)model->objects.size()) {
            int inst_idx = volume->instance_idx();
            std::vector<Owner>::iterator it = std::find_if(owners.begin(), owners.end(), [obj_idx, inst_idx](const Owner& owner) {
                return (owner.obj_idx == obj_idx) && (owner.inst_idx == inst_idx);
                });
            if (it != owners.end()) {
                it->world_box.merge(volume->transformed_bounding_box());
                it->selected &= volume->selected;
            } else {
                const ModelObject* model_object = model->objects[obj_idx];
                Owner owner;
                owner.obj_idx = obj_idx;
                owner.inst_idx = inst_idx;
                owner.model_instance_id = model_object->instances[inst_idx]->id().id;
                owner.world_box = volume->transformed_bounding_box();
                owner.title = "object" + std::to_string(obj_idx) + "_inst##" + std::to_string(inst_idx);
                owner.label = model_object->name;
                if (model_object->instances.size() > 1)
                    owner.label += " (" + std::to_string(inst_idx + 1) + ")";
                owner.selected = volume->selected;
                owners.emplace_back(owner);
            }
        }
    }

    // updates print order strings
    if (sorted_instances.size() > 1) {
        for (size_t i = 0; i < sorted_instances.size(); ++i) {
            size_t id = sorted_instances[i]->id().id;
            std::vector<Owner>::iterator it = std::find_if(owners.begin(), owners.end(), [id](const Owner& owner) {
                return owner.model_instance_id == id;
                });
            if (it != owners.end())
                it->print_order = _u8L("Seq.") + "#: " + std::to_string(i + 1);
        }
    }

    // calculate eye bounding boxes center zs
    for (Owner& owner : owners) {
        owner.eye_center_z = (world_to_eye * owner.world_box.center())(2);
    }

    // sort owners by center eye zs and selection
    std::sort(owners.begin(), owners.end(), [](const Owner& owner1, const Owner& owner2) {
        if (!owner1.selected && owner2.selected)
            return true;
        else if (owner1.selected && !owner2.selected)
            return false;
        else
            return (owner1.eye_center_z < owner2.eye_center_z);
        });

    // render info windows
    for (const Owner& owner : owners) {
        Vec3d screen_box_center = world_to_screen * owner.world_box.center();
        float x = 0.0f;
        float y = 0.0f;
        if (camera.get_type() == Camera::EType::Perspective) {
            x = (0.5f + 0.001f * 0.5f * (float)screen_box_center(0)) * viewport[2];
            y = (0.5f - 0.001f * 0.5f * (float)screen_box_center(1)) * viewport[3];
        } else {
            x = (0.5f + 0.5f * (float)screen_box_center(0)) * viewport[2];
            y = (0.5f - 0.5f * (float)screen_box_center(1)) * viewport[3];
        }

        if (x < 0.0f || viewport[2] < x || y < 0.0f || viewport[3] < y)
            continue;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, owner.selected ? 3.0f : 1.5f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Border, owner.selected ? ImVec4(0.757f, 0.404f, 0.216f, 1.0f) : ImVec4(0.75f, 0.75f, 0.75f, 1.0f));
        ImGuiPureWrap::set_next_window_pos(x, y, ImGuiCond_Always, 0.5f, 0.5f);
        ImGuiPureWrap::begin(owner.title, ImGuiWindowFlags_NoMouseInputs | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        float win_w = ImGui::GetWindowWidth();
        float label_len = ImGui::CalcTextSize(owner.label.c_str()).x;
        ImGui::SetCursorPosX(0.5f * (win_w - label_len));
        ImGui::AlignTextToFramePadding();
        ImGuiPureWrap::text(owner.label);

        if (!owner.print_order.empty()) {
            ImGui::Separator();
            float po_len = ImGui::CalcTextSize(owner.print_order.c_str()).x;
            ImGui::SetCursorPosX(0.5f * (win_w - po_len));
            ImGui::AlignTextToFramePadding();
            ImGuiPureWrap::text(owner.print_order);
        }

        // force re-render while the windows gets to its final size (it takes several frames)
        if (ImGui::GetWindowContentRegionWidth() + 2.0f * ImGui::GetStyle().WindowPadding.x != ImGui::CalcWindowNextAutoFitSize(ImGui::GetCurrentWindow()).x)
            wxGetApp().imgui()->set_requires_extra_frame();

        ImGuiPureWrap::end();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }
}

static float get_cursor_height()
{
    float ret = 16.0f;
#ifdef _WIN32
    // see: https://forums.codeguru.com/showthread.php?449040-get-the-system-current-cursor-size
    // this code is not perfect because it returns a maximum height equal to 31 even if the cursor bitmap shown on screen is bigger
    // but at least it gives the same result as wxWidgets in the settings tabs
    ICONINFO ii;
    if (::GetIconInfo((HICON)GetCursor(), &ii) != 0) {
        BITMAP bitmap;
        ::GetObject(ii.hbmMask, sizeof(BITMAP), &bitmap);
        const int width = bitmap.bmWidth;
        const int height = (ii.hbmColor == nullptr) ? bitmap.bmHeight / 2 : bitmap.bmHeight;
        HDC dc = ::CreateCompatibleDC(nullptr);
        if (dc != nullptr) {
            if (::SelectObject(dc, ii.hbmMask) != nullptr) {
                for (int i = 0; i < width; ++i) {
                    for (int j = 0; j < height; ++j) {
                        if (::GetPixel(dc, i, j) != RGB(255, 255, 255)) {
                            if (ret < float(j))
                                ret = float(j);
                        }
                    }
                }
                ::DeleteDC(dc);
            }
        }
        ::DeleteObject(ii.hbmColor);
        ::DeleteObject(ii.hbmMask);
    }
#endif //  _WIN32
    return ret;
}

void GLCanvas3D::Tooltip::set_text(const std::string& text)
{
    // If the mouse is inside an ImGUI dialog, then the tooltip is suppressed.
    const std::string& new_text = m_in_imgui ? std::string() : text;
    if (m_text != new_text) { // To avoid calling the expensive call to get_cursor_height.
        m_text = new_text;
        m_cursor_height = get_cursor_height();
    }
}

void GLCanvas3D::Tooltip::render(const Vec2d& mouse_position, GLCanvas3D& canvas)
{
    static ImVec2 size(0.0f, 0.0f);

    auto validate_position = [this](const Vec2d& position, const GLCanvas3D& canvas, const ImVec2& wnd_size) {
        const Size cnv_size = canvas.get_canvas_size();
        const float x = std::clamp(float(position.x()), 0.0f, float(cnv_size.get_width()) - wnd_size.x);
        const float y = std::clamp(float(position.y()) + m_cursor_height, 0.0f, float(cnv_size.get_height()) - wnd_size.y);
        return Vec2f(x, y);
    };

    if (m_text.empty()) {
        m_start_time = std::chrono::steady_clock::now();
        return;
    }

    // draw the tooltip as hidden until the delay is expired
    // use a value of alpha slightly different from 0.0f because newer imgui does not calculate properly the window size if alpha == 0.0f
    const float alpha = (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_start_time).count() < 500) ? 0.01f : 1.0f;

    const Vec2f position = validate_position(mouse_position, canvas, size);

    ImGuiWrapper& imgui = *wxGetApp().imgui();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    ImGuiPureWrap::set_next_window_pos(position.x(), position.y(), ImGuiCond_Always, 0.0f, 0.0f);

    ImGuiPureWrap::begin("canvas_tooltip", ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMouseInputs | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    ImGui::TextUnformatted(m_text.c_str());

    // force re-render while the windows gets to its final size (it may take several frames) or while hidden
    if (alpha < 1.0f || ImGui::GetWindowContentRegionWidth() + 2.0f * ImGui::GetStyle().WindowPadding.x != ImGui::CalcWindowNextAutoFitSize(ImGui::GetCurrentWindow()).x)
        imgui.set_requires_extra_frame();

    size = ImGui::GetWindowSize();

    ImGuiPureWrap::end();
    ImGui::PopStyleVar(2);
}

void GLCanvas3D::SequentialPrintClearance::set_contours(const ContoursList& contours, bool generate_fill)
{
    m_contours.clear();
    m_instances.clear();
    m_fill.reset();

    if (contours.empty())
        return;

    const Vec3d bed_offset = generate_fill ? s_multiple_beds.get_bed_translation(s_multiple_beds.get_active_bed()) : Vec3d::Zero();

    if (generate_fill) {
        GLModel::Geometry fill_data;
        fill_data.format = { GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3 };
        fill_data.color = { 0.3333f, 0.0f, 0.0f, 0.5f };

        // vertices + indices
        const ExPolygons polygons_union = union_ex(contours.contours);
        unsigned int vertices_counter = 0;
        for (const ExPolygon& poly : polygons_union) {
            const std::vector<Vec3d> triangulation = triangulate_expolygon_3d(poly);
            fill_data.reserve_vertices(fill_data.vertices_count() + triangulation.size());
            fill_data.reserve_indices(fill_data.indices_count() + triangulation.size());
            for (const Vec3d& v : triangulation) {
                fill_data.add_vertex((Vec3f)((bed_offset + v).cast<float>() + 0.0125f * Vec3f::UnitZ())); // add a small positive z to avoid z-fighting
                ++vertices_counter;
                if (vertices_counter % 3 == 0)
                    fill_data.add_triangle(vertices_counter - 3, vertices_counter - 2, vertices_counter - 1);
            }
        }
        m_fill.init_from(std::move(fill_data));
    }

    const Transform3d bed_transform = Geometry::translation_transform(bed_offset);

    for (size_t i = 0; i < contours.contours.size(); ++i) {
        GLModel& model = m_contours.emplace_back(GLModel());
        model.init_from(contours.contours[i], 0.025f); // add a small positive z to avoid z-fighting
    }

    if (contours.trafos.has_value()) {
        // create the requested instances
        for (const auto& instance : *contours.trafos) {
            m_instances.emplace_back(instance.first, bed_transform * instance.second);
        }
    }
    else {
        // no instances have been specified
        // create one instance for every polygon
        for (size_t i = 0; i < contours.contours.size(); ++i) {
            m_instances.emplace_back(i, bed_transform);
        }
    }
}

void GLCanvas3D::SequentialPrintClearance::update_instances_trafos(const std::vector<Transform3d>& trafos)
{
    if (trafos.size() == m_instances.size()) {
        for (size_t i = 0; i < trafos.size(); ++i) {
            m_instances[i].second = trafos[i];
        }
    }
    else
      assert(false);
}

void GLCanvas3D::SequentialPrintClearance::render()
{
    static const ColorRGBA FILL_COLOR               = { 1.0f, 0.0f, 0.0f, 0.5f };
    static const ColorRGBA NO_FILL_COLOR            = { 1.0f, 1.0f, 1.0f, 0.75f };
    static const ColorRGBA NO_FILL_EVALUATING_COLOR = { 1.0f, 1.0f, 0.0f, 1.0f };

    if (m_contours.empty() || m_instances.empty())
        return;

    GLShaderProgram* shader = wxGetApp().get_shader("flat");
    if (shader == nullptr)
        return;

    shader->start_using();

    const Camera& camera = wxGetApp().plater()->get_camera();
    shader->set_uniform("view_model_matrix", camera.get_view_matrix());
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());

    glsafe(::glEnable(GL_DEPTH_TEST));
    glsafe(::glDisable(GL_CULL_FACE));
    glsafe(::glEnable(GL_BLEND));
    glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

    if (!m_evaluating && !m_dragging)
        m_fill.render();

#if !SLIC3R_OPENGL_ES
    if (OpenGLManager::get_gl_info().is_core_profile()) {
#endif // !SLIC3R_OPENGL_ES
        shader->stop_using();

#if SLIC3R_OPENGL_ES
        shader = wxGetApp().get_shader("dashed_lines");
#else
        shader = wxGetApp().get_shader("dashed_thick_lines");
#endif // SLIC3R_OPENGL_ES
        if (shader == nullptr)
            return;

        shader->start_using();
        shader->set_uniform("projection_matrix", camera.get_projection_matrix());
        const std::array<int, 4>& viewport = camera.get_viewport();
        shader->set_uniform("viewport_size", Vec2d(double(viewport[2]), double(viewport[3])));
        shader->set_uniform("width", 1.0f);
        shader->set_uniform("gap_size", 0.0f);
#if !SLIC3R_OPENGL_ES
    }
    else
        glsafe(::glLineWidth(2.0f));
#endif // !SLIC3R_OPENGL_ES

    const ColorRGBA color = (!m_evaluating && !m_dragging && m_fill.is_initialized()) ? FILL_COLOR :
        m_evaluating ? NO_FILL_EVALUATING_COLOR : NO_FILL_COLOR;

    for (const auto& [id, trafo] : m_instances) {
        shader->set_uniform("view_model_matrix", camera.get_view_matrix() * trafo);
        assert(id < m_contours.size());
        m_contours[id].set_color(color);
        m_contours[id].render();
    }

    glsafe(::glDisable(GL_BLEND));
    glsafe(::glEnable(GL_CULL_FACE));
    glsafe(::glDisable(GL_DEPTH_TEST));

    shader->stop_using();
}

wxDEFINE_EVENT(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_OBJECT_SELECT, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_RIGHT_CLICK, RBtnEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_REMOVE_OBJECT, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_ARRANGE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_ARRANGE_CURRENT_BED, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_SELECT_ALL, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_QUESTION_MARK, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_INCREASE_INSTANCES, Event<int>);
wxDEFINE_EVENT(EVT_GLCANVAS_INSTANCE_MOVED, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_INSTANCE_ROTATED, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_RESET_SKEW, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_INSTANCE_SCALED, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_INSTANCE_MIRRORED, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_FORCE_UPDATE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_WIPETOWER_TOUCHED, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_ENABLE_ACTION_BUTTONS, Event<bool>);
wxDEFINE_EVENT(EVT_GLCANVAS_UPDATE_GEOMETRY, Vec3dsEvent<2>);
wxDEFINE_EVENT(EVT_GLCANVAS_MOUSE_DRAGGING_STARTED, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_MOUSE_DRAGGING_FINISHED, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_UPDATE_BED_SHAPE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_TAB, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_RESETGIZMOS, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_SLIDERS_MANIPULATION, wxKeyEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_UNDO, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_REDO, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_COLLAPSE_SIDEBAR, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_RESET_LAYER_HEIGHT_PROFILE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_ADAPTIVE_LAYER_HEIGHT_PROFILE, Event<float>);
wxDEFINE_EVENT(EVT_GLCANVAS_SMOOTH_LAYER_HEIGHT_PROFILE, HeightProfileSmoothEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_RELOAD_FROM_DISK, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_RENDER_TIMER, wxTimerEvent/*RenderTimerEvent*/);
wxDEFINE_EVENT(EVT_GLCANVAS_TOOLBAR_HIGHLIGHTER_TIMER, wxTimerEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_GIZMO_HIGHLIGHTER_TIMER, wxTimerEvent);

const double GLCanvas3D::DefaultCameraZoomToBoxMarginFactor = 1.25;

static std::vector<int> processed_objects_idxs(const Model& model, const SLAPrint& sla_print, const GLVolumePtrs& volumes)
{
    std::vector<int> ret;
    GLVolumePtrs matching_volumes;
    std::copy_if(volumes.begin(), volumes.end(), std::back_inserter(matching_volumes), [](GLVolume* v) {
        return v->volume_idx() == -(int)slaposDrillHoles; });
    for (const GLVolume* v : matching_volumes) {
        const int mo_idx = v->object_idx();
        const ModelObject* model_object = (mo_idx < (int)model.objects.size()) ? model.objects[mo_idx] : nullptr;
        if (model_object != nullptr && model_object->instances[v->instance_idx()]->is_printable()) {
            const SLAPrintObject* print_object = sla_print.get_print_object_by_model_object_id(model_object->id());
            if (print_object != nullptr && print_object->get_parts_to_slice().size() > 1)
                ret.push_back(mo_idx);
        }
    }
    std::sort(ret.begin(), ret.end());
    ret.erase(std::unique(ret.begin(), ret.end()), ret.end());
    return ret;
};

static bool composite_id_match(const GLVolume::CompositeID& id1, const GLVolume::CompositeID& id2)
{
    return id1.object_id == id2.object_id && id1.instance_id == id2.instance_id;
}

static bool object_contains_negative_volumes(const Model& model, int obj_id) {
    return (0 <= obj_id && obj_id < (int)model.objects.size()) ? model.objects[obj_id]->has_negative_volume_mesh() : false;
}

static bool object_has_sla_drain_holes(const Model& model, int obj_id) {
    return (0 <= obj_id && obj_id < (int)model.objects.size()) ? model.objects[obj_id]->has_sla_drain_holes() : false;
}

void GLCanvas3D::SLAView::detect_type_from_volumes(const GLVolumePtrs& volumes)
{
    for (auto& [id, type] : m_instances_cache) {
        type = ESLAViewType::Original;
    }

    for (const GLVolume* v : volumes) {
        if (v->volume_idx() == -(int)slaposDrillHoles) {
            if (object_contains_negative_volumes(*m_parent.get_model(), v->composite_id.object_id) ||
                object_has_sla_drain_holes(*m_parent.get_model(), v->composite_id.object_id)) {
                const InstancesCacheItem* instance = find_instance_item(v->composite_id);
                assert(instance != nullptr);
                set_type(instance->first, ESLAViewType::Processed);
            }
        }
    }
}

void GLCanvas3D::SLAView::set_type(ESLAViewType new_type)
{
    for (auto& [id, type] : m_instances_cache) {
        type = new_type;
        if (new_type == ESLAViewType::Processed)
            select_full_instance(id);
    }
}

void GLCanvas3D::SLAView::set_type(const GLVolume::CompositeID& id, ESLAViewType new_type)
{
    InstancesCacheItem* instance = find_instance_item(id);
    assert(instance != nullptr);
    instance->second = new_type;
    if (new_type == ESLAViewType::Processed)
        select_full_instance(id);
}

void GLCanvas3D::SLAView::update_volumes_visibility(GLVolumePtrs& volumes)
{
    const SLAPrint* sla_print = m_parent.sla_print();
    const std::vector<int> mo_idxs = (sla_print != nullptr) ? processed_objects_idxs(*m_parent.get_model(), *sla_print, volumes) : std::vector<int>();

    std::vector<std::shared_ptr<SceneRaycasterItem>>* raycasters = m_parent.get_raycasters_for_picking(SceneRaycaster::EType::Volume);

    for (GLVolume* v : volumes) {
        const int obj_idx = v->object_idx();
        bool active = std::find(mo_idxs.begin(), mo_idxs.end(), obj_idx) == mo_idxs.end();
        if (!active) {
            const InstancesCacheItem* instance = find_instance_item(v->composite_id);
            assert(instance != nullptr);
            active = (instance->second == ESLAViewType::Processed) ? v->volume_idx() < 0 : v->volume_idx() != -(int)slaposDrillHoles;
        }
        v->is_active = active;
        auto it = std::find_if(raycasters->begin(), raycasters->end(), [v](std::shared_ptr<SceneRaycasterItem> item) { return item->get_raycaster() == v->mesh_raycaster.get(); });
        if (it != raycasters->end())
            (*it)->set_active(v->is_active);
    }
}

void GLCanvas3D::SLAView::update_instances_cache(const std::vector<std::pair<GLVolume::CompositeID, GLVolume::CompositeID>>& new_to_old_ids_map)
{
    // First, extract current instances list from the volumes
    const GLVolumePtrs& volumes = m_parent.get_volumes().volumes;
    std::vector<InstancesCacheItem> new_instances_cache;
    for (const GLVolume* v : volumes) {
        new_instances_cache.emplace_back(v->composite_id, ESLAViewType::Original);
    }

    std::sort(new_instances_cache.begin(), new_instances_cache.end(),
        [](const InstancesCacheItem& i1, const InstancesCacheItem& i2) {
            return i1.first.object_id < i2.first.object_id || (i1.first.object_id == i2.first.object_id && i1.first.instance_id < i2.first.instance_id); });

    new_instances_cache.erase(std::unique(new_instances_cache.begin(), new_instances_cache.end(),
        [](const InstancesCacheItem& i1, const InstancesCacheItem& i2) {
            return composite_id_match(i1.first, i2.first); }), new_instances_cache.end());

    // Second, update instances type from previous state
    for (auto& inst_type : new_instances_cache) {
        const auto map_to_old_it = std::find_if(new_to_old_ids_map.begin(), new_to_old_ids_map.end(), [&inst_type](const std::pair<GLVolume::CompositeID, GLVolume::CompositeID>& item) {
            return composite_id_match(inst_type.first, item.first); });

        const GLVolume::CompositeID old_inst_id = (map_to_old_it != new_to_old_ids_map.end()) ? map_to_old_it->second : inst_type.first;
        const InstancesCacheItem* old_instance = find_instance_item(old_inst_id);
        if (old_instance != nullptr)
            inst_type.second = old_instance->second;
    }

    m_instances_cache = new_instances_cache;
}

void GLCanvas3D::SLAView::render_switch_button()
{
    const SLAPrint* sla_print = m_parent.sla_print();
    if (sla_print == nullptr)
        return;

    const std::vector<int> mo_idxs = processed_objects_idxs(*m_parent.get_model(), *sla_print, m_parent.get_volumes().volumes);
    if (mo_idxs.empty())
        return;

    Selection& selection = m_parent.get_selection();
    const int obj_idx = selection.get_object_idx();
    if (std::find(mo_idxs.begin(), mo_idxs.end(), obj_idx) == mo_idxs.end())
        return;

    if (!object_contains_negative_volumes(*m_parent.get_model(), obj_idx))
        return;

    const int inst_idx = selection.get_instance_idx();
    if (inst_idx < 0)
        return;

    const GLVolume::CompositeID composite_id(obj_idx, 0, inst_idx);
    const InstancesCacheItem* sel_instance = find_instance_item(composite_id);
    if (sel_instance == nullptr)
        return;

    const ESLAViewType type = sel_instance->second;

    BoundingBoxf ss_box;
    if (m_use_instance_bbox) {
        const Selection::EMode mode = selection.get_mode();
        if (obj_idx >= 0 && inst_idx >= 0) {
            const Selection::IndicesList selected_idxs = selection.get_volume_idxs();
            std::vector<unsigned int> idxs_as_vector;
            idxs_as_vector.assign(selected_idxs.begin(), selected_idxs.end());
            selection.add_instance(obj_idx, inst_idx, true);
            ss_box = selection.get_screen_space_bounding_box();
            selection.add_volumes(mode, idxs_as_vector, true);
        }
    }
        
    if (!ss_box.defined)
        ss_box = selection.get_screen_space_bounding_box();
    assert(ss_box.defined);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::SetNextWindowPos(ImVec2((float)ss_box.max.x(), (float)ss_box.center().y()), ImGuiCond_Always, ImVec2(0.0, 0.5));
    ImGuiPureWrap::begin(std::string("SLAViewSwitch"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration);
    const float icon_size = 1.5 * ImGui::GetTextLineHeight();
    if (ImGuiPureWrap::draw_radio_button(_u8L("SLA view"), 1.5f * icon_size, true,
        [sel_instance](ImGuiWindow& window, const ImVec2& pos, float size) {
            const wchar_t icon_id = (sel_instance->second == ESLAViewType::Original) ? ImGui::SlaViewProcessed : ImGui::SlaViewOriginal;
            wxGetApp().imgui()->draw_icon(window, pos, size, icon_id);
        })) {
        switch (sel_instance->second)
        {
        case ESLAViewType::Original:  { m_parent.set_sla_view_type(sel_instance->first, ESLAViewType::Processed); break; }
        case ESLAViewType::Processed: { m_parent.set_sla_view_type(sel_instance->first, ESLAViewType::Original); break; }
        default: { assert(false); break; }
        }
    }

    if (ImGui::IsItemHovered()) {
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGuiPureWrap::COL_WINDOW_BACKGROUND);
        ImGui::BeginTooltip();
        std::string tooltip;
        switch (type)
        {
        case ESLAViewType::Original:  { tooltip = _u8L("Show as processed"); break; }
        case ESLAViewType::Processed: { tooltip = _u8L("Show as original"); break; }
        default: { assert(false); break; }
        }

        ImGuiPureWrap::text(tooltip);
        ImGui::EndTooltip();
        ImGui::PopStyleColor();
    }
    ImGuiPureWrap::end();
    ImGui::PopStyleColor(2);
}

#if ENABLE_SLA_VIEW_DEBUG_WINDOW
void GLCanvas3D::SLAView::render_debug_window()
{
    ImGuiWrapper& imgui = *wxGetApp().imgui();
    ImGuiPureWrap::begin(std::string("SLAView"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize);
    for (const auto& [id, type] : m_instances_cache) {
        ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, "(" + std::to_string(id.object_id) + ", " + std::to_string(id.instance_id) + ")");
        ImGui::SameLine();
        ImGuiPureWrap::text_colored(ImGui::GetStyleColorVec4(ImGuiCol_Text), (type == ESLAViewType::Original) ? "Original" : "Processed");
    }
    if (!m_instances_cache.empty())
        ImGui::Separator();

    ImGuiPureWrap::checkbox("Use instance bounding box", m_use_instance_bbox);
    ImGuiPureWrap::end();
}
#endif // ENABLE_SLA_VIEW_DEBUG_WINDOW

GLCanvas3D::SLAView::InstancesCacheItem* GLCanvas3D::SLAView::find_instance_item(const GLVolume::CompositeID& id)
{
    auto it = std::find_if(m_instances_cache.begin(), m_instances_cache.end(),
        [&id](const InstancesCacheItem& item) { return composite_id_match(item.first, id); });
    return (it == m_instances_cache.end()) ? nullptr : &(*it);
}

void GLCanvas3D::SLAView::select_full_instance(const GLVolume::CompositeID& id)
{
    bool extended_selection = false;
    Selection& selection = m_parent.get_selection();
    const Selection::ObjectIdxsToInstanceIdxsMap& sel_cache = selection.get_content();
    auto obj_it = sel_cache.find(id.object_id);
    if (obj_it != sel_cache.end()) {
        auto inst_it = std::find(obj_it->second.begin(), obj_it->second.end(), id.instance_id);
        if (inst_it != obj_it->second.end()) {
            selection.add_instance(id.object_id, id.instance_id);
            extended_selection = true;
        }
    }

    if (extended_selection)
        m_parent.post_event(SimpleEvent(EVT_GLCANVAS_OBJECT_SELECT));
}

PrinterTechnology GLCanvas3D::current_printer_technology() const
{
    return m_process ? m_process->current_printer_technology() : ptFFF;
}

bool GLCanvas3D::is_arrange_alignment_enabled() const
{
    if (m_config == nullptr) {
        return false;
    }
    if (!is_XL_printer(*m_config)) {
        return false;
    }
    if (this->m_wipe_tower_bounding_boxes[s_multiple_beds.get_active_bed()]) {
        return false;
    }
    return true;
}

GLCanvas3D::GLCanvas3D(wxGLCanvas *canvas, Bed3D &bed)
    : m_canvas(canvas)
    , m_context(nullptr)
    , m_bed(bed)
#if ENABLE_RETINA_GL
    , m_retina_helper(nullptr)
#endif // ENABLE_RETINA_GL
    , m_in_render(false)
    , m_main_toolbar(GLToolbar::Normal, "Main")
    , m_undoredo_toolbar(GLToolbar::Normal, "Undo_Redo")
    , m_gizmos(*this)
    , m_use_clipping_planes(false)
    , m_sidebar_field("")
    , m_extra_frame_requested(false)
    , m_config(nullptr)
    , m_process(nullptr)
    , m_model(nullptr)
    , m_dirty(true)
    , m_initialized(false)
    , m_apply_zoom_to_volumes_filter(false)
    , m_picking_enabled(false)
    , m_moving_enabled(false)
    , m_dynamic_background_enabled(false)
    , m_multisample_allowed(false)
    , m_moving(false)
    , m_tab_down(false)
    , m_cursor_type(Standard)
    , m_reload_delayed(false)
    , m_render_sla_auxiliaries(true)
    , m_labels(*this)
    , m_slope(m_volumes)
    , m_sla_view(*this)
    , m_arrange_settings_db{wxGetApp().app_config}
    , m_arrange_settings_dialog{wxGetApp().imgui(), &m_arrange_settings_db}
{
    if (m_canvas != nullptr) {
        m_timer.SetOwner(m_canvas);
        m_render_timer.SetOwner(m_canvas);
#if ENABLE_RETINA_GL
        m_retina_helper.reset(new RetinaHelper(canvas));
#endif // ENABLE_RETINA_GL
    }

    m_selection.set_volumes(&m_volumes.volumes);
    m_arrange_settings_dialog.show_xl_align_combo([this](){
        return this->is_arrange_alignment_enabled();
    });
    m_arrange_settings_dialog.on_arrange_btn([]{
        wxGetApp().plater()->arrange();
    });
    m_arrange_settings_dialog.on_arrange_bed_btn([]{
        wxGetApp().plater()->arrange_current_bed();
    });
}

GLCanvas3D::~GLCanvas3D()
{
    reset_volumes();
}

void GLCanvas3D::post_event(wxEvent &&event)
{
    event.SetEventObject(m_canvas);
    wxPostEvent(m_canvas, event);
}

wxWindow* GLCanvas3D::get_wxglcanvas_parent()
{
    return m_canvas->GetParent();
}

bool GLCanvas3D::init()
{
    if (m_initialized)
        return true;

    if (m_canvas == nullptr || m_context == nullptr)
        return false;

    glsafe(::glClearColor(1.0f, 1.0f, 1.0f, 1.0f));
#if SLIC3R_OPENGL_ES
    glsafe(::glClearDepthf(1.0f));
#else
    glsafe(::glClearDepth(1.0f));
#endif // SLIC3R_OPENGL_ES

    glsafe(::glDepthFunc(GL_LESS));

    glsafe(::glEnable(GL_DEPTH_TEST));
    glsafe(::glEnable(GL_CULL_FACE));
    glsafe(::glEnable(GL_BLEND));
    glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

    if (m_multisample_allowed)
        glsafe(::glEnable(GL_MULTISAMPLE));

    if (m_main_toolbar.is_enabled())
        m_layers_editing.init();

    if (m_gizmos.is_enabled() && !m_gizmos.init())
        std::cout << "Unable to initialize gizmos: please, check that all the required textures are available" << std::endl;

    if (!_init_toolbars())
        return false;

    if (m_selection.is_enabled() && !m_selection.init())
        return false;

    m_initialized = true;

    return true;
}

void GLCanvas3D::reset_volumes()
{
    if (!m_initialized)
        return;

    if (m_volumes.empty())
        return;

    _set_current();

    m_selection.clear();
    m_volumes.clear();
    m_dirty = true;

    _set_warning_notification(EWarning::ObjectOutside, false);
}

ModelInstanceEPrintVolumeState GLCanvas3D::check_volumes_outside_state(bool selection_only) const
{
    ModelInstanceEPrintVolumeState state = ModelInstanceEPrintVolumeState::ModelInstancePVS_Inside;
    if (m_initialized && !m_volumes.empty())
        check_volumes_outside_state(const_cast<GLVolumeCollection&>(m_volumes), &state, selection_only);
    return state;
}

bool GLCanvas3D::check_volumes_outside_state(GLVolumeCollection& volumes, ModelInstanceEPrintVolumeState* out_state, bool selection_only) const
{
    auto                volume_below = [](GLVolume& volume) -> bool
    { return volume.object_idx() != -1 && volume.volume_idx() != -1 && volume.is_below_printbed(); };
    // Volume is partially below the print bed, thus a pre-calculated convex hull cannot be used.
    auto                volume_sinking = [](GLVolume& volume) -> bool
    { return volume.object_idx() != -1 && volume.volume_idx() != -1 && volume.is_sinking(); };
    // Cached bounding box of a volume above the print bed.
    auto                volume_bbox = [volume_sinking](GLVolume& volume) -> BoundingBoxf3
    { return volume_sinking(volume) ? volume.transformed_non_sinking_bounding_box() : volume.transformed_convex_hull_bounding_box(); };
    // Cached 3D convex hull of a volume above the print bed.
    auto                volume_convex_mesh = [this, volume_sinking](GLVolume& volume) -> const TriangleMesh&
    { return volume_sinking(volume) ? m_model->objects[volume.object_idx()]->volumes[volume.volume_idx()]->mesh() : *volume.convex_hull(); };

    auto volumes_to_process_idxs = [this, &volumes, selection_only]() {
      std::vector<unsigned int> ret;
      if (!selection_only || m_selection.is_empty()) {
          ret = std::vector<unsigned int>(volumes.volumes.size());
          std::iota(ret.begin(), ret.end(), 0);
      }
      else {
          const GUI::Selection::IndicesList& selected_volume_idxs = m_selection.get_volume_idxs();
          ret.assign(selected_volume_idxs.begin(), selected_volume_idxs.end());
      }
      return ret;
    };

    ModelInstanceEPrintVolumeState overall_state = ModelInstancePVS_Inside;
    bool contained_min_one = false;

    const Slic3r::BuildVolume& build_volume = m_bed.build_volume();

    const std::vector<unsigned int> volumes_idxs = volumes_to_process_idxs();
    for (unsigned int vol_idx : volumes_idxs) {
        GLVolume* volume = volumes.volumes[vol_idx];
        if (!volume->is_modifier && (volume->shader_outside_printer_detection_enabled || (!volume->is_wipe_tower() && volume->composite_id.volume_id >= 0))) {
            BuildVolume::ObjectState state;
            int bed_idx = -1;
            if (volume_below(*volume))
                state = BuildVolume::ObjectState::Below;
            else {
                switch (build_volume.type()) {
                case BuildVolume::Type::Rectangle:
                    //FIXME this test does not evaluate collision of a build volume bounding box with non-convex objects.
                    state = build_volume.volume_state_bbox(volume_bbox(*volume), true, &bed_idx);
                    break;
                case BuildVolume::Type::Circle:
                case BuildVolume::Type::Convex:
                //FIXME doing test on convex hull until we learn to do test on non-convex polygons efficiently.
                case BuildVolume::Type::Custom:
                    state = build_volume.object_state(volume_convex_mesh(*volume).its, volume->world_matrix().cast<float>(), volume_sinking(*volume), true, &bed_idx);
                    break;
                default:
                    // Ignore, don't produce any collision.
                    state = BuildVolume::ObjectState::Inside;
                    break;
                }
                assert(state != BuildVolume::ObjectState::Below);
            }
            volume->is_outside = state != BuildVolume::ObjectState::Inside;
            if (volume->printable) {
                if (overall_state == ModelInstancePVS_Inside && volume->is_outside)
                    overall_state = ModelInstancePVS_Fully_Outside;
                if (overall_state == ModelInstancePVS_Fully_Outside && volume->is_outside && state == BuildVolume::ObjectState::Colliding) {
                    overall_state = ModelInstancePVS_Partly_Outside;
                }
                contained_min_one |= !volume->is_outside;

                if (bed_idx != -1 && bed_idx == s_multiple_beds.get_number_of_beds())
                    s_multiple_beds.request_next_bed(true);
            }
        }
        else if (volume->is_modifier)
            volume->is_outside = false;
    }

    for (unsigned int vol_idx = 0; vol_idx < volumes.volumes.size(); ++vol_idx) {
        if (std::find(volumes_idxs.begin(), volumes_idxs.end(), vol_idx) == volumes_idxs.end()) {
            if (!volumes.volumes[vol_idx]->is_outside) {
                contained_min_one = true;
                break;
            }
        }
    }

    if (out_state != nullptr)
        *out_state = overall_state;

    return contained_min_one;
}

void GLCanvas3D::toggle_sla_auxiliaries_visibility(bool visible, const ModelObject* mo, int instance_idx)
{
    if (current_printer_technology() != ptSLA)
        return;

    m_render_sla_auxiliaries = visible;

    std::vector<std::shared_ptr<SceneRaycasterItem>>* raycasters = get_raycasters_for_picking(SceneRaycaster::EType::Volume);

    for (GLVolume* vol : m_volumes.volumes) {
      if ((mo == nullptr || m_model->objects[vol->composite_id.object_id] == mo)
            && (instance_idx == -1 || vol->composite_id.instance_id == instance_idx)
            && vol->composite_id.volume_id < 0) {
            vol->is_active = visible;
            auto it = std::find_if(raycasters->begin(), raycasters->end(), [vol](std::shared_ptr<SceneRaycasterItem> item) { return item->get_raycaster() == vol->mesh_raycaster.get(); });
            if (it != raycasters->end())
                (*it)->set_active(vol->is_active);
        }
    }
}

void GLCanvas3D::toggle_model_objects_visibility(bool visible, const ModelObject* mo, int instance_idx, const ModelVolume* mv)
{
    std::vector<std::shared_ptr<SceneRaycasterItem>>* raycasters = get_raycasters_for_picking(SceneRaycaster::EType::Volume);
    for (GLVolume* vol : m_volumes.volumes) {
        if (vol->is_wipe_tower())
            vol->is_active = (visible && mo == nullptr);
        else {
            if ((mo == nullptr || m_model->objects[vol->composite_id.object_id] == mo)
            && (instance_idx == -1 || vol->composite_id.instance_id == instance_idx)
            && (mv == nullptr || m_model->objects[vol->composite_id.object_id]->volumes[vol->composite_id.volume_id] == mv)) {
                vol->is_active = visible;
                if (!vol->is_modifier)
                    vol->color.a(1.f);

                if (instance_idx == -1) {
                    vol->force_native_color = false;
                    vol->force_neutral_color = false;
                } else {
                    const GLGizmosManager& gm = get_gizmos_manager();
                    auto gizmo_type = gm.get_current_type();
                    if (  (gizmo_type == GLGizmosManager::FdmSupports
                        || gizmo_type == GLGizmosManager::Seam
                        || gizmo_type == GLGizmosManager::Cut
                        || gizmo_type == GLGizmosManager::FuzzySkin)
                        && !vol->is_modifier) {
                        vol->force_neutral_color = true;
                    }
                    else if (gizmo_type == GLGizmosManager::MmSegmentation)
                        vol->is_active = false;
                    else
                        vol->force_native_color = true;
                }
            }
        }

        auto it = std::find_if(raycasters->begin(), raycasters->end(), [vol](std::shared_ptr<SceneRaycasterItem> item) { return item->get_raycaster() == vol->mesh_raycaster.get(); });
        if (it != raycasters->end())
            (*it)->set_active(vol->is_active);
    }

    if (visible && !mo)
        toggle_sla_auxiliaries_visibility(true, mo, instance_idx);

    if (!mo && !visible && !m_model->objects.empty() && (m_model->objects.size() > 1 || m_model->objects.front()->instances.size() > 1))
        _set_warning_notification(EWarning::SomethingNotShown, true);

    if (!mo && visible)
        _set_warning_notification(EWarning::SomethingNotShown, false);
}

void GLCanvas3D::update_instance_printable_state_for_object(const size_t obj_idx)
{
    ModelObject* model_object = m_model->objects[obj_idx];
    for (int inst_idx = 0; inst_idx < (int)model_object->instances.size(); ++inst_idx) {
        ModelInstance* instance = model_object->instances[inst_idx];

        for (GLVolume* volume : m_volumes.volumes) {
            if (volume->object_idx() == (int)obj_idx && volume->instance_idx() == inst_idx)
                volume->printable = instance->printable;
        }
    }
}

void GLCanvas3D::update_instance_printable_state_for_objects(const std::vector<size_t>& object_idxs)
{
    for (size_t obj_idx : object_idxs)
        update_instance_printable_state_for_object(obj_idx);
}

void GLCanvas3D::set_config(const DynamicPrintConfig* config)
{
    m_config = config;
    m_layers_editing.set_config(config);

    const PrinterTechnology ptech = current_printer_technology();
    if (const Print *print = fff_print(); ptech == ptFFF && print != nullptr)
        m_layers_editing.set_shrinkage_compensation(fff_print()->shrinkage_compensation());

    if (config) {
        auto slot = ArrangeSettingsDb_AppCfg::slotFFF;

        if (ptech == ptSLA) {
            slot = ArrangeSettingsDb_AppCfg::slotSLA;
        } else if (ptech == ptFFF) {
            auto co_opt = config->option<ConfigOptionBool>("complete_objects");
            if (co_opt && co_opt->value)
                slot = ArrangeSettingsDb_AppCfg::slotFFFSeqPrint;
            else
                slot = ArrangeSettingsDb_AppCfg::slotFFF;
        }

        m_arrange_settings_db.set_active_slot(slot);

        double objdst = min_object_distance(*config);
        double min_obj_dst = slot == ArrangeSettingsDb_AppCfg::slotFFFSeqPrint ? objdst : 0.;
        m_arrange_settings_db.set_distance_from_obj_range(slot, min_obj_dst, 100.);
        
        if (std::abs(m_arrange_settings_db.get_defaults(slot).d_obj - objdst) > EPSILON) {
            m_arrange_settings_db.get_defaults(slot).d_obj = objdst;

            // Defaults have changed, so let's sync with the app config and fill
            // in the missing values with the new defaults.
            m_arrange_settings_db.sync();
        }
    }
}

void GLCanvas3D::set_model(Model* model)
{
    m_model = model;
    m_selection.set_model(m_model);
}

void GLCanvas3D::bed_shape_changed()
{
    refresh_camera_scene_box();
    wxGetApp().plater()->get_camera().requires_zoom_to_bed = true;
    m_dirty = true;
}

void GLCanvas3D::refresh_camera_scene_box()
{
    wxGetApp().plater()->get_camera().set_scene_box(scene_bounding_box());
}

BoundingBoxf3 GLCanvas3D::volumes_bounding_box() const
{
    BoundingBoxf3 bb;
    for (const GLVolume* volume : m_volumes.volumes) {
        if (!m_apply_zoom_to_volumes_filter || ((volume != nullptr) && volume->zoom_to_volumes))
            bb.merge(volume->transformed_bounding_box());
    }
    return bb;
}

BoundingBoxf3 GLCanvas3D::scene_bounding_box() const
{
    BoundingBoxf3 bb = volumes_bounding_box();
    bb.merge(m_bed.extended_bounding_box());
    double h = m_bed.build_volume().max_print_height();
    //FIXME why -h?
    bb.min.z() = std::min(bb.min.z(), -h);
    bb.max.z() = std::max(bb.max.z(), h);
    return bb;
}

void GLCanvas3D::reset_layer_height_profile()
{
    wxGetApp().plater()->take_snapshot(_L("Variable layer height - Reset"));
    m_layers_editing.reset_layer_height_profile(*this);
    m_layers_editing.state = LayersEditing::Completed;
    m_dirty = true;
}

void GLCanvas3D::adaptive_layer_height_profile(float quality_factor)
{
    wxGetApp().plater()->take_snapshot(_L("Variable layer height - Adaptive"));
    m_layers_editing.adaptive_layer_height_profile(*this, quality_factor);
    m_layers_editing.state = LayersEditing::Completed;
    m_dirty = true;
}

void GLCanvas3D::smooth_layer_height_profile(const HeightProfileSmoothingParams& smoothing_params)
{
    wxGetApp().plater()->take_snapshot(_L("Variable layer height - Smooth all"));
    m_layers_editing.smooth_layer_height_profile(*this, smoothing_params);
    m_layers_editing.state = LayersEditing::Completed;
    m_dirty = true;
}

void GLCanvas3D::enable_layers_editing(bool enable)
{
    m_layers_editing.set_enabled(enable);
    set_as_dirty();
}

void GLCanvas3D::zoom_to_bed()
{
    BoundingBoxf3 box = m_bed.build_volume().bounding_volume();
    box.translate(s_multiple_beds.get_bed_translation(s_multiple_beds.get_active_bed()));
    box.min.z() = 0.0;
    box.max.z() = 0.0;

    _zoom_to_box(box);
}

void GLCanvas3D::zoom_to_volumes()
{
    m_apply_zoom_to_volumes_filter = true;
    _zoom_to_box(volumes_bounding_box());
    m_apply_zoom_to_volumes_filter = false;
}

void GLCanvas3D::zoom_to_selection()
{
    if (!m_selection.is_empty())
        _zoom_to_box(m_selection.get_bounding_box());
}

void GLCanvas3D::zoom_to_gcode()
{
    _zoom_to_box(m_gcode_viewer.get_paths_bounding_box(), 1.05);
}

void GLCanvas3D::select_view(const std::string& direction)
{
    wxGetApp().plater()->get_camera().select_view(direction);
    if (m_canvas != nullptr)
        m_canvas->Refresh();
}

void GLCanvas3D::update_volumes_colors_by_extruder()
{
    if (m_config != nullptr)
        m_volumes.update_colors_by_extruder(m_config);
}

using PerBedStatistics = std::vector<std::pair<
    std::size_t,
    std::optional<std::reference_wrapper<const PrintStatistics>>
>>;

PerBedStatistics get_statistics(){
    PerBedStatistics result;
    for (int bed_index=0; bed_index<s_multiple_beds.get_number_of_beds(); ++bed_index) {
        const Print* print = wxGetApp().plater()->get_fff_prints()[bed_index].get();
        if (print->empty() || !print->finished()) {
            result.emplace_back(bed_index, std::nullopt);
        } else {
            result.emplace_back(bed_index, std::optional{std::ref(print->print_statistics())});
        }
    }
    return result;
}

struct StatisticsSum {
    float cost{};
    float filement_weight{};
    float filament_length{};
    float normal_print_time{};
    float silent_print_time{};
};

StatisticsSum get_statistics_sum() {
    StatisticsSum result;
    for (const auto &[_, statistics] : get_statistics()) {
        if (!statistics) {
            continue;
        }
        result.cost += statistics->get().total_cost;
        result.filement_weight += statistics->get().total_weight;
        result.filament_length += statistics->get().total_used_filament;
        result.normal_print_time += statistics->get().normal_print_time_seconds;
        result.silent_print_time += statistics->get().silent_print_time_seconds;
    }

    return result;
}

// retur width of table
float project_overview_table(float scale) {
    const float width_gap = 10.f * scale;
    float total_width{ width_gap };

    ImGui::Text("%s", _u8L("Project overview").c_str());
    if (ImGui::BeginTable("project_overview_table", 6)) {

        float width = std::max<float>(ImGui::CalcTextSize(format(_u8L("Bed %1%"), 1).c_str()).x, ImGui::CalcTextSize(_u8L("Total").c_str()).x) + width_gap;
        total_width += width;
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, width);

        std::string name = _u8L("Cost");
        width = ImGui::CalcTextSize(name.c_str()).x + width_gap;
        total_width += width;
        ImGui::TableSetupColumn(
            name.c_str(),
            ImGuiTableColumnFlags_WidthFixed,
            width
        );

        name = _u8L("Filament (g)");
        width = ImGui::CalcTextSize(name.c_str()).x + width_gap;
        total_width += width;
        ImGui::TableSetupColumn(
            name.c_str(),
            ImGuiTableColumnFlags_WidthFixed,
            width
        );

        name = _u8L("Filament (m)");
        width = ImGui::CalcTextSize(name.c_str()).x + width_gap;
        total_width += width;
        ImGui::TableSetupColumn(
            name.c_str(),
            ImGuiTableColumnFlags_WidthFixed,
            width
        );

        // TRN %1% is one "Stealth mode" or "Normal mode"
        name = format(_u8L("Estimated Time (%1%)"), _u8L("Stealth mode"));
        width = ImGui::CalcTextSize(name.c_str()).x + width_gap;
        total_width += width;
        ImGui::TableSetupColumn(
            name.c_str(),
            ImGuiTableColumnFlags_WidthFixed,
            width
        );

        name = format(_u8L("Estimated Time (%1%)"), _u8L("Normal mode"));
        width = ImGui::CalcTextSize(name.c_str()).x + width_gap;
        total_width += width;
        ImGui::TableSetupColumn(
            name.c_str(),
            ImGuiTableColumnFlags_WidthFixed,
            width
        );
        ImGui::TableHeadersRow();

        for (const auto &[bed_index, optional_statistics] : get_statistics()) {
            if (optional_statistics) {
                const std::reference_wrapper<const PrintStatistics> statistics{*optional_statistics};
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                // TRN %1% is a number of the Bed
                ImGui::Text("%s", format(_u8L("Bed %1%"), bed_index + 1).c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%.2f", statistics.get().total_cost);
                ImGui::TableNextColumn();
                ImGui::Text("%.2f", statistics.get().total_weight);
                ImGui::TableNextColumn();
                ImGui::Text("%.2f", statistics.get().total_used_filament / 1000);
                ImGui::TableNextColumn();
                ImGui::Text("%s", statistics.get().estimated_silent_print_time.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%s", statistics.get().estimated_normal_print_time.c_str());
            } else {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%s", format(_u8L("Bed %1%"), bed_index + 1).c_str());
                ImGui::TableNextColumn();
                ImGui::Text("-");
                ImGui::TableNextColumn();
                ImGui::Text("-");
                ImGui::TableNextColumn();
                ImGui::Text("-");
                ImGui::TableNextColumn();
                ImGui::Text("-");
                ImGui::TableNextColumn();
                ImGui::Text("-");
            }
        }

        ImGui::PushStyleColor(ImGuiCol_Text, ImGuiPureWrap::COL_ORANGE_LIGHT);

        const StatisticsSum statistics_sum{get_statistics_sum()};
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%s", _u8L("Total").c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%.2f", statistics_sum.cost);
        ImGui::TableNextColumn();
        ImGui::Text("%.2f", statistics_sum.filement_weight);
        ImGui::TableNextColumn();
        ImGui::Text("%.2f", statistics_sum.filament_length / 1000);
        ImGui::TableNextColumn();
        ImGui::Text("%s", get_time_dhms(statistics_sum.silent_print_time).c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%s", get_time_dhms(statistics_sum.normal_print_time).c_str());

        ImGui::PopStyleColor();

        ImGui::EndTable();
    }

    return total_width + 2.f * width_gap;
}

struct ExtruderStatistics {
    float filament_weight{};
    float filament_length{};
};

using PerExtruderStatistics = std::map<
    std::size_t,
    ExtruderStatistics
>;

PerExtruderStatistics get_extruder_statistics(){
    PerExtruderStatistics result;
    for (int bed_index=0; bed_index<s_multiple_beds.get_number_of_beds(); ++bed_index) {
        const Print* print = wxGetApp().plater()->get_fff_prints()[bed_index].get();
        if (print->empty() || !print->finished()) {
            continue;
        }
        print->print_statistics();
        const auto& extruders_filaments{wxGetApp().preset_bundle->extruders_filaments};
        for (const auto &[filament_id, filament_volume] : print->print_statistics().filament_stats) {
            const Preset* preset = extruders_filaments[filament_id].get_selected_preset();
            if (preset == nullptr) {
                continue;
            }

            const double filament_density = preset->config.opt_float("filament_density", 0);
            const double diameter = preset->config.opt_float("filament_diameter", filament_id);
            result[filament_id].filament_weight += filament_volume * filament_density / 1000.0f;
            result[filament_id].filament_length += filament_volume / (M_PI * diameter * diameter / 4.0) / 1000.0;
        }
    }
    return result;
}

ExtruderStatistics sum_extruder_statistics(
    const PerExtruderStatistics &per_extruder_statistics
) {
    ExtruderStatistics result;
    for (const auto &[_, statistics] : per_extruder_statistics) {
        result.filament_weight += statistics.filament_weight;
        result.filament_length += statistics.filament_length;
    }

    return result;
}

void extruder_usage_table(const PerExtruderStatistics &extruder_statistics, const float scale) {

    ImGui::Text("%s", _u8L("Extruders usage breakdown").c_str());
    if (ImGui::BeginTable("extruder_usage_table", 3)) {
        const float width_gap = 10.f * scale;
        float width = width_gap + std::max<float>(ImGui::CalcTextSize(format(_u8L("Extruder %1%"), 1).c_str()).x, 
                                                  ImGui::CalcTextSize(_u8L("Total").c_str()).x);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, width);

        std::string name = _u8L("Filament (g)");
        width = ImGui::CalcTextSize(name.c_str()).x + width_gap;
        ImGui::TableSetupColumn(
            name.c_str(),
            ImGuiTableColumnFlags_WidthFixed,
            width
        );

        name = _u8L("Filament (m)");
        width = ImGui::CalcTextSize(name.c_str()).x + width_gap;
        ImGui::TableSetupColumn(
            name.c_str(),
            ImGuiTableColumnFlags_WidthFixed,
            width
        );
        ImGui::TableHeadersRow();

        for (const auto &[extruder_index, statistics] : extruder_statistics) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%s", format(_u8L("Extruder %1%"), extruder_index + 1).c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", statistics.filament_weight);
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", statistics.filament_length);
        }

        ImGui::PushStyleColor(ImGuiCol_Text, ImGuiPureWrap::COL_ORANGE_LIGHT);

        const ExtruderStatistics extruder_statistics_sum{sum_extruder_statistics(extruder_statistics)};
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%s", _u8L("Total").c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%.2f", extruder_statistics_sum.filament_weight);
        ImGui::TableNextColumn();
        ImGui::Text("%.2f", extruder_statistics_sum.filament_length);

        ImGui::PopStyleColor();

        ImGui::EndTable();
    }
}

void begin_statistics(const char *window_name) {
    ImGuiWindowFlags windows_flags =
        ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_HorizontalScrollbar;

    const ImVec2 center{ImGui::GetMainViewport()->GetCenter()};
    const float y_postion{std::max(0.5f * center.y, 150.0f)};
    const ImVec2 position{center.x, y_postion};
    ImGui::SetNextWindowPos(position, ImGuiCond_Always, ImVec2{0.5f, 0.f});

    ImGui::Begin(window_name, nullptr, windows_flags);
}

static float content_size_x = 0.0f;
void render_print_statistics(float scale) {
    ImGui::SetNextWindowContentSize(ImVec2(content_size_x, 0.0f));

    begin_statistics(_u8L("Statistics").c_str());
    ImGui::Spacing();
    content_size_x = project_overview_table(scale);
    ImGui::Separator();

    const PerExtruderStatistics extruder_statistics{get_extruder_statistics()};
    if (extruder_statistics.size() > 1) {
        ImGui::NewLine();
        extruder_usage_table(extruder_statistics, scale);
        ImGui::Separator();
    }
    ImGui::End();
}

void render_autoslicing_wait() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.f,30.f));
    begin_statistics((_u8L("Generating statistics") + " ...").c_str());
    ImGui::Text("%s", _u8L("Statistics will be available once all beds are sliced").c_str());
    ImGui::PopStyleVar();
    ImGui::End();
}

void GLCanvas3D::render()
{
    if (m_in_render) {
        // if called recursively, return
        m_dirty = true;
        return;
    }

    m_in_render = true;
    Slic3r::ScopeGuard in_render_guard([this]() { m_in_render = false; });
    (void)in_render_guard;

    if (m_canvas == nullptr)
        return;

    // ensures this canvas is current and initialized
    if (!_is_shown_on_screen() || !_set_current() || !wxGetApp().init_opengl())
        return;

    if (!is_initialized() && !init())
        return;

    if (!m_main_toolbar.is_enabled())
        m_gcode_viewer.init();

    if (! m_bed.build_volume().valid()) {
        // this happens at startup when no data is still saved under <>\AppData\Roaming\Slic3rPE
        post_event(SimpleEvent(EVT_GLCANVAS_UPDATE_BED_SHAPE));
        return;
    }

#if ENABLE_ENVIRONMENT_MAP
    if (wxGetApp().is_editor())
        wxGetApp().plater()->init_environment_texture();
#endif // ENABLE_ENVIRONMENT_MAP

#if ENABLE_GLMODEL_STATISTICS
    GLModel::reset_statistics_counters();
#endif // ENABLE_GLMODEL_STATISTICS

    const Size& cnv_size = get_canvas_size();
    // Probably due to different order of events on Linux/GTK2, when one switched from 3D scene
    // to preview, this was called before canvas had its final size. It reported zero width
    // and the viewport was set incorrectly, leading to tripping glAsserts further down
    // the road (in apply_projection). That's why the minimum size is forced to 10.
    Camera& camera = wxGetApp().plater()->get_camera();
    camera.set_viewport(0, 0, std::max(10u, (unsigned int)cnv_size.get_width()), std::max(10u, (unsigned int)cnv_size.get_height()));
    camera.apply_viewport();

    if (camera.requires_zoom_to_bed) {
        zoom_to_bed();
        _resize((unsigned int)cnv_size.get_width(), (unsigned int)cnv_size.get_height());
        camera.requires_zoom_to_bed = false;
    }

    camera.apply_projection(_max_bounding_box(true));

    const int curr_active_bed_id = s_multiple_beds.get_active_bed();
    if (m_last_active_bed_id != curr_active_bed_id) {
        const Vec3d bed_offset = s_multiple_beds.get_bed_translation(s_multiple_beds.get_active_bed());
        const Vec2d bed_center = m_bed.build_volume().bed_center() + Vec2d(bed_offset.x(), bed_offset.y());
        m_last_active_bed_id = curr_active_bed_id;
    }

    wxGetApp().imgui()->new_frame();

    if (m_picking_enabled) {
        if (m_rectangle_selection.is_dragging() && !m_rectangle_selection.is_empty())
            // picking pass using rectangle selection
            _rectangular_selection_picking_pass();
        else if (!m_volumes.empty())
            // regular picking pass
            _picking_pass();
#if ENABLE_RAYCAST_PICKING_DEBUG
        else {
            ImGuiWrapper& imgui = *wxGetApp().imgui();
            ImGuiPureWrap::begin(std::string("Hit result"), ImGuiWindowFlags_AlwaysAutoResize);
            ImGuiPureWrap::text("Picking disabled");
            ImGuiPureWrap::end();
        }
#endif // ENABLE_RAYCAST_PICKING_DEBUG
    }
    
#ifdef SHOW_IMGUI_DEMO_WINDOW
    if (show_imgui_demo_window) ImGui::ShowDemoWindow();
#endif // SHOW_IMGUI_DEMO_WINDOW


    


    const bool is_looking_downward = camera.is_looking_downward();

    // draw scene
    glsafe(::glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
    _render_background();

    if (! s_multiple_beds.is_autoslicing()) {
        _render_objects(GLVolumeCollection::ERenderType::Opaque);
        _render_sla_slices();
        _render_selection();
        _render_bed_axes();
        if (is_looking_downward)
            _render_bed(camera.get_view_matrix(), camera.get_projection_matrix(), false);
        if (!m_main_toolbar.is_enabled() && current_printer_technology() != ptSLA)
            _render_gcode();
        _render_objects(GLVolumeCollection::ERenderType::Transparent);

        _render_sequential_clearance();
    #if ENABLE_RENDER_SELECTION_CENTER
        _render_selection_center();
    #endif // ENABLE_RENDER_SELECTION_CENTER
        if (!m_main_toolbar.is_enabled())
            _render_gcode_cog();

        // we need to set the mouse's scene position here because the depth buffer
        // could be invalidated by the following gizmo render methods
        // this position is used later into on_mouse() to drag the objects
        if (m_picking_enabled)
            m_mouse.scene_position = _mouse_to_3d(m_mouse.position.cast<coord_t>());

        // sidebar hints need to be rendered before the gizmos because the depth buffer
        // could be invalidated by the following gizmo render methods
        _render_selection_sidebar_hints();
        _render_current_gizmo();
        if (!is_looking_downward)
            _render_bed(camera.get_view_matrix(), camera.get_projection_matrix(), true);

    #if ENABLE_RAYCAST_PICKING_DEBUG
        if (m_picking_enabled && !m_mouse.dragging && !m_gizmos.is_dragging() && !m_rectangle_selection.is_dragging())
            m_scene_raycaster.render_hit(camera);
    #endif // ENABLE_RAYCAST_PICKING_DEBUG

#if ENABLE_SHOW_CAMERA_TARGET
    _render_camera_target();
    _render_camera_target_validation_box();
#endif // ENABLE_SHOW_CAMERA_TARGET

        if (m_picking_enabled && m_rectangle_selection.is_dragging())
            m_rectangle_selection.render(*this);
    } else {
        const auto &prints{wxGetApp().plater()->get_fff_prints()};

        bool all_finished{true};
        for (std::size_t bed_index{}; bed_index < s_multiple_beds.get_number_of_beds(); ++bed_index) {
            const std::unique_ptr<Print> &print{prints[bed_index]};
            if (!print->finished() && is_sliceable(s_print_statuses[bed_index])) {
                all_finished = false;
                break;
            }
        }

        if (!all_finished) {
            render_autoslicing_wait();
            if (fff_print()->finished() || !is_sliceable(s_print_statuses[s_multiple_beds.get_active_bed()])) {
                s_multiple_beds.autoslice_next_bed();
                wxYield();
            } else {
                wxGetApp().plater()->schedule_background_process();
            }
        } else {
            wxGetApp().plater()->show_autoslicing_action_buttons();
#if ENABLE_RETINA_GL
            const float scale = m_retina_helper->get_scale_factor();
#else
            const float scale = 0.1f * wxGetApp().em_unit();
#endif // ENABLE_RETINA_GL
            render_print_statistics(scale);
        }
    }

    _render_overlays();

    _render_bed_selector();

    if (wxGetApp().plater()->is_render_statistic_dialog_visible()) {
        ImGuiPureWrap::begin(std::string("Render statistics"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
        ImGuiPureWrap::text("FPS (SwapBuffers() calls per second):");
        ImGui::SameLine();
        ImGuiPureWrap::text(std::to_string(m_render_stats.get_fps_and_reset_if_needed()));
        ImGui::Separator();
        ImGuiPureWrap::text("Compressed textures:");
        ImGui::SameLine();
        ImGuiPureWrap::text(OpenGLManager::are_compressed_textures_supported() ? "supported" : "not supported");
        ImGuiPureWrap::text("Max texture size:");
        ImGui::SameLine();
        ImGuiPureWrap::text(std::to_string(OpenGLManager::get_gl_info().get_max_tex_size()));
        ImGuiPureWrap::end();
    }

#if ENABLE_PROJECT_DIRTY_STATE_DEBUG_WINDOW
    if (wxGetApp().is_editor() && wxGetApp().plater()->is_view3D_shown())
        wxGetApp().plater()->render_project_state_debug_window();
#endif // ENABLE_PROJECT_DIRTY_STATE_DEBUG_WINDOW

#if ENABLE_CAMERA_STATISTICS
    camera.debug_render();
#endif // ENABLE_CAMERA_STATISTICS
#if ENABLE_GLMODEL_STATISTICS
    GLModel::render_statistics();
#endif // ENABLE_GLMODEL_STATISTICS
#if ENABLE_OBJECT_MANIPULATION_DEBUG
    wxGetApp().obj_manipul()->render_debug_window();
#endif // ENABLE_OBJECT_MANIPULATION_DEBUG

    if (wxGetApp().plater()->is_view3D_shown() && current_printer_technology() == ptSLA) {
        const GLGizmosManager::EType type = m_gizmos.get_current_type();
        if (type == GLGizmosManager::EType::Undefined)
            m_sla_view.render_switch_button();
#if ENABLE_SLA_VIEW_DEBUG_WINDOW
        m_sla_view.render_debug_window();
#endif // ENABLE_SLA_VIEW_DEBUG_WINDOW
    }

#if ENABLE_BINARIZED_GCODE_DEBUG_WINDOW
    if (wxGetApp().plater()->is_view3D_shown() && current_printer_technology() != ptSLA && fff_print()->config().binary_gcode)
        show_binary_gcode_debug_window();
#endif // ENABLE_BINARIZED_GCODE_DEBUG_WINDOW

    std::string tooltip;

	// Negative coordinate means out of the window, likely because the window was deactivated.
	// In that case the tooltip should be hidden.
    if (m_mouse.position.x() >= 0. && m_mouse.position.y() >= 0.) {
	    if (tooltip.empty())
	        tooltip = m_layers_editing.get_tooltip(*this);

	    if (tooltip.empty())
	        tooltip = m_gizmos.get_tooltip();

	    if (tooltip.empty())
	        tooltip = m_main_toolbar.get_tooltip();

	    if (tooltip.empty())
	        tooltip = m_undoredo_toolbar.get_tooltip();

	    if (tooltip.empty())
            tooltip = wxGetApp().plater()->get_collapse_toolbar().get_tooltip();

	    if (tooltip.empty())
            tooltip = wxGetApp().plater()->get_view_toolbar().get_tooltip();
    }

    set_tooltip(tooltip);

    if (m_tooltip_enabled)
        m_tooltip.render(m_mouse.position, *this);

    wxGetApp().plater()->get_mouse3d_controller().render_settings_dialog(*this);

    wxGetApp().plater()->get_notification_manager()->render_notifications(*this, get_overlay_window_width());

    if (! s_multiple_beds.is_autoslicing())
        wxGetApp().plater()->render_sliders(*this);

    wxGetApp().imgui()->render();

    m_canvas->SwapBuffers();
    m_render_stats.increment_fps_counter();
}

void GLCanvas3D::render_thumbnail(ThumbnailData& thumbnail_data, unsigned int w, unsigned int h, const ThumbnailsParams& thumbnail_params, Camera::EType camera_type)
{
    render_thumbnail(thumbnail_data, w, h, thumbnail_params, m_volumes, camera_type);
}

void GLCanvas3D::render_thumbnail(ThumbnailData& thumbnail_data, unsigned int w, unsigned int h, const ThumbnailsParams& thumbnail_params, const GLVolumeCollection& volumes, Camera::EType camera_type)
{
    switch (OpenGLManager::get_framebuffers_type())
    {
    case OpenGLManager::EFramebufferType::Arb: { _render_thumbnail_framebuffer(thumbnail_data, w, h, thumbnail_params, volumes, camera_type); break; }
    case OpenGLManager::EFramebufferType::Ext: { _render_thumbnail_framebuffer_ext(thumbnail_data, w, h, thumbnail_params, volumes, camera_type); break; }
    default: { _render_thumbnail_legacy(thumbnail_data, w, h, thumbnail_params, volumes, camera_type); break; }
    }
}

void GLCanvas3D::select_all()
{
    m_selection.add_all();
    m_dirty = true;
    wxGetApp().obj_manipul()->set_dirty();
    m_gizmos.reset_all_states();
    m_gizmos.update_data();
    post_event(SimpleEvent(EVT_GLCANVAS_OBJECT_SELECT));
}

void GLCanvas3D::deselect_all()
{
    if (m_selection.is_empty())
        return;

    // close actual opened gizmo before deselection(m_selection.remove_all()) write to undo/redo snapshot
    if (GLGizmosManager::EType current_type = m_gizmos.get_current_type();
        current_type != GLGizmosManager::Undefined)
        m_gizmos.open_gizmo(current_type);            

    m_selection.remove_all();
    wxGetApp().obj_manipul()->set_dirty();
    m_gizmos.reset_all_states();
    m_gizmos.update_data();
    post_event(SimpleEvent(EVT_GLCANVAS_OBJECT_SELECT));
}

void GLCanvas3D::ensure_on_bed(unsigned int object_idx, bool allow_negative_z)
{
    if (allow_negative_z)
        return;

    typedef std::map<std::pair<int, int>, double> InstancesToZMap;
    InstancesToZMap instances_min_z;

    for (GLVolume* volume : m_volumes.volumes) {
        if (volume->object_idx() == (int)object_idx && !volume->is_modifier) {
            double min_z = volume->transformed_convex_hull_bounding_box().min.z();
            std::pair<int, int> instance = std::make_pair(volume->object_idx(), volume->instance_idx());
            InstancesToZMap::iterator it = instances_min_z.find(instance);
            if (it == instances_min_z.end())
                it = instances_min_z.insert(InstancesToZMap::value_type(instance, DBL_MAX)).first;

            it->second = std::min(it->second, min_z);
        }
    }

    for (GLVolume* volume : m_volumes.volumes) {
        std::pair<int, int> instance = std::make_pair(volume->object_idx(), volume->instance_idx());
        InstancesToZMap::iterator it = instances_min_z.find(instance);
        if (it != instances_min_z.end())
            volume->set_instance_offset(Z, volume->get_instance_offset(Z) - it->second);
    }
}

void GLCanvas3D::set_toolpaths_z_range(const std::array<unsigned int, 2>& range)
{
    if (m_gcode_viewer.has_data())
        m_gcode_viewer.set_layers_z_range(range);
}

std::vector<int> GLCanvas3D::load_object(const ModelObject& model_object, int obj_idx, std::vector<int> instance_idxs)
{
    if (instance_idxs.empty()) {
        for (unsigned int i = 0; i < model_object.instances.size(); ++i) {
            instance_idxs.emplace_back(i);
        }
    }
    return m_volumes.load_object(&model_object, obj_idx, instance_idxs);
}

std::vector<int> GLCanvas3D::load_object(const Model& model, int obj_idx)
{
    if (0 <= obj_idx && obj_idx < (int)model.objects.size()) {
        const ModelObject* model_object = model.objects[obj_idx];
        if (model_object != nullptr)
            return load_object(*model_object, obj_idx, std::vector<int>());
    }

    return std::vector<int>();
}

void GLCanvas3D::mirror_selection(Axis axis)
{
    TransformationType transformation_type;
    if (wxGetApp().obj_manipul()->is_local_coordinates())
        transformation_type.set_local();
    else if (wxGetApp().obj_manipul()->is_instance_coordinates())
        transformation_type.set_instance();

    transformation_type.set_relative();

    m_selection.setup_cache();
    m_selection.mirror(axis, transformation_type);

    do_mirror(L("Mirror Object"));
    wxGetApp().obj_manipul()->set_dirty();
}

// Reload the 3D scene of 
// 1) Model / ModelObjects / ModelInstances / ModelVolumes
// 2) Print bed
// 3) SLA support meshes for their respective ModelObjects / ModelInstances
// 4) Wipe tower preview
// 5) Out of bed collision status & message overlay (texture)
void GLCanvas3D::reload_scene(bool refresh_immediately, bool force_full_scene_refresh)
{
    if (m_canvas == nullptr || m_config == nullptr || m_model == nullptr)
        return;

    if (!m_initialized)
        return;
    
    _set_current();

    m_hover_volume_idxs.clear();

    struct ModelVolumeState {
        ModelVolumeState(const GLVolume* volume) :
            model_volume(nullptr), geometry_id(volume->geometry_id), volume_idx(-1) {}
        ModelVolumeState(const ModelVolume* model_volume, const ObjectID& instance_id, const GLVolume::CompositeID& composite_id) :
            model_volume(model_volume), geometry_id(std::make_pair(model_volume->id().id, instance_id.id)), composite_id(composite_id), volume_idx(-1) {}
        ModelVolumeState(const ObjectID& volume_id, const ObjectID& instance_id) :
            model_volume(nullptr), geometry_id(std::make_pair(volume_id.id, instance_id.id)), volume_idx(-1) {}
        bool new_geometry() const { return this->volume_idx == size_t(-1); }
        const ModelVolume* model_volume;
        // ObjectID of ModelVolume + ObjectID of ModelInstance
        // or timestamp of an SLAPrintObjectStep + ObjectID of ModelInstance
        std::pair<size_t, size_t>   geometry_id;
        GLVolume::CompositeID       composite_id;
        // Volume index in the new GLVolume vector.
        size_t                      volume_idx;
    };
    std::vector<ModelVolumeState> model_volume_state;
    std::vector<ModelVolumeState> aux_volume_state;

    struct GLVolumeState {
        GLVolumeState() :
            volume_idx(size_t(-1)) {}
        GLVolumeState(const GLVolume* volume, unsigned int volume_idx) :
            composite_id(volume->composite_id), volume_idx(volume_idx) {}
        GLVolumeState(const GLVolume::CompositeID &composite_id) :
            composite_id(composite_id), volume_idx(size_t(-1)) {}

        GLVolume::CompositeID       composite_id;
        // Volume index in the old GLVolume vector.
        size_t                      volume_idx;
    };

    std::vector<std::pair<GLVolume::CompositeID, GLVolume::CompositeID>> new_to_old_ids_map;

    // SLA steps to pull the preview meshes for.
    typedef std::array<SLAPrintObjectStep, 3> SLASteps;
    SLASteps sla_steps = { slaposDrillHoles, slaposSupportTree, slaposPad };
    struct SLASupportState {
        std::array<PrintStateBase::StateWithTimeStamp, std::tuple_size<SLASteps>::value> step;
    };
    // State of the sla_steps for all SLAPrintObjects.
    std::vector<SLASupportState> sla_support_state;

    std::vector<size_t> instance_ids_selected;
    std::vector<size_t> map_glvolume_old_to_new(m_volumes.volumes.size(), size_t(-1));
    std::vector<GLVolumeState> deleted_volumes;
    std::vector<GLVolume*> glvolumes_new;
    glvolumes_new.reserve(m_volumes.volumes.size());
    auto model_volume_state_lower = [](const ModelVolumeState& m1, const ModelVolumeState& m2) { return m1.geometry_id < m2.geometry_id; };

    m_reload_delayed = !m_canvas->IsShown() && !refresh_immediately && !force_full_scene_refresh;

    PrinterTechnology printer_technology = current_printer_technology();
    std::map<size_t, size_t>               volume_idxs_wipe_towers_old; // map from geometry_id.second to volume_id

    // Release invalidated volumes to conserve GPU memory in case of delayed refresh (see m_reload_delayed).
    // First initialize model_volumes_new_sorted & model_instances_new_sorted.
    for (int object_idx = 0; object_idx < (int)m_model->objects.size(); ++object_idx) {
        const ModelObject* model_object = m_model->objects[object_idx];
        for (int instance_idx = 0; instance_idx < (int)model_object->instances.size(); ++instance_idx) {
            const ModelInstance* model_instance = model_object->instances[instance_idx];
            for (int volume_idx = 0; volume_idx < (int)model_object->volumes.size(); ++volume_idx) {
                const ModelVolume* model_volume = model_object->volumes[volume_idx];
                model_volume_state.emplace_back(model_volume, model_instance->id(), GLVolume::CompositeID(object_idx, volume_idx, instance_idx));
            }
        }
    }

    if (printer_technology == ptSLA) {
        const SLAPrint* sla_print = this->sla_print();
#ifndef NDEBUG
        // Verify that the SLAPrint object is synchronized with m_model.
        check_model_ids_equal(*m_model, sla_print->model());
#endif // NDEBUG
        sla_support_state.reserve(sla_print->objects().size());
        for (const SLAPrintObject* print_object : sla_print->objects()) {
            SLASupportState state;
            for (size_t istep = 0; istep < sla_steps.size(); ++istep) {
                state.step[istep] = print_object->step_state_with_timestamp(sla_steps[istep]);
                if (state.step[istep].state == PrintStateBase::State::Done) {
                    std::shared_ptr<const indexed_triangle_set> m = print_object->get_mesh_to_print();
                    if (m == nullptr || m->empty())
                        // Consider the DONE step without a valid mesh as invalid for the purpose
                        // of mesh visualization.
                        state.step[istep].state = PrintStateBase::State::Invalidated;
                    else {
                        for (const ModelInstance* model_instance : print_object->model_object()->instances) {
                            // Only the instances, which are currently printable, will have the SLA support structures kept.
                            // The instances outside the print bed will have the GLVolumes of their support structures released.
                            if (model_instance->is_printable())
                                aux_volume_state.emplace_back(state.step[istep].timestamp, model_instance->id());
                        }
                    }
                }
            }
            sla_support_state.emplace_back(state);
        }
    }

    std::sort(model_volume_state.begin(), model_volume_state.end(), model_volume_state_lower);
    std::sort(aux_volume_state.begin(), aux_volume_state.end(), model_volume_state_lower);
    // Release all ModelVolume based GLVolumes not found in the current Model. Find the GLVolume of a hollowed mesh.
    for (size_t volume_id = 0; volume_id < m_volumes.volumes.size(); ++volume_id) {
        GLVolume* volume = m_volumes.volumes[volume_id];
        ModelVolumeState  key(volume);
        ModelVolumeState* mvs = nullptr;
        if (volume->volume_idx() < 0) {
            auto it = std::lower_bound(aux_volume_state.begin(), aux_volume_state.end(), key, model_volume_state_lower);
            if (it != aux_volume_state.end() && it->geometry_id == key.geometry_id)
                // This can be an SLA support structure that should not be rendered (in case someone used undo
                // to revert to before it was generated). We only reuse the volume if that's not the case.
                if (m_model->objects[volume->composite_id.object_id]->sla_points_status != sla::PointsStatus::NoPoints)
                    mvs = &(*it);
        }
        else {
            auto it = std::lower_bound(model_volume_state.begin(), model_volume_state.end(), key, model_volume_state_lower);
            if (it != model_volume_state.end() && it->geometry_id == key.geometry_id)
                mvs = &(*it);
        }
        // Emplace instance ID of the volume. Both the aux volumes and model volumes share the same instance ID.
        // The wipe tower has its own wipe_tower_instance_id().
        if (m_selection.contains_volume(volume_id))
            instance_ids_selected.emplace_back(volume->geometry_id.second);
        if (mvs == nullptr || force_full_scene_refresh) {
            // This GLVolume will be released.
            if (volume->is_wipe_tower()) {
#if SLIC3R_OPENGL_ES
                m_wipe_tower_meshes.clear();
#endif // SLIC3R_OPENGL_ES
                volume_idxs_wipe_towers_old.emplace(std::make_pair(volume->geometry_id.second, volume_id));
            }
            if (!m_reload_delayed) {
                deleted_volumes.emplace_back(volume, volume_id);
                delete volume;
            }
        }
        else {
            // This GLVolume will be reused.
            volume->set_sla_shift_z(0.0);
            map_glvolume_old_to_new[volume_id] = glvolumes_new.size();
            mvs->volume_idx = glvolumes_new.size();
            glvolumes_new.emplace_back(volume);
            // Update color of the volume based on the current extruder.
            if (mvs->model_volume != nullptr) {
                int extruder_id = mvs->model_volume->extruder_id();
                if (extruder_id != -1)
                    volume->extruder_id = extruder_id;

                volume->is_modifier = !mvs->model_volume->is_model_part();
                volume->shader_outside_printer_detection_enabled = mvs->model_volume->is_model_part();
                volume->set_color(color_from_model_volume(*mvs->model_volume));
                // force update of render_color alpha channel 
                volume->set_render_color(volume->color.is_transparent());

                // updates volumes transformations
                volume->set_instance_transformation(mvs->model_volume->get_object()->instances[mvs->composite_id.instance_id]->get_transformation());
                volume->set_volume_transformation(mvs->model_volume->get_transformation());

                // updates volumes convex hull
                if (mvs->model_volume->is_model_part() && ! volume->convex_hull())
                    // Model volume was likely changed from modifier or support blocker / enforcer to a model part.
                    // Only model parts require convex hulls.
                    volume->set_convex_hull(mvs->model_volume->get_convex_hull_shared_ptr());
            }
        }
    }
    sort_remove_duplicates(instance_ids_selected);
    auto deleted_volumes_lower = [](const GLVolumeState &v1, const GLVolumeState &v2) { return v1.composite_id < v2.composite_id; };
    std::sort(deleted_volumes.begin(), deleted_volumes.end(), deleted_volumes_lower);

    if (m_reload_delayed)
        return;

    bool update_object_list = false;
    if (m_volumes.volumes != glvolumes_new)
		update_object_list = true;
    m_volumes.volumes = std::move(glvolumes_new);
    for (unsigned int obj_idx = 0; obj_idx < (unsigned int)m_model->objects.size(); ++ obj_idx) {
        const ModelObject &model_object = *m_model->objects[obj_idx];
        for (int volume_idx = 0; volume_idx < (int)model_object.volumes.size(); ++ volume_idx) {
            const ModelVolume &model_volume = *model_object.volumes[volume_idx];
            for (int instance_idx = 0; instance_idx < (int)model_object.instances.size(); ++ instance_idx) {
                const ModelInstance &model_instance = *model_object.instances[instance_idx];
                ModelVolumeState key(model_volume.id(), model_instance.id());
                auto it = std::lower_bound(model_volume_state.begin(), model_volume_state.end(), key, model_volume_state_lower);
                assert(it != model_volume_state.end() && it->geometry_id == key.geometry_id);
                if (it->new_geometry()) {
                    // New volume.
                    auto it_old_volume = std::lower_bound(deleted_volumes.begin(), deleted_volumes.end(), GLVolumeState(it->composite_id), deleted_volumes_lower);
                    if (it_old_volume != deleted_volumes.end() && it_old_volume->composite_id == it->composite_id)
                        // If a volume changed its ObjectID, but it reuses a GLVolume's CompositeID, maintain its selection.
                        map_glvolume_old_to_new[it_old_volume->volume_idx] = m_volumes.volumes.size();
                    // Note the index of the loaded volume, so that we can reload the main model GLVolume with the hollowed mesh
                    // later in this function.
                    it->volume_idx = m_volumes.volumes.size();
                    m_volumes.load_object_volume(&model_object, obj_idx, volume_idx, instance_idx);
                    m_volumes.volumes.back()->geometry_id = key.geometry_id;
                    update_object_list = true;
                }
                else {
                    // Recycling an old GLVolume.
                    GLVolume &existing_volume = *m_volumes.volumes[it->volume_idx];
                    assert(existing_volume.geometry_id == key.geometry_id);
                    // Update the Object/Volume/Instance indices into the current Model.
                    if (existing_volume.composite_id != it->composite_id) {
                        new_to_old_ids_map.push_back(std::make_pair(it->composite_id, existing_volume.composite_id));
                        existing_volume.composite_id = it->composite_id;
                        update_object_list = true;
                    }
                }
            }
        }
    }

    if (printer_technology == ptSLA) {
        size_t idx = 0;
        const SLAPrint *sla_print = this->sla_print();
        std::vector<double> shift_zs(m_model->objects.size(), 0);
        double relative_correction_z = sla_print->relative_correction().z();
        if (relative_correction_z <= EPSILON)
            relative_correction_z = 1.;
        for (const SLAPrintObject *print_object : sla_print->objects()) {
            SLASupportState   &state        = sla_support_state[idx ++];
            const ModelObject *model_object = print_object->model_object();
            // Find an index of the ModelObject
            int object_idx;
            // There may be new SLA volumes added to the scene for this print_object.
            // Find the object index of this print_object in the Model::objects list.
            auto it = std::find(sla_print->model().objects.begin(), sla_print->model().objects.end(), model_object);
            assert(it != sla_print->model().objects.end());
            object_idx = it - sla_print->model().objects.begin();
            // Cache the Z offset to be applied to all volumes with this object_idx.
            shift_zs[object_idx] = print_object->get_current_elevation() / relative_correction_z;
            // Collect indices of this print_object's instances, for which the SLA support meshes are to be added to the scene.
            // pairs of <instance_idx, print_instance_idx>
            std::vector<std::pair<size_t, size_t>> instances[std::tuple_size<SLASteps>::value];
            for (size_t print_instance_idx = 0; print_instance_idx < print_object->instances().size(); ++ print_instance_idx) {
                const SLAPrintObject::Instance &instance = print_object->instances()[print_instance_idx];
                // Find index of ModelInstance corresponding to this SLAPrintObject::Instance.
                auto it = std::find_if(model_object->instances.begin(), model_object->instances.end(),
                    [&instance](const ModelInstance *mi) { return mi->id() == instance.instance_id; });
                assert(it != model_object->instances.end());
                int instance_idx = it - model_object->instances.begin();
                for (size_t istep = 0; istep < sla_steps.size(); ++istep) {
                    if (state.step[istep].state == PrintStateBase::State::Done) {
                        // Check whether there is an existing auxiliary volume to be updated, or a new auxiliary volume to be created.
                        ModelVolumeState key(state.step[istep].timestamp, instance.instance_id.id);
                        auto it = std::lower_bound(aux_volume_state.begin(), aux_volume_state.end(), key, model_volume_state_lower);
                        assert(it != aux_volume_state.end() && it->geometry_id == key.geometry_id);
                        if (it->new_geometry()) {
                            // This can be an SLA support structure that should not be rendered (in case someone used undo
                            // to revert to before it was generated). If that's the case, we should not generate anything.
                            if (model_object->sla_points_status != sla::PointsStatus::NoPoints)
                                instances[istep].emplace_back(std::pair<size_t, size_t>(instance_idx, print_instance_idx));
                            else
                                shift_zs[object_idx] = 0.;
                        }
                        else {
                            // Recycling an old GLVolume. Update the Object/Instance indices into the current Model.
                            const GLVolume::CompositeID new_id(object_idx, m_volumes.volumes[it->volume_idx]->volume_idx(), instance_idx);
                            new_to_old_ids_map.push_back(std::make_pair(new_id, m_volumes.volumes[it->volume_idx]->composite_id));
                            m_volumes.volumes[it->volume_idx]->composite_id = new_id;
                            m_volumes.volumes[it->volume_idx]->set_instance_transformation(model_object->instances[instance_idx]->get_transformation());
                        }
                    }
                }
            }

            for (size_t istep = 0; istep < sla_steps.size(); ++istep) {
                if (!instances[istep].empty())
                    m_volumes.load_object_auxiliary(print_object, object_idx, instances[istep], sla_steps[istep], state.step[istep].timestamp);
            }
        }

        // Shift-up all volumes of the object so that it has the right elevation with respect to the print bed
        for (GLVolume* volume : m_volumes.volumes) {
            const ModelObject* model_object = (volume->object_idx() < (int)m_model->objects.size()) ? m_model->objects[volume->object_idx()] : nullptr;
            if (model_object != nullptr && model_object->instances[volume->instance_idx()]->is_printable()) {
                const SLAPrintObject* po = sla_print->get_print_object_by_model_object_id(model_object->id());
                if (po != nullptr)
                    volume->set_sla_shift_z(po->get_current_elevation() / sla_print->relative_correction().z());
            }
        }
    }

    if (printer_technology == ptFFF && m_config->has("nozzle_diameter")) {
        // Should the wipe tower be visualized ?
        unsigned int extruders_count = (unsigned int)dynamic_cast<const ConfigOptionFloats*>(m_config->option("nozzle_diameter"))->values.size();

        const bool wt = dynamic_cast<const ConfigOptionBool*>(m_config->option("wipe_tower"))->value;
        const bool co = dynamic_cast<const ConfigOptionBool*>(m_config->option("complete_objects"))->value;
        const float w = dynamic_cast<const ConfigOptionFloat*>(m_config->option("wipe_tower_width"))->value;
        const float bw = dynamic_cast<const ConfigOptionFloat*>(m_config->option("wipe_tower_brim_width"))->value;
        const float ca = dynamic_cast<const ConfigOptionFloat*>(m_config->option("wipe_tower_cone_angle"))->value;

        if (extruders_count > 1 && wt && !co) {
            for (size_t bed_idx = 0; bed_idx < s_multiple_beds.get_max_beds(); ++bed_idx) {
                const Print *print = wxGetApp().plater()->get_fff_prints()[bed_idx].get();

                const float x = m_model->get_wipe_tower_vector()[bed_idx].position.x();
                const float y = m_model->get_wipe_tower_vector()[bed_idx].position.y();
                const float a = m_model->get_wipe_tower_vector()[bed_idx].rotation;
                const float depth = print->wipe_tower_data(extruders_count).depth;
                const std::vector<std::pair<float, float>> z_and_depth_pairs = print->wipe_tower_data(extruders_count).z_and_depth_pairs;
                const float height_real = print->wipe_tower_data(extruders_count).height; // -1.f = unknown
                const bool is_wipe_tower_step_done = print->is_step_done(psWipeTower);

                // Height of a print (Show at least a slab).
                const double height = height_real < 0.f ? std::max(m_model->max_z(), 10.0) : height_real;
                if (depth != 0.) {
#if SLIC3R_OPENGL_ES
                    if (bed_idx >= m_wipe_tower_meshes.size())
                        m_wipe_tower_meshes.resize(bed_idx + 1);
                    GLVolume* volume = m_volumes.load_wipe_tower_preview(
                        x, y, w, depth, z_and_depth_pairs, (float)height, ca, a, !is_wipe_tower_step_done,
                        bw, bed_idx, &m_wipe_tower_meshes[bed_idx]);
#else
                    GLVolume* volume = m_volumes.load_wipe_tower_preview(
                        x, y, w, depth, z_and_depth_pairs, (float)height, ca, a, !is_wipe_tower_step_done,
                        bw, bed_idx);
#endif // SLIC3R_OPENGL_ES
                    const BoundingBoxf3& bb = volume->bounding_box();
                    m_wipe_tower_bounding_boxes[bed_idx] = BoundingBoxf{to_2d(bb.min), to_2d(bb.max)};
                    if(static_cast<int>(bed_idx) < s_multiple_beds.get_number_of_beds()) {
                        m_volumes.volumes.emplace_back(volume);
                        const auto volume_idx_wipe_tower_new{static_cast<int>(m_volumes.volumes.size() - 1)};
                        auto it = volume_idxs_wipe_towers_old.find(m_volumes.volumes.back()->geometry_id.second);
                        if (it != volume_idxs_wipe_towers_old.end())
                            map_glvolume_old_to_new[it->second] = volume_idx_wipe_tower_new;
                        m_volumes.volumes.back()->set_volume_offset(m_volumes.volumes.back()->get_volume_offset() + s_multiple_beds.get_bed_translation(bed_idx));
                    } else {
                        delete volume;
                    }
                } else {
                    m_wipe_tower_bounding_boxes[bed_idx] = std::nullopt;
                }
            }
            s_multiple_beds.ensure_wipe_towers_on_beds(wxGetApp().plater()->model(), wxGetApp().plater()->get_fff_prints());
        } else {
            m_wipe_tower_bounding_boxes.fill(std::nullopt);
        }
    } else {
        m_wipe_tower_bounding_boxes.fill(std::nullopt);
    }

    update_volumes_colors_by_extruder();
    // Update selection indices based on the old/new GLVolumeCollection.
    if (m_selection.get_mode() == Selection::Instance)
        m_selection.instances_changed(instance_ids_selected);
    else
        m_selection.volumes_changed(map_glvolume_old_to_new);

    if (printer_technology == ptSLA) {
        std::sort(new_to_old_ids_map.begin(), new_to_old_ids_map.end(),
            [](const std::pair<GLVolume::CompositeID, GLVolume::CompositeID>& i1, const std::pair<GLVolume::CompositeID, GLVolume::CompositeID>& i2) {
                return i1.first.object_id < i2.first.object_id || (i1.first.object_id == i2.first.object_id && i1.first.instance_id < i2.first.instance_id); });

        new_to_old_ids_map.erase(std::unique(new_to_old_ids_map.begin(), new_to_old_ids_map.end(),
            [](const std::pair<GLVolume::CompositeID, GLVolume::CompositeID>& i1, const std::pair<GLVolume::CompositeID, GLVolume::CompositeID>& i2) {
                return composite_id_match(i1.first, i2.first); }), new_to_old_ids_map.end());

        m_sla_view.update_instances_cache(new_to_old_ids_map);
        if (m_sla_view_type_detection_active) {
            m_sla_view.detect_type_from_volumes(m_volumes.volumes);
            m_sla_view_type_detection_active = false;
        }
        m_sla_view.update_volumes_visibility(m_volumes.volumes);
        update_object_list = true;
    }

    // @Enrico suggest this solution to preven accessing pointer on caster without data
    m_scene_raycaster.remove_raycasters(SceneRaycaster::EType::Volume);
    m_gizmos.update_data();
    m_gizmos.refresh_on_off_state();

    // Update the toolbar
    if (update_object_list)
        post_event(SimpleEvent(EVT_GLCANVAS_OBJECT_SELECT));

    // checks for geometry outside the print volume to render it accordingly
    if (!m_volumes.empty()) {
        ModelInstanceEPrintVolumeState state;
        check_volumes_outside_state(m_volumes, &state, !force_full_scene_refresh);
        const bool partlyOut = (state == ModelInstanceEPrintVolumeState::ModelInstancePVS_Partly_Outside);
        const bool fullyOut = (state == ModelInstanceEPrintVolumeState::ModelInstancePVS_Fully_Outside);

        if (printer_technology != ptSLA) {
            _set_warning_notification(EWarning::ObjectClashed, partlyOut);
            _set_warning_notification(EWarning::ObjectOutside, fullyOut);
            _set_warning_notification(EWarning::SlaSupportsOutside, false);
        }
        else {
            const auto [res, volume] = _is_any_volume_outside();
            const bool is_support = volume != nullptr && volume->is_sla_support();
            if (is_support) {
                _set_warning_notification(EWarning::ObjectClashed, false);
                _set_warning_notification(EWarning::ObjectOutside, false);
                _set_warning_notification(EWarning::SlaSupportsOutside, partlyOut || fullyOut);
            }
            else {
                _set_warning_notification(EWarning::ObjectClashed, partlyOut);
                _set_warning_notification(EWarning::ObjectOutside, fullyOut);
                _set_warning_notification(EWarning::SlaSupportsOutside, false);
            }
        }
    }
    else {
        _set_warning_notification(EWarning::ObjectOutside, false);
        _set_warning_notification(EWarning::ObjectClashed, false);
        _set_warning_notification(EWarning::SlaSupportsOutside, false);
    }

    refresh_camera_scene_box();

    if (m_selection.is_empty()) {
        // If no object is selected, deactivate the active gizmo, if any
        // Otherwise it may be shown after cleaning the scene (if it was active while the objects were deleted)
        m_gizmos.reset_all_states();

        // If no object is selected, reset the objects manipulator on the sidebar
        // to force a reset of its cache
        auto manip = wxGetApp().obj_manipul();
        if (manip != nullptr)
            manip->set_dirty();
    }

    // refresh volume raycasters for picking
    for (size_t i = 0; i < m_volumes.volumes.size(); ++i) {
        const GLVolume* v = m_volumes.volumes[i];
        assert(v->mesh_raycaster != nullptr);
        std::shared_ptr<SceneRaycasterItem> raycaster = add_raycaster_for_picking(SceneRaycaster::EType::Volume, i, *v->mesh_raycaster, v->world_matrix());
        raycaster->set_active(v->is_active);
    }

    // check activity/visibility of the modifiers in SLA mode
    for (GLVolume* volume : m_volumes.volumes)
        if (volume->object_idx() < (int)m_model->objects.size() && m_model->objects[volume->object_idx()]->instances[volume->instance_idx()]->is_printable()) {
            if (volume->is_active && volume->is_modifier && m_model->objects[volume->object_idx()]->volumes[volume->volume_idx()]->is_modifier())
                volume->is_active = printer_technology != ptSLA;
        }

    // refresh gizmo elements raycasters for picking
    GLGizmoBase* curr_gizmo = m_gizmos.get_current();
    if (curr_gizmo != nullptr)
        curr_gizmo->unregister_raycasters_for_picking();
    m_scene_raycaster.remove_raycasters(SceneRaycaster::EType::Gizmo);
    m_scene_raycaster.remove_raycasters(SceneRaycaster::EType::FallbackGizmo);
    if (curr_gizmo != nullptr && !m_selection.is_empty())
        curr_gizmo->register_raycasters_for_picking();

    // and force this canvas to be redrawn.
    m_dirty = true;
}

void GLCanvas3D::load_gcode_shells()
{
    m_gcode_viewer.load_shells(*this->fff_print());
    m_gcode_viewer.update_shells_color_by_extruder(m_config);
    m_gcode_viewer.set_force_shells_visible(true);
}

void GLCanvas3D::load_gcode_preview(const GCodeProcessorResult& gcode_result, const std::vector<std::string>& str_tool_colors,
    const std::vector<std::string>& str_color_print_colors)
{
    m_gcode_viewer.enable_legend(true);
    m_gcode_viewer.enable_view_type_cache_write(true);
    m_gcode_viewer.enable_view_type_cache_load(true);
    m_gcode_viewer.set_view_type(m_gcode_viewer.get_view_type());
    m_gcode_viewer.load_as_gcode(gcode_result, *this->fff_print(), str_tool_colors, str_color_print_colors);
    m_gcode_layers_times_cache = m_gcode_viewer.get_layers_times();
    m_gcode_viewer.set_force_shells_visible(false);
    if (wxGetApp().is_editor()) {
        _set_warning_notification_if_needed(EWarning::ToolpathOutside);
        _set_warning_notification_if_needed(EWarning::GCodeConflict);
    }

    set_as_dirty();
    request_extra_frame();
}

void GLCanvas3D::load_sla_preview()
{
    const SLAPrint* print = sla_print();
    if (m_canvas != nullptr && print != nullptr) {
        _set_current();
        // Release OpenGL data before generating new data.
        reset_volumes();
        _load_sla_shells();
        _update_sla_shells_outside_state();
        m_gcode_viewer.set_force_shells_visible(false);
        _set_warning_notification_if_needed(EWarning::ObjectClashed);
        _set_warning_notification_if_needed(EWarning::SlaSupportsOutside);
    }
}

void GLCanvas3D::load_preview(const std::vector<std::string>& str_tool_colors, const std::vector<std::string>& str_color_print_colors,
    const std::vector<CustomGCode::Item>& color_print_values)
{
    const Print *print = this->fff_print();
    if (print == nullptr)
        return;

    _set_current();

    libvgcode::GCodeInputData data = libvgcode::convert(*print, str_tool_colors, str_color_print_colors, color_print_values,
        static_cast<size_t>(wxGetApp().extruders_edited_cnt()));

    // send data to the viewer
    m_gcode_viewer.enable_legend(false);
    m_gcode_viewer.enable_view_type_cache_write(false);
    m_gcode_viewer.enable_view_type_cache_load(false);
    m_gcode_viewer.set_view_type(libvgcode::EViewType::FeatureType);
    m_gcode_viewer.load_as_preview(std::move(data));
    m_gcode_viewer.set_force_shells_visible(false);
    _set_warning_notification_if_needed(EWarning::ToolpathOutside);
}

void GLCanvas3D::bind_event_handlers()
{
    if (m_canvas != nullptr) {
        m_canvas->Bind(wxEVT_SIZE, &GLCanvas3D::on_size, this);
        m_canvas->Bind(wxEVT_IDLE, &GLCanvas3D::on_idle, this);
        m_canvas->Bind(wxEVT_CHAR, &GLCanvas3D::on_char, this);
        m_canvas->Bind(wxEVT_KEY_DOWN, &GLCanvas3D::on_key, this);
        m_canvas->Bind(wxEVT_KEY_UP, &GLCanvas3D::on_key, this);
        m_canvas->Bind(wxEVT_MOUSEWHEEL, &GLCanvas3D::on_mouse_wheel, this);
        m_canvas->Bind(wxEVT_TIMER, &GLCanvas3D::on_timer, this);
        m_canvas->Bind(EVT_GLCANVAS_RENDER_TIMER, &GLCanvas3D::on_render_timer, this);
        m_toolbar_highlighter.set_timer_owner(m_canvas, 0);
        m_canvas->Bind(EVT_GLCANVAS_TOOLBAR_HIGHLIGHTER_TIMER, [this](wxTimerEvent&) { m_toolbar_highlighter.blink(); });
        m_gizmo_highlighter.set_timer_owner(m_canvas, 0);
        m_canvas->Bind(EVT_GLCANVAS_GIZMO_HIGHLIGHTER_TIMER, [this](wxTimerEvent&) { m_gizmo_highlighter.blink(); });
        m_canvas->Bind(wxEVT_LEFT_DOWN, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_LEFT_UP, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_MIDDLE_DOWN, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_MIDDLE_UP, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_RIGHT_DOWN, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_RIGHT_UP, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_MOTION, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_ENTER_WINDOW, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_LEAVE_WINDOW, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_LEFT_DCLICK, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_MIDDLE_DCLICK, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_RIGHT_DCLICK, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_PAINT, &GLCanvas3D::on_paint, this);
        m_canvas->Bind(wxEVT_SET_FOCUS, &GLCanvas3D::on_set_focus, this);

        m_event_handlers_bound = true;
    }
}

void GLCanvas3D::unbind_event_handlers()
{
    if (m_canvas != nullptr && m_event_handlers_bound) {
        m_canvas->Unbind(wxEVT_SIZE, &GLCanvas3D::on_size, this);
        m_canvas->Unbind(wxEVT_IDLE, &GLCanvas3D::on_idle, this);
        m_canvas->Unbind(wxEVT_CHAR, &GLCanvas3D::on_char, this);
        m_canvas->Unbind(wxEVT_KEY_DOWN, &GLCanvas3D::on_key, this);
        m_canvas->Unbind(wxEVT_KEY_UP, &GLCanvas3D::on_key, this);
        m_canvas->Unbind(wxEVT_MOUSEWHEEL, &GLCanvas3D::on_mouse_wheel, this);
        m_canvas->Unbind(wxEVT_TIMER, &GLCanvas3D::on_timer, this);
        m_canvas->Unbind(EVT_GLCANVAS_RENDER_TIMER, &GLCanvas3D::on_render_timer, this);
        m_canvas->Unbind(wxEVT_LEFT_DOWN, &GLCanvas3D::on_mouse, this);
		m_canvas->Unbind(wxEVT_LEFT_UP, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_MIDDLE_DOWN, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_MIDDLE_UP, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_RIGHT_DOWN, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_RIGHT_UP, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_MOTION, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_ENTER_WINDOW, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_LEAVE_WINDOW, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_LEFT_DCLICK, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_MIDDLE_DCLICK, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_RIGHT_DCLICK, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_PAINT, &GLCanvas3D::on_paint, this);
        m_canvas->Unbind(wxEVT_SET_FOCUS, &GLCanvas3D::on_set_focus, this);

        m_event_handlers_bound = false;
    }
}
 
void GLCanvas3D::on_idle(wxIdleEvent& evt)
{
    if (!m_initialized)
        return;

    m_dirty |= m_main_toolbar.update_items_state();
    m_dirty |= m_undoredo_toolbar.update_items_state();
    m_dirty |= wxGetApp().plater()->get_view_toolbar().update_items_state();
    m_dirty |= wxGetApp().plater()->get_collapse_toolbar().update_items_state();
    bool mouse3d_controller_applied = wxGetApp().plater()->get_mouse3d_controller().apply(wxGetApp().plater()->get_camera());
    m_dirty |= mouse3d_controller_applied;
    m_dirty |= wxGetApp().plater()->get_notification_manager()->update_notifications(*this);
    auto gizmo = wxGetApp().plater()->canvas3D()->get_gizmos_manager().get_current();
    if (gizmo != nullptr) m_dirty |= gizmo->update_items_state();
    // ImGuiWrapper::m_requires_extra_frame may have been set by a render made outside of the OnIdle mechanism
    bool imgui_requires_extra_frame = wxGetApp().imgui()->requires_extra_frame();
    m_dirty |= imgui_requires_extra_frame;

    if (!m_dirty)
        return;

    // this needs to be done here.
    // during the render launched by the refresh the value may be set again 
    wxGetApp().imgui()->reset_requires_extra_frame();

    _refresh_if_shown_on_screen();

    if (m_extra_frame_requested || mouse3d_controller_applied || imgui_requires_extra_frame || wxGetApp().imgui()->requires_extra_frame()) {
        m_extra_frame_requested = false;
        evt.RequestMore();
    }
    else
        m_dirty = false;
}

void GLCanvas3D::on_char(wxKeyEvent& evt)
{
    if (!m_initialized)
        return;

#ifdef SHOW_IMGUI_DEMO_WINDOW
    static int cur = 0;
    if (get_logging_level() >= 3 && wxString("demo")[cur] == evt.GetUnicodeKey()) ++cur; else cur = 0;
    if (cur == 4) { show_imgui_demo_window = !show_imgui_demo_window; cur = 0;}
#endif // SHOW_IMGUI_DEMO_WINDOW

    auto imgui = wxGetApp().imgui();
    if (imgui->update_key_data(evt)) {
        render();
        return;
    }

    // see include/wx/defs.h enum wxKeyCode
    int keyCode = evt.GetKeyCode();
    int ctrlMask = wxMOD_CONTROL;
    int shiftMask = wxMOD_SHIFT;
    if (keyCode == WXK_ESCAPE && (_deactivate_undo_redo_toolbar_items() || _deactivate_arrange_menu()))
        return;

    if (m_gizmos.on_char(evt)) {
        if (m_gizmos.get_current_type() == GLGizmosManager::EType::Scale &&
            m_gizmos.get_current()->get_state() == GLGizmoBase::EState::On) {
            // Update selection from object list to check selection of the cut objects
            // It's not allowed to scale separate ct parts
            wxGetApp().obj_list()->selection_changed();
        }
        return;
    }

    if ((evt.GetModifiers() & ctrlMask) != 0) {
        // CTRL is pressed
        switch (keyCode) {
#ifdef __APPLE__
        case 'a':
        case 'A':
#else /* __APPLE__ */
        case WXK_CONTROL_A:
#endif /* __APPLE__ */
            post_event(SimpleEvent(EVT_GLCANVAS_SELECT_ALL));
        break;
#ifdef __APPLE__
        case 'c':
        case 'C':
#else /* __APPLE__ */
        case WXK_CONTROL_C:
#endif /* __APPLE__ */
            post_event(SimpleEvent(EVT_GLTOOLBAR_COPY));
        break;
#ifdef __APPLE__
        case 'm':
        case 'M':
#else /* __APPLE__ */
        case WXK_CONTROL_M:
#endif /* __APPLE__ */
        {
#ifdef _WIN32
            if (wxGetApp().app_config->get_bool("use_legacy_3DConnexion")) {
#endif //_WIN32
#ifdef __APPLE__
            // On OSX use Cmd+Shift+M to "Show/Hide 3Dconnexion devices settings dialog"
            if ((evt.GetModifiers() & shiftMask) != 0) {
#endif // __APPLE__
                Mouse3DController& controller = wxGetApp().plater()->get_mouse3d_controller();
                controller.show_settings_dialog(!controller.is_settings_dialog_shown());
                m_dirty = true;
#ifdef __APPLE__
            } 
            else 
            // and Cmd+M to minimize application
                wxGetApp().mainframe->Iconize();
#endif // __APPLE__
#ifdef _WIN32
            }
#endif //_WIN32
            break;
        }
#ifdef __APPLE__
        case 'v':
        case 'V':
#else /* __APPLE__ */
        case WXK_CONTROL_V:
#endif /* __APPLE__ */
            post_event(SimpleEvent(EVT_GLTOOLBAR_PASTE));
        break;
#ifdef __APPLE__
        case 'y':
        case 'Y':
#else /* __APPLE__ */
        case WXK_CONTROL_Y:
#endif /* __APPLE__ */
            post_event(SimpleEvent(EVT_GLCANVAS_REDO));
        break;
#ifdef __APPLE__
        case 'z':
        case 'Z':
#else /* __APPLE__ */
        case WXK_CONTROL_Z:
#endif /* __APPLE__ */
            post_event(SimpleEvent(EVT_GLCANVAS_UNDO));
        break;

        case WXK_BACK:
        case WXK_DELETE:
             post_event(SimpleEvent(EVT_GLTOOLBAR_DELETE_ALL)); break;
        default:            evt.Skip();
        }
    } else {
        switch (keyCode)
        {
        case WXK_BACK:
        case WXK_DELETE: { post_event(SimpleEvent(EVT_GLTOOLBAR_DELETE)); break; }
        case WXK_ESCAPE: { deselect_all(); break; }
        case WXK_F5: {
            if ((wxGetApp().is_editor() && !wxGetApp().plater()->model().objects.empty()) ||
                (wxGetApp().is_gcode_viewer() && !wxGetApp().plater()->get_last_loaded_gcode().empty()))
                post_event(SimpleEvent(EVT_GLCANVAS_RELOAD_FROM_DISK));
            break;
        }
        case '0': { select_view("iso"); break; }
        case '1': { select_view("top"); break; }
        case '2': { select_view("bottom"); break; }
        case '3': { select_view("front"); break; }
        case '4': { select_view("rear"); break; }
        case '5': { select_view("left"); break; }
        case '6': { select_view("right"); break; }
        case '+': {
            if (dynamic_cast<Preview*>(m_canvas->GetParent()) != nullptr)
                post_event(wxKeyEvent(EVT_GLCANVAS_SLIDERS_MANIPULATION, evt));
            else
                post_event(Event<int>(EVT_GLCANVAS_INCREASE_INSTANCES, +1));
            break;
        }
        case '-': {
            if (dynamic_cast<Preview*>(m_canvas->GetParent()) != nullptr)
                post_event(wxKeyEvent(EVT_GLCANVAS_SLIDERS_MANIPULATION, evt));
            else
                post_event(Event<int>(EVT_GLCANVAS_INCREASE_INSTANCES, -1)); 
            break;
        }
        case '?': { post_event(SimpleEvent(EVT_GLCANVAS_QUESTION_MARK)); break; }
        case 'A':
        case 'a': { post_event(SimpleEvent(EVT_GLCANVAS_ARRANGE)); break; }
        case 'B':
        case 'b': { zoom_to_bed(); break; }
        case 'C':
        case 'c': { m_gcode_viewer.toggle_gcode_window_visibility(); m_dirty = true; request_extra_frame(); break; }
        case 'D':
        case 'd': { post_event(SimpleEvent(EVT_GLCANVAS_ARRANGE_CURRENT_BED)); break; }
        case 'E':
        case 'e': { m_labels.show(!m_labels.is_shown()); m_dirty = true; break; }
        case 'G':
        case 'g': {
            if ((evt.GetModifiers() & shiftMask) != 0) {
                if (dynamic_cast<Preview*>(m_canvas->GetParent()) != nullptr)
                    post_event(wxKeyEvent(EVT_GLCANVAS_SLIDERS_MANIPULATION, evt));
            }
            break;
        }
        case 'I':
        case 'i': { _update_camera_zoom(1.0); break; }
        case 'K':
        case 'k': { wxGetApp().plater()->get_camera().select_next_type(); m_dirty = true; break; }
        case 'L': 
        case 'l': { 
            if (!m_main_toolbar.is_enabled())
                show_legend(!is_legend_shown());
            break;
        }
        case 'O':
        case 'o': { _update_camera_zoom(-1.0); break; }
        case 'Z':
        case 'z': {
            if (!m_selection.is_empty())
                zoom_to_selection();
            else {
                if (!m_volumes.empty())
                    zoom_to_volumes();
                else
                    _zoom_to_box(m_gcode_viewer.get_paths_bounding_box());
            }
            break;
        }
        default:  { evt.Skip(); break; }
        }
    }
}

class TranslationProcessor
{
    using UpAction = std::function<void(void)>;
    using DownAction = std::function<void(const Vec3d&, bool, bool)>;

    UpAction m_up_action{ nullptr };
    DownAction m_down_action{ nullptr };

    bool m_running{ false };
    Vec3d m_direction{ Vec3d::UnitX() };

public:
    TranslationProcessor(UpAction up_action, DownAction down_action)
        : m_up_action(up_action), m_down_action(down_action)
    {
    }

    void process(wxKeyEvent& evt)
    {
        const int keyCode = evt.GetKeyCode();
        wxEventType type = evt.GetEventType();
        if (type == wxEVT_KEY_UP) {
            switch (keyCode)
            {
            case WXK_NUMPAD_LEFT:  case WXK_LEFT:
            case WXK_NUMPAD_RIGHT: case WXK_RIGHT:
            case WXK_NUMPAD_UP:    case WXK_UP:
            case WXK_NUMPAD_DOWN:  case WXK_DOWN:
            {
                m_running = false;
                m_up_action();
                break;
            }
            default: { break; }
            }
        }
        else if (type == wxEVT_KEY_DOWN) {
            bool apply = false;

            switch (keyCode)
            {
            case WXK_SHIFT:
            {
                if (m_running) 
                    apply = true;

                break;
            }
            case WXK_NUMPAD_LEFT:
            case WXK_LEFT:
            {
                m_direction = -Vec3d::UnitX();
                apply = true;
                break;
            }
            case WXK_NUMPAD_RIGHT:
            case WXK_RIGHT:
            {
                m_direction = Vec3d::UnitX();
                apply = true;
                break;
            }
            case WXK_NUMPAD_UP:
            case WXK_UP:
            {
                m_direction = Vec3d::UnitY();
                apply = true;
                break;
            }
            case WXK_NUMPAD_DOWN:
            case WXK_DOWN:
            {
                m_direction = -Vec3d::UnitY();
                apply = true;
                break;
            }
            default: { break; }
            }

            if (apply) {
                m_running = true;
                m_down_action(m_direction, evt.ShiftDown(), evt.CmdDown());
            }
        }
    }
};

void GLCanvas3D::on_key(wxKeyEvent& evt)
{
    static TranslationProcessor translationProcessor(
        [this]() {
            do_move(L("Gizmo-Move"));
            m_gizmos.update_data();

            wxGetApp().obj_manipul()->set_dirty();
            // Let the plater know that the dragging finished, so a delayed refresh
            // of the scene with the background processing data should be performed.
            post_event(SimpleEvent(EVT_GLCANVAS_MOUSE_DRAGGING_FINISHED));
            // updates camera target constraints
            refresh_camera_scene_box();
            m_dirty = true;
        },
        [this](const Vec3d& direction, bool slow, bool camera_space) {
            m_selection.setup_cache();
            double multiplier = slow ? 1.0 : 10.0;

            Vec3d displacement;
            if (camera_space) {
                Eigen::Matrix<double, 3, 3, Eigen::DontAlign> inv_view_3x3 = wxGetApp().plater()->get_camera().get_view_matrix().inverse().matrix().block(0, 0, 3, 3);
                displacement = multiplier * (inv_view_3x3 * direction);
                displacement.z() = 0.0;
            }
            else
                displacement = multiplier * direction;

            TransformationType trafo_type;
            trafo_type.set_relative();
            m_selection.translate(displacement, trafo_type);
            m_dirty = true;
        }
    );

    const int keyCode = evt.GetKeyCode();

    auto imgui = wxGetApp().imgui();
    if (imgui->update_key_data(evt))
        render();
    else {
        if (!m_gizmos.on_key(evt)) {
            if (evt.GetEventType() == wxEVT_KEY_UP) {
                if (get_logging_level() >= 3 && evt.ShiftDown() && evt.ControlDown() && keyCode == WXK_SPACE) {
                    wxGetApp().plater()->toggle_render_statistic_dialog();
                    m_dirty = true;
                }
                if (m_tab_down && keyCode == WXK_TAB && !evt.HasAnyModifiers()) {
                    // Enable switching between 3D and Preview with Tab
                    // m_canvas->HandleAsNavigationKey(evt);   // XXX: Doesn't work in some cases / on Linux
                    post_event(SimpleEvent(EVT_GLCANVAS_TAB));
                }
                else if (! wxGetApp().is_gcode_viewer() && keyCode == WXK_TAB &&
                    // Use strong condition for modifiers state to avoid cases when Shift can be combined with other modifiers
                    // (see https://github.com/prusa3d/PrusaSlicer/issues/7799)
                    evt.GetModifiers() == wxMOD_SHIFT) {
                    // Collapse side-panel with Shift+Tab
                    post_event(SimpleEvent(EVT_GLCANVAS_COLLAPSE_SIDEBAR));
                }
                else if (keyCode == WXK_SHIFT) {
                    translationProcessor.process(evt);

                    if (m_picking_enabled && m_rectangle_selection.is_dragging()) {
                        _update_selection_from_hover();
                        m_rectangle_selection.stop_dragging();
                        m_mouse.ignore_left_up = true;
                    }
                    m_shift_kar_filter.reset_count();
                    m_dirty = true;
//                    set_cursor(Standard);
                }
                else if (keyCode == WXK_ALT) {
                    if (m_picking_enabled && m_rectangle_selection.is_dragging()) {
                        _update_selection_from_hover();
                        m_rectangle_selection.stop_dragging();
                        m_mouse.ignore_left_up = true;
                        m_dirty = true;
                    }
//                    set_cursor(Standard);
                }
                else if (keyCode == WXK_CONTROL) {
                  if (m_mouse.dragging && !m_moving) {
                        // if the user releases CTRL while rotating the 3D scene
                        // prevent from moving the selected volume
                        m_mouse.drag.move_volume_idx = -1;
                        m_mouse.set_start_position_3D_as_invalid();
                    }
                    m_ctrl_kar_filter.reset_count();
                    m_dirty = true;
                }
                else if (m_gizmos.is_enabled() && !m_selection.is_empty()) {
                    translationProcessor.process(evt);

                    switch (keyCode)
                    {
                    case WXK_NUMPAD_PAGEUP:   case WXK_PAGEUP:
                    case WXK_NUMPAD_PAGEDOWN: case WXK_PAGEDOWN:
                    {
                        do_rotate(L("Gizmo-Rotate"));
                        m_gizmos.update_data();

                        wxGetApp().obj_manipul()->set_dirty();
                        // Let the plater know that the dragging finished, so a delayed refresh
                        // of the scene with the background processing data should be performed.
                        post_event(SimpleEvent(EVT_GLCANVAS_MOUSE_DRAGGING_FINISHED));
                        // updates camera target constraints
                        refresh_camera_scene_box();
                        m_dirty = true;

                        break;
                    }
                    default: { break; }
                    }
                }
            }
            else if (evt.GetEventType() == wxEVT_KEY_DOWN) {
                m_tab_down = keyCode == WXK_TAB && !evt.HasAnyModifiers();
                if (keyCode == WXK_SHIFT) {
                    translationProcessor.process(evt);

                    if (m_picking_enabled && (m_gizmos.get_current_type() != GLGizmosManager::SlaSupports)) {
                        m_mouse.ignore_left_up = false;
//                        set_cursor(Cross);
                    }
                    if (m_shift_kar_filter.is_first())
                        m_dirty = true;

                    m_shift_kar_filter.increase_count();
                }
                else if (keyCode == WXK_ALT) {
                    if (m_picking_enabled && (m_gizmos.get_current_type() != GLGizmosManager::SlaSupports)) {
                        m_mouse.ignore_left_up = false;
//                        set_cursor(Cross);
                    }
                }
                else if (keyCode == WXK_CONTROL) {
                    if (m_ctrl_kar_filter.is_first())
                        m_dirty = true;

                    m_ctrl_kar_filter.increase_count();
                }
                else if (m_gizmos.is_enabled() && !m_selection.is_empty()) {
                    auto do_rotate = [this](double angle_z_rad) {
                        m_selection.setup_cache();
                        m_selection.rotate(Vec3d(0.0, 0.0, angle_z_rad), TransformationType(TransformationType::World_Relative_Joint));
                        m_dirty = true;
//                        wxGetApp().obj_manipul()->set_dirty();
                    };

                    translationProcessor.process(evt);

                    switch (keyCode)
                    {
                    case WXK_NUMPAD_PAGEUP:   case WXK_PAGEUP:   { do_rotate(0.25 * M_PI); break; }
                    case WXK_NUMPAD_PAGEDOWN: case WXK_PAGEDOWN: { do_rotate(-0.25 * M_PI); break; }
                    default: { break; }
                    }
                }
                else if (!m_gizmos.is_enabled()) {
                    // DoubleSlider navigation in Preview
                    if (keyCode == WXK_LEFT ||
                        keyCode == WXK_RIGHT ||
                        keyCode == WXK_UP ||
                        keyCode == WXK_DOWN) {
                        if (dynamic_cast<Preview*>(m_canvas->GetParent()) != nullptr)
                            post_event(wxKeyEvent(EVT_GLCANVAS_SLIDERS_MANIPULATION, evt));
                    }
                }
            }
        }
    }

    const GLGizmosManager::EType gizmo_type = m_gizmos.get_current_type();
    if (keyCode == WXK_ALT && (gizmo_type == GLGizmosManager::FdmSupports ||
                               gizmo_type == GLGizmosManager::Seam ||
                               gizmo_type == GLGizmosManager::MmSegmentation ||
                               gizmo_type == GLGizmosManager::FuzzySkin)) {
        // Prevents focusing on the menu bar when ALT is pressed in painting gizmos (FdmSupports, Seam, MmSegmentation, and FuzzySkin).
        evt.Skip(false);
    } else if (keyCode != WXK_TAB
        && keyCode != WXK_LEFT
        && keyCode != WXK_UP
        && keyCode != WXK_RIGHT
        && keyCode != WXK_DOWN) {
        evt.Skip();   // Needed to have EVT_CHAR generated as well
    }
}

void GLCanvas3D::on_mouse_wheel(wxMouseEvent& evt)
{
#ifdef WIN32
    // Try to filter out spurious mouse wheel events comming from 3D mouse.
    if (wxGetApp().plater()->get_mouse3d_controller().process_mouse_wheel())
        return;
#endif

    if (!m_initialized)
        return;

    // Ignore the wheel events if the middle button is pressed.
    if (evt.MiddleIsDown())
        return;

#if ENABLE_RETINA_GL
    const float scale = m_retina_helper->get_scale_factor();
    evt.SetX(evt.GetX() * scale);
    evt.SetY(evt.GetY() * scale);
#endif

    if (wxGetApp().imgui()->update_mouse_data(evt)) {
        m_dirty = true;
        return;
    }

#ifdef __WXMSW__
	// For some reason the Idle event is not being generated after the mouse scroll event in case of scrolling with the two fingers on the touch pad,
	// if the event is not allowed to be passed further.
	// https://github.com/prusa3d/PrusaSlicer/issues/2750
    // evt.Skip() used to trigger the needed screen refresh, but it does no more. wxWakeUpIdle() seem to work now.
    wxWakeUpIdle();
#endif /* __WXMSW__ */

    // Performs layers editing updates, if enabled
    if (is_layers_editing_enabled()) {
        int object_idx_selected = m_selection.get_object_idx();
        if (object_idx_selected != -1) {
            // A volume is selected. Test, whether hovering over a layer thickness bar.
            if (m_layers_editing.bar_rect_contains(*this, (float)evt.GetX(), (float)evt.GetY())) {
                // Adjust the width of the selection.
                m_layers_editing.band_width = std::max(std::min(m_layers_editing.band_width * (1.0f + 0.1f * (float)evt.GetWheelRotation() / (float)evt.GetWheelDelta()), 10.0f), 1.5f);
                if (m_canvas != nullptr)
                    m_canvas->Refresh();

                return;
            }
        }
    }

    // If Undo/Redo list is opened,
    // update them according to the event
    if (m_undoredo_toolbar.is_item_pressed("undo")  ||
        m_undoredo_toolbar.is_item_pressed("redo")) {
        m_mouse_wheel = int((double)evt.GetWheelRotation() / (double)evt.GetWheelDelta());
        return;
    }

    // Inform gizmos about the event so they have the opportunity to react.
    if (m_gizmos.on_mouse_wheel(evt))
        return;

    // Calculate the zoom delta and apply it to the current zoom factor
    const double direction_factor = wxGetApp().app_config->get_bool("reverse_mouse_wheel_zoom") ? -1.0 : 1.0;
    const double delta = direction_factor * (double)evt.GetWheelRotation() / (double)evt.GetWheelDelta();
    if (wxGetKeyState(WXK_SHIFT)) {
        const auto cnv_size = get_canvas_size();
        const Vec3d screen_center_3d_pos = _mouse_to_3d({ cnv_size.get_width() * 0.5, cnv_size.get_height() * 0.5 });
        const Vec3d mouse_3d_pos = _mouse_to_3d({ evt.GetX(), evt.GetY() });
        const Vec3d displacement = mouse_3d_pos - screen_center_3d_pos;
        wxGetApp().plater()->get_camera().translate_world(displacement);
        const double origin_zoom = wxGetApp().plater()->get_camera().get_zoom();
        _update_camera_zoom(delta);
        const double new_zoom = wxGetApp().plater()->get_camera().get_zoom();
        wxGetApp().plater()->get_camera().translate_world((-displacement) / (new_zoom / origin_zoom));
    }
    else
        _update_camera_zoom(delta);
}

void GLCanvas3D::on_timer(wxTimerEvent& evt)
{
    if (m_layers_editing.state == LayersEditing::Editing)
        _perform_layer_editing_action();
}

void GLCanvas3D::on_render_timer(wxTimerEvent& evt)
{
    m_dirty = true;

    // no need to wake up idle
    // right after this event, idle event is fired
    //wxWakeUpIdle();
}


void GLCanvas3D::schedule_extra_frame(int miliseconds)
{
    // Schedule idle event right now
    if (miliseconds == 0)
    {
        // We want to wakeup idle evnt but most likely this is call inside render cycle so we need to wait
        if (m_in_render)
            miliseconds = 33;
        else {
            m_dirty = true;
            wxWakeUpIdle();
            return;
        }
    } 
    int remaining_time = m_render_timer.GetInterval();
    // Timer is not running
    if (!m_render_timer.IsRunning()) {
        m_render_timer.StartOnce(miliseconds);
    // Timer is running - restart only if new period is shorter than remaning period
    } else {
        if (miliseconds + 20 < remaining_time) {
            m_render_timer.Stop(); 
            m_render_timer.StartOnce(miliseconds);
        }
    }
}

#ifndef NDEBUG
// #define SLIC3R_DEBUG_MOUSE_EVENTS
#endif

#ifdef SLIC3R_DEBUG_MOUSE_EVENTS
std::string format_mouse_event_debug_message(const wxMouseEvent &evt)
{
	static int idx = 0;
	char buf[2048];
	std::string out;
	sprintf(buf, "Mouse Event %d - ", idx ++);
	out = buf;

	if (evt.Entering())
		out += "Entering ";
	if (evt.Leaving())
		out += "Leaving ";
	if (evt.Dragging())
		out += "Dragging ";
	if (evt.Moving())
		out += "Moving ";
	if (evt.Magnify())
		out += "Magnify ";
	if (evt.LeftDown())
		out += "LeftDown ";
	if (evt.LeftUp())
		out += "LeftUp ";
	if (evt.LeftDClick())
		out += "LeftDClick ";
	if (evt.MiddleDown())
		out += "MiddleDown ";
	if (evt.MiddleUp())
		out += "MiddleUp ";
	if (evt.MiddleDClick())
		out += "MiddleDClick ";
	if (evt.RightDown())
		out += "RightDown ";
	if (evt.RightUp())
		out += "RightUp ";
	if (evt.RightDClick())
		out += "RightDClick ";

	sprintf(buf, "(%d, %d)", evt.GetX(), evt.GetY());
	out += buf;
	return out;
}
#endif /* SLIC3R_DEBUG_MOUSE_EVENTS */

void GLCanvas3D::on_mouse(wxMouseEvent& evt)
{
    if (!m_initialized || !_set_current())
        return;

#if ENABLE_RETINA_GL
    const float scale = m_retina_helper->get_scale_factor();
    evt.SetX(evt.GetX() * scale);
    evt.SetY(evt.GetY() * scale);
#endif

    Point pos(evt.GetX(), evt.GetY());

    static wxTimer s_virtual_bed_timer;
    s_virtual_bed_timer.Bind(wxEVT_TIMER, [this](wxTimerEvent&) { s_multiple_beds.request_next_bed(true); schedule_extra_frame(100); });

    ImGuiWrapper* imgui = wxGetApp().imgui();
    if (m_tooltip.is_in_imgui() && evt.LeftUp())
        // ignore left up events coming from imgui windows and not processed by them
        m_mouse.ignore_left_up = true;
    m_tooltip.set_in_imgui(false);
    if (imgui->update_mouse_data(evt)) {
        m_mouse.position = evt.Leaving() ? Vec2d(-1.0, -1.0) : pos.cast<double>();
        m_tooltip.set_in_imgui(true);
        render();
#ifdef SLIC3R_DEBUG_MOUSE_EVENTS
        printf((format_mouse_event_debug_message(evt) + " - Consumed by ImGUI\n").c_str());
#endif /* SLIC3R_DEBUG_MOUSE_EVENTS */
        m_dirty = true;
        // do not return if dragging or tooltip not empty to allow for tooltip update
        // also, do not return if the mouse is moving and also is inside MM gizmo to allow update seed fill selection
        if (!m_mouse.dragging && m_tooltip.is_empty() && (m_gizmos.get_current_type() != GLGizmosManager::MmSegmentation || !evt.Moving()))
            return;
    }

#ifdef __WXMSW__
	bool on_enter_workaround = false;
    if (! evt.Entering() && ! evt.Leaving() && m_mouse.position.x() == -1.0) {
        // Workaround for SPE-832: There seems to be a mouse event sent to the window before evt.Entering()
        m_mouse.position = pos.cast<double>();
        render();
#ifdef SLIC3R_DEBUG_MOUSE_EVENTS
		printf((format_mouse_event_debug_message(evt) + " - OnEnter workaround\n").c_str());
#endif /* SLIC3R_DEBUG_MOUSE_EVENTS */
		on_enter_workaround = true;
    } else 
#endif /* __WXMSW__ */
    {
#ifdef SLIC3R_DEBUG_MOUSE_EVENTS
		printf((format_mouse_event_debug_message(evt) + " - other\n").c_str());
#endif /* SLIC3R_DEBUG_MOUSE_EVENTS */
	}

    if (m_main_toolbar.on_mouse(evt, *this)) {
        if (evt.LeftUp() || evt.MiddleUp() || evt.RightUp())
            mouse_up_cleanup();
        m_mouse.set_start_position_3D_as_invalid();
        return;
    }

    if (m_undoredo_toolbar.on_mouse(evt, *this)) {
        if (evt.LeftUp() || evt.MiddleUp() || evt.RightUp())
            mouse_up_cleanup();
        m_mouse.set_start_position_3D_as_invalid();
        return;
    }

    if (wxGetApp().plater()->get_collapse_toolbar().on_mouse(evt, *this)) {
        if (evt.LeftUp() || evt.MiddleUp() || evt.RightUp())
            mouse_up_cleanup();
        m_mouse.set_start_position_3D_as_invalid();
        return;
    }

    if (wxGetApp().plater()->get_view_toolbar().on_mouse(evt, *this)) {
        if (evt.LeftUp() || evt.MiddleUp() || evt.RightUp())
            mouse_up_cleanup();
        m_mouse.set_start_position_3D_as_invalid();
        return;
    }

    for (GLVolume* volume : m_volumes.volumes) {
        volume->force_sinking_contours = false;
    }

    auto show_sinking_contours = [this]() {
        const Selection::IndicesList& idxs = m_selection.get_volume_idxs();
        for (unsigned int idx : idxs) {
            m_volumes.volumes[idx]->force_sinking_contours = true;
        }
        m_dirty = true;
    };

    if (m_gizmos.on_mouse(evt)) {
        if (wxWindow::FindFocus() != m_canvas)
            // Grab keyboard focus for input in gizmo dialogs.
            m_canvas->SetFocus();

        if (evt.LeftUp() || evt.MiddleUp() || evt.RightUp())
            mouse_up_cleanup();

        m_mouse.set_start_position_3D_as_invalid();
        m_mouse.position = pos.cast<double>();

        // It should be detection of volume change
        // Not only detection of some modifiers !!!
        if (evt.Dragging()) {
            GLGizmosManager::EType c = m_gizmos.get_current_type();
            if (c == GLGizmosManager::EType::Move ||
                c == GLGizmosManager::EType::Scale ||
                c == GLGizmosManager::EType::Rotate) {
                show_sinking_contours();
                if (_is_sequential_print_enabled())
                    update_sequential_clearance(true);
            }
        }
        else if (evt.LeftUp() &&
            m_gizmos.get_current_type() == GLGizmosManager::EType::Scale &&
            m_gizmos.get_current()->get_state() == GLGizmoBase::EState::On) {
            // Update selection from object list to check selection of the cut objects
            // It's not allowed to scale separate ct parts
            wxGetApp().obj_list()->selection_changed();
        }

        return;
    }

    bool any_gizmo_active = m_gizmos.get_current() != nullptr;

    int selected_object_idx = m_selection.get_object_idx();
    int layer_editing_object_idx = is_layers_editing_enabled() ? selected_object_idx : -1;
    m_layers_editing.select_object(*m_model, layer_editing_object_idx);

    if (m_mouse.drag.move_requires_threshold && m_mouse.is_move_start_threshold_position_2D_defined() && m_mouse.is_move_threshold_met(pos)) {
        m_mouse.drag.move_requires_threshold = false;
        m_mouse.set_move_start_threshold_position_2D_as_invalid();
    }

    if (evt.ButtonDown() && wxWindow::FindFocus() != m_canvas)
        // Grab keyboard focus on any mouse click event.
        m_canvas->SetFocus();

    if (evt.Entering()) {
        if (m_mouse.dragging && !evt.LeftIsDown() && !evt.RightIsDown() && !evt.MiddleIsDown()) {
            // ensure to stop layers editing if enabled
            if (m_layers_editing.state != LayersEditing::Unknown) {
                m_layers_editing.state = LayersEditing::Unknown;
                _stop_timer();
                m_layers_editing.accept_changes(*this);
            }
            mouse_up_cleanup();
        }

//#if defined(__WXMSW__) || defined(__linux__)
//        // On Windows and Linux needs focus in order to catch key events
        if (m_canvas != nullptr) {

            // Set focus in order to remove it from sidebar but not from TextControl (in ManipulationPanel f.e.)
            if (!m_canvas->HasFocus()) {
                wxTopLevelWindow* tlw = find_toplevel_parent(m_canvas);
                // Only set focus, if the top level window of this canvas is active
                if (tlw->IsActive()) {
                    auto* text_ctrl = dynamic_cast<wxTextCtrl*>(tlw->FindFocus());
                    if (text_ctrl == nullptr)
                        m_canvas->SetFocus();
                }
            }

            m_mouse.position = pos.cast<double>();
            m_tooltip_enabled = false;
            // 1) forces a frame render to ensure that m_hover_volume_idxs is updated even when the user right clicks while
            // the context menu is shown, ensuring it to disappear if the mouse is outside any volume and to
            // change the volume hover state if any is under the mouse 
            // 2) when switching between 3d view and preview the size of the canvas changes if the side panels are visible,
            // so forces a resize to avoid multiple renders with different sizes (seen as flickering)
            _refresh_if_shown_on_screen();
            m_tooltip_enabled = true;
        }
        m_mouse.set_start_position_2D_as_invalid();
//#endif
    }
    else if (evt.Leaving()) {
        _deactivate_undo_redo_toolbar_items();

        if (m_layers_editing.state != LayersEditing::Unknown)
            m_layers_editing.state = LayersEditing::Paused;

        // to remove hover on objects when the mouse goes out of this canvas
        m_mouse.position = Vec2d(-1.0, -1.0);
        m_dirty = true;
    }
    else if (evt.LeftDown() || evt.RightDown() || evt.MiddleDown()) {
        if (_deactivate_undo_redo_toolbar_items() || _deactivate_arrange_menu())
            return;

        // If user pressed left or right button we first check whether this happened
        // on a volume or not.
        m_layers_editing.state = LayersEditing::Unknown;
        if (layer_editing_object_idx != -1 && m_layers_editing.bar_rect_contains(*this, pos(0), pos(1))) {
            // A volume is selected and the mouse is inside the layer thickness bar.
            // Start editing the layer height.
            m_layers_editing.state = LayersEditing::Editing;
            _perform_layer_editing_action(&evt);
        }
        else {
            const bool rectangle_selection_dragging = m_rectangle_selection.is_dragging();
            if (evt.LeftDown() && (evt.ShiftDown() || evt.AltDown()) && m_picking_enabled) {
                if (m_gizmos.get_current_type() != GLGizmosManager::SlaSupports &&
                    m_gizmos.get_current_type() != GLGizmosManager::FdmSupports &&
                    m_gizmos.get_current_type() != GLGizmosManager::Seam &&
                    m_gizmos.get_current_type() != GLGizmosManager::Cut &&
                    m_gizmos.get_current_type() != GLGizmosManager::Measure &&
                    m_gizmos.get_current_type() != GLGizmosManager::MmSegmentation &&
                    m_gizmos.get_current_type() != GLGizmosManager::FuzzySkin) {
                    m_rectangle_selection.start_dragging(m_mouse.position, evt.ShiftDown() ? GLSelectionRectangle::EState::Select : GLSelectionRectangle::EState::Deselect);
                    m_dirty = true;
                }
            }

            // Select volume in this 3D canvas.
            // Don't deselect a volume if layer editing is enabled or any gizmo is active. We want the object to stay selected
            // during the scene manipulation.

            if (m_picking_enabled && (!any_gizmo_active || !evt.CmdDown()) && (!m_hover_volume_idxs.empty() || !is_layers_editing_enabled()) && !rectangle_selection_dragging) {
                if (evt.LeftDown() && !m_hover_volume_idxs.empty()) {
                    int volume_idx = get_first_hover_volume_idx();
                    bool already_selected = m_selection.contains_volume(volume_idx);
                    bool shift_down = evt.ShiftDown();

                    Selection::IndicesList curr_idxs = m_selection.get_volume_idxs();

                    if (already_selected && shift_down)
                        m_selection.remove(volume_idx);
                    else {
                        m_selection.add(volume_idx, !shift_down, true);
                        m_mouse.drag.move_requires_threshold = !already_selected;
                        if (already_selected)
                            m_mouse.set_move_start_threshold_position_2D_as_invalid();
                        else
                            m_mouse.drag.move_start_threshold_position_2D = pos;
                    }

                    // propagate event through callback
                    if (curr_idxs != m_selection.get_volume_idxs()) {
                        if (m_selection.is_empty())
                            m_gizmos.reset_all_states();
                        else
                            m_gizmos.refresh_on_off_state();

                        m_gizmos.update_data();
                        post_event(SimpleEvent(EVT_GLCANVAS_OBJECT_SELECT));
                        m_dirty = true;
                    }
                } else if (evt.LeftDown()) {
                    select_bed(s_multiple_beds.get_last_hovered_bed(), true);
                }
            }

            if (!m_hover_volume_idxs.empty() && !m_rectangle_selection.is_dragging()) {
                if (evt.LeftDown() && m_moving_enabled && m_mouse.drag.move_volume_idx == -1) {
                    // Only accept the initial position, if it is inside the volume bounding box.
                    const int volume_idx = get_first_hover_volume_idx();
                    BoundingBoxf3 volume_bbox = m_volumes.volumes[volume_idx]->transformed_bounding_box();
                    volume_bbox.offset(1.0);
                    const bool is_cut_connector_selected = m_selection.is_any_connector();
                    if ((!any_gizmo_active || !evt.CmdDown()) && volume_bbox.contains(m_mouse.scene_position) && !is_cut_connector_selected) {
                        m_volumes.volumes[volume_idx]->hover = GLVolume::HS_None;
                        // The dragging operation is initiated.
                        m_mouse.drag.move_volume_idx = volume_idx;
                        m_selection.setup_cache();
                        if (!evt.CmdDown())
                            m_mouse.drag.start_position_3D = m_mouse.scene_position;
                        m_sequential_print_clearance.m_first_displacement = true;
                        if (_is_sequential_print_enabled())
                            update_sequential_clearance(true);
                        m_sequential_print_clearance.start_dragging();
                    }
                }
            }
        }
    }
    else if (evt.Dragging() && evt.LeftIsDown() && !evt.CmdDown() && m_layers_editing.state == LayersEditing::Unknown &&
             m_mouse.drag.move_volume_idx != -1 && m_mouse.is_start_position_3D_defined()) {
        if (!m_mouse.drag.move_requires_threshold) {
            static bool was_dragging = false;
            was_dragging = m_mouse.dragging;
            m_mouse.dragging = true;
            Vec3d cur_pos = m_mouse.drag.start_position_3D;
            // we do not want to translate objects if the user just clicked on an object while pressing shift to remove it from the selection and then drag
            if (m_selection.contains_volume(get_first_hover_volume_idx())) {
                const Camera& camera = wxGetApp().plater()->get_camera();
                if (std::abs(camera.get_dir_forward().z()) < EPSILON) {
                    // side view -> move selected volumes orthogonally to camera view direction
                    const Linef3 ray = mouse_ray(pos);
                    const Vec3d dir = ray.unit_vector();
                    // finds the intersection of the mouse ray with the plane parallel to the camera viewport and passing throught the starting position
                    // use ray-plane intersection see i.e. https://en.wikipedia.org/wiki/Line%E2%80%93plane_intersection algebric form
                    // in our case plane normal and ray direction are the same (orthogonal view)
                    // when moving to perspective camera the negative z unit axis of the camera needs to be transformed in world space and used as plane normal
                    const Vec3d inters = ray.a + (m_mouse.drag.start_position_3D - ray.a).dot(dir) / dir.squaredNorm() * dir;
                    // vector from the starting position to the found intersection
                    const Vec3d inters_vec = inters - m_mouse.drag.start_position_3D;

                    const Vec3d camera_right = camera.get_dir_right();
                    const Vec3d camera_up = camera.get_dir_up();

                    // finds projection of the vector along the camera axes
                    const double projection_x = inters_vec.dot(camera_right);
                    const double projection_z = inters_vec.dot(camera_up);

                    // apply offset
                    cur_pos = m_mouse.drag.start_position_3D + projection_x * camera_right + projection_z * camera_up;
                }
                else {
                    // Generic view
                    // Get new position at the same Z of the initial click point.
                    cur_pos = mouse_ray(pos).intersect_plane(m_mouse.drag.start_position_3D.z());
                }
            }

            m_moving = true;
            TransformationType trafo_type;
            trafo_type.set_relative();
            m_selection.translate(cur_pos - m_mouse.drag.start_position_3D, trafo_type);
            if (_is_sequential_print_enabled())
                update_sequential_clearance(false);
            wxGetApp().obj_manipul()->set_dirty();
            m_dirty = true;
            
            const Selection::IndicesList& list = m_selection.get_volume_idxs();
            static bool was_outside = true;
            bool is_outside = std::any_of(list.begin(), list.end(), [this](unsigned int i) { return m_volumes.volumes[i]->is_outside; });
            if (is_outside && (! was_dragging || ! was_outside))
                s_virtual_bed_timer.Start(1000, true);
            was_outside = is_outside;
        }
    }
    else if (evt.Dragging() && evt.LeftIsDown() && m_picking_enabled && m_rectangle_selection.is_dragging()) {
        // keeps the mouse position updated while dragging the selection rectangle
        m_mouse.position = pos.cast<double>();
        m_rectangle_selection.dragging(m_mouse.position);
        m_dirty = true;
    }
    else if (evt.Dragging()) {
        m_mouse.dragging = true;

        if (m_layers_editing.state != LayersEditing::Unknown && layer_editing_object_idx != -1) {
            if (m_layers_editing.state == LayersEditing::Editing) {
                _perform_layer_editing_action(&evt);
                m_mouse.position = pos.cast<double>();
            }
        }
        // do not process the dragging if the left mouse was set down in another canvas
        else if (evt.LeftIsDown()) {
            // if dragging over blank area with left button, rotate
            if (!m_moving) {
                if ((any_gizmo_active || evt.CmdDown() || m_hover_volume_idxs.empty()) && m_mouse.is_start_position_3D_defined()) {
                    const Vec3d rot = (Vec3d(pos.x(), pos.y(), 0.0) - m_mouse.drag.start_position_3D) * (PI * TRACKBALLSIZE / 180.0);
                    if (wxGetApp().app_config->get_bool("use_free_camera"))
                        // Virtual track ball (similar to the 3DConnexion mouse).
                        wxGetApp().plater()->get_camera().rotate_local_around_target(Vec3d(rot.y(), rot.x(), 0.0));
                    else {
                        // Forces camera right vector to be parallel to XY plane in case it has been misaligned using the 3D mouse free rotation.
                        // It is cheaper to call this function right away instead of testing wxGetApp().plater()->get_mouse3d_controller().connected(),
                        // which checks an atomics (flushes CPU caches).
                        // See GH issue #3816.
                        Camera& camera = wxGetApp().plater()->get_camera();
                        camera.recover_from_free_camera();
                        camera.rotate_on_sphere(rot.x(), rot.y(), current_printer_technology() != ptSLA);
                    }

                    m_dirty = true;
                }
                m_mouse.drag.start_position_3D = Vec3d((double)pos.x(), (double)pos.y(), 0.0);
            }
        }
        else if (evt.MiddleIsDown() || evt.RightIsDown()) {
            Camera& camera = wxGetApp().plater()->get_camera();
            // If dragging over blank area with right/middle button, pan.
            if (m_mouse.is_start_position_2D_defined()) {
                // get point in model space at Z = 0
                const float z = 0.0f;
                const Vec3d cur_pos = _mouse_to_3d(pos, &z, true);
                const Vec3d orig = _mouse_to_3d(m_mouse.drag.start_position_2D, &z, true);
                if (!wxGetApp().app_config->get_bool("use_free_camera"))
                    // Forces camera right vector to be parallel to XY plane in case it has been misaligned using the 3D mouse free rotation.
                    // It is cheaper to call this function right away instead of testing wxGetApp().plater()->get_mouse3d_controller().connected(),
                    // which checks an atomics (flushes CPU caches).
                    // See GH issue #3816.
                    camera.recover_from_free_camera();

                camera.set_target(m_mouse.drag.camera_start_target + orig - cur_pos);
                m_dirty = true;
            }
            else {
                m_mouse.drag.start_position_2D = pos;
                m_mouse.drag.camera_start_target = camera.get_target();
            }
        }
    }
    else if (evt.LeftUp() || evt.MiddleUp() || evt.RightUp()) {
        m_mouse.position = pos.cast<double>();

        if (evt.LeftUp() && m_sequential_print_clearance.is_dragging())
            m_sequential_print_clearance.stop_dragging();
        if (evt.RightUp() && m_mouse.is_start_position_2D_defined()) {
            // forces camera target to be on the plane z = 0
            Camera& camera = wxGetApp().plater()->get_camera();
            if (std::abs(camera.get_dir_forward().dot(Vec3d::UnitZ())) > EPSILON) {
                const Vec3d old_pos = camera.get_position();
                const double old_distance = camera.get_distance();
                const Vec3d old_target = camera.get_target();
                const Linef3 ray(old_pos, old_target);
                const Vec3d new_target = ray.intersect_plane(0.0);
                const BoundingBoxf3 validation_box = camera.get_target_validation_box();
                if (validation_box.contains(new_target)) {
                    const double new_distance = (new_target - old_pos).norm();
                    camera.set_target(new_target);
                    camera.set_distance(new_distance);
                    if (camera.get_type() == Camera::EType::Perspective)
                        camera.set_zoom(camera.get_zoom() * old_distance / new_distance);
                }
            }
        }

        if (m_layers_editing.state != LayersEditing::Unknown) {
            m_layers_editing.state = LayersEditing::Unknown;
            _stop_timer();
            m_layers_editing.accept_changes(*this);
        }
        else if (m_mouse.drag.move_volume_idx != -1 && m_mouse.dragging) {
            s_multiple_beds.request_next_bed(false);
            s_virtual_bed_timer.Stop();
            do_move(L("Move Object"));
            wxGetApp().obj_manipul()->set_dirty();
            // Let the plater know that the dragging finished, so a delayed refresh
            // of the scene with the background processing data should be performed.
            post_event(SimpleEvent(EVT_GLCANVAS_MOUSE_DRAGGING_FINISHED));
        }
        else if (evt.LeftUp() && m_picking_enabled && m_rectangle_selection.is_dragging()) {
            if (evt.ShiftDown() || evt.AltDown())
                _update_selection_from_hover();

            m_rectangle_selection.stop_dragging();
        }
        else if (evt.LeftUp() && !m_mouse.ignore_left_up && !m_mouse.dragging && m_hover_volume_idxs.empty() && !is_layers_editing_enabled()) {
            // deselect and propagate event through callback
            if (!evt.ShiftDown() && (!any_gizmo_active || !evt.CmdDown()) && m_picking_enabled)
                deselect_all();
        }
        else if (evt.RightUp()) {
            // forces a frame render to ensure that m_hover_volume_idxs is updated even when the user right clicks while
            // the context menu is already shown
            render();
            if (!m_hover_volume_idxs.empty()) {
                // if right clicking on volume, propagate event through callback (shows context menu)
                int volume_idx = get_first_hover_volume_idx();
                if (!m_volumes.volumes[volume_idx]->is_wipe_tower() // no context menu for the wipe tower
                    && (m_gizmos.get_current_type() != GLGizmosManager::SlaSupports && m_gizmos.get_current_type() != GLGizmosManager::Measure))  // disable context menu when the gizmo is open
                {
                    // forces the selection of the volume
                    /* m_selection.add(volume_idx); // #et_FIXME_if_needed
                     * To avoid extra "Add-Selection" snapshots,
                     * call add() with check_for_already_contained=true
                     * */
                    m_selection.add(volume_idx, true, true); 
                    m_gizmos.refresh_on_off_state();
                    post_event(SimpleEvent(EVT_GLCANVAS_OBJECT_SELECT));
                    m_gizmos.update_data();
                    wxGetApp().obj_manipul()->set_dirty();
                    // forces a frame render to update the view before the context menu is shown
                    render();
                }
            }
            Vec2d logical_pos = pos.cast<double>();
#if ENABLE_RETINA_GL
            const float factor = m_retina_helper->get_scale_factor();
            logical_pos = logical_pos.cwiseQuotient(Vec2d(factor, factor));
#endif // ENABLE_RETINA_GL
            if (!m_mouse.dragging) {
                // do not post the event if the user is panning the scene
                // or if right click was done over the wipe tower
                const bool post_right_click_event = (m_hover_volume_idxs.empty() || !m_volumes.volumes[get_first_hover_volume_idx()]->is_wipe_tower()) &&
                    m_gizmos.get_current_type() != GLGizmosManager::Measure;
                if (post_right_click_event)
                    post_event(RBtnEvent(EVT_GLCANVAS_RIGHT_CLICK, { logical_pos, m_hover_volume_idxs.empty() }));
            }
        }

        mouse_up_cleanup();
    }
    else if (evt.Moving()) {
        m_mouse.position = pos.cast<double>();

        // updates gizmos overlay
        if (m_selection.is_empty())
            m_gizmos.reset_all_states();

        m_dirty = true;
    }
    else
        evt.Skip();

    // Detection of doubleclick on text to open emboss edit window
    auto type = m_gizmos.get_current_type();
    if (evt.LeftDClick() && !m_hover_volume_idxs.empty() && 
        (type == GLGizmosManager::EType::Undefined ||
         type == GLGizmosManager::EType::Move ||
         type == GLGizmosManager::EType::Rotate ||
         type == GLGizmosManager::EType::Scale ||
         type == GLGizmosManager::EType::Emboss||
         type == GLGizmosManager::EType::Svg) ) {
        for (int hover_volume_id : m_hover_volume_idxs) { 
            const GLVolume &hover_gl_volume = *m_volumes.volumes[hover_volume_id];
            int object_idx = hover_gl_volume.object_idx();
            if (object_idx < 0 || static_cast<size_t>(object_idx) >= m_model->objects.size()) continue;
            const ModelObject* hover_object = m_model->objects[object_idx];
            int hover_volume_idx = hover_gl_volume.volume_idx();
            if (hover_volume_idx < 0 || static_cast<size_t>(hover_volume_idx) >= hover_object->volumes.size()) continue;
            const ModelVolume* hover_volume = hover_object->volumes[hover_volume_idx];

            if (hover_volume->text_configuration.has_value()) {
                m_selection.add_volumes(Selection::EMode::Volume, {(unsigned) hover_volume_id});
                if (type != GLGizmosManager::EType::Emboss)
                    m_gizmos.open_gizmo(GLGizmosManager::EType::Emboss);            
                wxGetApp().obj_list()->update_selections();
                return;
            } else if (hover_volume->emboss_shape.has_value()) {
                m_selection.add_volumes(Selection::EMode::Volume, {(unsigned) hover_volume_id});
                if (type != GLGizmosManager::EType::Svg)
                    m_gizmos.open_gizmo(GLGizmosManager::EType::Svg);
                wxGetApp().obj_list()->update_selections();
                return;
            }
        }
    }

    if (m_moving)
        show_sinking_contours();

#ifdef __WXMSW__
	if (on_enter_workaround)
		m_mouse.position = Vec2d(-1., -1.);
#endif /* __WXMSW__ */
}

void GLCanvas3D::on_paint(wxPaintEvent& evt)
{
    if (m_initialized)
        m_dirty = true;
    else
        // Call render directly, so it gets initialized immediately, not from On Idle handler.
        this->render();
}

void GLCanvas3D::on_set_focus(wxFocusEvent& evt)
{
    m_tooltip_enabled = false;
    _refresh_if_shown_on_screen();
    m_tooltip_enabled = true;
}

Size GLCanvas3D::get_canvas_size() const
{
    int w = 0;
    int h = 0;

    if (m_canvas != nullptr)
        m_canvas->GetSize(&w, &h);

#if ENABLE_RETINA_GL
    const float factor = m_retina_helper->get_scale_factor();
    w *= factor;
    h *= factor;
#else
    const float factor = 1.0f;
#endif

    return Size(w, h, factor);
}

Vec2d GLCanvas3D::get_local_mouse_position() const
{
    if (m_canvas == nullptr)
		return Vec2d::Zero();

    wxPoint mouse_pos = m_canvas->ScreenToClient(wxGetMousePosition());
    const double factor = 
#if ENABLE_RETINA_GL
        m_retina_helper->get_scale_factor();
#else
        1.0;
#endif
    return Vec2d(factor * mouse_pos.x, factor * mouse_pos.y);
}

void GLCanvas3D::set_tooltip(const std::string& tooltip)
{
    if (m_canvas != nullptr)
        m_tooltip.set_text(tooltip);
}

void GLCanvas3D::do_move(const std::string& snapshot_type)
{
    if (m_model == nullptr)
        return;

    if (!snapshot_type.empty())
        wxGetApp().plater()->take_snapshot(_(snapshot_type));

    std::set<std::pair<int, int>> done;  // keeps track of modified instances
    bool object_moved = false;
    std::vector<Vec3d> wipe_tower_origin = std::vector<Vec3d>(s_multiple_beds.get_max_beds(), Vec3d::Zero());

    Selection::EMode selection_mode = m_selection.get_mode();
    int vol_id = -1;

    for (const GLVolume* v : m_volumes.volumes) {
        ++vol_id;
        int object_idx = v->object_idx();
        int instance_idx = v->instance_idx();
        int volume_idx = v->volume_idx();

        if (volume_idx < 0)
            continue;

        std::pair<int, int> done_id(object_idx, instance_idx);

        if (0 <= object_idx && object_idx < (int)m_model->objects.size()) {
            done.insert(done_id);

            // Move instances/volumes
            ModelObject* model_object = m_model->objects[object_idx];
            if (model_object != nullptr) {
                if (selection_mode == Selection::Instance)
                    model_object->instances[instance_idx]->set_transformation(v->get_instance_transformation());
                else if (selection_mode == Selection::Volume)
                    model_object->volumes[volume_idx]->set_transformation(v->get_volume_transformation());

                object_moved = true;
                model_object->invalidate_bounding_box();
            }
        }
        else if (m_selection.is_wipe_tower() && v->is_wipe_tower() && m_selection.contains_volume(vol_id)) {
            // Move a wipe tower proxy.
            for (size_t bed_idx = 0; bed_idx < s_multiple_beds.get_max_beds(); ++bed_idx) {
                if (v->geometry_id.second == wipe_tower_instance_id(bed_idx).id) {
                    wipe_tower_origin[bed_idx] = v->get_volume_offset();
                    break;
                }
            }
        }
    }

    // Fixes flying instances
    std::set<int> obj_idx_for_update_info_items;
    for (const std::pair<int, int>& i : done) {
        ModelObject* m = m_model->objects[i.first];
        const double shift_z = m->get_instance_min_z(i.second);
        if (current_printer_technology() == ptSLA || shift_z > SINKING_Z_THRESHOLD) {
            const Vec3d shift(0.0, 0.0, -shift_z);
            m_selection.translate(i.first, i.second, shift);
            m->translate_instance(i.second, shift);
        }
        obj_idx_for_update_info_items.emplace(i.first);
    }
    //update sinking information in ObjectList
    for (int id : obj_idx_for_update_info_items)
        wxGetApp().obj_list()->update_info_items(static_cast<size_t>(id));

    // if the selection is not valid to allow for layer editing after the move, we need to turn off the tool if it is running
    // similar to void Plater::priv::selection_changed()
    if (!wxGetApp().plater()->can_layers_editing() && is_layers_editing_enabled())
        post_event(SimpleEvent(EVT_GLTOOLBAR_LAYERSEDITING));

    if (object_moved)
        post_event(SimpleEvent(EVT_GLCANVAS_INSTANCE_MOVED));

    if (auto it = std::find_if(wipe_tower_origin.begin(), wipe_tower_origin.end(), [](const Vec3d& pos) { return pos != Vec3d::Zero(); }); it != wipe_tower_origin.end()) {
        size_t bed_idx = it - wipe_tower_origin.begin();
        m_model->get_wipe_tower_vector()[bed_idx].position = Vec2d((*it)[0] - s_multiple_beds.get_bed_translation(bed_idx).x(), (*it)[1] - s_multiple_beds.get_bed_translation(bed_idx).y());
        post_event(SimpleEvent(EVT_GLCANVAS_WIPETOWER_TOUCHED));
    }

    if (_is_sequential_print_enabled()) {
        update_sequential_clearance(true);
        m_sequential_print_clearance.m_evaluating = true;
    }

    m_dirty = true;
}

void GLCanvas3D::do_rotate(const std::string& snapshot_type)
{
    if (m_model == nullptr)
        return;

    if (!snapshot_type.empty())
        wxGetApp().plater()->take_snapshot(_(snapshot_type));

    // stores current min_z of instances
    std::map<std::pair<int, int>, double> min_zs;
    for (int i = 0; i < static_cast<int>(m_model->objects.size()); ++i) {
        const ModelObject* obj = m_model->objects[i];
        for (int j = 0; j < static_cast<int>(obj->instances.size()); ++j) {
            if (snapshot_type == L("Gizmo-Place on Face") && m_selection.get_object_idx() == i) {
                // This means we are flattening this object. In that case pretend
                // that it is not sinking (even if it is), so it is placed on bed
                // later on (whatever is sinking will be left sinking).
                min_zs[{ i, j }] = SINKING_Z_THRESHOLD;
            }
            else
                min_zs[{ i, j }] = obj->instance_bounding_box(j).min.z();

        }
    }

    std::set<std::pair<int, int>> done;  // keeps track of modified instances

    Selection::EMode selection_mode = m_selection.get_mode();
    int v_id = -1;

    for (const GLVolume* v : m_volumes.volumes) {
        ++v_id;
        if (v->is_wipe_tower()) {
            if (m_selection.contains_volume(v_id)) {
                for (size_t bed_idx = 0; bed_idx < s_multiple_beds.get_max_beds(); ++bed_idx) {
                    if (v->geometry_id.second == wipe_tower_instance_id(bed_idx).id) {
                        const Vec3d offset = v->get_volume_offset() - s_multiple_beds.get_bed_translation(bed_idx);
                        Vec3d rot_unit_x = v->get_volume_transformation().get_matrix().linear() * Vec3d::UnitX();
                        double z_rot = std::atan2(rot_unit_x.y(), rot_unit_x.x());
                        m_model->get_wipe_tower_vector()[bed_idx].position = Vec2d(offset.x(), offset.y());
                        m_model->get_wipe_tower_vector()[bed_idx].rotation = (180. / M_PI) * z_rot;
                        break;
                    }
                }
            }
        }
        const int object_idx = v->object_idx();
        if (object_idx < 0 || (int)m_model->objects.size() <= object_idx)
            continue;

        const int instance_idx = v->instance_idx();
        const int volume_idx = v->volume_idx();

        if (volume_idx < 0)
            continue;

        done.insert(std::pair<int, int>(object_idx, instance_idx));

        // Rotate instances/volumes.
        ModelObject* model_object = m_model->objects[object_idx];
        if (model_object != nullptr) {
            if (selection_mode == Selection::Instance)
                model_object->instances[instance_idx]->set_transformation(v->get_instance_transformation());
            else if (selection_mode == Selection::Volume)
                model_object->volumes[volume_idx]->set_transformation(v->get_volume_transformation());
            model_object->invalidate_bounding_box();
        }
    }

    // Fixes sinking/flying instances
    std::set<int> obj_idx_for_update_info_items;
    for (const std::pair<int, int>& i : done) {
        ModelObject* m = m_model->objects[i.first];
        const double shift_z = m->get_instance_min_z(i.second);
        // leave sinking instances as sinking
        if (min_zs.find({ i.first, i.second })->second >= SINKING_Z_THRESHOLD || shift_z > SINKING_Z_THRESHOLD) {
            const Vec3d shift(0.0, 0.0, -shift_z);
            m_selection.translate(i.first, i.second, shift);
            m->translate_instance(i.second, shift);
        }

        obj_idx_for_update_info_items.emplace(i.first);
    }
    //update sinking information in ObjectList
    for (int id : obj_idx_for_update_info_items)
        wxGetApp().obj_list()->update_info_items(static_cast<size_t>(id));

    if (!done.empty())
        post_event(SimpleEvent(EVT_GLCANVAS_INSTANCE_ROTATED));

    if (_is_sequential_print_enabled()) {
        update_sequential_clearance(true);
        m_sequential_print_clearance.m_evaluating = true;
    }

    m_dirty = true;
}

void GLCanvas3D::do_scale(const std::string& snapshot_type)
{
    if (m_model == nullptr)
        return;

    if (!snapshot_type.empty())
        wxGetApp().plater()->take_snapshot(_(snapshot_type));

    // stores current min_z of instances
    std::map<std::pair<int, int>, double> min_zs;
    if (!snapshot_type.empty()) {
        for (int i = 0; i < static_cast<int>(m_model->objects.size()); ++i) {
            const ModelObject* obj = m_model->objects[i];
            for (int j = 0; j < static_cast<int>(obj->instances.size()); ++j) {
                min_zs[{ i, j }] = obj->instance_bounding_box(j).min.z();
            }
        }
    }

    std::set<std::pair<int, int>> done;  // keeps track of modified instances

    Selection::EMode selection_mode = m_selection.get_mode();

    for (const GLVolume* v : m_volumes.volumes) {
        const int object_idx = v->object_idx();
        if (object_idx < 0 || (int)m_model->objects.size() <= object_idx)
            continue;

        const int instance_idx = v->instance_idx();
        const int volume_idx = v->volume_idx();

        if (volume_idx < 0)
            continue;

        done.insert(std::pair<int, int>(object_idx, instance_idx));

        // Rotate instances/volumes
        ModelObject* model_object = m_model->objects[object_idx];
        if (model_object != nullptr) {
            if (selection_mode == Selection::Instance)
                model_object->instances[instance_idx]->set_transformation(v->get_instance_transformation());
            else if (selection_mode == Selection::Volume) {
                model_object->instances[instance_idx]->set_transformation(v->get_instance_transformation());
                model_object->volumes[volume_idx]->set_transformation(v->get_volume_transformation());
            }
            model_object->invalidate_bounding_box();
        }
    }

    // Fixes sinking/flying instances
    std::set<int> obj_idx_for_update_info_items;
    for (const std::pair<int, int>& i : done) {
        ModelObject* m = m_model->objects[i.first];
        const double shift_z = m->get_instance_min_z(i.second);
        // leave sinking instances as sinking
        if (min_zs.empty() || min_zs.find({ i.first, i.second })->second >= SINKING_Z_THRESHOLD || shift_z > SINKING_Z_THRESHOLD) {
            const Vec3d shift(0.0, 0.0, -shift_z);
            m_selection.translate(i.first, i.second, shift);
            m->translate_instance(i.second, shift);
        }
        obj_idx_for_update_info_items.emplace(i.first);
    }
    //update sinking information in ObjectList
    for (int id : obj_idx_for_update_info_items)
        wxGetApp().obj_list()->update_info_items(static_cast<size_t>(id));

    if (!done.empty())
        post_event(SimpleEvent(EVT_GLCANVAS_INSTANCE_SCALED));

    if (_is_sequential_print_enabled()) {
        update_sequential_clearance(true);
        m_sequential_print_clearance.m_evaluating = true;
    }

    m_dirty = true;
}

void GLCanvas3D::do_mirror(const std::string& snapshot_type)
{
    if (m_model == nullptr)
        return;

    if (!snapshot_type.empty())
        wxGetApp().plater()->take_snapshot(_(snapshot_type));

    // stores current min_z of instances
    std::map<std::pair<int, int>, double> min_zs;
    if (!snapshot_type.empty()) {
        for (int i = 0; i < static_cast<int>(m_model->objects.size()); ++i) {
            const ModelObject* obj = m_model->objects[i];
            for (int j = 0; j < static_cast<int>(obj->instances.size()); ++j) {
                min_zs[{ i, j }] = obj->instance_bounding_box(j).min.z();
            }
        }
    }

    std::set<std::pair<int, int>> done;  // keeps track of modified instances

    Selection::EMode selection_mode = m_selection.get_mode();

    for (const GLVolume* v : m_volumes.volumes) {
        int object_idx = v->object_idx();
        if (object_idx < 0 || (int)m_model->objects.size() <= object_idx)
            continue;

        int instance_idx = v->instance_idx();
        int volume_idx = v->volume_idx();

        done.insert(std::pair<int, int>(object_idx, instance_idx));

        // Mirror instances/volumes
        ModelObject* model_object = m_model->objects[object_idx];
        if (model_object != nullptr) {
            if (selection_mode == Selection::Instance)
                model_object->instances[instance_idx]->set_transformation(v->get_instance_transformation());
            else if (selection_mode == Selection::Volume)
                model_object->volumes[volume_idx]->set_transformation(v->get_volume_transformation());
            model_object->invalidate_bounding_box();
        }
    }

    // Fixes sinking/flying instances
    std::set<int> obj_idx_for_update_info_items;
    for (const std::pair<int, int>& i : done) {
        ModelObject* m = m_model->objects[i.first];
        double shift_z = m->get_instance_min_z(i.second);
        // leave sinking instances as sinking
        if (min_zs.empty() || min_zs.find({ i.first, i.second })->second >= SINKING_Z_THRESHOLD || shift_z > SINKING_Z_THRESHOLD) {
            Vec3d shift(0.0, 0.0, -shift_z);
            m_selection.translate(i.first, i.second, shift);
            m->translate_instance(i.second, shift);
        }
        obj_idx_for_update_info_items.emplace(i.first);
    }
    //update sinking information in ObjectList
    for (int id : obj_idx_for_update_info_items)
        wxGetApp().obj_list()->update_info_items(static_cast<size_t>(id));

    post_event(SimpleEvent(EVT_GLCANVAS_INSTANCE_MIRRORED));

    m_dirty = true;
}

void GLCanvas3D::do_reset_skew(const std::string& snapshot_type)
{
    if (m_model == nullptr)
        return;

    if (!snapshot_type.empty())
        wxGetApp().plater()->take_snapshot(_(snapshot_type));

    // stores current min_z of instances
    std::map<std::pair<int, int>, double> min_zs;
    if (!snapshot_type.empty()) {
        for (int i = 0; i < static_cast<int>(m_model->objects.size()); ++i) {
            const ModelObject* obj = m_model->objects[i];
            for (int j = 0; j < static_cast<int>(obj->instances.size()); ++j) {
                min_zs[{ i, j }] = obj->instance_bounding_box(j).min.z();
            }
        }
    }

    std::set<std::pair<int, int>> done;  // keeps track of modified instances

    Selection::EMode selection_mode = m_selection.get_mode();

    for (const GLVolume* v : m_volumes.volumes) {
        int object_idx = v->object_idx();
        if (object_idx < 0 || (int)m_model->objects.size() <= object_idx)
            continue;

        int instance_idx = v->instance_idx();
        int volume_idx = v->volume_idx();

        done.insert(std::pair<int, int>(object_idx, instance_idx));

        // Mirror instances/volumes
        ModelObject* model_object = m_model->objects[object_idx];
        if (model_object != nullptr) {
            if (selection_mode == Selection::Instance)
                model_object->instances[instance_idx]->set_transformation(v->get_instance_transformation());
            else if (selection_mode == Selection::Volume)
                model_object->volumes[volume_idx]->set_transformation(v->get_volume_transformation());
            model_object->invalidate_bounding_box();
        }
    }

    // Fixes sinking/flying instances
    std::set<int> obj_idx_for_update_info_items;
    for (const std::pair<int, int>& i : done) {
        ModelObject* m = m_model->objects[i.first];
        double shift_z = m->get_instance_min_z(i.second);
        // leave sinking instances as sinking
        if (min_zs.empty() || min_zs.find({ i.first, i.second })->second >= SINKING_Z_THRESHOLD || shift_z > SINKING_Z_THRESHOLD) {
            Vec3d shift(0.0, 0.0, -shift_z);
            m_selection.translate(i.first, i.second, shift);
            m->translate_instance(i.second, shift);
        }
        obj_idx_for_update_info_items.emplace(i.first);
    }
    //update sinking information in ObjectList
    for (int id : obj_idx_for_update_info_items)
        wxGetApp().obj_list()->update_info_items(static_cast<size_t>(id));

    post_event(SimpleEvent(EVT_GLCANVAS_RESET_SKEW));

    m_dirty = true;
}

void GLCanvas3D::update_gizmos_on_off_state()
{
    set_as_dirty();
    m_gizmos.update_data();
    m_gizmos.refresh_on_off_state();
}

void GLCanvas3D::handle_sidebar_focus_event(const std::string& opt_key, bool focus_on)
{
    m_sidebar_field = focus_on ? opt_key : "";
    if (!m_sidebar_field.empty())
        m_gizmos.reset_all_states();

    m_dirty = true;
}

void GLCanvas3D::handle_layers_data_focus_event(const t_layer_height_range range, const EditorType type)
{
    std::string field = "layer_" + std::to_string(type) + "_" + float_to_string_decimal_point(range.first) + "_" + float_to_string_decimal_point(range.second);
    handle_sidebar_focus_event(field, true);
}

void GLCanvas3D::update_ui_from_settings()
{
    m_dirty = true;

#if __APPLE__
    // Update OpenGL scaling on OSX after the user toggled the "use_retina_opengl" settings in Preferences dialog.
    const float orig_scaling = m_retina_helper->get_scale_factor();

    const bool use_retina = wxGetApp().app_config->get_bool("use_retina_opengl");
    BOOST_LOG_TRIVIAL(debug) << "GLCanvas3D: Use Retina OpenGL: " << use_retina;
    m_retina_helper->set_use_retina(use_retina);
    const float new_scaling = m_retina_helper->get_scale_factor();

    if (new_scaling != orig_scaling) {
        BOOST_LOG_TRIVIAL(debug) << "GLCanvas3D: Scaling factor: " << new_scaling;

        Camera& camera = wxGetApp().plater()->get_camera();
        camera.set_zoom(camera.get_zoom() * new_scaling / orig_scaling);
        _refresh_if_shown_on_screen();
    }
#endif // ENABLE_RETINA_GL

    if (wxGetApp().is_editor())
        wxGetApp().plater()->enable_collapse_toolbar(wxGetApp().app_config->get_bool("show_collapse_button") || !wxGetApp().sidebar().IsShown());
}

std::vector<GLCanvas3D::WipeTowerInfo> GLCanvas3D::get_wipe_tower_infos() const
{
    std::vector<WipeTowerInfo> result;

    for (size_t bed_idx = 0; bed_idx < s_multiple_beds.get_max_beds(); ++bed_idx) {
        if (m_wipe_tower_bounding_boxes[bed_idx]) {
            const ModelWipeTower &wipe_tower{m_model->wipe_tower(bed_idx)};
            WipeTowerInfo wti;
            wti.m_pos = Vec2d(wipe_tower.position.x(), wipe_tower.position.y());
            wti.m_rotation = (M_PI/180.) * wipe_tower.rotation;
            wti.m_bb = *m_wipe_tower_bounding_boxes[bed_idx];
            wti.m_bed_index = bed_idx;
            result.push_back(std::move(wti));
        }
    }

    return result;
}

Linef3 GLCanvas3D::mouse_ray(const Point& mouse_pos)
{
    float z0 = 0.0f;
    float z1 = 1.0f;
    return Linef3(_mouse_to_3d(mouse_pos, &z0), _mouse_to_3d(mouse_pos, &z1));
}

double GLCanvas3D::get_size_proportional_to_max_bed_size(double factor) const
{
    const BoundingBoxf& bbox = m_bed.build_volume().bounding_volume2d();
    return factor * std::max(bbox.size()[0], bbox.size()[1]);
}

void GLCanvas3D::set_cursor(ECursorType type)
{
    if ((m_canvas != nullptr) && (m_cursor_type != type))
    {
        switch (type)
        {
        case Standard: { m_canvas->SetCursor(*wxSTANDARD_CURSOR); break; }
        case Cross: { m_canvas->SetCursor(*wxCROSS_CURSOR); break; }
        }

        m_cursor_type = type;
    }
}

void GLCanvas3D::update_tooltip_for_settings_item_in_main_toolbar()
{
    std::string new_tooltip = _u8L("Switch to Settings") + 
                             "\n" + "[" + GUI::shortkey_ctrl_prefix() + "2] - " + _u8L("Print Settings Tab")    + 
                             "\n" + "[" + GUI::shortkey_ctrl_prefix() + "3] - " + (current_printer_technology() == ptFFF ? _u8L("Filament Settings Tab") : _u8L("Material Settings Tab")) +
                             "\n" + "[" + GUI::shortkey_ctrl_prefix() + "4] - " + _u8L("Printer Settings Tab") ;

    m_main_toolbar.set_tooltip(get_main_toolbar_item_id("settings"), new_tooltip);
}

void GLCanvas3D::mouse_up_cleanup()
{
    m_moving = false;
    m_mouse.drag.move_volume_idx = -1;
    m_mouse.set_start_position_3D_as_invalid();
    m_mouse.set_start_position_2D_as_invalid();
    m_mouse.dragging = false;
    m_mouse.ignore_left_up = false;
    m_dirty = true;

    if (m_canvas->HasCapture())
        m_canvas->ReleaseMouse();
}

void GLCanvas3D::update_sequential_clearance(bool force_contours_generation)
{
    if (!_is_sequential_print_enabled())
        return;

    if (m_layers_editing.is_enabled())
        return;

    auto instance_transform_from_volumes = [this](int object_idx, int instance_idx) {
        for (const GLVolume* v : m_volumes.volumes) {
            if (v->object_idx() == object_idx && v->instance_idx() == instance_idx)
                return v->get_instance_transformation();
        }
        assert(false);
        return Geometry::Transformation();
    };

    auto is_object_outside_printbed = [this](int object_idx) {
        for (const GLVolume* v : m_volumes.volumes) {
            if (v->object_idx() == object_idx && v->is_outside)
                return true;
        }
        return false;
    };

    // collects instance transformations from volumes
    // first: define temporary cache
    unsigned int instances_count = 0;
    std::vector<std::vector<std::optional<Geometry::Transformation>>> instance_transforms;
    for (size_t obj = 0; obj < m_model->objects.size(); ++obj) {
        instance_transforms.emplace_back(std::vector<std::optional<Geometry::Transformation>>());
        const ModelObject* model_object = m_model->objects[obj];
        for (size_t i = 0; i < model_object->instances.size(); ++i) {
            instance_transforms[obj].emplace_back(std::optional<Geometry::Transformation>());
            ++instances_count;
        }
    }

    if (instances_count == 1)
        return;

    // second: fill temporary cache with data from volumes
    for (const GLVolume* v : m_volumes.volumes) {
        if (v->is_wipe_tower())
            continue;

        const int object_idx = v->object_idx();
        const int instance_idx = v->instance_idx();
        auto& transform = instance_transforms[object_idx][instance_idx];
        if (!transform.has_value())
            transform = instance_transform_from_volumes(object_idx, instance_idx);
    }

    // helper function to calculate the transformation to be applied to the sequential print clearance contours
    auto instance_trafo = [](const Transform3d& hull_trafo, const Geometry::Transformation& inst_trafo) {
        Vec3d offset = inst_trafo.get_offset() - hull_trafo.translation();
        offset.z() = 0.0;
        return Geometry::translation_transform(offset) *
            Geometry::rotation_transform(Geometry::rotation_diff_z(hull_trafo, inst_trafo.get_matrix()) * Vec3d::UnitZ());
    };

    // calculates objects 2d hulls (see also: Print::sequential_print_horizontal_clearance_valid())
    // this is done only the first time this method is called while moving the mouse,
    // the results are then cached for following displacements
    if (force_contours_generation || m_sequential_print_clearance.m_first_displacement) {
        m_sequential_print_clearance.m_evaluating = false;
        m_sequential_print_clearance.m_hulls_2d_cache.clear();
        const float shrink_factor = static_cast<float>(scale_(0.5 * fff_print()->config().extruder_clearance_radius.value - EPSILON));
        const double mitter_limit = scale_(0.1);
        m_sequential_print_clearance.m_hulls_2d_cache.reserve(m_model->objects.size());
        for (size_t i = 0; i < m_model->objects.size(); ++i) {
            ModelObject* model_object = m_model->objects[i];
            Geometry::Transformation trafo = instance_transform_from_volumes((int)i, 0);
            trafo.set_offset({ 0.0, 0.0, trafo.get_offset().z() });
            Pointf3s& new_hull_2d = m_sequential_print_clearance.m_hulls_2d_cache.emplace_back(std::make_pair(Pointf3s(), trafo.get_matrix())).first;
            if (is_object_outside_printbed((int)i))
                continue;

            Polygon hull_2d = model_object->convex_hull_2d(trafo.get_matrix());
            if (!hull_2d.empty()) {
                // Shrink the extruder_clearance_radius a tiny bit, so that if the object arrangement algorithm placed the objects
                // exactly by satisfying the extruder_clearance_radius, this test will not trigger collision.
                const Polygons offset_res = offset(hull_2d, shrink_factor, jtRound, mitter_limit);
                if (!offset_res.empty())
                    hull_2d = offset_res.front();
            }

            new_hull_2d.reserve(hull_2d.points.size());
            for (const Point& p : hull_2d.points) {
                new_hull_2d.emplace_back(Vec3d(unscale<double>(p.x()), unscale<double>(p.y()), 0.0));
            }
        }

        ContoursList contours;
        contours.contours.reserve(instance_transforms.size());
        contours.trafos = std::vector<std::pair<size_t, Transform3d>>();
        (*contours.trafos).reserve(instances_count);
        for (size_t i = 0; i < instance_transforms.size(); ++i) {
            const auto& [hull, hull_trafo] = m_sequential_print_clearance.m_hulls_2d_cache[i];
            Points hull_pts;
            hull_pts.reserve(hull.size());
            for (size_t j = 0; j < hull.size(); ++j) {
                hull_pts.emplace_back(scaled<double>(hull[j].x()), scaled<double>(hull[j].y()));
            }
            contours.contours.emplace_back(Geometry::convex_hull(std::move(hull_pts)));

            const auto& instances = instance_transforms[i];
            for (const auto& instance : instances) {
                (*contours.trafos).emplace_back(i, instance_trafo(hull_trafo, *instance));
            }
        }

        set_sequential_print_clearance_contours(contours, false);
        m_sequential_print_clearance.m_first_displacement = false;
    }
    else {
        if (!m_sequential_print_clearance.empty()) {
            std::vector<Transform3d> trafos;
            trafos.reserve(instances_count);
            for (size_t i = 0; i < instance_transforms.size(); ++i) {
                const auto& [hull, hull_trafo] = m_sequential_print_clearance.m_hulls_2d_cache[i];
                const auto& instances = instance_transforms[i];
                for (const auto& instance : instances) {
                    trafos.emplace_back(instance_trafo(hull_trafo, *instance));
                }
            }
            m_sequential_print_clearance.update_instances_trafos(trafos);
        }
    }
}

bool GLCanvas3D::is_object_sinking(int object_idx) const
{
    for (const GLVolume* v : m_volumes.volumes) {
        if (v->object_idx() == object_idx && (v->is_sinking() || (!v->is_modifier && v->is_below_printbed())))
            return true;
    }
    return false;
}

void GLCanvas3D::apply_retina_scale(Vec2d &screen_coordinate) const 
{
#if ENABLE_RETINA_GL
    double scale = static_cast<double>(m_retina_helper->get_scale_factor());
    screen_coordinate *= scale;
#endif // ENABLE_RETINA_GL
}

std::pair<SlicingParameters, const std::vector<double>> GLCanvas3D::get_layers_height_data(int object_id)
{
    m_layers_editing.select_object(*m_model, object_id);
    std::pair<SlicingParameters, const std::vector<double>> ret = m_layers_editing.get_layers_height_data();
    m_layers_editing.select_object(*m_model, -1);
    return ret;
}

void GLCanvas3D::detect_sla_view_type()
{
    m_sla_view.detect_type_from_volumes(m_volumes.volumes);
    m_sla_view.update_volumes_visibility(m_volumes.volumes);
    m_dirty = true;
}

void GLCanvas3D::set_sla_view_type(ESLAViewType type)
{
    m_sla_view.set_type(type);
    m_sla_view.update_volumes_visibility(m_volumes.volumes);
    m_dirty = true;
}

void GLCanvas3D::set_sla_view_type(const GLVolume::CompositeID& id, ESLAViewType type)
{
    m_sla_view.set_type(id, type);
    m_sla_view.update_volumes_visibility(m_volumes.volumes);
    m_dirty = true;
}

bool GLCanvas3D::_is_shown_on_screen() const
{
    return (m_canvas != nullptr) ? m_canvas->IsShownOnScreen() : false;
}

// Getter for the const char*[]
static bool string_getter(const bool is_undo, int idx, const char** out_text)
{
    return wxGetApp().plater()->undo_redo_string_getter(is_undo, idx, out_text);
}

bool GLCanvas3D::_render_undo_redo_stack(const bool is_undo, float pos_x)
{
    bool action_taken = false;

    ImGuiPureWrap::set_next_window_pos(pos_x, m_undoredo_toolbar.get_height(), ImGuiCond_Always, 0.5f, 0.0f);
    std::string title = is_undo ? _u8L("Undo History") : _u8L("Redo History");
    ImGuiPureWrap::begin(title, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    int hovered = m_imgui_undo_redo_hovered_pos;
    int selected = -1;
    float em = static_cast<float>(wxGetApp().em_unit());
#if ENABLE_RETINA_GL
	em *= m_retina_helper->get_scale_factor();
#endif

    if (ImGuiPureWrap::undo_redo_list(ImVec2(18 * em, 26 * em), is_undo, &string_getter, hovered, selected, m_mouse_wheel))
        m_imgui_undo_redo_hovered_pos = hovered;
    else
        m_imgui_undo_redo_hovered_pos = -1;

    if (selected >= 0) {
        is_undo ? wxGetApp().plater()->undo_to(selected) : wxGetApp().plater()->redo_to(selected);
        action_taken = true;
    }

    ImGuiPureWrap::text(GUI::format(is_undo ? _L_PLURAL("Undo %1$d Action", "Undo %1$d Actions", hovered + 1) : _L_PLURAL("Redo %1$d Action", "Redo %1$d Actions", hovered + 1), hovered + 1));

    ImGuiPureWrap::end();

    return action_taken;
}

bool GLCanvas3D::_render_arrange_menu(float pos_x, bool current_bed)
{
    m_arrange_settings_dialog.render(pos_x, m_main_toolbar.get_height(), current_bed);
    return true;
}

void GLCanvas3D::_render_thumbnail_internal(ThumbnailData& thumbnail_data, const ThumbnailsParams& thumbnail_params, const GLVolumeCollection& volumes, Camera::EType camera_type)
{
    auto is_visible = [](const GLVolume& v) {
        bool ret = v.printable;
        ret &= (!v.shader_outside_printer_detection_enabled || !v.is_outside);
        return ret;
    };

    GLVolumePtrs visible_volumes;

    for (GLVolume* vol : volumes.volumes) {
        if (!vol->is_modifier && !vol->is_wipe_tower() && (!thumbnail_params.parts_only || vol->composite_id.volume_id >= 0)) {
            if (!thumbnail_params.printable_only || is_visible(*vol)) {
                if (s_multiple_beds.is_glvolume_on_thumbnail_bed(wxGetApp().model(), vol->composite_id.object_id, vol->composite_id.instance_id))
                    visible_volumes.emplace_back(vol);
            }
        }
    }

    BoundingBoxf3 volumes_box;
    if (!visible_volumes.empty()) {
        for (const GLVolume* vol : visible_volumes) {
            volumes_box.merge(vol->transformed_bounding_box());
        }
    }
    else
        // This happens for empty projects
        volumes_box = m_bed.extended_bounding_box();

    Camera camera;
    camera.set_type(camera_type);
    camera.set_scene_box(scene_bounding_box());
    camera.set_viewport(0, 0, thumbnail_data.width, thumbnail_data.height);
    camera.apply_viewport();
    camera.zoom_to_box(volumes_box);

    const Transform3d& view_matrix = camera.get_view_matrix();

    double near_z = -1.0;
    double far_z = -1.0;

    if (thumbnail_params.show_bed) {
        // extends the near and far z of the frustrum to avoid the bed being clipped

        // box in eye space
        BoundingBoxf3 t_bed_box = m_bed.extended_bounding_box();
        if (s_multiple_beds.get_thumbnail_bed_idx() != -1) {
            BoundingBoxf3 bed_bb = m_bed.build_volume().bounding_volume();
            bed_bb.translate(s_multiple_beds.get_bed_translation(s_multiple_beds.get_thumbnail_bed_idx()));
            t_bed_box.merge(bed_bb);
        }
        t_bed_box = t_bed_box.transformed(view_matrix);
        near_z = -t_bed_box.max.z();
        far_z = -t_bed_box.min.z();
    }

    camera.apply_projection(volumes_box, near_z, far_z);

    const ModelObjectPtrs &model_objects                = GUI::wxGetApp().model().objects;
    std::vector<ColorRGBA> extruders_colors             = GUI::wxGetApp().plater()->get_extruder_colors_from_plater_config();
    const bool             is_enabled_painted_thumbnail = !model_objects.empty() && !extruders_colors.empty();

    if (thumbnail_params.transparent_background)
//        glsafe(::glClearColor(0.0f, 0.0f, 0.0f, 0.0f));
        glsafe(::glClearColor(0.4f, 0.4f, 0.4f, 0.0f));

    glsafe(::glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
    glsafe(::glEnable(GL_DEPTH_TEST));
    glsafe(::glCullFace(GL_BACK));

    const Transform3d& projection_matrix = camera.get_projection_matrix();

    const int extruders_count = wxGetApp().extruders_edited_cnt();
    for (GLVolume *vol : visible_volumes) {
        const int obj_idx = vol->object_idx();
        const int vol_idx = vol->volume_idx();
        const bool render_as_painted = is_enabled_painted_thumbnail && obj_idx >= 0 && vol_idx >= 0 && !model_objects[obj_idx]->volumes[vol_idx]->mm_segmentation_facets.empty();
        GLShaderProgram* shader = wxGetApp().get_shader(render_as_painted ? "mm_gouraud" : "gouraud_light");
        if (shader == nullptr)
            continue;

        shader->start_using();
        const std::array<float, 4> clp_data = { 0.0f, 0.0f, 1.0f, FLT_MAX };
        const std::array<float, 2> z_range = { -FLT_MAX, FLT_MAX };
        const bool is_left_handed = vol->is_left_handed();
        if (render_as_painted) {
            shader->set_uniform("volume_world_matrix", vol->world_matrix());
            shader->set_uniform("volume_mirrored", is_left_handed);
            shader->set_uniform("clipping_plane", clp_data);
            shader->set_uniform("z_range", z_range);
        }
        else {
            shader->set_uniform("emission_factor", 0.0f);
            vol->model.set_color((vol->printable && !vol->is_outside) ? vol->color : ColorRGBA::GRAY());
        }

        // the volume may have been deactivated by an active gizmo
        const bool is_active = vol->is_active;
        vol->is_active = true;
        const Transform3d model_matrix = vol->world_matrix();
        shader->set_uniform("view_model_matrix", view_matrix * model_matrix);
        shader->set_uniform("projection_matrix", projection_matrix);
        const Matrix3d view_normal_matrix = view_matrix.matrix().block(0, 0, 3, 3) * model_matrix.matrix().block(0, 0, 3, 3).inverse().transpose();
        shader->set_uniform("view_normal_matrix", view_normal_matrix);

        if (is_left_handed)
            glsafe(::glFrontFace(GL_CW));

        if (render_as_painted) {
            const ModelVolume& model_volume = *model_objects[obj_idx]->volumes[vol_idx];
            const size_t extruder_idx = ModelVolume::get_extruder_color_idx(model_volume, extruders_count);
            TriangleSelectorMmGui ts(model_volume.mesh(), extruders_colors, extruders_colors[extruder_idx]);
            ts.deserialize(model_volume.mm_segmentation_facets.get_data(), true);
            ts.request_update_render_data();

            ts.render(nullptr, model_matrix);
        }
        else
            vol->render();

        if (is_left_handed)
            glsafe(::glFrontFace(GL_CCW));

        shader->stop_using();

        vol->is_active = is_active;
    }

    glsafe(::glDisable(GL_DEPTH_TEST));

    if (thumbnail_params.show_bed)
        _render_bed(view_matrix, projection_matrix, !camera.is_looking_downward());

    // restore background color
    if (thumbnail_params.transparent_background)
        glsafe(::glClearColor(1.0f, 1.0f, 1.0f, 1.0f));
}

void GLCanvas3D::_render_thumbnail_framebuffer(ThumbnailData& thumbnail_data, unsigned int w, unsigned int h, const ThumbnailsParams& thumbnail_params, const GLVolumeCollection& volumes, Camera::EType camera_type)
{
    thumbnail_data.set(w, h);
    if (!thumbnail_data.is_valid())
        return;

    const bool multisample = ::glIsEnabled(GL_MULTISAMPLE);
    glcheck();
    if (m_multisample_allowed && !multisample)
        glsafe(::glEnable(GL_MULTISAMPLE));

    GLint max_samples;
    glsafe(::glGetIntegerv(GL_MAX_SAMPLES, &max_samples));
    GLsizei num_samples = max_samples / 2;

    GLuint render_fbo;
    glsafe(::glGenFramebuffers(1, &render_fbo));
    glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, render_fbo));

    GLuint render_tex = 0;
    GLuint render_tex_buffer = 0;
    if (multisample) {
        // use renderbuffer instead of texture to avoid the need to use glTexImage2DMultisample which is available only since OpenGL 3.2
        glsafe(::glGenRenderbuffers(1, &render_tex_buffer));
        glsafe(::glBindRenderbuffer(GL_RENDERBUFFER, render_tex_buffer));
        glsafe(::glRenderbufferStorageMultisample(GL_RENDERBUFFER, num_samples, GL_RGBA8, w, h));
        glsafe(::glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, render_tex_buffer));
    }
    else {
        glsafe(::glGenTextures(1, &render_tex));
        glsafe(::glBindTexture(GL_TEXTURE_2D, render_tex));
        glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr));
        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
        glsafe(::glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, render_tex, 0));
    }

    GLuint render_depth;
    glsafe(::glGenRenderbuffers(1, &render_depth));
    glsafe(::glBindRenderbuffer(GL_RENDERBUFFER, render_depth));
    if (multisample)
        glsafe(::glRenderbufferStorageMultisample(GL_RENDERBUFFER, num_samples, GL_DEPTH_COMPONENT24, w, h));
    else
        glsafe(::glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, w, h));

    glsafe(::glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, render_depth));

    GLenum drawBufs[] = { GL_COLOR_ATTACHMENT0 };
    glsafe(::glDrawBuffers(1, drawBufs));

    if (::glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        _render_thumbnail_internal(thumbnail_data, thumbnail_params, volumes, camera_type);

        if (multisample) {
            GLuint resolve_fbo;
            glsafe(::glGenFramebuffers(1, &resolve_fbo));
            glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, resolve_fbo));

            GLuint resolve_tex;
            glsafe(::glGenTextures(1, &resolve_tex));
            glsafe(::glBindTexture(GL_TEXTURE_2D, resolve_tex));
            glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr));
            glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
            glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
            glsafe(::glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolve_tex, 0));

            glsafe(::glDrawBuffers(1, drawBufs));

            if (::glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
                glsafe(::glBindFramebuffer(GL_READ_FRAMEBUFFER, render_fbo));
                glsafe(::glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolve_fbo));
                glsafe(::glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_LINEAR));

                glsafe(::glBindFramebuffer(GL_READ_FRAMEBUFFER, resolve_fbo));
                glsafe(::glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, (void*)thumbnail_data.pixels.data()));
            }

            glsafe(::glDeleteTextures(1, &resolve_tex));
            glsafe(::glDeleteFramebuffers(1, &resolve_fbo));
        }
        else
            glsafe(::glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, (void*)thumbnail_data.pixels.data()));
    }

    glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, 0));
    glsafe(::glDeleteRenderbuffers(1, &render_depth));
    if (render_tex_buffer != 0)
        glsafe(::glDeleteRenderbuffers(1, &render_tex_buffer));
    if (render_tex != 0)
        glsafe(::glDeleteTextures(1, &render_tex));
    glsafe(::glDeleteFramebuffers(1, &render_fbo));

    if (!multisample)
        glsafe(::glDisable(GL_MULTISAMPLE));
}

void GLCanvas3D::_render_thumbnail_framebuffer_ext(ThumbnailData& thumbnail_data, unsigned int w, unsigned int h, const ThumbnailsParams& thumbnail_params, const GLVolumeCollection& volumes, Camera::EType camera_type)
{
    thumbnail_data.set(w, h);
    if (!thumbnail_data.is_valid())
        return;

    const bool multisample = ::glIsEnabled(GL_MULTISAMPLE);
    glcheck();
    if (m_multisample_allowed && !multisample)
        glsafe(::glEnable(GL_MULTISAMPLE));

    GLint max_samples;
    glsafe(::glGetIntegerv(GL_MAX_SAMPLES_EXT, &max_samples));
    GLsizei num_samples = max_samples / 2;

    GLuint render_fbo;
    glsafe(::glGenFramebuffersEXT(1, &render_fbo));
    glsafe(::glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, render_fbo));

    GLuint render_tex = 0;
    GLuint render_tex_buffer = 0;
    if (multisample) {
        // use renderbuffer instead of texture to avoid the need to use glTexImage2DMultisample which is available only since OpenGL 3.2
        glsafe(::glGenRenderbuffersEXT(1, &render_tex_buffer));
        glsafe(::glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, render_tex_buffer));
        glsafe(::glRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER_EXT, num_samples, GL_RGBA8, w, h));
        glsafe(::glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_RENDERBUFFER_EXT, render_tex_buffer));
    }
    else {
        glsafe(::glGenTextures(1, &render_tex));
        glsafe(::glBindTexture(GL_TEXTURE_2D, render_tex));
        glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr));
        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
        glsafe(::glFramebufferTexture2D(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, render_tex, 0));
    }

    GLuint render_depth;
    glsafe(::glGenRenderbuffersEXT(1, &render_depth));
    glsafe(::glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, render_depth));
    if (multisample)
        glsafe(::glRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER_EXT, num_samples, GL_DEPTH_COMPONENT24, w, h));
    else
        glsafe(::glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT, w, h));

    glsafe(::glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT, render_depth));

    GLenum drawBufs[] = { GL_COLOR_ATTACHMENT0 };
    glsafe(::glDrawBuffers(1, drawBufs));

    if (::glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) == GL_FRAMEBUFFER_COMPLETE_EXT) {
        _render_thumbnail_internal(thumbnail_data, thumbnail_params, volumes, camera_type);

        if (multisample) {
            GLuint resolve_fbo;
            glsafe(::glGenFramebuffersEXT(1, &resolve_fbo));
            glsafe(::glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, resolve_fbo));

            GLuint resolve_tex;
            glsafe(::glGenTextures(1, &resolve_tex));
            glsafe(::glBindTexture(GL_TEXTURE_2D, resolve_tex));
            glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr));
            glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
            glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
            glsafe(::glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, resolve_tex, 0));

            glsafe(::glDrawBuffers(1, drawBufs));

            if (::glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) == GL_FRAMEBUFFER_COMPLETE_EXT) {
                glsafe(::glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT, render_fbo));
                glsafe(::glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT, resolve_fbo));
                glsafe(::glBlitFramebufferEXT(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_LINEAR));

                glsafe(::glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT, resolve_fbo));
                glsafe(::glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, (void*)thumbnail_data.pixels.data()));
            }

            glsafe(::glDeleteTextures(1, &resolve_tex));
            glsafe(::glDeleteFramebuffersEXT(1, &resolve_fbo));
        }
        else
            glsafe(::glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, (void*)thumbnail_data.pixels.data()));
    }

    glsafe(::glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0));
    glsafe(::glDeleteRenderbuffersEXT(1, &render_depth));
    if (render_tex_buffer != 0)
        glsafe(::glDeleteRenderbuffersEXT(1, &render_tex_buffer));
    if (render_tex != 0)
        glsafe(::glDeleteTextures(1, &render_tex));
    glsafe(::glDeleteFramebuffersEXT(1, &render_fbo));

    if (!multisample)
        glsafe(::glDisable(GL_MULTISAMPLE));
}

void GLCanvas3D::_render_thumbnail_legacy(ThumbnailData& thumbnail_data, unsigned int w, unsigned int h, const ThumbnailsParams& thumbnail_params, const GLVolumeCollection& volumes, Camera::EType camera_type)
{
    // check that thumbnail size does not exceed the default framebuffer size
    const Size& cnv_size = get_canvas_size();
    unsigned int cnv_w = (unsigned int)cnv_size.get_width();
    unsigned int cnv_h = (unsigned int)cnv_size.get_height();
    if (w > cnv_w || h > cnv_h) {
        float ratio = std::min((float)cnv_w / (float)w, (float)cnv_h / (float)h);
        w = (unsigned int)(ratio * (float)w);
        h = (unsigned int)(ratio * (float)h);
    }

    thumbnail_data.set(w, h);
    if (!thumbnail_data.is_valid())
        return;

    _render_thumbnail_internal(thumbnail_data, thumbnail_params, volumes, camera_type);

    glsafe(::glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, (void*)thumbnail_data.pixels.data()));

    // restore the default framebuffer size to avoid flickering on the 3D scene
    wxGetApp().plater()->get_camera().apply_viewport();
}

bool GLCanvas3D::_init_toolbars()
{
    if (!_init_main_toolbar())
        return false;

    if (!_init_undoredo_toolbar())
        return false;

    if (!_init_view_toolbar())
        return false;

    if (!_init_collapse_toolbar())
        return false;

    return true;
}

bool GLCanvas3D::_init_main_toolbar()
{
    //if (!m_main_toolbar.is_enabled())
    //    return true;

    BackgroundTexture::Metadata background_data;
    background_data.filename = "toolbar_background.png";
    background_data.left = 16;
    background_data.top = 16;
    background_data.right = 16;
    background_data.bottom = 16;

    if (!m_main_toolbar.init(background_data)) {
        // unable to init the toolbar texture, disable it
        m_main_toolbar.set_enabled(false);
        return true;
    }
    // init arrow
    if (!m_main_toolbar.init_arrow("toolbar_arrow_2.svg"))
        BOOST_LOG_TRIVIAL(error) << "Main toolbar failed to load arrow texture.";

    // m_gizmos is created at constructor, thus we can init arrow here.
    if (!m_gizmos.init_arrow("toolbar_arrow_2.svg"))
        BOOST_LOG_TRIVIAL(error) << "Gizmos manager failed to load arrow texture.";

//    m_main_toolbar.set_layout_type(GLToolbar::Layout::Vertical);
    m_main_toolbar.set_layout_type(GLToolbar::Layout::Horizontal);
    m_main_toolbar.set_horizontal_orientation(GLToolbar::Layout::HO_Right);
    m_main_toolbar.set_vertical_orientation(GLToolbar::Layout::VO_Top);
    //m_main_toolbar.set_border(5.0f);
    //m_main_toolbar.set_separator_size(5.f);
    //m_main_toolbar.set_gap_size(5.f);

    GLToolbarItem::Data item;

    unsigned int sprite_id = 0;

    item.name = "add";
    item.icon_filename = "add.svg";
    item.tooltip = _u8L("Add...") + " [" + GUI::shortkey_ctrl_prefix() + "I]";
    item.sprite_id = sprite_id++;
    item.left.action_callback = [this]() { if (m_canvas != nullptr) wxPostEvent(m_canvas, SimpleEvent(EVT_GLTOOLBAR_ADD)); };
    if (!m_main_toolbar.add_item(item))
        return false;

    item.name = "delete";
    item.icon_filename = "remove.svg";
    item.tooltip = _u8L("Delete") + " [Del]";
    item.sprite_id = sprite_id++;
    item.left.action_callback = [this]() { if (m_canvas != nullptr) wxPostEvent(m_canvas, SimpleEvent(EVT_GLTOOLBAR_DELETE)); };
    item.enabling_callback = []()->bool { return wxGetApp().plater()->can_delete(); };
    if (!m_main_toolbar.add_item(item))
        return false;

    item.name = "deleteall";
    item.icon_filename = "delete_all.svg";
    item.tooltip = _u8L("Delete all") + " [" + GUI::shortkey_ctrl_prefix() + "Del]";
    item.sprite_id = sprite_id++;
    item.left.action_callback = [this]() { if (m_canvas != nullptr) wxPostEvent(m_canvas, SimpleEvent(EVT_GLTOOLBAR_DELETE_ALL)); };
    item.enabling_callback = []()->bool { return wxGetApp().plater()->can_delete_all(); };
    if (!m_main_toolbar.add_item(item))
        return false;

    item.name = "arrange";
    item.icon_filename = "arrange.svg";
    item.tooltip = _u8L("Arrange") + " [A]\n" + _u8L("Arrange selection") + " [Shift+A]\n" + _u8L("Click right mouse button to show arrangement options");
    item.sprite_id = sprite_id++;
    item.left.action_callback = [this]() { if (m_canvas != nullptr) wxPostEvent(m_canvas, SimpleEvent(EVT_GLTOOLBAR_ARRANGE)); };
    item.enabling_callback = []()->bool { return wxGetApp().plater()->can_arrange(); };
    item.right.toggable = true;
    item.right.render_callback = [this](float left, float right, float, float) {
        if (m_canvas != nullptr)
            _render_arrange_menu(0.5f * (left + right), false);
    };
    if (!m_main_toolbar.add_item(item))
        return false;

    item.name = "arrangecurrent";
    item.icon_filename = "arrange_current.svg";
    item.tooltip =
        _u8L("Arrange current bed") + " [D]\n"
        + _u8L("Arrange selection on current bed") + " [Shift+D]\n";
    item.sprite_id = sprite_id++;
    item.left.action_callback = [this]() { if (m_canvas != nullptr) wxPostEvent(m_canvas, SimpleEvent(EVT_GLTOOLBAR_ARRANGE_CURRENT_BED)); };
    item.enabling_callback = []()->bool { return wxGetApp().plater()->can_arrange(); };
    item.right.toggable = true;
    item.right.render_callback = [this](float left, float right, float, float) {
        if (m_canvas != nullptr)
            _render_arrange_menu(0.5f * (left + right), true);
    };
    if (!m_main_toolbar.add_item(item))
        return false;

    item.right.toggable = false;
    item.right.render_callback = GLToolbarItem::Default_Render_Callback;

    if (!m_main_toolbar.add_separator())
        return false;

    item.name = "copy";
    item.icon_filename = "copy.svg";
    item.tooltip = _u8L("Copy") + " [" + GUI::shortkey_ctrl_prefix() + "C]";
    item.sprite_id = sprite_id++;
    item.left.action_callback = [this]() { if (m_canvas != nullptr) wxPostEvent(m_canvas, SimpleEvent(EVT_GLTOOLBAR_COPY)); };
    item.enabling_callback = []()->bool { return wxGetApp().plater()->can_copy_to_clipboard(); };
    if (!m_main_toolbar.add_item(item))
        return false;

    item.name = "paste";
    item.icon_filename = "paste.svg";
    item.tooltip = _u8L("Paste") + " [" + GUI::shortkey_ctrl_prefix() + "V]";
    item.sprite_id = sprite_id++;
    item.left.action_callback = [this]() { if (m_canvas != nullptr) wxPostEvent(m_canvas, SimpleEvent(EVT_GLTOOLBAR_PASTE)); };
    item.enabling_callback = []()->bool { return wxGetApp().plater()->can_paste_from_clipboard(); };
    if (!m_main_toolbar.add_item(item))
        return false;

    if (!m_main_toolbar.add_separator())
        return false;

    item.name = "more";
    item.icon_filename = "instance_add.svg";
    item.tooltip = _u8L("Add instance") + " [+]";
    item.sprite_id = sprite_id++;
    item.left.action_callback = [this]() { if (m_canvas != nullptr) wxPostEvent(m_canvas, SimpleEvent(EVT_GLTOOLBAR_MORE)); };
    item.visibility_callback = []()->bool { return wxGetApp().get_mode() != comSimple; };
    item.enabling_callback = []()->bool { return wxGetApp().plater()->can_increase_instances(); };

    if (!m_main_toolbar.add_item(item))
        return false;

    item.name = "fewer";
    item.icon_filename = "instance_remove.svg";
    item.tooltip = _u8L("Remove instance") + " [-]";
    item.sprite_id = sprite_id++;
    item.left.action_callback = [this]() { if (m_canvas != nullptr) wxPostEvent(m_canvas, SimpleEvent(EVT_GLTOOLBAR_FEWER)); };
    item.visibility_callback = []()->bool { return wxGetApp().get_mode() != comSimple; };
    item.enabling_callback = []()->bool { return wxGetApp().plater()->can_decrease_instances(); };
    if (!m_main_toolbar.add_item(item))
        return false;

    if (!m_main_toolbar.add_separator())
        return false;

    item.name = "splitobjects";
    item.icon_filename = "split_objects.svg";
    item.tooltip = _u8L("Split to objects");
    item.sprite_id = sprite_id++;
    item.left.action_callback = [this]() { if (m_canvas != nullptr) wxPostEvent(m_canvas, SimpleEvent(EVT_GLTOOLBAR_SPLIT_OBJECTS)); };
    item.visibility_callback = GLToolbarItem::Default_Visibility_Callback;
    item.enabling_callback = []()->bool { return wxGetApp().plater()->can_split_to_objects(); };
    if (!m_main_toolbar.add_item(item))
        return false;

    item.name = "splitvolumes";
    item.icon_filename = "split_parts.svg";
    item.tooltip = _u8L("Split to parts");
    item.sprite_id = sprite_id++;
    item.left.action_callback = [this]() { if (m_canvas != nullptr) wxPostEvent(m_canvas, SimpleEvent(EVT_GLTOOLBAR_SPLIT_VOLUMES)); };
    item.visibility_callback = []()->bool { return wxGetApp().get_mode() != comSimple; };
    item.enabling_callback = []()->bool { return wxGetApp().plater()->can_split_to_volumes(); };
    if (!m_main_toolbar.add_item(item))
        return false;

    if (!m_main_toolbar.add_separator())
        return false;
/*
    item.name = "settings";
    item.icon_filename = "settings.svg";
    item.tooltip = _u8L("Switch to Settings") + "\n" + "[" + GUI::shortkey_ctrl_prefix() + "2] - " + _u8L("Print Settings Tab")    + 
                                                "\n" + "[" + GUI::shortkey_ctrl_prefix() + "3] - " + (current_printer_technology() == ptFFF ? _u8L("Filament Settings Tab") : _u8L("Material Settings Tab") +
                                                "\n" + "[" + GUI::shortkey_ctrl_prefix() + "4] - " + _u8L("Printer Settings Tab")) ;
    item.sprite_id = sprite_id++;
    item.enabling_callback    = GLToolbarItem::Default_Enabling_Callback;
    item.visibility_callback  = []() { return wxGetApp().app_config->get_bool("dlg_settings_layout_mode"); };
    item.left.action_callback = []() { wxGetApp().mainframe->select_tab(); };
    if (!m_main_toolbar.add_item(item))
        return false;
*/
    if (!m_main_toolbar.add_separator())
        return false;

    item.name = "layersediting";
    item.icon_filename = "layers_white.svg";
    item.tooltip = _u8L("Variable layer height");
    item.sprite_id = sprite_id++;
    item.left.action_callback = [this]() { 
        if (m_canvas != nullptr) 
            wxPostEvent(m_canvas, SimpleEvent(EVT_GLTOOLBAR_LAYERSEDITING)); };
    item.visibility_callback = [this]()->bool {
        bool res = current_printer_technology() == ptFFF;
        // turns off if changing printer technology
        if (!res && m_main_toolbar.is_item_visible("layersediting") && m_main_toolbar.is_item_pressed("layersediting"))
            force_main_toolbar_left_action(get_main_toolbar_item_id("layersediting"));

        return res;
    };
    item.enabling_callback      = []()->bool { return wxGetApp().plater()->can_layers_editing(); };
    item.left.render_callback   = GLToolbarItem::Default_Render_Callback;
    if (!m_main_toolbar.add_item(item))
        return false;

    if (!m_main_toolbar.generate_icons_texture())
        return false;

    return true;
}

bool GLCanvas3D::_init_undoredo_toolbar()
{
    //if (!m_undoredo_toolbar.is_enabled())
    //    return true;

    BackgroundTexture::Metadata background_data;
    background_data.filename = "toolbar_background.png";
    background_data.left = 16;
    background_data.top = 16;
    background_data.right = 16;
    background_data.bottom = 16;

    if (!m_undoredo_toolbar.init(background_data)) {
        // unable to init the toolbar texture, disable it
        m_undoredo_toolbar.set_enabled(false);
        return true;
    }

    // init arrow
    if (!m_undoredo_toolbar.init_arrow("toolbar_arrow_2.svg"))
        BOOST_LOG_TRIVIAL(error) << "Undo/Redo toolbar failed to load arrow texture.";

//    m_undoredo_toolbar.set_layout_type(GLToolbar::Layout::Vertical);
    m_undoredo_toolbar.set_layout_type(GLToolbar::Layout::Horizontal);
    m_undoredo_toolbar.set_horizontal_orientation(GLToolbar::Layout::HO_Left);
    m_undoredo_toolbar.set_vertical_orientation(GLToolbar::Layout::VO_Top);
    //m_undoredo_toolbar.set_border(5.f);
    //m_undoredo_toolbar.set_separator_size(5.f);
    //m_undoredo_toolbar.set_gap_size(5.f);

    GLToolbarItem::Data item;

    unsigned int sprite_id = 0;

    item.name = "undo";
    item.icon_filename = "undo_toolbar.svg";
    item.tooltip = _u8L("Undo") + " [" + GUI::shortkey_ctrl_prefix() + "Z]\n" + _u8L("Click right mouse button to open/close History");
    item.sprite_id = sprite_id++;
    item.left.action_callback = [this]() { post_event(SimpleEvent(EVT_GLCANVAS_UNDO)); };
    item.right.toggable = true;
    item.right.action_callback = [this]() { m_imgui_undo_redo_hovered_pos = -1; };
    item.right.render_callback = [this](float left, float right, float, float) {
        if (m_canvas != nullptr) {
            if (_render_undo_redo_stack(true, 0.5f * (left + right)))
                _deactivate_undo_redo_toolbar_items();
        }
    };
    item.enabling_callback = [this]()->bool {
        bool can_undo = wxGetApp().plater()->can_undo();
        int id = m_undoredo_toolbar.get_item_id("undo");

        std::string curr_additional_tooltip;
        m_undoredo_toolbar.get_additional_tooltip(id, curr_additional_tooltip);

        std::string new_additional_tooltip;
        if (can_undo) {
        	std::string action;
            wxGetApp().plater()->undo_redo_topmost_string_getter(true, action);
            new_additional_tooltip = format(_L("Next Undo action: %1%"), action);
        }

        if (new_additional_tooltip != curr_additional_tooltip) {
            m_undoredo_toolbar.set_additional_tooltip(id, new_additional_tooltip);
            set_tooltip("");
        }
        return can_undo;
    };

    if (!m_undoredo_toolbar.add_item(item))
        return false;

    item.name = "redo";
    item.icon_filename = "redo_toolbar.svg";
    item.tooltip = _u8L("Redo") + " [" + GUI::shortkey_ctrl_prefix() + "Y]\n" + _u8L("Click right mouse button to open/close History");
    item.sprite_id = sprite_id++;
    item.left.action_callback = [this]() { post_event(SimpleEvent(EVT_GLCANVAS_REDO)); };
    item.right.action_callback = [this]() { m_imgui_undo_redo_hovered_pos = -1; };
    item.right.render_callback = [this](float left, float right, float, float) {
        if (m_canvas != nullptr) {
            if (_render_undo_redo_stack(false, 0.5f * (left + right)))
                _deactivate_undo_redo_toolbar_items();
        }
    };
    item.enabling_callback = [this]()->bool {
        bool can_redo = wxGetApp().plater()->can_redo();
        int id = m_undoredo_toolbar.get_item_id("redo");

        std::string curr_additional_tooltip;
        m_undoredo_toolbar.get_additional_tooltip(id, curr_additional_tooltip);

        std::string new_additional_tooltip;
        if (can_redo) {
        	std::string action;
            wxGetApp().plater()->undo_redo_topmost_string_getter(false, action);
            new_additional_tooltip = format(_L("Next Redo action: %1%"), action);
        }

        if (new_additional_tooltip != curr_additional_tooltip) {
            m_undoredo_toolbar.set_additional_tooltip(id, new_additional_tooltip);
            set_tooltip("");
        }
        return can_redo;
    };

    if (!m_undoredo_toolbar.add_item(item))
        return false;
    /*
    if (!m_undoredo_toolbar.add_separator())
        return false;
        */

    if (!m_undoredo_toolbar.generate_icons_texture())
        return false;

    return true;
}

bool GLCanvas3D::_init_view_toolbar()
{
    return wxGetApp().plater()->init_view_toolbar();
}

bool GLCanvas3D::_init_collapse_toolbar()
{
    return wxGetApp().plater()->init_collapse_toolbar();
}

bool GLCanvas3D::_set_current()
{
    return m_context != nullptr && m_canvas->SetCurrent(*m_context);
}

void GLCanvas3D::_resize(unsigned int w, unsigned int h)
{
    if (m_canvas == nullptr && m_context == nullptr)
        return;

    const std::array<unsigned int, 2> new_size = { w, h };
    if (m_old_size == new_size)
        return;

    m_old_size = new_size;

    auto *imgui = wxGetApp().imgui();
    ImGuiPureWrap::set_display_size(static_cast<float>(w), static_cast<float>(h));
    const float font_size = 1.5f * wxGetApp().em_unit();
#if ENABLE_RETINA_GL
    imgui->set_scaling(font_size, 1.0f, m_retina_helper->get_scale_factor());
#else
    imgui->set_scaling(font_size, m_canvas->GetContentScaleFactor(), 1.0f);
#endif

    this->request_extra_frame();

    // ensures that this canvas is current
    _set_current();
}

BoundingBoxf3 GLCanvas3D::_max_bounding_box(bool include_bed_model) const
{
    const bool is_preview = wxGetApp().plater()->is_preview_shown();

    BoundingBoxf3 bb = volumes_bounding_box();

    // The following is a workaround for gizmos not being taken in account when calculating the tight camera frustrum
    // A better solution would ask the gizmo manager for the bounding box of the current active gizmo, if any
    if (!is_preview && m_gizmos.is_running()) {
        const BoundingBoxf3 sel_bb = m_selection.get_bounding_box();
        const Vec3d sel_bb_center = sel_bb.center();
        const Vec3d extend_by = sel_bb.max_size() * Vec3d::Ones();
        bb.merge(BoundingBoxf3(sel_bb_center - extend_by, sel_bb_center + extend_by));
    }

    const BoundingBoxf3 first_bed_bb = include_bed_model ? m_bed.extended_bounding_box() : m_bed.build_volume().bounding_volume();
    BoundingBoxf3 bed_bb;

    for (int i = 0; i < s_multiple_beds.get_number_of_beds() + int(s_multiple_beds.should_show_next_bed()); ++i) {
        if (!is_preview || i == s_multiple_beds.get_active_bed()) {
            BoundingBoxf3 this_bed = first_bed_bb;
            this_bed.translate(s_multiple_beds.get_bed_translation(i));
            bed_bb.merge(this_bed);
        }
    }
    bb.merge(bed_bb);
    
    if (is_preview) {
        BoundingBoxf3 paths_bb = m_gcode_viewer.get_max_bounding_box();
        paths_bb.translate(s_multiple_beds.get_bed_translation(s_multiple_beds.get_active_bed()));
        bb.merge(paths_bb);
    }

    // clamp max bb size with respect to bed bb size
    if (!m_picking_enabled) {
        static const double max_scale_factor = 2.0;
        const Vec3d bb_size = bb.size();
        const Vec3d bed_bb_size = m_bed.build_volume().bounding_volume().size();

        if ((bed_bb_size.x() > 0.0 && bb_size.x() > max_scale_factor * bed_bb_size.x()) ||
            (bed_bb_size.y() > 0.0 && bb_size.y() > max_scale_factor * bed_bb_size.y()) ||
            (bed_bb_size.z() > 0.0 && bb_size.z() > max_scale_factor * bed_bb_size.z())) {
            const Vec3d bed_bb_center = bed_bb.center();
            const Vec3d extend_by = max_scale_factor * bed_bb_size;
            bb = BoundingBoxf3(bed_bb_center - extend_by, bed_bb_center + extend_by);
        }
    }

    return bb;
}

void GLCanvas3D::_zoom_to_box(const BoundingBoxf3& box, double margin_factor)
{
    wxGetApp().plater()->get_camera().zoom_to_box(box, margin_factor);
    m_dirty = true;
}

void GLCanvas3D::_update_camera_zoom(double zoom)
{
    wxGetApp().plater()->get_camera().update_zoom(zoom);
    m_dirty = true;
}

void GLCanvas3D::_refresh_if_shown_on_screen()
{
    if (_is_shown_on_screen()) {
        const Size& cnv_size = get_canvas_size();
        _resize((unsigned int)cnv_size.get_width(), (unsigned int)cnv_size.get_height());

        // When the application starts the following call to render() triggers the opengl initialization.
        // We need to ask for an extra call to reload_scene() to force the generation of the model for wipe tower
        // for printers using it, which is skipped by all the previous calls to reload_scene() because m_initialized == false
        const bool requires_reload_scene = !m_initialized;

        // Because of performance problems on macOS, where PaintEvents are not delivered
        // frequently enough, we call render() here directly when we can.
        render();
        assert(m_initialized);
        if (requires_reload_scene) {
            if (wxGetApp().plater()->is_view3D_shown())
                reload_scene(true);
        }
    }
}

void GLCanvas3D::_picking_pass()
{
    if (!m_picking_enabled || m_mouse.dragging || m_mouse.position == Vec2d(DBL_MAX, DBL_MAX) || m_gizmos.is_dragging()) {
#if ENABLE_RAYCAST_PICKING_DEBUG
        ImGuiPureWrap::begin(std::string("Hit result"), ImGuiWindowFlags_AlwaysAutoResize);
        ImGuiPureWrap::text("Picking disabled");
        ImGuiPureWrap::end();
#endif // ENABLE_RAYCAST_PICKING_DEBUG
        return;
    }

    m_hover_volume_idxs.clear();

    const ClippingPlane clipping_plane = m_gizmos.get_clipping_plane().inverted_normal();
    const SceneRaycaster::HitResult hit = m_scene_raycaster.hit(m_mouse.position, wxGetApp().plater()->get_camera(), &clipping_plane);
    if (hit.is_valid()) {
        switch (hit.type)
        {
        case SceneRaycaster::EType::Volume:
        {
            if (0 <= hit.raycaster_id && hit.raycaster_id < (int)m_volumes.volumes.size()) {
                const GLVolume* volume = m_volumes.volumes[hit.raycaster_id];
                if (volume->is_active && !volume->disabled && (volume->composite_id.volume_id >= 0 || m_render_sla_auxiliaries)) {
                    // do not add the volume id if any gizmo is active and CTRL is pressed
                    if (m_gizmos.get_current_type() == GLGizmosManager::EType::Undefined || !wxGetKeyState(WXK_CONTROL))
                        m_hover_volume_idxs.emplace_back(hit.raycaster_id);
                    m_gizmos.set_hover_id(-1);
                }
            }
            else
                assert(false);

            break;
        }
        case SceneRaycaster::EType::Gizmo:
        case SceneRaycaster::EType::FallbackGizmo:
        {
            const Size& cnv_size = get_canvas_size();
            const bool inside = 0 <= m_mouse.position.x() && m_mouse.position.x() < cnv_size.get_width() &&
                0 <= m_mouse.position.y() && m_mouse.position.y() < cnv_size.get_height();
            m_gizmos.set_hover_id(inside ? hit.raycaster_id : -1);
            break;
        }
        case SceneRaycaster::EType::Bed:
        {
            m_gizmos.set_hover_id(-1);
            break;
        }
        default:
        {
            assert(false);
            break;
        }
        }
    }
    else
        m_gizmos.set_hover_id(-1);

    _update_volumes_hover_state();

#if ENABLE_RAYCAST_PICKING_DEBUG
    ImGuiWrapper& imgui = *wxGetApp().imgui();
    ImGuiPureWrap::begin(std::string("Hit result"), ImGuiWindowFlags_AlwaysAutoResize);
    std::string object_type = "None";
    switch (hit.type)
    {
    case SceneRaycaster::EType::Bed:   { object_type = "Bed"; break; }
    case SceneRaycaster::EType::Gizmo: { object_type = "Gizmo element"; break; }
    case SceneRaycaster::EType::FallbackGizmo: { object_type = "Gizmo2 element"; break; }
    case SceneRaycaster::EType::Volume:
    {
        if (m_volumes.volumes[hit.raycaster_id]->is_wipe_tower)
            object_type = "Volume (Wipe tower)";
        else if (m_volumes.volumes[hit.raycaster_id]->volume_idx() == -int(slaposPad))
            object_type = "Volume (SLA pad)";
        else if (m_volumes.volumes[hit.raycaster_id]->volume_idx() == -int(slaposSupportTree))
            object_type = "Volume (SLA supports)";
        else if (m_volumes.volumes[hit.raycaster_id]->is_modifier)
            object_type = "Volume (Modifier)";
        else
            object_type = "Volume (Part)";
        break;
    }
    default: { break; }
    }

    auto add_strings_row_to_table = [](const std::string& col_1, const ImVec4& col_1_color, const std::string& col_2, const ImVec4& col_2_color,
        const std::string& col_3 = "", const ImVec4& col_3_color = ImGui::GetStyleColorVec4(ImGuiCol_Text)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGuiPureWrap::text_colored(col_1_color, col_1.c_str());
        ImGui::TableSetColumnIndex(1);
        ImGuiPureWrap::text_colored(col_2_color, col_2.c_str());
        if (!col_3.empty()) {
            ImGui::TableSetColumnIndex(2);
            ImGuiPureWrap::text_colored(col_3_color, col_3.c_str());
        }
    };

    char buf[1024];
    if (hit.type != SceneRaycaster::EType::None) {
        if (ImGui::BeginTable("Hit", 2)) {
            add_strings_row_to_table("Object ID", ImGuiPureWrap::COL_ORANGE_LIGHT, std::to_string(hit.raycaster_id), ImGui::GetStyleColorVec4(ImGuiCol_Text));
            add_strings_row_to_table("Type", ImGuiPureWrap::COL_ORANGE_LIGHT, object_type, ImGui::GetStyleColorVec4(ImGuiCol_Text));
            sprintf(buf, "%.3f, %.3f, %.3f", hit.position.x(), hit.position.y(), hit.position.z());
            add_strings_row_to_table("Position", ImGuiPureWrap::COL_ORANGE_LIGHT, std::string(buf), ImGui::GetStyleColorVec4(ImGuiCol_Text));
            sprintf(buf, "%.3f, %.3f, %.3f", hit.normal.x(), hit.normal.y(), hit.normal.z());
            add_strings_row_to_table("Normal", ImGuiPureWrap::COL_ORANGE_LIGHT, std::string(buf), ImGui::GetStyleColorVec4(ImGuiCol_Text));
            ImGui::EndTable();
        }
    }
    else
        ImGuiPureWrap::text("NO HIT");

    ImGui::Separator();
    ImGuiPureWrap::text("Registered for picking:");
    if (ImGui::BeginTable("Raycasters", 2)) {
        sprintf(buf, "%d (%d)", (int)m_scene_raycaster.beds_count(), (int)m_scene_raycaster.active_beds_count());
        add_strings_row_to_table("Beds", ImGuiPureWrap::COL_ORANGE_LIGHT, std::string(buf), ImGui::GetStyleColorVec4(ImGuiCol_Text));
        sprintf(buf, "%d (%d)", (int)m_scene_raycaster.volumes_count(), (int)m_scene_raycaster.active_volumes_count());
        add_strings_row_to_table("Volumes", ImGuiPureWrap::COL_ORANGE_LIGHT, std::string(buf), ImGui::GetStyleColorVec4(ImGuiCol_Text));
        sprintf(buf, "%d (%d)", (int)m_scene_raycaster.gizmos_count(), (int)m_scene_raycaster.active_gizmos_count());
        add_strings_row_to_table("Gizmo elements", ImGuiPureWrap::COL_ORANGE_LIGHT, std::string(buf), ImGui::GetStyleColorVec4(ImGuiCol_Text));
        sprintf(buf, "%d (%d)", (int)m_scene_raycaster.fallback_gizmos_count(), (int)m_scene_raycaster.active_fallback_gizmos_count());
        add_strings_row_to_table("Gizmo2 elements", ImGuiPureWrap::COL_ORANGE_LIGHT, std::string(buf), ImGui::GetStyleColorVec4(ImGuiCol_Text));
        ImGui::EndTable();
    }

    std::vector<std::shared_ptr<SceneRaycasterItem>>* gizmo_raycasters = m_scene_raycaster.get_raycasters(SceneRaycaster::EType::Gizmo);
    if (gizmo_raycasters != nullptr && !gizmo_raycasters->empty()) {
        ImGui::Separator();
        ImGuiPureWrap::text("Gizmo raycasters IDs:");
        if (ImGui::BeginTable("GizmoRaycasters", 3)) {
            for (size_t i = 0; i < gizmo_raycasters->size(); ++i) {
                add_strings_row_to_table(std::to_string(i), ImGuiPureWrap::COL_ORANGE_LIGHT,
                    std::to_string(SceneRaycaster::decode_id(SceneRaycaster::EType::Gizmo, (*gizmo_raycasters)[i]->get_id())), ImGui::GetStyleColorVec4(ImGuiCol_Text),
                    to_string(Geometry::Transformation((*gizmo_raycasters)[i]->get_transform()).get_offset()), ImGui::GetStyleColorVec4(ImGuiCol_Text));
            }
            ImGui::EndTable();
        }
    }

    std::vector<std::shared_ptr<SceneRaycasterItem>>* gizmo2_raycasters = m_scene_raycaster.get_raycasters(SceneRaycaster::EType::FallbackGizmo);
    if (gizmo2_raycasters != nullptr && !gizmo2_raycasters->empty()) {
        ImGui::Separator();
        ImGuiPureWrap::text("Gizmo2 raycasters IDs:");
        if (ImGui::BeginTable("Gizmo2Raycasters", 3)) {
            for (size_t i = 0; i < gizmo2_raycasters->size(); ++i) {
                add_strings_row_to_table(std::to_string(i), ImGuiPureWrap::COL_ORANGE_LIGHT,
                    std::to_string(SceneRaycaster::decode_id(SceneRaycaster::EType::FallbackGizmo, (*gizmo2_raycasters)[i]->get_id())), ImGui::GetStyleColorVec4(ImGuiCol_Text),
                    to_string(Geometry::Transformation((*gizmo2_raycasters)[i]->get_transform()).get_offset()), ImGui::GetStyleColorVec4(ImGuiCol_Text));
            }
            ImGui::EndTable();
        }
    }

    ImGuiPureWrap::end();
#endif // ENABLE_RAYCAST_PICKING_DEBUG
}

void GLCanvas3D::_rectangular_selection_picking_pass()
{
    m_gizmos.set_hover_id(-1);

    std::set<int> idxs;

    if (m_picking_enabled) {
        const size_t width  = std::max<size_t>(m_rectangle_selection.get_width(), 1);
        const size_t height = std::max<size_t>(m_rectangle_selection.get_height(), 1);

        const OpenGLManager::EFramebufferType framebuffers_type = OpenGLManager::get_framebuffers_type();
        bool use_framebuffer = framebuffers_type != OpenGLManager::EFramebufferType::Unknown;

        GLuint render_fbo = 0;
        GLuint render_tex = 0;
        GLuint render_depth = 0;
        if (use_framebuffer) {
            // setup a framebuffer which covers only the selection rectangle
            if (framebuffers_type == OpenGLManager::EFramebufferType::Arb) {
                glsafe(::glGenFramebuffers(1, &render_fbo));
                glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, render_fbo));
            }
            else {
                glsafe(::glGenFramebuffersEXT(1, &render_fbo));
                glsafe(::glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, render_fbo));
            }
            glsafe(::glGenTextures(1, &render_tex));
            glsafe(::glBindTexture(GL_TEXTURE_2D, render_tex));
            glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr));
            glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
            glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
            if (framebuffers_type == OpenGLManager::EFramebufferType::Arb) {
                glsafe(::glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, render_tex, 0));
                glsafe(::glGenRenderbuffers(1, &render_depth));
                glsafe(::glBindRenderbuffer(GL_RENDERBUFFER, render_depth));
#if SLIC3R_OPENGL_ES
                glsafe(::glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, width, height));
#else
                glsafe(::glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, width, height));
#endif // SLIC3R_OPENGL_ES
                glsafe(::glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, render_depth));
            }
            else {
                glsafe(::glFramebufferTexture2D(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, render_tex, 0));
                glsafe(::glGenRenderbuffersEXT(1, &render_depth));
                glsafe(::glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, render_depth));
                glsafe(::glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT, width, height));
                glsafe(::glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT, render_depth));
            }
            const GLenum drawBufs[] = { GL_COLOR_ATTACHMENT0 };
            glsafe(::glDrawBuffers(1, drawBufs));
            if (framebuffers_type == OpenGLManager::EFramebufferType::Arb) {
                if (::glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                    use_framebuffer = false;
            }
            else {
                if (::glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) != GL_FRAMEBUFFER_COMPLETE_EXT)
                    use_framebuffer = false;
            }
        }

        if (m_multisample_allowed)
        	// This flag is often ignored by NVIDIA drivers if rendering into a screen buffer.
            glsafe(::glDisable(GL_MULTISAMPLE));

        glsafe(::glDisable(GL_BLEND));
        glsafe(::glEnable(GL_DEPTH_TEST));

        glsafe(::glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));

        const Camera& main_camera = wxGetApp().plater()->get_camera();
        Camera framebuffer_camera;
        framebuffer_camera.set_type(main_camera.get_type());
        const Camera* camera = &main_camera;
        if (use_framebuffer) {
            // setup a camera which covers only the selection rectangle
            const std::array<int, 4>& viewport = camera->get_viewport();
            const double near_left   = camera->get_near_left();
            const double near_bottom = camera->get_near_bottom();
            const double near_width  = camera->get_near_width();
            const double near_height = camera->get_near_height();

            const double ratio_x = near_width / double(viewport[2]);
            const double ratio_y = near_height / double(viewport[3]);

            const double rect_near_left   = near_left + double(m_rectangle_selection.get_left()) * ratio_x;
            const double rect_near_bottom = near_bottom + (double(viewport[3]) - double(m_rectangle_selection.get_bottom())) * ratio_y;
            double rect_near_right = near_left + double(m_rectangle_selection.get_right()) * ratio_x;
            double rect_near_top   = near_bottom + (double(viewport[3]) - double(m_rectangle_selection.get_top())) * ratio_y;

            if (rect_near_left == rect_near_right)
                rect_near_right = rect_near_left + ratio_x;
            if (rect_near_bottom == rect_near_top)
                rect_near_top = rect_near_bottom + ratio_y;

            framebuffer_camera.look_at(camera->get_position(), camera->get_target(), camera->get_dir_up());
            framebuffer_camera.apply_projection(rect_near_left, rect_near_right, rect_near_bottom, rect_near_top, camera->get_near_z(), camera->get_far_z());
            framebuffer_camera.set_viewport(0, 0, width, height);
            framebuffer_camera.apply_viewport();
            camera = &framebuffer_camera;
        }

        _render_volumes_for_picking(*camera);
        _render_bed_for_picking(camera->get_view_matrix(), camera->get_projection_matrix(), !camera->is_looking_downward());

        if (m_multisample_allowed)
            glsafe(::glEnable(GL_MULTISAMPLE));

        const size_t px_count = width * height;

        const size_t left = use_framebuffer ? 0 : (size_t)m_rectangle_selection.get_left();
        const size_t top  = use_framebuffer ? 0 : (size_t)get_canvas_size().get_height() - (size_t)m_rectangle_selection.get_top();
#define USE_PARALLEL 1
#if USE_PARALLEL
            struct Pixel
            {
                std::array<GLubyte, 4> data;
            	// Only non-interpolated colors are valid, those have their lowest three bits zeroed.
                bool valid() const { return picking_checksum_alpha_channel(data[0], data[1], data[2]) == data[3]; }
                // we reserve color = (0,0,0) for occluders (as the printbed) 
                // volumes' id are shifted by 1
                // see: _render_volumes_for_picking()
                int id() const { return data[0] + (data[1] << 8) + (data[2] << 16) - 1; }
            };

            std::vector<Pixel> frame(px_count);
            glsafe(::glReadPixels(left, top, width, height, GL_RGBA, GL_UNSIGNED_BYTE, (void*)frame.data()));

            tbb::spin_mutex mutex;
            tbb::parallel_for(tbb::blocked_range<size_t>(0, frame.size(), (size_t)width),
                [this, &frame, &idxs, &mutex](const tbb::blocked_range<size_t>& range) {
                for (size_t i = range.begin(); i < range.end(); ++i)
                	if (frame[i].valid()) {
                    	int volume_id = frame[i].id();
                    	if (0 <= volume_id && volume_id < (int)m_volumes.volumes.size()) {
                        	mutex.lock();
                        	idxs.insert(volume_id);
                        	mutex.unlock();
                    	}
                	}
            });
#else
            std::vector<GLubyte> frame(4 * px_count);
            glsafe(::glReadPixels(left, top, width, height, GL_RGBA, GL_UNSIGNED_BYTE, (void*)frame.data()));

            for (int i = 0; i < px_count; ++i) {
                int px_id = 4 * i;
                int volume_id = frame[px_id] + (frame[px_id + 1] << 8) + (frame[px_id + 2] << 16);
                if (0 <= volume_id && volume_id < (int)m_volumes.volumes.size())
                    idxs.insert(volume_id);
            }
#endif // USE_PARALLEL
            if (camera != &main_camera)
                main_camera.apply_viewport();

            if (framebuffers_type == OpenGLManager::EFramebufferType::Arb) {
                glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, 0));
                if (render_depth != 0)
                    glsafe(::glDeleteRenderbuffers(1, &render_depth));
                if (render_fbo != 0)
                    glsafe(::glDeleteFramebuffers(1, &render_fbo));
            }
            else if (framebuffers_type == OpenGLManager::EFramebufferType::Ext) {
                glsafe(::glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0));
                if (render_depth != 0)
                    glsafe(::glDeleteRenderbuffersEXT(1, &render_depth));
                if (render_fbo != 0)
                    glsafe(::glDeleteFramebuffersEXT(1, &render_fbo));
            }

            if (render_tex != 0)
                glsafe(::glDeleteTextures(1, &render_tex));
    }

    m_hover_volume_idxs.assign(idxs.begin(), idxs.end());
    _update_volumes_hover_state();
}

void GLCanvas3D::_render_background()
{
    bool use_error_color = false;
    if (wxGetApp().is_editor()) {
        use_error_color = m_dynamic_background_enabled &&
        (current_printer_technology() != ptSLA || !m_volumes.empty());

        if (s_multiple_beds.is_autoslicing()) {
            use_error_color &= std::any_of(
                s_print_statuses.begin(),
                s_print_statuses.end(),
                [](const PrintStatus status){
                    return status == PrintStatus::toolpath_outside;
                }
            );
        } else {
            if (!m_volumes.empty())
                use_error_color &= _is_any_volume_outside().first;
            else
                use_error_color &= m_gcode_viewer.has_data() && !m_gcode_viewer.is_contained_in_bed();
        }
    }

    // Draws a bottom to top gradient over the complete screen.
    glsafe(::glDisable(GL_DEPTH_TEST));

    const ColorRGBA top_color = use_error_color ? ERROR_BG_LIGHT_COLOR : DEFAULT_BG_LIGHT_COLOR;
    const ColorRGBA bottom_color = use_error_color ? ERROR_BG_DARK_COLOR : DEFAULT_BG_DARK_COLOR;

    if (!m_background.is_initialized()) {
        m_background.reset();

        GLModel::Geometry init_data;
        init_data.format = { GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P2T2 };
        init_data.reserve_vertices(4);
        init_data.reserve_indices(6);

        // vertices
        init_data.add_vertex(Vec2f(-1.0f, -1.0f), Vec2f(0.0f, 0.0f));
        init_data.add_vertex(Vec2f(1.0f, -1.0f),  Vec2f(1.0f, 0.0f));
        init_data.add_vertex(Vec2f(1.0f, 1.0f),   Vec2f(1.0f, 1.0f));
        init_data.add_vertex(Vec2f(-1.0f, 1.0f),  Vec2f(0.0f, 1.0f));

        // indices
        init_data.add_triangle(0, 1, 2);
        init_data.add_triangle(2, 3, 0);

        m_background.init_from(std::move(init_data));
    }

    GLShaderProgram* shader = wxGetApp().get_shader("background");
    if (shader != nullptr) {
        shader->start_using();
        shader->set_uniform("top_color", top_color);
        shader->set_uniform("bottom_color", bottom_color);
        m_background.render();
        shader->stop_using();
    }

    glsafe(::glEnable(GL_DEPTH_TEST));
}

void GLCanvas3D::_render_bed(const Transform3d& view_matrix, const Transform3d& projection_matrix, bool bottom)
{
    float scale_factor = 1.0;
#if ENABLE_RETINA_GL
    scale_factor = m_retina_helper->get_scale_factor();
#endif // ENABLE_RETINA_GL

    bool show_texture = ! bottom ||
            (m_gizmos.get_current_type() != GLGizmosManager::FdmSupports
          && m_gizmos.get_current_type() != GLGizmosManager::SlaSupports
          && m_gizmos.get_current_type() != GLGizmosManager::Hollow
          && m_gizmos.get_current_type() != GLGizmosManager::Seam
          && m_gizmos.get_current_type() != GLGizmosManager::MmSegmentation
          && m_gizmos.get_current_type() != GLGizmosManager::FuzzySkin);

    m_bed.render(*this, view_matrix, projection_matrix, bottom, scale_factor, show_texture);
}

void GLCanvas3D::_render_bed_axes()
{
    m_bed.render_axes();
}

void GLCanvas3D::_render_bed_for_picking(const Transform3d& view_matrix, const Transform3d& projection_matrix, bool bottom)
{
    float scale_factor = 1.0;
#if ENABLE_RETINA_GL
    scale_factor = m_retina_helper->get_scale_factor();
#endif // ENABLE_RETINA_GL

    m_bed.render_for_picking(*this, view_matrix, projection_matrix, bottom, scale_factor);
}

void GLCanvas3D::_render_objects(GLVolumeCollection::ERenderType type)
{
    if (m_volumes.empty())
        return;

    glsafe(::glEnable(GL_DEPTH_TEST));

    m_camera_clipping_plane = m_gizmos.get_clipping_plane();

    if (m_picking_enabled)
        // Update the layer editing selection to the first object selected, update the current object maximum Z.
        m_layers_editing.select_object(*m_model, this->is_layers_editing_enabled() ? m_selection.get_object_idx() : -1);

    if (const BuildVolume &build_volume = m_bed.build_volume(); build_volume.valid()) {
      const Vec3d bed_offset = s_multiple_beds.get_bed_translation(s_multiple_beds.get_active_bed());
      switch (build_volume.type()) {
        case BuildVolume::Type::Rectangle: {
            const BoundingBox3Base<Vec3d> bed_bb = build_volume.bounding_volume().inflated(BuildVolume::SceneEpsilon);
            m_volumes.set_print_volume({ 0, // rectangle
                { float(bed_bb.min.x() + bed_offset.x()), float(bed_bb.min.y() + bed_offset.y()),
                  float(bed_bb.max.x() + bed_offset.x()), float(bed_bb.max.y() + bed_offset.y()) },
                { float(0.0 + bed_offset.z()), float(build_volume.max_print_height() + bed_offset.z()) } });
            break;
        }
        case BuildVolume::Type::Circle: {
            m_volumes.set_print_volume({ 1, // circle
                { unscaled<float>(build_volume.circle().center.x() + bed_offset.x()),
                  unscaled<float>(build_volume.circle().center.y() + bed_offset.y()),
                  unscaled<float>(build_volume.circle().radius + BuildVolume::SceneEpsilon), 0.0f },
                { float(0.0 + bed_offset.z()), float(build_volume.max_print_height() + bed_offset.z() + BuildVolume::SceneEpsilon) } });
            break;
        }
        default:
        case BuildVolume::Type::Convex:
        case BuildVolume::Type::Custom: {
            m_volumes.set_print_volume({ static_cast<int>(type),
                { -FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX },
                { -FLT_MAX, FLT_MAX } }
            );
        }
        }
        if (m_requires_check_outside_state) {
            check_volumes_outside_state(m_volumes, nullptr);
            m_requires_check_outside_state = false;
        }
    }

    if (m_use_clipping_planes)
        m_volumes.set_z_range(-m_clipping_planes[0].get_data()[3], m_clipping_planes[1].get_data()[3]);
    else
        m_volumes.set_z_range(-FLT_MAX, FLT_MAX);

    m_volumes.set_clipping_plane(m_camera_clipping_plane.get_data());
    m_volumes.set_show_sinking_contours(! m_gizmos.is_hiding_instances());
    m_volumes.set_show_non_manifold_edges(!m_gizmos.is_hiding_instances() && m_gizmos.get_current_type() != GLGizmosManager::Simplify);

    const Camera& camera = wxGetApp().plater()->get_camera();
    auto trafo = camera.get_view_matrix();
    if (current_printer_technology() == ptSLA && wxGetApp().plater()->is_preview_shown()) {
        trafo.translate(s_multiple_beds.get_bed_translation(s_multiple_beds.get_active_bed()));
    }

    GLShaderProgram* shader = wxGetApp().get_shader("gouraud");
    if (shader != nullptr) {
        shader->start_using();

        switch (type)
        {
        default:
        case GLVolumeCollection::ERenderType::Opaque:
        {
            if (m_picking_enabled && !m_gizmos.is_dragging() && m_layers_editing.is_enabled() && (m_layers_editing.last_object_id != -1) && (m_layers_editing.object_max_z() > 0.0f)) {
                int object_id = m_layers_editing.last_object_id;
                
                m_volumes.render(type, false, trafo, camera.get_projection_matrix(), [object_id](const GLVolume& volume) {
                    // Which volume to paint without the layer height profile shader?
                    return volume.is_active && (volume.is_modifier || volume.composite_id.object_id != object_id);
                    });
                // Let LayersEditing handle rendering of the active object using the layer height profile shader.
                m_layers_editing.render_volumes(*this, m_volumes);
            }
            else {
                // do not cull backfaces to show broken geometry, if any
                const Camera& camera = wxGetApp().plater()->get_camera();
                m_volumes.render(type, m_picking_enabled, trafo, camera.get_projection_matrix(), [this](const GLVolume& volume) {
                    return (m_render_sla_auxiliaries || volume.composite_id.volume_id >= 0);
                    });
            }

            // In case a painting gizmo is open, it should render the painted triangles
            // before transparent objects are rendered. Otherwise they would not be
            // visible when inside modifier meshes etc.
            {
                GLGizmosManager& gm = get_gizmos_manager();
//                GLGizmosManager::EType type = gm.get_current_type();
                if (dynamic_cast<GLGizmoPainterBase*>(gm.get_current())) {
                    shader->stop_using();
                    gm.render_painter_gizmo();
                    shader->start_using();
                }
            }
            break;
        }
        case GLVolumeCollection::ERenderType::Transparent:
        {
            const Camera& camera = wxGetApp().plater()->get_camera();
            m_volumes.render(type, false, trafo, camera.get_projection_matrix());
            break;
        }
        }
        shader->stop_using();
    }

    m_camera_clipping_plane = ClippingPlane::ClipsNothing();
}

void GLCanvas3D::_render_selection()
{
    float scale_factor = 1.0;
#if ENABLE_RETINA_GL
    scale_factor = m_retina_helper->get_scale_factor();
#endif // ENABLE_RETINA_GL

    if (!m_gizmos.is_running())
        m_selection.render(scale_factor);

#if ENABLE_MATRICES_DEBUG
    m_selection.render_debug_window();
#endif // ENABLE_MATRICES_DEBUG
}

void GLCanvas3D::_render_sequential_clearance()
{
    if (!_is_sequential_print_enabled())
        return;

    if (m_layers_editing.is_enabled())
        return;

    switch (m_gizmos.get_current_type())
    {
    case GLGizmosManager::EType::Flatten:
    case GLGizmosManager::EType::Cut:
    case GLGizmosManager::EType::MmSegmentation:
    case GLGizmosManager::EType::Measure:
    case GLGizmosManager::EType::Emboss:
    case GLGizmosManager::EType::Simplify:
    case GLGizmosManager::EType::FdmSupports:
    case GLGizmosManager::EType::Seam:
    case GLGizmosManager::EType::FuzzySkin: { return; }
    default: { break; }
    }
 
    m_sequential_print_clearance.render();
}


bool GLCanvas3D::check_toolbar_icon_size(float init_scale, float& new_scale_to_save, bool is_custom, int counter/* = 3*/)
{
    const Size cnv_size = get_canvas_size();

#if ENABLE_RETINA_GL
    float max_scale = m_retina_helper->get_scale_factor();
#else
    float max_scale = 0.1f * wxGetApp().em_unit();
#endif // ENABLE_RETINA_GL

    float scale = init_scale * max_scale;

    int size = int(GLToolbar::Default_Icons_Size * scale);
    int gizmo_size = int(GLGizmosManager::Default_Icons_Size * scale);

    // Set current scale for all top toolbars. It will be used for next calculations

    GLToolbar& collapse_toolbar = wxGetApp().plater()->get_collapse_toolbar();
    GLToolbar& view_toolbar = wxGetApp().plater()->get_view_toolbar();

    if (!is_approx(scale, m_main_toolbar.get_scale(), 0.015f)) {
        m_main_toolbar.set_scale(scale);
        m_undoredo_toolbar.set_scale(scale);
        collapse_toolbar.set_scale(scale);
        view_toolbar.set_scale(scale);
        m_gizmos.set_overlay_scale(scale);

        view_toolbar.set_icons_size(gizmo_size);
    }

    const float top_tb_width = m_main_toolbar.get_width() + m_undoredo_toolbar.get_width() + collapse_toolbar.get_width();
    float items_cnt = float(m_main_toolbar.get_visible_items_cnt() + m_undoredo_toolbar.get_visible_items_cnt() + collapse_toolbar.get_visible_items_cnt());
    const float noitems_width = top_tb_width - float(size) * items_cnt; // width of separators and borders in top toolbars 
    items_cnt += 1.6f; // +1.6 means a place for some minimal margin between toolbars

    // calculate scale needed for items in all top toolbars
    // the std::max() is there because on some Linux dialects/virtual machines this code is called when the canvas has not been properly initialized yet,
    // leading to negative values for the scale.
    // See: https://github.com/prusa3d/PrusaSlicer/issues/8563
    //      https://github.com/supermerill/SuperSlicer/issues/854
    const float new_h_scale = std::max((cnv_size.get_width() - noitems_width), 1.0f) / (items_cnt * GLToolbar::Default_Icons_Size);

    float   gizmos_height   = m_gizmos.get_scaled_total_height();
    int     giz_items_cnt   = m_gizmos.get_selectable_icons_cnt();
    float   noitems_height  = gizmos_height - gizmo_size * giz_items_cnt; // height of separators and borders in gizmos toolbars 

    noitems_height += m_main_toolbar.get_height(); // increase its value to main_toolbar height
    giz_items_cnt += 2; // +2 means a place for view toolbar

    const float new_v_scale = std::max((cnv_size.get_height() - noitems_height), 1.0f) / (giz_items_cnt * GLGizmosManager::Default_Icons_Size);

    // set minimum scale as a auto scale for the toolbars
    float new_scale = std::min(new_h_scale, new_v_scale);

    new_scale_to_save = std::min(new_scale / max_scale, 1.f);

    if (is_custom && new_scale_to_save > init_scale)
        return true; // we need to save new value, so return true

    if (is_approx(init_scale, new_scale_to_save, 0.015f) || counter == 0)
        return true;

    // scale is changed by 1.5% and more
    init_scale = new_scale_to_save;
    counter--;
    return check_toolbar_icon_size(init_scale, new_scale_to_save, is_custom, counter);
}


void GLCanvas3D::_check_and_update_toolbar_icon_scale()
{
    // Don't update a toolbar scale, when we are on a Preview
    if (wxGetApp().plater()->is_preview_shown())
        return;

    bool is_custom;
    const float init_scale = wxGetApp().toolbar_icon_scale(is_custom);
    float new_scale_to_save;
    if (check_toolbar_icon_size(init_scale, new_scale_to_save, is_custom) &&
        !is_approx(init_scale, new_scale_to_save, 0.015f))
        wxGetApp().set_auto_toolbar_icon_scale(new_scale_to_save);
}

void GLCanvas3D::_render_overlays()
{
    glsafe(::glDisable(GL_DEPTH_TEST));

    // main toolbar and undoredo toolbar need to be both updated before rendering because both their sizes are needed
    // to correctly place them
    _check_and_update_toolbar_icon_scale();

    _render_gizmos_overlay();

    _render_main_toolbar();
    _render_undoredo_toolbar();
    _render_collapse_toolbar();
    _render_view_toolbar();

    if (m_layers_editing.last_object_id >= 0 && m_layers_editing.object_max_z() > 0.0f)
        m_layers_editing.render_overlay(*this);

    std::vector<const ModelInstance*> sorted_instances;
    if (_is_sequential_print_enabled()) {
        for (ModelObject* model_object : m_model->objects)
            for (ModelInstance* model_instance : model_object->instances) {
                sorted_instances.emplace_back(model_instance);
            }
    }
    m_labels.render(sorted_instances);
}

#define use_scrolling 1

std::string get_status_text(PrintStatus status) {
    switch(status) {
        case PrintStatus::idle: return _u8L("Unsliced bed");
        case PrintStatus::running: return _u8L("Slicing") + "...";
        case PrintStatus::finished: return _u8L("Sliced bed");
        case PrintStatus::outside: return _u8L("Object at boundary");
        case PrintStatus::invalid: return _u8L("Invalid data");
        case PrintStatus::empty: return _u8L("Empty bed");
        case PrintStatus::toolpath_outside: return _u8L("Toolpath exceeds bounds");
    }
    return {};
}

wchar_t get_raw_status_icon(const PrintStatus status) {
    switch(status) {
        case PrintStatus::finished: return ImGui::PrintFinished;
        case PrintStatus::running: return ImGui::PrintRunning;
        case PrintStatus::idle: return ImGui::PrintIdle;
        case PrintStatus::outside: return ImGui::PrintIdle;
        case PrintStatus::invalid: return ImGui::PrintIdle;
        case PrintStatus::empty: return ImGui::PrintIdle;
        case PrintStatus::toolpath_outside: return ImGui::PrintIdle;
    }
    return ImGui::PrintIdle;
}

std::string get_status_icon(const PrintStatus status) {
    return boost::nowide::narrow(std::wstring{get_raw_status_icon(status)});
}

bool bed_selector_thumbnail(
    const ImVec2 size,
    const ImVec2 padding,
    const float side,
    const float border,
    const float scale,
    const int bed_id,
    const std::optional<PrintStatus> status
) {
    ImGuiWindow* window = GImGui->CurrentWindow;
    const ImVec2 current_position = GImGui->CurrentWindow->DC.CursorPos;
    const ImVec2 state_pos = current_position + ImVec2(3.f * border, side - 20.f * scale);

    const GLuint texture_id = s_bed_selector_thumbnail_texture_ids[bed_id];
    const bool clicked{ImGui::ImageButton(
        (void*)(int64_t)texture_id,
        size - padding,
        ImVec2(0, 1),
        ImVec2(1, 0),
        border
    )};

    if (status) {
        const std::string icon{get_status_icon(*status)};

        window->DrawList->AddText(
            GImGui->Font,
            GImGui->FontSize,
            state_pos,
            ImGui::GetColorU32(ImGuiCol_Text),
            icon.c_str(),
            icon.c_str() + icon.size()
        );
    }

    const ImVec2 id_pos = current_position + ImVec2(3.f * border, 1.5f * border);
    const std::string id = std::to_string(bed_id+1);

    window->DrawList->AddText(
        GImGui->Font,
        GImGui->FontSize * 1.5f,
        id_pos,
        ImGui::GetColorU32(ImGuiCol_Text),
        id.c_str(),
        id.c_str() + id.size()
    );

    return clicked;
}

bool button_with_icon(const wchar_t icon, const std::string& tooltip, bool is_active, const ImVec2 size)
{
    std::string     btn_name = boost::nowide::narrow(std::wstring{ icon });

    ImGuiButtonFlags flags = ImGuiButtonFlags_None;

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(btn_name.c_str());
    const ImFontAtlasCustomRect* const rect = wxGetApp().imgui()->GetTextureCustomRect(icon);
    const ImVec2 label_size = ImVec2(rect->Width, rect->Height);

    ImVec2 pos = window->DC.CursorPos;
    const ImRect bb(pos, pos + size);
    ImGui::ItemSize(size, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id))
        return false;

    if (g.CurrentItemFlags & ImGuiItemFlags_ButtonRepeat)
        flags |= ImGuiButtonFlags_Repeat;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, flags);

    // Render
    const ImU32 col = ImGui::GetColorU32((held && hovered) ? ImGuiPureWrap::COL_ORANGE_DARK : hovered ? ImGuiPureWrap::COL_ORANGE_LIGHT : ImGuiPureWrap::COL_GREY_DARK);
    ImGui::RenderNavHighlight(bb, id);
    ImGui::PushStyleColor(ImGuiCol_Border, is_active ? ImGuiPureWrap::COL_BUTTON_ACTIVE : ImGuiPureWrap::COL_GREY_DARK);
    ImGui::RenderFrame(bb.Min, bb.Max, col, true, style.FrameRounding);
    ImGui::PopStyleColor();

    if (g.LogEnabled)
        ImGui::LogSetNextTextDecoration("[", "]");
    ImGui::RenderTextClipped(bb.Min + style.FramePadding, bb.Max - style.FramePadding, btn_name.c_str(), NULL, &label_size, style.ButtonTextAlign, &bb);

    IMGUI_TEST_ENGINE_ITEM_INFO(id, label, window->DC.LastItemStatusFlags);

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tooltip.c_str());

    return pressed;
}

void Slic3r::GUI::GLCanvas3D::_render_bed_selector()
{
    bool extra_frame{ false };
    static std::array<std::optional<PrintStatus>, MAX_NUMBER_OF_BEDS> previous_print_status;

    if (s_multiple_beds.get_number_of_beds() != 1 && wxGetApp().plater()->is_preview_shown()) {
#if ENABLE_RETINA_GL
        float scale = m_retina_helper->get_scale_factor();
#else
        float scale = 0.1f * wxGetApp().em_unit();
#endif // ENABLE_RETINA_GL

        const float btn_side = 80.f * scale;
        const float btn_border = 2.f * scale;

        const ImVec2 btn_size = ImVec2(btn_side, btn_side);
        const ImVec2 btn_padding = ImVec2(btn_border, btn_border);

        auto render_bed_button = [btn_side, btn_border, btn_size, btn_padding, this, &extra_frame, scale](int i)
        {
            bool inactive = i != s_multiple_beds.get_active_bed() || s_multiple_beds.is_autoslicing();

            ImGui::PushStyleColor(ImGuiCol_Button, ImGuiPureWrap::COL_GREY_DARK);
            ImGui::PushStyleColor(ImGuiCol_Border, inactive ? ImGuiPureWrap::COL_GREY_DARK : ImGuiPureWrap::COL_BUTTON_ACTIVE);

            const PrintStatus print_status{s_print_statuses[i]};

            if (current_printer_technology() == ptFFF) {
                if ( !previous_print_status[i]
                    || print_status != previous_print_status[i]
                ) {
                    extra_frame = true;
                }
                previous_print_status[i] = print_status;
            }

            if (s_bed_selector_thumbnail_changed[i]) {
                extra_frame = true;
                s_bed_selector_thumbnail_changed[i] = false;
            }

            if (
                !is_sliceable(print_status)
            ) {
                ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
            }

            bool clicked = false;
            if (
                !is_sliceable(print_status)
            ) {
                clicked = button_with_icon(
                    ImGui::WarningMarkerDisabled,
                    get_status_text(print_status),
                    !inactive,
                    btn_size + btn_padding
                );
            } else if (print_status == PrintStatus::toolpath_outside) {
                clicked = button_with_icon(
                    ImGui::WarningMarker,
                    get_status_text(print_status),
                    !inactive,
                    btn_size + btn_padding
                );
            } else if (
                i >= int(s_bed_selector_thumbnail_texture_ids.size())
            ) {
                clicked = ImGui::Button(
                    std::to_string(i + 1).c_str(), btn_size + btn_padding
                );
            } else {
                clicked = bed_selector_thumbnail(
                    btn_size,
                    btn_padding,
                    btn_side,
                    btn_border,
                    scale,
                    i,
                    current_printer_technology() == ptFFF ? std::optional{print_status} : std::nullopt
                );
            }

            if (clicked && is_sliceable(print_status))
                select_bed(i, true);

            ImGui::PopStyleColor(2);
            if (
                !is_sliceable(print_status)
            ) {
                ImGui::PopItemFlag();
            }

            if (current_printer_technology() == ptFFF) {
                const std::string status_text{get_status_text(print_status)};
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", status_text.c_str());
                }
            }
        };

        float win_x_pos = get_canvas_size().get_width();

        float right_shift = 0.f;
        if (const Preview* preview = dynamic_cast<Preview*>(m_canvas->GetParent()))
            right_shift = preview->get_layers_slider_width(true);
        if (right_shift == 0.f) {
            GLToolbar& collapse_toolbar = wxGetApp().plater()->get_collapse_toolbar();
#if ENABLE_HACK_GCODEVIEWER_SLOW_ON_MAC
            // When the application is run as GCodeViewer the collapse toolbar is enabled but invisible, as it is renderer
            // outside of the screen
            const bool  is_collapse_btn_shown = wxGetApp().is_editor() ? collapse_toolbar.is_enabled() : false;
#else
            const bool  is_collapse_btn_shown = collapse_toolbar.is_enabled();
#endif // ENABLE_HACK_GCODEVIEWER_SLOW_ON_MAC
            if (is_collapse_btn_shown)
                right_shift = collapse_toolbar.get_width();
        }
        win_x_pos -= right_shift; 

#if use_scrolling
        static float width  { 0.f };
        static float height { 0.f };
        static float v_pos  { 1.f };

        ImGui::SetNextWindowPos({ win_x_pos - scale * 5.f, v_pos }, ImGuiCond_Always, { 1.f, 0.f });
        ImGui::SetNextWindowSize({ width, height });
#else
        ImGuiPureWrap::set_next_window_pos(win_x_pos - scale * 5.f, 1.f, ImGuiCond_Always, 1.f);
#endif
        ImGui::Begin("Bed selector", 0, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);

        ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2());
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2());
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, btn_border);

        if (
            current_printer_technology() == ptFFF &&
            button_with_icon(ImGui::SliceAllBtnIcon, _u8L("Slice all"), s_multiple_beds.is_autoslicing(), btn_size + btn_padding)
        ) {
            if (!s_multiple_beds.is_autoslicing()) {
                s_multiple_beds.start_autoslice([this](int i, bool user) { this->select_bed(i, user); });
                wxGetApp().sidebar().switch_to_autoslicing_mode();
                wxGetApp().plater()->show_autoslicing_action_buttons();
            }
        }

        ImGui::SameLine();

        int beds_num = s_multiple_beds.get_number_of_beds();

        for (int i = 0; i < beds_num; ++i) {
            render_bed_button(i);
            if (i < beds_num - 1)
                ImGui::SameLine();
        }

        ImGui::PopStyleVar(3);

#if use_scrolling
        bool is_legend_visible = is_legend_shown() && !s_multiple_beds.is_autoslicing();
        ImVec2 win_size = ImGui::GetCurrentWindow()->ContentSizeIdeal + 
                          ImGui::GetCurrentWindow()->WindowPadding * 2.f + 
                          ImGui::GetCurrentWindow()->ScrollbarSizes + 
                          ImVec2(0.f, is_legend_visible ? ImGui::GetCurrentWindow()->TitleBarHeight() : 0.f);

        if (!is_approx(height, win_size.y)) {
            height = win_size.y;
            wxGetApp().imgui()->set_requires_extra_frame();
        }
        m_bed_selector_current_height = height;

        float max_width = win_x_pos;
        if (is_legend_visible)
            max_width -= 400.f * scale; // 400.f is used instead of legend width

        if (max_width < height) {
            width = win_x_pos - 5.f * scale;

            v_pos = ImGui::GetCurrentWindow()->CalcFontSize() + GImGui->Style.FramePadding.y * 2.f + 5.f;
            extra_frame = true;
        }
        else {
            if (v_pos > 1.f) {
                v_pos = 1.f;
                extra_frame = true;
            }

            if (win_size.x > max_width) {
                width = max_width;
                extra_frame = true;
            }
            else if (!is_approx(width, win_size.x)) {
                width = win_size.x;
                extra_frame = true;
            }
        }

        if (extra_frame)
            wxGetApp().imgui()->set_requires_extra_frame();
#endif
        ImGui::End();
    }
}

void GLCanvas3D::_render_volumes_for_picking(const Camera& camera) const
{
    GLShaderProgram* shader = wxGetApp().get_shader("flat_clip");
    if (shader == nullptr)
        return;

    // do not cull backfaces to show broken geometry, if any
    glsafe(::glDisable(GL_CULL_FACE));

    const Transform3d& view_matrix = camera.get_view_matrix();
    for (size_t type = 0; type < 2; ++ type) {
        GLVolumeWithIdAndZList to_render = volumes_to_render(m_volumes.volumes, (type == 0) ? GLVolumeCollection::ERenderType::Opaque : GLVolumeCollection::ERenderType::Transparent, view_matrix);
        for (const GLVolumeWithIdAndZ& volume : to_render)
	        if (!volume.first->disabled && (volume.first->composite_id.volume_id >= 0 || m_render_sla_auxiliaries)) {
		        // Object picking mode. Render the object with a color encoding the object index.
                // we reserve color = (0,0,0) for occluders (as the printbed) 
                // so we shift volumes' id by 1 to get the proper color
                const unsigned int id = 1 + volume.second.first;
                volume.first->model.set_color(picking_decode(id));
                shader->start_using();
                shader->set_uniform("view_model_matrix", view_matrix * volume.first->world_matrix());
                shader->set_uniform("projection_matrix", camera.get_projection_matrix());
                shader->set_uniform("volume_world_matrix", volume.first->world_matrix());
                shader->set_uniform("z_range", m_volumes.get_z_range());
                shader->set_uniform("clipping_plane", m_volumes.get_clipping_plane());
                volume.first->render();
                shader->stop_using();
          }
	  }

    glsafe(::glEnable(GL_CULL_FACE));
}

void GLCanvas3D::_render_gizmos_overlay()
{
    m_gizmos.render_overlay();

    if (m_gizmo_highlighter.m_render_arrow)
        m_gizmos.render_arrow(*this, m_gizmo_highlighter.m_gizmo_type);
}

void GLCanvas3D::_render_main_toolbar()
{
    if (!m_main_toolbar.is_enabled())
        return;

    const Size cnv_size = get_canvas_size();
    const float top = 0.5f * (float)cnv_size.get_height();

    GLToolbar& collapse_toolbar = wxGetApp().plater()->get_collapse_toolbar();
    const float collapse_toolbar_width = collapse_toolbar.is_enabled() ? collapse_toolbar.get_width() : 0.0f;
    const float left = -0.5f * (m_main_toolbar.get_width() + m_undoredo_toolbar.get_width() + collapse_toolbar_width);

    m_main_toolbar.set_position(top, left);
    m_main_toolbar.render(*this);
    if (m_toolbar_highlighter.m_render_arrow)
        m_main_toolbar.render_arrow(*this, m_toolbar_highlighter.m_toolbar_item);
}

void GLCanvas3D::_render_undoredo_toolbar()
{
    if (!m_undoredo_toolbar.is_enabled())
        return;

    const Size cnv_size = get_canvas_size();
    const float top = 0.5f * (float)cnv_size.get_height();
    GLToolbar& collapse_toolbar = wxGetApp().plater()->get_collapse_toolbar();
    const float collapse_toolbar_width = collapse_toolbar.is_enabled() ? collapse_toolbar.get_width() : 0.0f;
    const float left = m_main_toolbar.get_width() - 0.5f * (m_main_toolbar.get_width() + m_undoredo_toolbar.get_width() + collapse_toolbar_width);

    m_undoredo_toolbar.set_position(top, left);
    m_undoredo_toolbar.render(*this);
    if (m_toolbar_highlighter.m_render_arrow)
        m_undoredo_toolbar.render_arrow(*this, m_toolbar_highlighter.m_toolbar_item);
}

void GLCanvas3D::_render_collapse_toolbar() const
{
    GLToolbar& collapse_toolbar = wxGetApp().plater()->get_collapse_toolbar();

    const Size cnv_size = get_canvas_size();
    const float band = m_layers_editing.is_enabled() ? (wxGetApp().imgui()->get_style_scaling() * LayersEditing::THICKNESS_BAR_WIDTH) : 0.0;
    const float top  = 0.5f * (float)cnv_size.get_height();
#if ENABLE_HACK_GCODEVIEWER_SLOW_ON_MAC
    // When the application is run as GCodeViewer, render the collapse toolbar outside of the screen
    const float left = wxGetApp().is_gcode_viewer() ? 0.5f * (float)cnv_size.get_width() :
        0.5f * (float)cnv_size.get_width() - collapse_toolbar.get_width() - band;
#else
    const float left = 0.5f * (float)cnv_size.get_width() - collapse_toolbar.get_width() - band;
#endif // ENABLE_HACK_GCODEVIEWER_SLOW_ON_MAC

    collapse_toolbar.set_position(top, left);
    collapse_toolbar.render(*this);
}

void GLCanvas3D::_render_view_toolbar() const
{
    GLToolbar& view_toolbar = wxGetApp().plater()->get_view_toolbar();

    const Size cnv_size = get_canvas_size();
    // places the toolbar on the bottom-left corner of the 3d scene
    const float top = -0.5f * (float)cnv_size.get_height() + view_toolbar.get_height();
    const float left = -0.5f * (float)cnv_size.get_width();
    view_toolbar.set_position(top, left);
    view_toolbar.render(*this);
}

#if ENABLE_SHOW_CAMERA_TARGET
void GLCanvas3D::_render_camera_target()
{
    static const float half_length = 10.0f;

    glsafe(::glDisable(GL_DEPTH_TEST));
#if !SLIC3R_OPENGL_ES
    if (!OpenGLManager::get_gl_info().is_core_profile())
        glsafe(::glLineWidth(2.0f));
#endif // !SLIC3R_OPENGL_ES

    m_camera_target.target = wxGetApp().plater()->get_camera().get_target();

    for (int i = 0; i < 3; ++i) {
        if (!m_camera_target.axis[i].is_initialized()) {
            m_camera_target.axis[i].reset();

            GLModel::Geometry init_data;
            init_data.format = { GLModel::Geometry::EPrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P3 };
            init_data.color = (i == X) ? ColorRGBA::X() : (i == Y) ? ColorRGBA::Y() : ColorRGBA::Z();
            init_data.reserve_vertices(2);
            init_data.reserve_indices(2);

            // vertices
            if (i == X) {
                init_data.add_vertex(Vec3f(-half_length, 0.0f, 0.0f));
                init_data.add_vertex(Vec3f(+half_length, 0.0f, 0.0f));
            }
            else if (i == Y) {
                init_data.add_vertex(Vec3f(0.0f, -half_length, 0.0f));
                init_data.add_vertex(Vec3f(0.0f, +half_length, 0.0f));
            }
            else {
                init_data.add_vertex(Vec3f(0.0f, 0.0f, -half_length));
                init_data.add_vertex(Vec3f(0.0f, 0.0f, +half_length));
            }

            // indices
            init_data.add_line(0, 1);

            m_camera_target.axis[i].init_from(std::move(init_data));
        }
    }

#if SLIC3R_OPENGL_ES
    GLShaderProgram* shader = wxGetApp().get_shader("dashed_lines");
#else
    GLShaderProgram* shader = OpenGLManager::get_gl_info().is_core_profile() ? wxGetApp().get_shader("dashed_thick_lines") : wxGetApp().get_shader("flat");
#endif // SLIC3R_OPENGL_ES
    if (shader != nullptr) {
        shader->start_using();
        const Camera& camera = wxGetApp().plater()->get_camera();
        shader->set_uniform("view_model_matrix", camera.get_view_matrix() * Geometry::translation_transform(m_camera_target.target));
        shader->set_uniform("projection_matrix", camera.get_projection_matrix());
#if !SLIC3R_OPENGL_ES
        if (OpenGLManager::get_gl_info().is_core_profile()) {
#endif // !SLIC3R_OPENGL_ES
            const std::array<int, 4>& viewport = camera.get_viewport();
            shader->set_uniform("viewport_size", Vec2d(double(viewport[2]), double(viewport[3])));
            shader->set_uniform("width", 0.5f);
            shader->set_uniform("gap_size", 0.0f);
#if !SLIC3R_OPENGL_ES
        }
#endif // !SLIC3R_OPENGL_ES
        for (int i = 0; i < 3; ++i) {
            m_camera_target.axis[i].render();
        }
        shader->stop_using();
    }
}

void GLCanvas3D::_render_camera_target_validation_box()
{
    const BoundingBoxf3& curr_box = m_target_validation_box.get_bounding_box();
    const BoundingBoxf3 camera_box = wxGetApp().plater()->get_camera().get_target_validation_box();

    if (!m_target_validation_box.is_initialized() || !is_approx(camera_box.min, curr_box.min) || !is_approx(camera_box.max, curr_box.max)) {
        m_target_validation_box.reset();

        const Vec3f b_min = camera_box.min.cast<float>();
        const Vec3f b_max = camera_box.max.cast<float>();

        GLModel::Geometry init_data;
        init_data.format = { GLModel::Geometry::EPrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P3 };
        init_data.reserve_vertices(12);
        init_data.reserve_indices(12);

        // vertices
        init_data.add_vertex(Vec3f(b_min.x(), b_min.y(), b_min.z()));
        init_data.add_vertex(Vec3f(b_max.x(), b_min.y(), b_min.z()));
        init_data.add_vertex(Vec3f(b_min.x(), b_min.y(), b_min.z()));
        init_data.add_vertex(Vec3f(b_min.x(), b_max.y(), b_min.z()));
        init_data.add_vertex(Vec3f(b_min.x(), b_min.y(), b_min.z()));
        init_data.add_vertex(Vec3f(b_min.x(), b_min.y(), b_max.z()));
        init_data.add_vertex(Vec3f(b_max.x(), b_max.y(), b_max.z()));
        init_data.add_vertex(Vec3f(b_min.x(), b_max.y(), b_max.z()));
        init_data.add_vertex(Vec3f(b_max.x(), b_max.y(), b_max.z()));
        init_data.add_vertex(Vec3f(b_max.x(), b_min.y(), b_max.z()));
        init_data.add_vertex(Vec3f(b_max.x(), b_max.y(), b_max.z()));
        init_data.add_vertex(Vec3f(b_max.x(), b_max.y(), b_min.z()));

        // indices
        for (unsigned int i = 0; i < 12; ++i) {
            init_data.add_index(i);
        }

        m_target_validation_box.init_from(std::move(init_data));
    }

    glsafe(::glEnable(GL_DEPTH_TEST));

#if SLIC3R_OPENGL_ES
    GLShaderProgram* shader = wxGetApp().get_shader("dashed_lines");
#else
    if (!OpenGLManager::get_gl_info().is_core_profile())
        glsafe(::glLineWidth(2.0f));

    GLShaderProgram* shader = OpenGLManager::get_gl_info().is_core_profile() ? wxGetApp().get_shader("dashed_thick_lines") : wxGetApp().get_shader("flat");
#endif // SLIC3R_OPENGL_ES
    if (shader == nullptr)
        return;

    shader->start_using();
    const Camera& camera = wxGetApp().plater()->get_camera();
    shader->set_uniform("view_model_matrix", camera.get_view_matrix());
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());
#if !SLIC3R_OPENGL_ES
    if (OpenGLManager::get_gl_info().is_core_profile()) {
#endif // !SLIC3R_OPENGL_ES
        const std::array<int, 4>& viewport = camera.get_viewport();
        shader->set_uniform("viewport_size", Vec2d(double(viewport[2]), double(viewport[3])));
        shader->set_uniform("width", 1.5f);
        shader->set_uniform("gap_size", 0.0f);
#if !SLIC3R_OPENGL_ES
    }
#endif // !SLIC3R_OPENGL_ES
    m_target_validation_box.set_color(to_rgba(ColorRGB::WHITE()));
    m_target_validation_box.render();
    shader->stop_using();
}
#endif // ENABLE_SHOW_CAMERA_TARGET

static void render_sla_layer_legend(const SLAPrint& print, int layer_idx, int cnv_width, float bed_sel_height)
{
    const std::vector<double>& areas = print.print_statistics().layers_areas;
    const std::vector<double>& times = print.print_statistics().layers_times_running_total;
    const double display_area        = print.printer_config().display_height * print.printer_config().display_width;
    if (layer_idx >= 0 && layer_idx < int(areas.size())) {
        const double area = areas[layer_idx];
        const double time = times[layer_idx] - (layer_idx == 0 ? 0. : times[layer_idx-1]);
        const double time_until_layer = times[layer_idx];
            
        ImGuiWrapper& imgui = *wxGetApp().imgui();
        ImGuiPureWrap::set_next_window_pos(float(cnv_width) - imgui.get_style_scaling() * 5.f, 5.f + bed_sel_height, ImGuiCond_Always, 1.0f, 0.0f);
        ImGui::SetNextWindowBgAlpha(0.6f);

        ImGuiPureWrap::begin(_u8L("Layer statistics"), ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing);
        ImGui::Text("%s", GUI::format(_u8L("Layer area: %1% mm²"), int(0.1 + std::round(area))).c_str());
        int area_percent_int = int(std::round(100. * area/display_area));
        ImGui::Text("%s", GUI::format(_u8L("Area fill: %1% %%"), area_percent_int == 0 ? "<1" : std::to_string(area_percent_int)).c_str());
        ImGui::Separator();
        ImGui::Text("%s", GUI::format(_u8L("Layer time: %1%"), get_time_dhms(time)).c_str());
        std::string buffer_str = _u8L("Time since start: %1%"); 
        ImGui::Text("%s", GUI::format(buffer_str, get_time_dhms(time_until_layer)).c_str());

        // The dummy control below uses the assumption that the total time string will be the longest
        // and forces the width of the window large enough so it does not resize depending on the current value.
        ImGui::Dummy(ImVec2(ImGui::CalcTextSize(GUI::format(buffer_str, get_time_dhms(82799)).c_str()).x, 0.));
        ImGuiPureWrap::end();
    }
}



void GLCanvas3D::_render_sla_slices()
{
    if (!m_use_clipping_planes || current_printer_technology() != ptSLA)
        return;

    const SLAPrint* print = this->sla_print();
    const PrintObjects& print_objects = print->objects();
    if (print_objects.empty())
        // nothing to render, return
        return;

    if (print->finished()) {
        double slider_width = 0.;
        if (const Preview* preview = dynamic_cast<Preview*>(m_canvas->GetParent()))
            slider_width = preview->get_layers_slider_width();
        render_sla_layer_legend(*print, m_layer_slider_index, get_canvas_size().get_width() - slider_width, m_bed_selector_current_height);
    }

    double clip_min_z = -m_clipping_planes[0].get_data()[3];
    double clip_max_z = m_clipping_planes[1].get_data()[3];
    for (unsigned int i = 0; i < (unsigned int)print_objects.size(); ++i) {
        const SLAPrintObject* obj = print_objects[i];

        if (!obj->is_step_done(slaposSliceSupports))
            continue;

        SlaCap::ObjectIdToModelsMap::iterator it_caps_bottom = m_sla_caps[0].triangles.find(i);
        SlaCap::ObjectIdToModelsMap::iterator it_caps_top = m_sla_caps[1].triangles.find(i);
        {
            if (it_caps_bottom == m_sla_caps[0].triangles.end())
                it_caps_bottom = m_sla_caps[0].triangles.emplace(i, SlaCap::Triangles()).first;
            if (!m_sla_caps[0].matches(clip_min_z)) {
                m_sla_caps[0].z = clip_min_z;
                it_caps_bottom->second.object.reset();
                it_caps_bottom->second.supports.reset();
            }
            if (it_caps_top == m_sla_caps[1].triangles.end())
                it_caps_top = m_sla_caps[1].triangles.emplace(i, SlaCap::Triangles()).first;
            if (!m_sla_caps[1].matches(clip_max_z)) {
                m_sla_caps[1].z = clip_max_z;
                it_caps_top->second.object.reset();
                it_caps_top->second.supports.reset();
            }
        }
        GLModel& bottom_obj_triangles = it_caps_bottom->second.object;
        GLModel& bottom_sup_triangles = it_caps_bottom->second.supports;
        GLModel& top_obj_triangles = it_caps_top->second.object;
        GLModel& top_sup_triangles = it_caps_top->second.supports;

        auto init_model = [](GLModel& model, const Pointf3s& triangles, const ColorRGBA& color) {
            GLModel::Geometry init_data;
            init_data.format = { GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3 };
            init_data.reserve_vertices(triangles.size());
            init_data.reserve_indices(triangles.size() / 3);
            init_data.color = color;

            unsigned int vertices_count = 0;
            for (const Vec3d& v : triangles) {
                init_data.add_vertex((Vec3f)v.cast<float>());
                ++vertices_count;
                if (vertices_count % 3 == 0)
                    init_data.add_triangle(vertices_count - 3, vertices_count - 2, vertices_count - 1);
            }

            if (!init_data.is_empty())
                model.init_from(std::move(init_data));
        };

        if ((!bottom_obj_triangles.is_initialized() || !bottom_sup_triangles.is_initialized() ||
            !top_obj_triangles.is_initialized() || !top_sup_triangles.is_initialized()) && !obj->get_slice_index().empty()) {
            double layer_height         = print->default_object_config().layer_height.value;
            double initial_layer_height = print->material_config().initial_layer_height.value;
            bool   left_handed          = obj->is_left_handed();

            coord_t key_zero = obj->get_slice_index().front().print_level();
            // Slice at the center of the slab starting at clip_min_z will be rendered for the lower plane.
            coord_t key_low  = coord_t((clip_min_z - initial_layer_height + layer_height) / SCALING_FACTOR) + key_zero;
            // Slice at the center of the slab ending at clip_max_z will be rendered for the upper plane.
            coord_t key_high = coord_t((clip_max_z - initial_layer_height) / SCALING_FACTOR) + key_zero;

            const SliceRecord& slice_low  = obj->closest_slice_to_print_level(key_low, coord_t(SCALED_EPSILON));
            const SliceRecord& slice_high = obj->closest_slice_to_print_level(key_high, coord_t(SCALED_EPSILON));

            // Offset to avoid OpenGL Z fighting between the object's horizontal surfaces and the triangluated surfaces of the cuts.
            const double plane_shift_z = 0.002;

            if (slice_low.is_valid()) {
                const ExPolygons& obj_bottom = slice_low.get_slice(soModel);
                const ExPolygons& sup_bottom = slice_low.get_slice(soSupport);
                // calculate model bottom cap
                if (!bottom_obj_triangles.is_initialized() && !obj_bottom.empty())
                    init_model(bottom_obj_triangles, triangulate_expolygons_3d(obj_bottom, clip_min_z - plane_shift_z, !left_handed), { 1.0f, 0.37f, 0.0f, 1.0f });
                // calculate support bottom cap
                if (!bottom_sup_triangles.is_initialized() && !sup_bottom.empty())
                    init_model(bottom_sup_triangles, triangulate_expolygons_3d(sup_bottom, clip_min_z - plane_shift_z, !left_handed), { 1.0f, 0.0f, 0.37f, 1.0f });
            }

            if (slice_high.is_valid()) {
                const ExPolygons& obj_top = slice_high.get_slice(soModel);
                const ExPolygons& sup_top = slice_high.get_slice(soSupport);
                // calculate model top cap
                if (!top_obj_triangles.is_initialized() && !obj_top.empty())
                    init_model(top_obj_triangles, triangulate_expolygons_3d(obj_top, clip_max_z + plane_shift_z, left_handed), { 1.0f, 0.37f, 0.0f, 1.0f });
                // calculate support top cap
                if (!top_sup_triangles.is_initialized() && !sup_top.empty())
                    init_model(top_sup_triangles, triangulate_expolygons_3d(sup_top, clip_max_z + plane_shift_z, left_handed), { 1.0f, 0.0f, 0.37f, 1.0f });
            }
        }

        GLShaderProgram* shader = wxGetApp().get_shader("flat");
        if (shader != nullptr) {
            shader->start_using();

            for (const SLAPrintObject::Instance& inst : obj->instances()) {
                const Camera& camera = wxGetApp().plater()->get_camera();
                Transform3d view_model_matrix = camera.get_view_matrix() *
                    Geometry::translation_transform(s_multiple_beds.get_bed_translation(s_multiple_beds.get_active_bed())) *
                    Geometry::translation_transform({ unscale<double>(inst.shift.x()), unscale<double>(inst.shift.y()), 0.0 }) *
                    Geometry::rotation_transform(inst.rotation * Vec3d::UnitZ());
                if (obj->is_left_handed())
                    view_model_matrix = view_model_matrix * Geometry::scale_transform({ -1.0f, 1.0f, 1.0f });

                shader->set_uniform("view_model_matrix", view_model_matrix);
                shader->set_uniform("projection_matrix", camera.get_projection_matrix());

                bottom_obj_triangles.render();
                top_obj_triangles.render();
                bottom_sup_triangles.render();
                top_sup_triangles.render();
            }

            shader->stop_using();
        }
    }
}

void GLCanvas3D::_update_volumes_hover_state()
{
    // skip update if the Gizmo Measure is active
    if (m_gizmos.get_current_type() == GLGizmosManager::Measure)
        return;

    for (GLVolume* v : m_volumes.volumes) {
        v->hover = GLVolume::HS_None;
    }

    if (m_hover_volume_idxs.empty())
        return;

    const bool ctrl_pressed  = wxGetKeyState(WXK_CONTROL);
    const bool shift_pressed = wxGetKeyState(WXK_SHIFT);
    const bool alt_pressed   = wxGetKeyState(WXK_ALT);

    if (alt_pressed && (shift_pressed || ctrl_pressed)) {
        // illegal combinations of keys
        m_hover_volume_idxs.clear();
        return;
    }

    bool hover_modifiers_only = true;
    for (int i : m_hover_volume_idxs) {
        if (!m_volumes.volumes[i]->is_modifier) {
            hover_modifiers_only = false;
            break;
        }
    }

    std::set<std::pair<int, int>> hover_instances;
    for (int i : m_hover_volume_idxs) {
        const GLVolume& v = *m_volumes.volumes[i];
        hover_instances.insert(std::make_pair(v.object_idx(), v.instance_idx()));
    }

    bool hover_from_single_instance = hover_instances.size() == 1;

    if (hover_modifiers_only && !hover_from_single_instance) {
        // do not allow to select volumes from different instances
        m_hover_volume_idxs.clear();
        return;
    }

    for (int i : m_hover_volume_idxs) {
        GLVolume& volume = *m_volumes.volumes[i];
        if (volume.hover != GLVolume::HS_None)
            continue;

        const bool deselect = volume.selected && ((shift_pressed && m_rectangle_selection.is_empty()) || (alt_pressed && !m_rectangle_selection.is_empty()));
        const bool select   = !volume.selected && (m_rectangle_selection.is_empty() || (shift_pressed && !m_rectangle_selection.is_empty()));

        if (select || deselect) {
            const bool as_volume =
                volume.is_modifier && hover_from_single_instance && !ctrl_pressed &&
                (
                !deselect ||
                (deselect && !m_selection.is_single_full_instance() && volume.object_idx() == m_selection.get_object_idx() && volume.instance_idx() == m_selection.get_instance_idx())
                );

            if (as_volume)
                volume.hover = deselect ? GLVolume::HS_Deselect : GLVolume::HS_Select;
            else {
                const int object_idx = volume.object_idx();
                const int instance_idx = volume.instance_idx();

                for (GLVolume* v : m_volumes.volumes) {
                    if (v->object_idx() == object_idx && v->instance_idx() == instance_idx)
                        v->hover = deselect ? GLVolume::HS_Deselect : GLVolume::HS_Select;
                }
            }
        }
        else if (volume.selected)
            volume.hover = GLVolume::HS_Hover;
    }
}

void GLCanvas3D::_perform_layer_editing_action(wxMouseEvent* evt)
{
    int object_idx_selected = m_layers_editing.last_object_id;
    if (object_idx_selected == -1)
        return;

    // A volume is selected. Test, whether hovering over a layer thickness bar.
    if (evt != nullptr) {
        const Rect& rect = LayersEditing::get_bar_rect_screen(*this);
        float b = rect.get_bottom();
        m_layers_editing.last_z = m_layers_editing.object_max_z() * (b - evt->GetY() - 1.0f) / (b - rect.get_top());
        m_layers_editing.last_action = 
            evt->ShiftDown() ? (evt->RightIsDown() ? LAYER_HEIGHT_EDIT_ACTION_SMOOTH : LAYER_HEIGHT_EDIT_ACTION_REDUCE) : 
                               (evt->RightIsDown() ? LAYER_HEIGHT_EDIT_ACTION_INCREASE : LAYER_HEIGHT_EDIT_ACTION_DECREASE);
    }

    if (m_layers_editing.state != LayersEditing::Paused) {
        m_layers_editing.adjust_layer_height_profile();
        _refresh_if_shown_on_screen();
    }

    // Automatic action on mouse down with the same coordinate.
    _start_timer();
}

Vec3d GLCanvas3D::_mouse_to_3d(const Point& mouse_pos, const float* z, bool use_ortho)
{
    if (m_canvas == nullptr)
        return Vec3d(DBL_MAX, DBL_MAX, DBL_MAX);

    if (z == nullptr) {
        const SceneRaycaster::HitResult hit = m_scene_raycaster.hit(mouse_pos.cast<double>(), wxGetApp().plater()->get_camera(), nullptr);
        return hit.is_valid() ? hit.position.cast<double>() : _mouse_to_bed_3d(mouse_pos);
    }
    else {
        Camera& camera = wxGetApp().plater()->get_camera();
        const Camera::EType type = camera.get_type();
        const Vec4i viewport(camera.get_viewport().data());
        Transform3d projection_matrix;
        if (use_ortho && type != Camera::EType::Ortho) {
            const double inv_zoom = camera.get_inv_zoom();
            const double left   = -0.5 * inv_zoom * double(viewport[2]);
            const double bottom = -0.5 * inv_zoom * double(viewport[3]);
            const double right  = 0.5 * inv_zoom * double(viewport[2]);
            const double top    = 0.5 * inv_zoom * double(viewport[3]);
            const double near_z = camera.get_near_z();
            const double far_z  = camera.get_far_z();
            const double inv_dx = 1.0 / (right - left);
            const double inv_dy = 1.0 / (top - bottom);
            const double inv_dz = 1.0 / (far_z - near_z);
            projection_matrix.matrix() << 2.0 * near_z * inv_dx, 0.0, (left + right) * inv_dx, 0.0,
                0.0, 2.0 * near_z * inv_dy, (bottom + top) * inv_dy, 0.0,
                0.0, 0.0, -(near_z + far_z) * inv_dz, -2.0 * near_z * far_z * inv_dz,
                0.0, 0.0, -1.0, 0.0;
        }
        else
            projection_matrix = camera.get_projection_matrix();

        Vec3d out;
        igl::unproject(Vec3d(mouse_pos.x(), viewport[3] - mouse_pos.y(), *z), camera.get_view_matrix().matrix(), projection_matrix.matrix(), viewport, out);
        return out;
    }
}

Vec3d GLCanvas3D::_mouse_to_bed_3d(const Point& mouse_pos)
{
    const Linef3 ray = mouse_ray(mouse_pos);
    return (std::abs(ray.unit_vector().z()) < EPSILON) ? ray.a : ray.intersect_plane(0.0);
}

// While it looks like we can call 
// this->reload_scene(true, true)
// the two functions are quite different:
// 1) This function only loads objects, for which the step slaposSliceSupports already finished. Therefore objects outside of the print bed never load.
// 2) This function loads object mesh with the relative scaling correction (the "relative_correction" parameter) was applied,
// 	  therefore the mesh may be slightly larger or smaller than the mesh shown in the 3D scene.
void GLCanvas3D::_load_sla_shells()
{
    const SLAPrint* print = this->sla_print();
    if (print->objects().empty())
        // nothing to render, return
        return;

    auto add_volume = [this](const SLAPrintObject &object, int volume_id, const SLAPrintObject::Instance& instance,
        const indexed_triangle_set& mesh, const ColorRGBA& color, bool outside_printer_detection_enabled) {
        m_volumes.volumes.emplace_back(new GLVolume(color));
        GLVolume& v = *m_volumes.volumes.back();
#if ENABLE_SMOOTH_NORMALS
        v.model.init_from(mesh, true);
#else
        v.model.init_from(mesh);
#endif // ENABLE_SMOOTH_NORMALS
        v.shader_outside_printer_detection_enabled = outside_printer_detection_enabled;
        v.composite_id.volume_id = volume_id;
        v.set_instance_offset(unscale(instance.shift.x(), instance.shift.y(), 0.0));
        v.set_instance_rotation({ 0.0, 0.0, (double)instance.rotation });
        v.set_instance_mirror(X, object.is_left_handed() ? -1. : 1.);
        v.set_convex_hull(TriangleMesh{its_convex_hull(mesh)});
    };

    // adds objects' volumes 
    for (const SLAPrintObject* obj : print->objects()) {
        unsigned int initial_volumes_count = (unsigned int)m_volumes.volumes.size();
        std::shared_ptr<const indexed_triangle_set> m = obj->get_mesh_to_print();
        if (m && !m->empty()) {
            for (const SLAPrintObject::Instance& instance : obj->instances()) {
                add_volume(*obj, 0, instance, *m, GLVolume::MODEL_COLOR[0], true);
                // Set the extruder_id and volume_id to achieve the same color as in the 3D scene when
                // through the update_volumes_colors_by_extruder() call.
                m_volumes.volumes.back()->extruder_id = obj->model_object()->volumes.front()->extruder_id();
                if (auto &tree_mesh = obj->support_mesh().its; !tree_mesh.empty())
                    add_volume(*obj, -int(slaposSupportTree), instance, tree_mesh, GLVolume::SLA_SUPPORT_COLOR, true);
                if (auto &pad_mesh = obj->pad_mesh().its; !pad_mesh.empty())
                    add_volume(*obj, -int(slaposPad), instance, pad_mesh, GLVolume::SLA_PAD_COLOR, false);
            }
        }
        const double shift_z = obj->get_current_elevation();
        for (unsigned int i = initial_volumes_count; i < m_volumes.volumes.size(); ++ i) {
            // apply shift z
            m_volumes.volumes[i]->set_sla_shift_z(shift_z);
        }
    }

    update_volumes_colors_by_extruder();
}

void GLCanvas3D::_set_warning_notification_if_needed(EWarning warning)
{
    _set_current();
    bool show = false;
    if (!m_volumes.empty()) {
        if (current_printer_technology() == ptSLA) {
            const auto [res, volume] = _is_any_volume_outside();
            if (res) {
                if (warning == EWarning::ObjectClashed)
                    show = !volume->is_sla_support();
                else if (warning == EWarning::SlaSupportsOutside)
                    show = volume->is_sla_support();
            }
        }
        else
            show = _is_any_volume_outside().first;
    }
    else {
        if (wxGetApp().is_editor()) {
            if (current_printer_technology() != ptSLA) {
                if (warning == EWarning::ToolpathOutside)
                    show = m_gcode_viewer.has_data() && !m_gcode_viewer.is_contained_in_bed();
                else if (warning == EWarning::GCodeConflict)
                    show = m_gcode_viewer.has_data() && m_gcode_viewer.is_contained_in_bed() && m_gcode_viewer.get_conflict_result().has_value();
            }
        }
    }

    _set_warning_notification(warning, show);
}

void GLCanvas3D::_set_warning_notification(EWarning warning, bool state)
{
    enum ErrorType{
        PLATER_WARNING,
        PLATER_ERROR,
        SLICING_ERROR
    };
    std::string text;
    ErrorType error = ErrorType::PLATER_WARNING;
    switch (warning) {
    case EWarning::ObjectOutside:      text = _u8L("An object outside the print area was detected."); break;
    case EWarning::ToolpathOutside:    text = _u8L("A toolpath outside the print area was detected."); error = ErrorType::SLICING_ERROR; break;
    case EWarning::SlaSupportsOutside: text = _u8L("SLA supports outside the print area were detected."); error = ErrorType::PLATER_ERROR; break;
    case EWarning::SomethingNotShown:  text = _u8L("Some objects are not visible during editing."); break;
    case EWarning::ObjectClashed:
        text = _u8L("An object outside the print area was detected.\n"
            "Resolve the current problem to continue slicing.");
        error = ErrorType::PLATER_ERROR;
        break;
    case EWarning::GCodeConflict: {
        const ConflictResultOpt& conflict_result = m_gcode_viewer.get_conflict_result();
        if (!conflict_result.has_value()) { break; }
        std::string objName1 = conflict_result->_objName1;
        std::string objName2 = conflict_result->_objName2;
        double      height = conflict_result->_height;
        int         layer = conflict_result->layer;
        // TRN %3% is name of Object1, %4% is name of Object2
        text = format(_u8L("Conflicts in G-code paths have been detected at layer %1%, z=%2$.2f mm. Please reposition the conflicting objects (%3% <-> %4%) further apart."), 
                      layer, height, objName1, objName2);
        error = ErrorType::SLICING_ERROR;
        break;
    }
    }
    auto& notification_manager = *wxGetApp().plater()->get_notification_manager();

    const ConflictResultOpt& conflict_result = m_gcode_viewer.get_conflict_result();
    if (warning == EWarning::GCodeConflict) {
        if (conflict_result.has_value()) {
            const PrintObject* obj2 = reinterpret_cast<const PrintObject*>(conflict_result->_obj2);
            auto     mo = obj2->model_object();
            ObjectID id = mo->id();
            int layer_id = conflict_result->layer;
            auto     action_fn = [id, layer_id](wxEvtHandler*) {
                auto& objects = wxGetApp().model().objects;
                auto  iter = id.id ? std::find_if(objects.begin(), objects.end(), [id](auto o) { return o->id() == id; }) : objects.end();
                if (iter != objects.end()) {
                    const unsigned int obj_idx = std::distance(objects.begin(), iter);
                    wxGetApp().CallAfter([obj_idx, layer_id]() {
                        wxGetApp().plater()->set_preview_layers_slider_values_range(0, layer_id - 1);
                        wxGetApp().plater()->select_view_3D("3D");
                        wxGetApp().plater()->canvas3D()->reset_all_gizmos();
                        wxGetApp().plater()->canvas3D()->get_selection().add_object(obj_idx, true);
                        wxGetApp().obj_list()->update_selections();
                    });
                }
                return false;
            };
            auto hypertext = _u8L("Jump to");
            hypertext += std::string(" [") + mo->name + "]";
            notification_manager.push_notification(NotificationType::SlicingError, NotificationManager::NotificationLevel::ErrorNotificationLevel,
                _u8L("ERROR:") + "\n" + text, hypertext, action_fn);
        }
        else
            notification_manager.close_slicing_error_notification(text);

        return;
    }

    switch (error)
    {
    case PLATER_WARNING:
        if (state)
            notification_manager.push_plater_warning_notification(text);
        else
            notification_manager.close_plater_warning_notification(text);
        break;
    case PLATER_ERROR:
        if (state)
            notification_manager.push_plater_error_notification(text);
        else
            notification_manager.close_plater_error_notification(text);
        break;
    case SLICING_ERROR:
        if (state)
            notification_manager.push_slicing_error_notification(text);
        else
            notification_manager.close_slicing_error_notification(text);
        break;
    default:
        break;
    }
}

std::pair<bool, const GLVolume*> GLCanvas3D::_is_any_volume_outside() const
{
    for (const GLVolume* volume : m_volumes.volumes) {
        if (volume != nullptr && volume->is_outside)
            return std::make_pair(true, volume);
    }

    return std::make_pair(false, nullptr);
}

bool GLCanvas3D::_is_sequential_print_enabled() const
{
    return current_printer_technology() == ptFFF && fff_print()->config().complete_objects;
}

void GLCanvas3D::_update_selection_from_hover()
{
    bool ctrl_pressed = wxGetKeyState(WXK_CONTROL);
    bool selection_changed = false;

    if (m_hover_volume_idxs.empty()) {
        if (!ctrl_pressed && m_rectangle_selection.get_state() == GLSelectionRectangle::EState::Select) {
            selection_changed = ! m_selection.is_empty();
            m_selection.remove_all();
        }
    }

    GLSelectionRectangle::EState state = m_rectangle_selection.get_state();

    bool hover_modifiers_only = true;
    for (int i : m_hover_volume_idxs) {
        if (!m_volumes.volumes[i]->is_modifier) {
            hover_modifiers_only = false;
            break;
        }
    }

    if (!m_rectangle_selection.is_empty()) {
        if (state == GLSelectionRectangle::EState::Select) {
            bool contains_all = true;
            for (int i : m_hover_volume_idxs) {
                if (!m_selection.contains_volume((unsigned int)i)) {
                    contains_all = false;
                    break;
                }
            }

            // the selection is going to be modified (Add)
            if (!contains_all) {
                wxGetApp().plater()->take_snapshot(_L("Selection-Add from rectangle"), UndoRedo::SnapshotType::Selection);
                selection_changed = true;
            }
        }
        else {
            bool contains_any = false;
            for (int i : m_hover_volume_idxs) {
                if (m_selection.contains_volume((unsigned int)i)) {
                    contains_any = true;
                    break;
                }
            }

            // the selection is going to be modified (Remove)
            if (contains_any) {
                wxGetApp().plater()->take_snapshot(_L("Selection-Remove from rectangle"), UndoRedo::SnapshotType::Selection);
                selection_changed = true;
            }
        }
    }

    if (!selection_changed)
        return;

    Plater::SuppressSnapshots suppress(wxGetApp().plater());

    if (state == GLSelectionRectangle::EState::Select && !ctrl_pressed)
        m_selection.clear();

    for (int i : m_hover_volume_idxs) {
        if (state == GLSelectionRectangle::EState::Select) {
            if (hover_modifiers_only) {
                const GLVolume& v = *m_volumes.volumes[i];
                m_selection.add_volume(v.object_idx(), v.volume_idx(), v.instance_idx(), false);
            }
            else
                m_selection.add(i, false);
        }
        else
            m_selection.remove(i);
    }

    if (m_selection.is_empty())
        m_gizmos.reset_all_states();
    else
        m_gizmos.refresh_on_off_state();

    m_gizmos.update_data();
    post_event(SimpleEvent(EVT_GLCANVAS_OBJECT_SELECT));
    m_dirty = true;
}

bool GLCanvas3D::_deactivate_undo_redo_toolbar_items()
{
    if (m_undoredo_toolbar.is_item_pressed("undo")) {
        m_undoredo_toolbar.force_right_action(m_undoredo_toolbar.get_item_id("undo"), *this);
        return true;
    }
    else if (m_undoredo_toolbar.is_item_pressed("redo")) {
        m_undoredo_toolbar.force_right_action(m_undoredo_toolbar.get_item_id("redo"), *this);
        return true;
    }

    return false;
}

bool GLCanvas3D::_deactivate_arrange_menu()
{
    if (m_main_toolbar.is_item_pressed("arrange")) {
        m_main_toolbar.force_right_action(m_main_toolbar.get_item_id("arrange"), *this);
        return true;
    }

    if (m_main_toolbar.is_item_pressed("arrangecurrent")) {
        m_main_toolbar.force_right_action(m_main_toolbar.get_item_id("arrangecurrent"), *this);
        return true;
    }

    return false;
}

bool GLCanvas3D::_deactivate_collapse_toolbar_items()
{
    GLToolbar& collapse_toolbar = wxGetApp().plater()->get_collapse_toolbar();
    if (collapse_toolbar.is_item_pressed("print")) {
        collapse_toolbar.force_left_action(collapse_toolbar.get_item_id("print"), *this);
        return true;
    }

    return false;
}

void GLCanvas3D::highlight_toolbar_item(const std::string& item_name)
{
    GLToolbarItem* item = m_main_toolbar.get_item(item_name);
    if (!item)
        item = m_undoredo_toolbar.get_item(item_name);
    if (!item || !item->is_visible())
        return;
    m_toolbar_highlighter.init(item, this);
}

void GLCanvas3D::highlight_gizmo(const std::string& gizmo_name)
{
    GLGizmosManager::EType gizmo = m_gizmos.get_gizmo_from_name(gizmo_name);
    if(gizmo == GLGizmosManager::EType::Undefined)
        return;
    m_gizmo_highlighter.init(&m_gizmos, gizmo, this);
}

const Print* GLCanvas3D::fff_print() const
{
    return (m_process == nullptr) ? nullptr : m_process->fff_print();
}

const SLAPrint* GLCanvas3D::sla_print() const
{
    return (m_process == nullptr) ? nullptr : m_process->sla_print();
}

void GLCanvas3D::WipeTowerInfo::apply_wipe_tower(Vec2d pos, double rot, int bed_index)
{
    wxGetApp().plater()->model().wipe_tower(bed_index).position = pos;
    wxGetApp().plater()->model().wipe_tower(bed_index).rotation = (180. / M_PI) * rot;
}

void GLCanvas3D::RenderTimer::Notify()
{
    wxPostEvent((wxEvtHandler*)GetOwner(), RenderTimerEvent( EVT_GLCANVAS_RENDER_TIMER, *this));
}

void GLCanvas3D::ToolbarHighlighterTimer::Notify()
{
    wxPostEvent((wxEvtHandler*)GetOwner(), ToolbarHighlighterTimerEvent(EVT_GLCANVAS_TOOLBAR_HIGHLIGHTER_TIMER, *this));
}

void GLCanvas3D::GizmoHighlighterTimer::Notify()
{
    wxPostEvent((wxEvtHandler*)GetOwner(), GizmoHighlighterTimerEvent(EVT_GLCANVAS_GIZMO_HIGHLIGHTER_TIMER, *this));
}

void GLCanvas3D::ToolbarHighlighter::init(GLToolbarItem* toolbar_item, GLCanvas3D* canvas)
{
    if (m_timer.IsRunning())
        invalidate();
    if (!toolbar_item || !canvas)
        return;

    m_timer.Start(300, false);

    m_toolbar_item = toolbar_item;
    m_canvas       = canvas;
}

void GLCanvas3D::ToolbarHighlighter::invalidate()
{
    m_timer.Stop();

    if (m_toolbar_item)
        m_toolbar_item->set_highlight(GLToolbarItem::EHighlightState::NotHighlighted);

    m_toolbar_item = nullptr;
    m_blink_counter = 0;
    m_render_arrow = false;
}

void GLCanvas3D::ToolbarHighlighter::blink()
{
    if (m_toolbar_item) {
        char state = m_toolbar_item->get_highlight();
        if (state != (char)GLToolbarItem::EHighlightState::HighlightedShown)
            m_toolbar_item->set_highlight(GLToolbarItem::EHighlightState::HighlightedShown);
        else 
            m_toolbar_item->set_highlight(GLToolbarItem::EHighlightState::HighlightedHidden);

        m_render_arrow = !m_render_arrow;
        m_canvas->set_as_dirty();
    }
    else
        invalidate();

    if ((++m_blink_counter) >= 11)
        invalidate();
}

void GLCanvas3D::GizmoHighlighter::init(GLGizmosManager* manager, GLGizmosManager::EType gizmo, GLCanvas3D* canvas)
{
    if (m_timer.IsRunning())
        invalidate();
    if (gizmo == GLGizmosManager::EType::Undefined || !canvas)
        return;

    m_timer.Start(300, false);

    m_gizmo_manager = manager;
    m_gizmo_type    = gizmo;
    m_canvas        = canvas;
}

void GLCanvas3D::GizmoHighlighter::invalidate()
{
    m_timer.Stop();

    if (m_gizmo_manager) {
        m_gizmo_manager->set_highlight(GLGizmosManager::EType::Undefined, false);
    }
    m_gizmo_manager = nullptr;
    m_gizmo_type = GLGizmosManager::EType::Undefined;
    m_blink_counter = 0;
    m_render_arrow = false;
}

void GLCanvas3D::GizmoHighlighter::blink()
{
    if (m_gizmo_manager) {
        if (m_blink_counter % 2 == 0)
            m_gizmo_manager->set_highlight(m_gizmo_type, true);
        else
            m_gizmo_manager->set_highlight(m_gizmo_type, false);

        m_render_arrow = !m_render_arrow;
        m_canvas->set_as_dirty();
    }
    else
        invalidate();

    if ((++m_blink_counter) >= 11)
        invalidate();
}

#if ENABLE_BINARIZED_GCODE_DEBUG_WINDOW
void GLCanvas3D::show_binary_gcode_debug_window()
{
    bgcode::binarize::BinarizerConfig& binarizer_config = GCodeProcessor::get_binarizer_config();

    ImGuiWrapper& imgui = *wxGetApp().imgui();
    ImGuiPureWrap::begin(std::string("Binary GCode"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    using namespace bgcode::core;
    if (ImGui::BeginTable("BinaryGCodeConfig", 2)) {

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, "File metadata compression");
        ImGui::TableSetColumnIndex(1);
        std::vector<std::string> options = { "None", "Deflate", "heatshrink 11,4", "heatshrink 12,4" };
        int option_id = (int)binarizer_config.compression.file_metadata;
        if (imgui.combo(std::string("##file_metadata_compression"), options, option_id, ImGuiComboFlags_HeightLargest, 0.0f, 175.0f))
            binarizer_config.compression.file_metadata = (ECompressionType)option_id;

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, "Printer metadata compression");
        ImGui::TableSetColumnIndex(1);
        option_id = (int)binarizer_config.compression.printer_metadata;
        if (imgui.combo(std::string("##printer_metadata_compression"), options, option_id, ImGuiComboFlags_HeightLargest, 0.0f, 175.0f))
            binarizer_config.compression.printer_metadata = (ECompressionType)option_id;

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, "Print metadata compression");
        ImGui::TableSetColumnIndex(1);
        option_id = (int)binarizer_config.compression.print_metadata;
        if (imgui.combo(std::string("##print_metadata_compression"), options, option_id, ImGuiComboFlags_HeightLargest, 0.0f, 175.0f))
            binarizer_config.compression.print_metadata = (ECompressionType)option_id;

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, "Slicer metadata compression");
        ImGui::TableSetColumnIndex(1);
        option_id = (int)binarizer_config.compression.slicer_metadata;
        if (imgui.combo(std::string("##slicer_metadata_compression"), options, option_id, ImGuiComboFlags_HeightLargest, 0.0f, 175.0f))
            binarizer_config.compression.slicer_metadata = (ECompressionType)option_id;

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, "GCode compression");
        ImGui::TableSetColumnIndex(1);
        option_id = (int)binarizer_config.compression.gcode;
        if (imgui.combo(std::string("##gcode_compression"), options, option_id, ImGuiComboFlags_HeightLargest, 0.0f, 175.0f))
            binarizer_config.compression.gcode = (ECompressionType)option_id;

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, "GCode encoding");
        ImGui::TableSetColumnIndex(1);
        options = { "None", "MeatPack", "MeatPack Comments" };
        option_id = (int)binarizer_config.gcode_encoding;
        if (imgui.combo(std::string("##gcode_encoding"), options, option_id, ImGuiComboFlags_HeightLargest, 0.0f, 175.0f))
            binarizer_config.gcode_encoding = (EGCodeEncodingType)option_id;

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, "Metadata encoding");
        ImGui::TableSetColumnIndex(1);
        options = { "INI" };
        option_id = (int)binarizer_config.metadata_encoding;
        if (imgui.combo(std::string("##metadata_encoding"), options, option_id, ImGuiComboFlags_HeightLargest, 0.0f, 175.0f))
            binarizer_config.metadata_encoding = (EMetadataEncodingType)option_id;

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, "Checksum type");
        ImGui::TableSetColumnIndex(1);
        options = { "None", "CRC32" };
        option_id = (int)binarizer_config.checksum;
        if (imgui.combo(std::string("##4"), options, option_id, ImGuiComboFlags_HeightLargest, 0.0f, 175.0f))
            binarizer_config.checksum = (EChecksumType)option_id;

        ImGui::EndTable();

        ImGui::Separator();
        ImGuiPureWrap::text("!!! WARNING !!!");
        ImGuiPureWrap::text("Changing values does NOT invalidate the current slice");
    }

    ImGuiPureWrap::end();
}
#endif // ENABLE_BINARIZED_GCODE_DEBUG_WINDOW

const ModelVolume *get_model_volume(const GLVolume &v, const Model &model)
{
    const ModelVolume * ret = nullptr;

    if (v.object_idx() < (int)model.objects.size()) {
        const ModelObject *obj = model.objects[v.object_idx()];
        if (v.volume_idx() < (int)obj->volumes.size())
            ret = obj->volumes[v.volume_idx()];
    }

    return ret;
}

ModelVolume *get_model_volume(const ObjectID &volume_id, const ModelObjectPtrs &objects)
{
    for (const ModelObject *obj : objects)
        for (ModelVolume *vol : obj->volumes)
            if (vol->id() == volume_id)
                return vol;
    return nullptr;
}

ModelVolume *get_model_volume(const GLVolume &v, const ModelObject& object) {
    if (v.volume_idx() < 0)
        return nullptr;

    size_t volume_idx = static_cast<size_t>(v.volume_idx());
    if (volume_idx >= object.volumes.size())
        return nullptr;

    return object.volumes[volume_idx];
}

ModelVolume *get_model_volume(const GLVolume &v, const ModelObjectPtrs &objects)
{
    if (v.object_idx() < 0)
        return nullptr;
    size_t objext_idx = static_cast<size_t>(v.object_idx());
    if (objext_idx >= objects.size())
        return nullptr;
    if (objects[objext_idx] == nullptr)
        return nullptr;
    return get_model_volume(v, *objects[objext_idx]);
}

GLVolume *get_first_hovered_gl_volume(const GLCanvas3D &canvas) {
    int hovered_id_signed = canvas.get_first_hover_volume_idx();
    if (hovered_id_signed < 0)
        return nullptr;

    size_t hovered_id = static_cast<size_t>(hovered_id_signed);
    const GLVolumePtrs &volumes = canvas.get_volumes().volumes;
    if (hovered_id >= volumes.size())
        return nullptr;

    return volumes[hovered_id];
}

GLVolume *get_selected_gl_volume(const GLCanvas3D &canvas) {
    const GLVolume *gl_volume = get_selected_gl_volume(canvas.get_selection());
    if (gl_volume == nullptr)
        return nullptr;

    const GLVolumePtrs &gl_volumes = canvas.get_volumes().volumes;
    for (GLVolume *v : gl_volumes)
        if (v->composite_id == gl_volume->composite_id)
            return v;
    return nullptr;
}

ModelObject *get_model_object(const GLVolume &gl_volume, const Model &model) {
    return get_model_object(gl_volume, model.objects);
}

ModelObject *get_model_object(const GLVolume &gl_volume, const ModelObjectPtrs &objects) {
    if (gl_volume.object_idx() < 0)
        return nullptr;
    size_t objext_idx = static_cast<size_t>(gl_volume.object_idx());
    if (objext_idx >= objects.size())
        return nullptr;
    return objects[objext_idx];
}

ModelInstance *get_model_instance(const GLVolume &gl_volume, const Model& model) {
    return get_model_instance(gl_volume, model.objects);
}

ModelInstance *get_model_instance(const GLVolume &gl_volume, const ModelObjectPtrs &objects) {
    if (gl_volume.instance_idx() < 0)
        return nullptr;
    ModelObject *object = get_model_object(gl_volume, objects);
    return get_model_instance(gl_volume, *object);
}

ModelInstance *get_model_instance(const GLVolume &gl_volume, const ModelObject &object) {
    if (gl_volume.instance_idx() < 0)
        return nullptr;
    size_t instance_idx = static_cast<size_t>(gl_volume.instance_idx());
    if (instance_idx >= object.instances.size())
        return nullptr;
    return object.instances[instance_idx];
}

} // namespace GUI
} // namespace Slic3r
