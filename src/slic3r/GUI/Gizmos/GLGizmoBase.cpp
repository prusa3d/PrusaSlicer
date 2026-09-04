///|/ Copyright (c) Prusa Research 2019 - 2023 Oleksandra Iushchenko @YuSanka, Enrico Turri @enricoturri1966, Lukáš Matěna @lukasmatena, Filip Sykala @Jony01, Vojtěch Bubník @bubnikv
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "GLGizmoBase.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"

#include <GL/glew.h>

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectManipulation.hpp"
#include "slic3r/GUI/Plater.hpp"

// TODO: Display tooltips quicker on Linux

namespace Slic3r {
namespace GUI {

const float GLGizmoBase::Grabber::SizeFactor = 0.05f;
const float GLGizmoBase::Grabber::MinHalfSize = 1.5f;
const float GLGizmoBase::Grabber::DraggingScaleFactor = 1.25f;

PickingModel GLGizmoBase::Grabber::s_cube;
PickingModel GLGizmoBase::Grabber::s_cone;

GLGizmoBase::Grabber::~Grabber()
{
    if (s_cube.model.is_initialized())
        s_cube.model.reset();

    if (s_cone.model.is_initialized())
        s_cone.model.reset();
}

float GLGizmoBase::Grabber::get_half_size(float size) const
{
    return std::max(size * SizeFactor, MinHalfSize);
}

float GLGizmoBase::Grabber::get_dragging_half_size(float size) const
{
    return get_half_size(size) * DraggingScaleFactor;
}

void GLGizmoBase::Grabber::register_raycasters_for_picking(int id)
{
    picking_id = id;
    // registration will happen on next call to render()
}

void GLGizmoBase::Grabber::unregister_raycasters_for_picking()
{
    wxGetApp().plater()->canvas3D()->remove_raycasters_for_picking(SceneRaycaster::EType::Gizmo, picking_id);
    picking_id = -1;
    raycasters = { nullptr };
}

void GLGizmoBase::Grabber::render(float size, const ColorRGBA& render_color)
{
    GLShaderProgram* shader = wxGetApp().get_current_shader();
    if (shader == nullptr)
        return;

    if (!s_cube.model.is_initialized()) {
        // This cannot be done in constructor, OpenGL is not yet
        // initialized at that point (on Linux at least).
        indexed_triangle_set its = its_make_cube(1.0, 1.0, 1.0);
        its_translate(its, -0.5f * Vec3f::Ones());
        s_cube.model.init_from(its);
        s_cube.mesh_raycaster = std::make_unique<MeshRaycaster>(std::make_shared<const TriangleMesh>(std::move(its)));
    }

    if (!s_cone.model.is_initialized()) {
        indexed_triangle_set its = its_make_cone(0.375, 1.5, double(PI) / 18.0);
        s_cone.model.init_from(its);
        s_cone.mesh_raycaster = std::make_unique<MeshRaycaster>(std::make_shared<const TriangleMesh>(std::move(its)));
    }

    const float half_size = dragging ? get_dragging_half_size(size) : get_half_size(size);

    s_cube.model.set_color(render_color);
    s_cone.model.set_color(render_color);

    const Camera& camera = wxGetApp().plater()->get_camera();
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());
    const Transform3d& view_matrix = camera.get_view_matrix();
    const Matrix3d view_matrix_no_offset = view_matrix.matrix().block(0, 0, 3, 3);
    std::vector<Transform3d> elements_matrices(GRABBER_ELEMENTS_MAX_COUNT, Transform3d::Identity());
    elements_matrices[0] = matrix * Geometry::translation_transform(center) * Geometry::rotation_transform(angles) * Geometry::scale_transform(2.0 * half_size);
    Transform3d view_model_matrix = view_matrix * elements_matrices[0];

    shader->set_uniform("view_model_matrix", view_model_matrix);
    Matrix3d view_normal_matrix = view_matrix_no_offset * elements_matrices[0].matrix().block(0, 0, 3, 3).inverse().transpose();
    shader->set_uniform("view_normal_matrix", view_normal_matrix);
    s_cube.model.render();

    auto render_extension = [&view_matrix, &view_matrix_no_offset, shader](const Transform3d& matrix) {
        const Transform3d view_model_matrix = view_matrix * matrix;
        shader->set_uniform("view_model_matrix", view_model_matrix);
        const Matrix3d view_normal_matrix = view_matrix_no_offset * matrix.matrix().block(0, 0, 3, 3).inverse().transpose();
        shader->set_uniform("view_normal_matrix", view_normal_matrix);
        s_cone.model.render();
    };

