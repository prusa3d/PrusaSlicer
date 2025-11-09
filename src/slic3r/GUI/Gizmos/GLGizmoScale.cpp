///|/ Copyright (c) Prusa Research 2019 - 2023 Oleksandra Iushchenko @YuSanka, Lukáš Matěna @lukasmatena, Enrico Turri @enricoturri1966, Filip Sykala @Jony01, Vojtěch Bubník @bubnikv
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "GLGizmoScale.hpp"

#include <boost/nowide/convert.hpp>
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectManipulation.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/Utils/UndoRedo.hpp"
#include "libslic3r/Model.hpp"

#include <GL/glew.h>

#include <wx/utils.h>

namespace Slic3r {
namespace GUI {


const double GLGizmoScale3D::Offset = 5.0;

GLGizmoScale3D::GLGizmoScale3D(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoBase(parent, icon_filename, sprite_id)
    , m_scale(Vec3d::Ones())
    , m_snap_step(0.05)
    , m_base_color(DEFAULT_BASE_COLOR)
    , m_drag_color(DEFAULT_DRAG_COLOR)
    , m_highlight_color(DEFAULT_HIGHLIGHT_COLOR)
{
    m_grabber_connections[0].grabber_indices = { 0, 1 };
    m_grabber_connections[1].grabber_indices = { 2, 3 };
    m_grabber_connections[2].grabber_indices = { 4, 5 };
    m_grabber_connections[3].grabber_indices = { 6, 7 };
    m_grabber_connections[4].grabber_indices = { 7, 8 }; 
    m_grabber_connections[5].grabber_indices = { 8, 9 };
    m_grabber_connections[6].grabber_indices = { 9, 6 };

    // Must match the order of PartsRelationsAdjustment enum
    m_relations_adjustment_modes = {_u8L("Move None"), _u8L("Move All")};
}

std::string GLGizmoScale3D::get_tooltip() const
{
    const Vec3d scale = 100.0 * m_scale;
    Vec3d delta = Vec3d::Zero();
    if (m_starting.box.defined) {
        delta = m_starting.box.min - m_bounding_box.min;
        delta *= 2;
        if (m_imperial_units) {
            delta *= ObjectManipulation::mm_to_in;
        }

    }

    if (m_hover_id == 0 || m_hover_id == 1 || m_grabbers[0].dragging || m_grabbers[1].dragging)
        return "X: " + format(scale.x(), 4) + "% (size delta: " + format(delta.x(), 4) + ")";
    else if (m_hover_id == 2 || m_hover_id == 3 || m_grabbers[2].dragging || m_grabbers[3].dragging)
        return "Y: " + format(scale.y(), 4) + "% (size delta: " + format(delta.y(), 4) + ")";
    else if (m_hover_id == 4 || m_hover_id == 5 || m_grabbers[4].dragging || m_grabbers[5].dragging)
        return "Z: " + format(scale.z(), 4) + "% (size delta: " + format(delta.z(), 4) + ")";
    else if (m_hover_id == 6 || m_hover_id == 7 || m_hover_id == 8 || m_hover_id == 9 ||
        m_grabbers[6].dragging || m_grabbers[7].dragging || m_grabbers[8].dragging || m_grabbers[9].dragging)
    {
        std::string tooltip = "X: " + format(scale.x(), 4) + "% (size delta: " + format(delta.x(), 4) + ")\n";
        tooltip += "Y: " + format(scale.y(), 4) + "% (size delta: " + format(delta.y(), 4) + ")\n";
        tooltip += "Z: " + format(scale.z(), 4) + "% (size delta: " + format(delta.z(), 4) + ")";
        return tooltip;
    }
    else
        return "";
}

static int constraint_id(int grabber_id)
{
  static const std::vector<int> id_map = { 1, 0, 3, 2, 5, 4, 8, 9, 6, 7 };
  return (0 <= grabber_id && grabber_id < (int)id_map.size()) ? id_map[grabber_id] : -1;
}

GLGizmoScale3D::AdjacentVolumes GLGizmoScale3D::get_adjacent_volumes(
    Selection& selection, const BoundingBoxf3& world_bounding_box
) {
    const int object_id = selection.get_object_idx();
    AdjacentVolumes adjacent_volumes{};
    if (object_id != -1) {
        const std::vector<unsigned int> unselected_volume_idxes = selection.get_unselected_volume_idxs_from(
            selection.get_volume_idxs_from_object(object_id)
        );

        for (auto curr_unselected_volume_idx : unselected_volume_idxes) {
            const auto& curr_unselected_volume = selection.get_volume(curr_unselected_volume_idx);

            BoundingBoxf3 curr_unselected_bound_box =
                curr_unselected_volume->transformed_convex_hull_bounding_box();

            for (const auto axis : {X, Y, Z}) {
                if (is_approx(curr_unselected_bound_box.min[axis], world_bounding_box.max[axis]) ||
                    curr_unselected_bound_box.min[axis] >= world_bounding_box.max[axis]) {
                    adjacent_volumes.volumes[axis][0].insert(curr_unselected_volume_idx);
                } else if (is_approx(curr_unselected_bound_box.max[axis], world_bounding_box.min[axis]) ||
                           world_bounding_box.min[axis] >= curr_unselected_bound_box.max[axis]) {
                    adjacent_volumes.volumes[axis][1].insert(curr_unselected_volume_idx);
                }
            }
        }
    }

    return adjacent_volumes;
}

bool GLGizmoScale3D::on_mouse(const wxMouseEvent &mouse_event)
{
    if (mouse_event.Dragging()) {
        if (m_dragging) {
            // Apply new temporary scale factors
            TransformationType transformation_type;
            if (wxGetApp().obj_manipul()->is_local_coordinates())
                transformation_type.set_local();
            else if (wxGetApp().obj_manipul()->is_instance_coordinates())
                transformation_type.set_instance();

            transformation_type.set_relative();

            if (mouse_event.AltDown())
                transformation_type.set_independent();

            Selection& selection = m_parent.get_selection();
            const BoundingBoxf3 world_bounding_box_before = selection.get_bounding_box();

            selection.scale(m_scale, transformation_type);
            if (m_starting.ctrl_down) {
                // constrained scale:
                // uses the performed scale to calculate the new position of the constrained grabber
                // and from that calculates the offset (in world coordinates) to be applied to fullfill the constraint
                update_render_data();
                const Vec3d constraint_position = m_grabbers_transform * m_grabbers[constraint_id(m_hover_id)].center;
                // re-apply the scale because the selection always applies the transformations with respect to the initial state 
                // set into on_start_dragging() with the call to selection.setup_cache()
                m_parent.get_selection().scale_and_translate(m_scale, m_starting.constraint_position - constraint_position, transformation_type);
            }

            const BoundingBoxf3 world_bounding_box_after = selection.get_bounding_box();

            for (const auto axis : {X, Y, Z}) {
                if (world_bounding_box_before.max[axis] != world_bounding_box_after.max[axis] &&
                    !m_starting.adjacent_volumes.volumes[axis][0].empty()) {

                    Vec3d displacement = Vec3d::Zero();
                    displacement[axis] = (world_bounding_box_after.max[axis] - world_bounding_box_before.max[axis]);
                    for (const auto current_unselected_volume_idx :
                         m_starting.adjacent_volumes.volumes[axis][0]) {
                        GLVolume* current_unselected_volume = selection.get_volume(current_unselected_volume_idx);

                        const Geometry::Transformation& vol_trafo = current_unselected_volume->get_volume_transformation();
                        const Geometry::Transformation& inst_trafo = current_unselected_volume->get_instance_transformation();

                        const Vec3d inst_pivot = vol_trafo.get_offset();
                        const Transform3d inst_matrix_no_offset = inst_trafo.get_matrix_no_offset();
                        const Transform3d trafo = Geometry::translation_transform(inst_pivot) * inst_matrix_no_offset.inverse() * Geometry::translation_transform(displacement) * inst_matrix_no_offset * Geometry::translation_transform(-inst_pivot);
                        current_unselected_volume->set_volume_transformation(trafo * vol_trafo.get_matrix());
                    }
                }

                if (world_bounding_box_before.min[axis] != world_bounding_box_after.min[axis] &&
                    !m_starting.adjacent_volumes.volumes[axis][1].empty()) {

                    Vec3d displacement = Vec3d::Zero();
                    displacement[axis] = (world_bounding_box_after.min[axis] - world_bounding_box_before.min[axis]);
                    for (const auto current_unselected_volume_idx :
                         m_starting.adjacent_volumes.volumes[axis][1]) {
                        GLVolume* current_unselected_volume = selection.get_volume(current_unselected_volume_idx);

                        const Geometry::Transformation& vol_trafo = current_unselected_volume->get_volume_transformation();
                        const Geometry::Transformation& inst_trafo = current_unselected_volume->get_instance_transformation();

                        const Vec3d inst_pivot = vol_trafo.get_offset();
                        const Transform3d inst_matrix_no_offset = inst_trafo.get_matrix_no_offset();
                        const Transform3d trafo = Geometry::translation_transform(inst_pivot) * inst_matrix_no_offset.inverse() * Geometry::translation_transform(displacement) * inst_matrix_no_offset * Geometry::translation_transform(-inst_pivot);
                        current_unselected_volume->set_volume_transformation(trafo * vol_trafo.get_matrix());
                    }
                }

            }
        }
    }
    return use_grabbers(mouse_event);
}

void GLGizmoScale3D::enable_ununiversal_scale(bool enable)
{
    for (unsigned int i = 0; i < 6; ++i)
        m_grabbers[i].enabled = enable;
}

void GLGizmoScale3D::data_changed(bool is_serializing) {
    set_scale(Vec3d::Ones());
}

bool GLGizmoScale3D::on_init()
{
    for (int i = 0; i < 10; ++i) {
        m_grabbers.push_back(Grabber());
    }

    m_shortcut_key = WXK_CONTROL_S;

    // initiate info shortcuts
    const std::string ctrl  = _u8L(GUI::shortkey_ctrl());
    const std::string alt   = _u8L(GUI::shortkey_alt());
    const std::string shift = _u8L("Shift");

    m_shortcuts.emplace_back(ctrl, _u8L("Scale in one direction"));
    m_shortcuts.emplace_back(shift, _u8L("Scale in fixed increments"));
    m_shortcuts.emplace_back(alt, _u8L("Scale independent (when multi-select)"));

    return true;
}

std::string GLGizmoScale3D::on_get_name() const
{
    return _u8L("Scale");
}

bool GLGizmoScale3D::on_is_activable() const
{
    const Selection& selection = m_parent.get_selection();
    return !selection.is_any_cut_volume() && !selection.is_any_connector() && !selection.is_empty() && !selection.is_wipe_tower();
}

void GLGizmoScale3D::on_start_dragging()
{
    assert(m_hover_id != -1);
    m_starting.ctrl_down = wxGetKeyState(WXK_CONTROL);
    m_starting.drag_position = m_grabbers_transform * m_grabbers[m_hover_id].center;
    m_starting.box = m_bounding_box;
    if (m_relations_adjustment_mode == PartsRelationsAdjustment::MoveNone) {
        m_starting.adjacent_volumes = {};
    }
    else {
        m_starting.adjacent_volumes = get_adjacent_volumes(m_parent.get_selection(), m_parent.get_selection().get_bounding_box());
    }

    m_starting.center = m_center;
    m_starting.instance_center = m_instance_center;
    m_starting.constraint_position = m_grabbers_transform * m_grabbers[constraint_id(m_hover_id)].center;
    m_imperial_units = wxGetApp().app_config->get_bool("use_inches");
}

void GLGizmoScale3D::on_stop_dragging()
{
    m_parent.do_scale(L("Gizmo-Scale"));
    m_starting.ctrl_down = false;
    m_starting.box.reset();
}

void GLGizmoScale3D::on_dragging(const UpdateData& data)
{
    if (m_hover_id == 0 || m_hover_id == 1)
        do_scale_along_axis(X, data);
    else if (m_hover_id == 2 || m_hover_id == 3)
        do_scale_along_axis(Y, data);
    else if (m_hover_id == 4 || m_hover_id == 5)
        do_scale_along_axis(Z, data);
    else if (m_hover_id >= 6)
        do_scale_uniform(data);
}

void GLGizmoScale3D::on_render()
{
    glsafe(::glClear(GL_DEPTH_BUFFER_BIT));
    glsafe(::glEnable(GL_DEPTH_TEST));

    update_render_data();

#if !SLIC3R_OPENGL_ES
    if (!OpenGLManager::get_gl_info().is_core_profile())
        glsafe(::glLineWidth((m_hover_id != -1) ? 2.0f : 1.5f));
#endif // !SLIC3R_OPENGL_ES

    const float grabber_mean_size = (float)((m_bounding_box.size().x() + m_bounding_box.size().y() + m_bounding_box.size().z()) / 3.0);

    if (m_hover_id == -1) {
        // draw connections
#if SLIC3R_OPENGL_ES
        GLShaderProgram* shader = wxGetApp().get_shader("dashed_lines");
#else
        GLShaderProgram* shader = OpenGLManager::get_gl_info().is_core_profile() ? wxGetApp().get_shader("dashed_thick_lines") : wxGetApp().get_shader("flat");
#endif // SLIC3R_OPENGL_ES
        if (shader != nullptr) {
            shader->start_using();
            const Camera& camera = wxGetApp().plater()->get_camera();
            shader->set_uniform("view_model_matrix", camera.get_view_matrix() * m_grabbers_transform);
            shader->set_uniform("projection_matrix", camera.get_projection_matrix());
#if !SLIC3R_OPENGL_ES
            if (OpenGLManager::get_gl_info().is_core_profile()) {
#endif // !SLIC3R_OPENGL_ES
                const std::array<int, 4>& viewport = camera.get_viewport();
                shader->set_uniform("viewport_size", Vec2d(double(viewport[2]), double(viewport[3])));
                shader->set_uniform("width", 0.25f);
                shader->set_uniform("gap_size", 0.0f);
#if !SLIC3R_OPENGL_ES
            }
#endif // !SLIC3R_OPENGL_ES
            if (m_grabbers[0].enabled && m_grabbers[1].enabled)
                render_grabbers_connection(0, 1, m_grabbers[0].color);
            if (m_grabbers[2].enabled && m_grabbers[3].enabled)
                render_grabbers_connection(2, 3, m_grabbers[2].color);
            if (m_grabbers[4].enabled && m_grabbers[5].enabled)
                render_grabbers_connection(4, 5, m_grabbers[4].color);
            render_grabbers_connection(6, 7, m_base_color);
            render_grabbers_connection(7, 8, m_base_color);
            render_grabbers_connection(8, 9, m_base_color);
            render_grabbers_connection(9, 6, m_base_color);
            shader->stop_using();
        }

        // draw grabbers
        render_grabbers(grabber_mean_size);
    }
    else if ((m_hover_id == 0 || m_hover_id == 1) && m_grabbers[0].enabled && m_grabbers[1].enabled) {
        // draw connections
#if SLIC3R_OPENGL_ES
        GLShaderProgram* shader = wxGetApp().get_shader("dashed_lines");
#else
        GLShaderProgram* shader = OpenGLManager::get_gl_info().is_core_profile() ? wxGetApp().get_shader("dashed_thick_lines") : wxGetApp().get_shader("flat");
#endif // SLIC3R_OPENGL_ES
        if (shader != nullptr) {
            shader->start_using();
            const Camera& camera = wxGetApp().plater()->get_camera();
            shader->set_uniform("view_model_matrix", camera.get_view_matrix() * m_grabbers_transform);
            shader->set_uniform("projection_matrix", camera.get_projection_matrix());
#if !SLIC3R_OPENGL_ES
            if (OpenGLManager::get_gl_info().is_core_profile()) {
#endif // !SLIC3R_OPENGL_ES
                const std::array<int, 4>& viewport = camera.get_viewport();
                shader->set_uniform("viewport_size", Vec2d(double(viewport[2]), double(viewport[3])));
                shader->set_uniform("width", 0.25f);
                shader->set_uniform("gap_size", 0.0f);
#if !SLIC3R_OPENGL_ES
            }
#endif // !SLIC3R_OPENGL_ES
            render_grabbers_connection(0, 1, m_grabbers[0].color);
            shader->stop_using();
        }

        // draw grabbers
        shader = wxGetApp().get_shader("gouraud_light");
        if (shader != nullptr) {
            shader->start_using();
            shader->set_uniform("emission_factor", 0.1f);
            render_grabbers(0, 1, grabber_mean_size, true);
            shader->stop_using();
        }
    }
    else if ((m_hover_id == 2 || m_hover_id == 3) && m_grabbers[2].enabled && m_grabbers[3].enabled) {
        // draw connections
#if SLIC3R_OPENGL_ES
        GLShaderProgram* shader = wxGetApp().get_shader("dashed_lines");
#else
        GLShaderProgram* shader = OpenGLManager::get_gl_info().is_core_profile() ? wxGetApp().get_shader("dashed_thick_lines") : wxGetApp().get_shader("flat");
#endif // SLIC3R_OPENGL_ES
        if (shader != nullptr) {
            shader->start_using();
            const Camera& camera = wxGetApp().plater()->get_camera();
            shader->set_uniform("view_model_matrix", camera.get_view_matrix() * m_grabbers_transform);
            shader->set_uniform("projection_matrix", camera.get_projection_matrix());
#if !SLIC3R_OPENGL_ES
            if (OpenGLManager::get_gl_info().is_core_profile()) {
#endif // !SLIC3R_OPENGL_ES
                const std::array<int, 4>& viewport = camera.get_viewport();
                shader->set_uniform("viewport_size", Vec2d(double(viewport[2]), double(viewport[3])));
                shader->set_uniform("width", 0.25f);
                shader->set_uniform("gap_size", 0.0f);
#if !SLIC3R_OPENGL_ES
            }
#endif // !SLIC3R_OPENGL_ES
            render_grabbers_connection(2, 3, m_grabbers[2].color);
            shader->stop_using();
        }

        // draw grabbers
        shader = wxGetApp().get_shader("gouraud_light");
        if (shader != nullptr) {
            shader->start_using();
            shader->set_uniform("emission_factor", 0.1f);
            render_grabbers(2, 3, grabber_mean_size, true);
            shader->stop_using();
        }
    }
    else if ((m_hover_id == 4 || m_hover_id == 5) && m_grabbers[4].enabled && m_grabbers[5].enabled) {
        // draw connections
#if SLIC3R_OPENGL_ES
        GLShaderProgram* shader = wxGetApp().get_shader("dashed_lines");
#else
        GLShaderProgram* shader = OpenGLManager::get_gl_info().is_core_profile() ? wxGetApp().get_shader("dashed_thick_lines") : wxGetApp().get_shader("flat");
#endif // SLIC3R_OPENGL_ES
        if (shader != nullptr) {
            shader->start_using();
            const Camera& camera = wxGetApp().plater()->get_camera();
            shader->set_uniform("view_model_matrix", camera.get_view_matrix() * m_grabbers_transform);
            shader->set_uniform("projection_matrix", camera.get_projection_matrix());
#if !SLIC3R_OPENGL_ES
            if (OpenGLManager::get_gl_info().is_core_profile()) {
#endif // !SLIC3R_OPENGL_ES
                const std::array<int, 4>& viewport = camera.get_viewport();
                shader->set_uniform("viewport_size", Vec2d(double(viewport[2]), double(viewport[3])));
                shader->set_uniform("width", 0.25f);
                shader->set_uniform("gap_size", 0.0f);
#if !SLIC3R_OPENGL_ES
            }
#endif // !SLIC3R_OPENGL_ES
            render_grabbers_connection(4, 5, m_grabbers[4].color);
            shader->stop_using();
        }

        // draw grabbers
        shader = wxGetApp().get_shader("gouraud_light");
        if (shader != nullptr) {
            shader->start_using();
            shader->set_uniform("emission_factor", 0.1f);
            render_grabbers(4, 5, grabber_mean_size, true);
            shader->stop_using();
        }
    }
    else if (m_hover_id >= 6) {
        // draw connections
#if SLIC3R_OPENGL_ES
        GLShaderProgram* shader = wxGetApp().get_shader("dashed_lines");
#else
        GLShaderProgram* shader = OpenGLManager::get_gl_info().is_core_profile() ? wxGetApp().get_shader("dashed_thick_lines") : wxGetApp().get_shader("flat");
#endif // SLIC3R_OPENGL_ES
        if (shader != nullptr) {
            shader->start_using();
            const Camera& camera = wxGetApp().plater()->get_camera();
            shader->set_uniform("view_model_matrix", camera.get_view_matrix() * m_grabbers_transform);
            shader->set_uniform("projection_matrix", camera.get_projection_matrix());
#if !SLIC3R_OPENGL_ES
            if (OpenGLManager::get_gl_info().is_core_profile()) {
#endif // !SLIC3R_OPENGL_ES
                const std::array<int, 4>& viewport = camera.get_viewport();
                shader->set_uniform("viewport_size", Vec2d(double(viewport[2]), double(viewport[3])));
                shader->set_uniform("width", 0.25f);
                shader->set_uniform("gap_size", 0.0f);
#if !SLIC3R_OPENGL_ES
            }
#endif // !SLIC3R_OPENGL_ES
            render_grabbers_connection(6, 7, m_drag_color);
            render_grabbers_connection(7, 8, m_drag_color);
            render_grabbers_connection(8, 9, m_drag_color);
            render_grabbers_connection(9, 6, m_drag_color);
            shader->stop_using();
        }

        // draw grabbers
        shader = wxGetApp().get_shader("gouraud_light");
        if (shader != nullptr) {
            shader->start_using();
            shader->set_uniform("emission_factor", 0.1f);
            render_grabbers(6, 9, grabber_mean_size, true);
            shader->stop_using();
        }
    }
}

void GLGizmoScale3D::on_register_raycasters_for_picking()
{
    // the gizmo grabbers are rendered on top of the scene, so the raytraced picker should take it into account
    m_parent.set_raycaster_gizmos_on_top(true);
}

void GLGizmoScale3D::on_unregister_raycasters_for_picking()
{
    m_parent.set_raycaster_gizmos_on_top(false);
}

void GLGizmoScale3D::on_render_input_window(float x, float y, float bottom_limit)
{
    const std::string relations_adjustment = "Relations Adjustment";


    ImGuiPureWrap::begin(get_name(), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    // adjust window position to avoid overlap the view toolbar
    adjust_window_position(x, y, bottom_limit);

    // add shortcuts panel
    render_shortcuts();

    ImGui::Separator();

    if (m_label_width == 0.f) {
        m_label_width = ImGuiPureWrap::calc_text_size(_u8L(relations_adjustment)).x;
        m_label_width += m_imgui->scaled(1.f);
    }

    ImGui::AlignTextToFramePadding();
    int selection_idx = static_cast<int>(m_relations_adjustment_mode);
    const bool is_changed = ImGuiPureWrap::combo(_u8L(relations_adjustment), m_relations_adjustment_modes, selection_idx, 0, m_label_width, m_control_width);

    if (is_changed) {
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), _L("Change Scale & Push mode"), UndoRedo::SnapshotType::GizmoAction);
        m_relations_adjustment_mode = static_cast<PartsRelationsAdjustment>(selection_idx);
    }

    ImGuiPureWrap::end();
}

void GLGizmoScale3D::render_shortcuts()
{
    std::wstring btn_label;
    btn_label = m_show_shortcuts ? ImGui::CollapseBtn : ImGui::ExpandBtn;

    if (ImGuiPureWrap::button("? " + boost::nowide::narrow(btn_label)))
        m_show_shortcuts = !m_show_shortcuts;

    if (m_shortcut_label_width < 0.f) {
        for (const auto& shortcut : m_shortcuts) {
            const float width = ImGuiPureWrap::calc_text_size(shortcut.first).x;
            if (m_shortcut_label_width < width)
                m_shortcut_label_width = width;
        }
        m_shortcut_label_width += +m_imgui->scaled(1.f);
    }

    if (m_show_shortcuts)
        for (const auto& [shortcut, meaning] : m_shortcuts ){
            ImGuiPureWrap::text_colored(ImGuiPureWrap::COL_ORANGE_LIGHT, shortcut);
            ImGui::SameLine(m_shortcut_label_width);
            ImGuiPureWrap::text(meaning);
        }
}

void GLGizmoScale3D::adjust_window_position(float x, float y, float bottom_limit)
{
    static float last_y = 0.0f;
    static float last_h = 0.0f;

    const float win_h = ImGui::GetWindowHeight();
    y                 = std::min(y, bottom_limit - win_h);

    ImGui::SetWindowPos(ImVec2(x, y), ImGuiCond_Always);

    if (!is_approx(last_h, win_h) || !is_approx(last_y, y)) {
        // ask canvas for another frame to render the window in the correct position
        m_imgui->set_requires_extra_frame();
        if (!is_approx(last_h, win_h))
            last_h = win_h;
        if (!is_approx(last_y, y))
            last_y = y;
    }
}

void GLGizmoScale3D::render_grabbers_connection(unsigned int id_1, unsigned int id_2, const ColorRGBA& color)
{
    auto grabber_connection = [this](unsigned int id_1, unsigned int id_2) {
        for (int i = 0; i < int(m_grabber_connections.size()); ++i) {
            if (m_grabber_connections[i].grabber_indices.first == id_1 && m_grabber_connections[i].grabber_indices.second == id_2)
                return i;
        }
        return -1;
    };

    const int id = grabber_connection(id_1, id_2);
    if (id == -1)
        return;

    if (!m_grabber_connections[id].model.is_initialized() ||
        !m_grabber_connections[id].old_v1.isApprox(m_grabbers[id_1].center) ||
        !m_grabber_connections[id].old_v2.isApprox(m_grabbers[id_2].center)) {
        m_grabber_connections[id].old_v1 = m_grabbers[id_1].center;
        m_grabber_connections[id].old_v2 = m_grabbers[id_2].center;
        m_grabber_connections[id].model.reset();

        GLModel::Geometry init_data;
        init_data.format = { GLModel::Geometry::EPrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P3 };
        init_data.reserve_vertices(2);
        init_data.reserve_indices(2);

        // vertices
        init_data.add_vertex((Vec3f)m_grabbers[id_1].center.cast<float>());
        init_data.add_vertex((Vec3f)m_grabbers[id_2].center.cast<float>());

        // indices
        init_data.add_line(0, 1);

        m_grabber_connections[id].model.init_from(std::move(init_data));
    }

    m_grabber_connections[id].model.set_color(color);
    m_grabber_connections[id].model.render();
}

void GLGizmoScale3D::do_scale_along_axis(Axis axis, const UpdateData& data)
{
    double ratio = calc_ratio(data);
    if (ratio > 0.0) {
        Vec3d curr_scale = m_scale;
        curr_scale(axis) = m_starting.scale(axis) * ratio;
        m_scale = curr_scale;
    }
}

void GLGizmoScale3D::do_scale_uniform(const UpdateData & data)
{
    const double ratio = calc_ratio(data);
    if (ratio > 0.0)
        m_scale = m_starting.scale * ratio;
}

double GLGizmoScale3D::calc_ratio(const UpdateData& data) const
{
    double ratio = 0.0;
    const Vec3d starting_vec = m_starting.drag_position - m_starting.center;

    const double len_starting_vec = starting_vec.norm();

    if (len_starting_vec != 0.0) {
        const Vec3d mouse_dir = data.mouse_ray.unit_vector();
        // finds the intersection of the mouse ray with the plane parallel to the camera viewport and passing throught the starting position
        // use ray-plane intersection see i.e. https://en.wikipedia.org/wiki/Line%E2%80%93plane_intersection algebric form
        // in our case plane normal and ray direction are the same (orthogonal view)
        // when moving to perspective camera the negative z unit axis of the camera needs to be transformed in world space and used as plane normal
        const Vec3d inters = data.mouse_ray.a + (m_starting.drag_position - data.mouse_ray.a).dot(mouse_dir) * mouse_dir;
        // vector from the starting position to the found intersection
        const Vec3d inters_vec = inters - m_starting.drag_position;

        // finds projection of the vector along the staring direction
        const double proj = inters_vec.dot(starting_vec.normalized());

        ratio = (len_starting_vec + proj) / len_starting_vec;
    }

    if (wxGetKeyState(WXK_SHIFT))
        ratio = m_snap_step * (double)std::round(ratio / m_snap_step);

    return ratio;
}

void GLGizmoScale3D::update_render_data()
{
    const Selection& selection = m_parent.get_selection();
    const auto& [box, box_trafo] = selection.get_bounding_box_in_current_reference_system();
    m_bounding_box = box;
    m_center = box_trafo.translation();
    m_grabbers_transform = box_trafo;
    m_instance_center = (selection.is_single_full_instance() || selection.is_single_volume_or_modifier()) ? selection.get_first_volume()->get_instance_offset() : m_center;

    const Vec3d box_half_size = 0.5 * m_bounding_box.size();
    bool use_constrain = wxGetKeyState(WXK_CONTROL);

    // x axis
    m_grabbers[0].center = { -(box_half_size.x() + Offset), 0.0, 0.0 };
    m_grabbers[0].color = (use_constrain && m_hover_id == 1) ? CONSTRAINED_COLOR : AXES_COLOR[0];
    m_grabbers[1].center = { box_half_size.x() + Offset, 0.0, 0.0 };
    m_grabbers[1].color = (use_constrain && m_hover_id == 0) ? CONSTRAINED_COLOR : AXES_COLOR[0];

    // y axis
    m_grabbers[2].center = { 0.0, -(box_half_size.y() + Offset), 0.0 };
    m_grabbers[2].color = (use_constrain && m_hover_id == 3) ? CONSTRAINED_COLOR : AXES_COLOR[1];
    m_grabbers[3].center = { 0.0, box_half_size.y() + Offset, 0.0 };
    m_grabbers[3].color = (use_constrain && m_hover_id == 2) ? CONSTRAINED_COLOR : AXES_COLOR[1];

    // z axis
    m_grabbers[4].center = { 0.0, 0.0, -(box_half_size.z() + Offset) };
    m_grabbers[4].color = (use_constrain && m_hover_id == 5) ? CONSTRAINED_COLOR : AXES_COLOR[2];
    m_grabbers[5].center = { 0.0, 0.0, box_half_size.z() + Offset };
    m_grabbers[5].color = (use_constrain && m_hover_id == 4) ? CONSTRAINED_COLOR : AXES_COLOR[2];

    // uniform
    m_grabbers[6].center = { -(box_half_size.x() + Offset), -(box_half_size.y() + Offset), 0.0 };
    m_grabbers[6].color = (use_constrain && m_hover_id == 8) ? CONSTRAINED_COLOR : m_highlight_color;
    m_grabbers[7].center = { box_half_size.x() + Offset, -(box_half_size.y() + Offset), 0.0 };
    m_grabbers[7].color = (use_constrain && m_hover_id == 9) ? CONSTRAINED_COLOR : m_highlight_color;
    m_grabbers[8].center = { box_half_size.x() + Offset, box_half_size.y() + Offset, 0.0 };
    m_grabbers[8].color = (use_constrain && m_hover_id == 6) ? CONSTRAINED_COLOR : m_highlight_color;
    m_grabbers[9].center = { -(box_half_size.x() + Offset), box_half_size.y() + Offset, 0.0 };
    m_grabbers[9].color = (use_constrain && m_hover_id == 7) ? CONSTRAINED_COLOR : m_highlight_color;

    for (int i = 0; i < 10; ++i) {
        m_grabbers[i].matrix = m_grabbers_transform;
    }
}

} // namespace GUI
} // namespace Slic3r