    if ((int(extensions) & int(GLGizmoBase::EGrabberExtension::PosX)) != 0) {
        elements_matrices[1] = elements_matrices[0] * Geometry::translation_transform(Vec3d::UnitX()) * Geometry::rotation_transform({ 0.0, 0.5 * double(PI), 0.0 });
        render_extension(elements_matrices[1]);
    }
    if ((int(extensions) & int(GLGizmoBase::EGrabberExtension::NegX)) != 0) {
        elements_matrices[2] = elements_matrices[0] * Geometry::translation_transform(-Vec3d::UnitX()) * Geometry::rotation_transform({ 0.0, -0.5 * double(PI), 0.0 });
        render_extension(elements_matrices[2]);
    }
    if ((int(extensions) & int(GLGizmoBase::EGrabberExtension::PosY)) != 0) {
        elements_matrices[3] = elements_matrices[0] * Geometry::translation_transform(Vec3d::UnitY()) * Geometry::rotation_transform({ -0.5 * double(PI), 0.0, 0.0 });
        render_extension(elements_matrices[3]);
    }
    if ((int(extensions) & int(GLGizmoBase::EGrabberExtension::NegY)) != 0) {
        elements_matrices[4] = elements_matrices[0] * Geometry::translation_transform(-Vec3d::UnitY()) * Geometry::rotation_transform({ 0.5 * double(PI), 0.0, 0.0 });
        render_extension(elements_matrices[4]);
    }
    if ((int(extensions) & int(GLGizmoBase::EGrabberExtension::PosZ)) != 0) {
        elements_matrices[5] = elements_matrices[0] * Geometry::translation_transform(Vec3d::UnitZ());
        render_extension(elements_matrices[5]);
    }
    if ((int(extensions) & int(GLGizmoBase::EGrabberExtension::NegZ)) != 0) {
        elements_matrices[6] = elements_matrices[0] * Geometry::translation_transform(-Vec3d::UnitZ()) * Geometry::rotation_transform({ double(PI), 0.0, 0.0 });
        render_extension(elements_matrices[6]);
    }

    if (raycasters[0] == nullptr) {
        GLCanvas3D& canvas = *wxGetApp().plater()->canvas3D();
        raycasters[0] = canvas.add_raycaster_for_picking(SceneRaycaster::EType::Gizmo, picking_id, *s_cube.mesh_raycaster, elements_matrices[0]);
        if ((int(extensions) & int(GLGizmoBase::EGrabberExtension::PosX)) != 0)
            raycasters[1] = canvas.add_raycaster_for_picking(SceneRaycaster::EType::Gizmo, picking_id, *s_cone.mesh_raycaster, elements_matrices[1]);
        if ((int(extensions) & int(GLGizmoBase::EGrabberExtension::NegX)) != 0)
            raycasters[2] = canvas.add_raycaster_for_picking(SceneRaycaster::EType::Gizmo, picking_id, *s_cone.mesh_raycaster, elements_matrices[2]);
        if ((int(extensions) & int(GLGizmoBase::EGrabberExtension::PosY)) != 0)
            raycasters[3] = canvas.add_raycaster_for_picking(SceneRaycaster::EType::Gizmo, picking_id, *s_cone.mesh_raycaster, elements_matrices[3]);
        if ((int(extensions) & int(GLGizmoBase::EGrabberExtension::NegY)) != 0)
            raycasters[4] = canvas.add_raycaster_for_picking(SceneRaycaster::EType::Gizmo, picking_id, *s_cone.mesh_raycaster, elements_matrices[4]);
        if ((int(extensions) & int(GLGizmoBase::EGrabberExtension::PosZ)) != 0)
            raycasters[5] = canvas.add_raycaster_for_picking(SceneRaycaster::EType::Gizmo, picking_id, *s_cone.mesh_raycaster, elements_matrices[5]);
        if ((int(extensions) & int(GLGizmoBase::EGrabberExtension::NegZ)) != 0)
            raycasters[6] = canvas.add_raycaster_for_picking(SceneRaycaster::EType::Gizmo, picking_id, *s_cone.mesh_raycaster, elements_matrices[6]);
    }
    else {
        for (size_t i = 0; i < GRABBER_ELEMENTS_MAX_COUNT; ++i) {
            if (raycasters[i] != nullptr)
                raycasters[i]->set_transform(elements_matrices[i]);
        }
    }
}

GLGizmoBase::GLGizmoBase(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : m_parent(parent)
    , m_group_id(-1)
    , m_state(Off)
    , m_shortcut_key(NO_SHORTCUT_KEY_VALUE)
    , m_icon_filename(icon_filename)
    , m_sprite_id(sprite_id)
    , m_imgui(wxGetApp().imgui())
{
}


std::string GLGizmoBase::get_action_snapshot_name() const
{
    return "Gizmo action";
}

void GLGizmoBase::set_hover_id(int id)
{
    // do not change hover id during dragging
    assert(!m_dragging);

    // allow empty grabbers when not using grabbers but use hover_id - flatten, rotate
//    if (!m_grabbers.empty() && id >= (int) m_grabbers.size())
//        return;
    
    m_hover_id = id;
    on_set_hover_id();    
}

bool GLGizmoBase::update_items_state()
{
    bool res = m_dirty;
    m_dirty  = false;
    return res;
}

void GLGizmoBase::register_grabbers_for_picking()
{
    for (size_t i = 0; i < m_grabbers.size(); ++i) {
        m_grabbers[i].register_raycasters_for_picking((m_group_id >= 0) ? m_group_id : i);
    }
}

void GLGizmoBase::unregister_grabbers_for_picking()
{
    for (size_t i = 0; i < m_grabbers.size(); ++i) {
        m_grabbers[i].unregister_raycasters_for_picking();
    }
}

void GLGizmoBase::render_grabbers(const BoundingBoxf3& box) const
{
    render_grabbers((float)((box.size().x() + box.size().y() + box.size().z()) / 3.0));
}

void GLGizmoBase::render_grabbers(float size) const
{
    render_grabbers(0, m_grabbers.size() - 1, size, false);
}

void GLGizmoBase::render_grabbers(size_t first, size_t last, float size, bool force_hover) const
{
    GLShaderProgram* shader = wxGetApp().get_shader("gouraud_light");
    if (shader == nullptr)
        return;
    shader->start_using();
    shader->set_uniform("emission_factor", 0.1f);
    glsafe(::glDisable(GL_CULL_FACE));
    for (size_t i = first; i <= last; ++i) {
        if (m_grabbers[i].enabled)
            m_grabbers[i].render(force_hover ? true : m_hover_id == (int)i, size);
    }
    glsafe(::glEnable(GL_CULL_FACE));
    shader->stop_using();
}

// help function to process grabbers
// call start_dragging, stop_dragging, on_dragging
bool GLGizmoBase::use_grabbers(const wxMouseEvent &mouse_event) {
    bool is_dragging_finished = false;
    if (mouse_event.Moving()) { 
        // it should not happen but for sure
        assert(!m_dragging);
        if (m_dragging) is_dragging_finished = true;
        else return false; 
    } 

    if (mouse_event.LeftDown()) {
        Selection &selection = m_parent.get_selection();
        if (!selection.is_empty() && m_hover_id != -1 /* &&
            (m_grabbers.empty() || m_hover_id < static_cast<int>(m_grabbers.size()))*/) {
            selection.setup_cache();

            m_dragging = true;
            for (auto &grabber : m_grabbers) grabber.dragging = false;
//            if (!m_grabbers.empty() && m_hover_id < int(m_grabbers.size()))
//                m_grabbers[m_hover_id].dragging = true;
            
            on_start_dragging();

            // Let the plater know that the dragging started
            m_parent.post_event(SimpleEvent(EVT_GLCANVAS_MOUSE_DRAGGING_STARTED));
            m_parent.set_as_dirty();
            return true;
        }
    } else if (m_dragging) {
        // when mouse cursor leave window than finish actual dragging operation
        bool is_leaving = mouse_event.Leaving();
        if (mouse_event.Dragging()) {
            Point      mouse_coord(mouse_event.GetX(), mouse_event.GetY());
            auto       ray = m_parent.mouse_ray(mouse_coord);
            UpdateData data(ray, mouse_coord);

            on_dragging(data);

            wxGetApp().obj_manipul()->set_dirty();
            m_parent.set_as_dirty();
            return true;
        }
        else if (mouse_event.LeftUp() || is_leaving || is_dragging_finished) {
            do_stop_dragging(is_leaving);
            return true;
        }
    }
    return false;
}

void GLGizmoBase::do_stop_dragging(bool perform_mouse_cleanup)
{
    for (auto& grabber : m_grabbers) grabber.dragging = false;
    m_dragging = false;

    // NOTE: This should be part of GLCanvas3D
    // Reset hover_id when leave window
    if (perform_mouse_cleanup) m_parent.mouse_up_cleanup();

    on_stop_dragging();

    // There is prediction that after draggign, data are changed
    // Data are updated twice also by canvas3D::reload_scene.
    // Should be fixed.
    m_parent.get_gizmos_manager().update_data();

    wxGetApp().obj_manipul()->set_dirty();

    // Let the plater know that the dragging finished, so a delayed
    // refresh of the scene with the background processing data should
    // be performed.
    m_parent.post_event(SimpleEvent(EVT_GLCANVAS_MOUSE_DRAGGING_FINISHED));
    // updates camera target constraints
    m_parent.refresh_camera_scene_box();
}

std::string GLGizmoBase::format(float value, unsigned int decimals) const
{
    return Slic3r::string_printf("%.*f", decimals, value);
}

void GLGizmoBase::set_dirty() {
    m_dirty = true;
}



void GLGizmoBase::render_input_window(float x, float y, float bottom_limit)
{
    on_render_input_window(x, y, bottom_limit);
    if (m_first_input_window_render) {
        // imgui windows that don't have an initial size needs to be processed once to get one
        // and are not rendered in the first frame
        // so, we forces to render another frame the first time the imgui window is shown
        // https://github.com/ocornut/imgui/issues/2949
        m_parent.set_as_dirty();
        m_parent.request_extra_frame();
        m_first_input_window_render = false;
    }
}



std::string GLGizmoBase::get_name(bool include_shortcut) const
{
    std::string out = on_get_name();
    if (!include_shortcut) return out;

    int key = get_shortcut_key();
    assert(key==NO_SHORTCUT_KEY_VALUE || (key >= WXK_CONTROL_A && key <= WXK_CONTROL_Z));
    out += std::string(" [") + char(int('A') + key - int(WXK_CONTROL_A)) + "]";
    return out;
}


} // namespace GUI
} // namespace Slic3r

