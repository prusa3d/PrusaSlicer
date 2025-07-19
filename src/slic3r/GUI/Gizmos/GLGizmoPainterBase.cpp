///|/ Copyright (c) Prusa Research 2019 - 2023 Oleksandra Iushchenko @YuSanka, Lukáš Matěna @lukasmatena, Enrico Turri @enricoturri1966, Vojtěch Bubník @bubnikv, Filip Sykala @Jony01, Lukáš Hejl @hejllukas
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "GLGizmoPainterBase.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/Gizmos/GLGizmosCommon.hpp"

#include <GL/glew.h>

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/OpenGLManager.hpp"
#include "slic3r/Utils/UndoRedo.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <memory>
#include <numeric>
#include <optional>
#include <libslic3r/TriangleMeshSlicer.hpp>

namespace Slic3r::GUI {

std::shared_ptr<GLModel> GLGizmoPainterBase::s_sphere = nullptr;

GLGizmoPainterBase::GLGizmoPainterBase(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoBase(parent, icon_filename, sprite_id)
{
}

GLGizmoPainterBase::~GLGizmoPainterBase()
{
    if (s_sphere != nullptr)
        s_sphere.reset();
}

void GLGizmoPainterBase::data_changed(bool is_serializing)
{
    if (m_state != On)
        return;

    const ModelObject* mo = m_c->selection_info() ? m_c->selection_info()->model_object() : nullptr;
    const Selection &  selection = m_parent.get_selection();
    if (mo && selection.is_from_single_instance()
     && (m_schedule_update || mo->id() != m_old_mo_id || mo->volumes.size() != m_old_volumes_size))
    {
        update_from_model_object();
        m_old_mo_id = mo->id();
        m_old_volumes_size = mo->volumes.size();
        m_schedule_update = false;
    }
}

GLGizmoPainterBase::ClippingPlaneDataWrapper GLGizmoPainterBase::get_clipping_plane_data() const
{
    ClippingPlaneDataWrapper clp_data_out{{0.f, 0.f, 1.f, FLT_MAX}, {-FLT_MAX, FLT_MAX}};
    // Take care of the clipping plane. The normal of the clipping plane is
    // saved with opposite sign than we need to pass to OpenGL (FIXME)
    if (bool clipping_plane_active = m_c->object_clipper()->get_position() != 0.; clipping_plane_active) {
        const ClippingPlane *clp = m_c->object_clipper()->get_clipping_plane();
        for (size_t i = 0; i < 3; ++i)
            clp_data_out.clp_dataf[i] = -1.f * float(clp->get_data()[i]);
        clp_data_out.clp_dataf[3] = float(clp->get_data()[3]);
    }

    // z_range is calculated in the same way as in GLCanvas3D::_render_objects(GLVolumeCollection::ERenderType type)
    if (m_c->get_canvas()->get_use_clipping_planes()) {
        const std::array<ClippingPlane, 2> &clps = m_c->get_canvas()->get_clipping_planes();
        clp_data_out.z_range                     = {float(-clps[0].get_data()[3]), float(clps[1].get_data()[3])};
    }

    return clp_data_out;
}

void GLGizmoPainterBase::render_triangles(const Selection& selection) const
{
    auto* shader = wxGetApp().get_shader("gouraud");
    if (! shader)
        return;
    shader->start_using();
    shader->set_uniform("slope.actived", false);
    shader->set_uniform("print_volume.type", 0);
    shader->set_uniform("clipping_plane", this->get_clipping_plane_data().clp_dataf);
    ScopeGuard guard([shader]() { if (shader) shader->stop_using(); });

    const ModelObject *mo      = m_c->selection_info()->model_object();
    int                mesh_id = -1;
    for (const ModelVolume* mv : mo->volumes) {
        if (! mv->is_model_part())
            continue;

        ++mesh_id;

        const Transform3d trafo_matrix =
            mo->instances[selection.get_instance_idx()]->get_transformation().get_matrix() *
            mv->get_matrix();

        bool is_left_handed = trafo_matrix.matrix().determinant() < 0.;
        if (is_left_handed)
            glsafe(::glFrontFace(GL_CW));

        const Camera& camera = wxGetApp().plater()->get_camera();
        const Transform3d& view_matrix = camera.get_view_matrix();
        shader->set_uniform("view_model_matrix", view_matrix * trafo_matrix);
        shader->set_uniform("projection_matrix", camera.get_projection_matrix());
        const Matrix3d view_normal_matrix = view_matrix.matrix().block(0, 0, 3, 3) * trafo_matrix.matrix().block(0, 0, 3, 3).inverse().transpose();
        shader->set_uniform("view_normal_matrix", view_normal_matrix);

        // For printers with multiple extruders, it is necessary to pass trafo_matrix
        // to the shader input variable print_box.volume_world_matrix before
        // rendering the painted triangles. When this matrix is not set, the
        // wrong transformation matrix is used for "Clipping of view".
        shader->set_uniform("volume_world_matrix", trafo_matrix);

        m_triangle_selectors[mesh_id]->render(m_imgui, trafo_matrix);
        if (is_left_handed)
            glsafe(::glFrontFace(GL_CCW));
    }
}

void GLGizmoPainterBase::render_cursor()
{
    // First check that the mouse pointer is on an object.
    const ModelObject* mo = m_c->selection_info()->model_object();
    const Selection& selection = m_parent.get_selection();
    const ModelInstance* mi = mo->instances[selection.get_instance_idx()];
    const Camera& camera = wxGetApp().plater()->get_camera();

    // Precalculate transformations of individual meshes.
    std::vector<Transform3d> trafo_matrices;
    for (const ModelVolume* mv : mo->volumes) {
        if (mv->is_model_part())
            trafo_matrices.emplace_back(mi->get_transformation().get_matrix() * mv->get_matrix());
    }
    // Raycast and return if there's no hit.
    update_raycast_cache(m_parent.get_local_mouse_position(), camera, trafo_matrices);
    if (m_rr.mesh_id == -1)
        return;

    if (m_tool_type == ToolType::BRUSH) {
        if (m_cursor_type == TriangleSelector::CursorType::SPHERE)
            render_cursor_sphere(trafo_matrices[m_rr.mesh_id]);
        else if (m_cursor_type == TriangleSelector::CursorType::CIRCLE)
            render_cursor_circle();
    } else if (m_tool_type == ToolType::HEIGHT_RANGE) {
        render_cursor_height_range(trafo_matrices[m_rr.mesh_id]);
    }
}

void GLGizmoPainterBase::render_cursor_circle()
{
    const Size cnv_size = m_parent.get_canvas_size();
    const float cnv_width  = float(cnv_size.get_width());
    const float cnv_height = float(cnv_size.get_height());
    if (cnv_width == 0.0f || cnv_height == 0.0f)
        return;

    const float cnv_inv_width  = 1.0f / cnv_width;
    const float cnv_inv_height = 1.0f / cnv_height;

    const Vec2d center = m_parent.get_local_mouse_position();
    const float zoom = float(wxGetApp().plater()->get_camera().get_zoom());
    const float radius = m_cursor_radius * zoom;

#if !SLIC3R_OPENGL_ES
    if (!OpenGLManager::get_gl_info().is_core_profile())
        glsafe(::glLineWidth(1.5f));
#endif // !SLIC3R_OPENGL_ES

    glsafe(::glDisable(GL_DEPTH_TEST));

#if !SLIC3R_OPENGL_ES
    if (!OpenGLManager::get_gl_info().is_core_profile()) {
        glsafe(::glPushAttrib(GL_ENABLE_BIT));
        glsafe(::glLineStipple(4, 0xAAAA));
        glsafe(::glEnable(GL_LINE_STIPPLE));
    }
#endif // !SLIC3R_OPENGL_ES

    if (!m_circle.is_initialized() || !m_old_center.isApprox(center) || std::abs(m_old_cursor_radius - radius) > EPSILON) {
        m_old_cursor_radius = radius;
        m_old_center = center;
        m_circle.reset();

        GLModel::Geometry init_data;
        unsigned int steps_count = 0;
#if !SLIC3R_OPENGL_ES
        if (OpenGLManager::get_gl_info().is_core_profile()) {
#endif // !SLIC3R_OPENGL_ES
            steps_count = (unsigned int)(2 * (4 + int(252 * (zoom - 1.0f) / (250.0f - 1.0f))));
            init_data.format = { GLModel::Geometry::EPrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P2 };
#if !SLIC3R_OPENGL_ES
        }
        else {
            steps_count = 32;
            init_data.format = { GLModel::Geometry::EPrimitiveType::LineLoop, GLModel::Geometry::EVertexLayout::P2 };
        }
#endif // !SLIC3R_OPENGL_ES
        const float step_size = 2.0f * float(PI) / float(steps_count);
        init_data.color  = { 0.0f, 1.0f, 0.3f, 1.0f };
        init_data.reserve_vertices(steps_count);
        init_data.reserve_indices(steps_count);

        // vertices + indices
        for (unsigned int i = 0; i < steps_count; ++i) {
#if !SLIC3R_OPENGL_ES
            if (OpenGLManager::get_gl_info().is_core_profile()) {
#endif // !SLIC3R_OPENGL_ES
                if (i % 2 != 0) continue;

                const float angle_i = float(i) * step_size;
                const unsigned int j = (i + 1) % steps_count;
                const float angle_j = float(j) * step_size;
                const Vec2d v_i(::cos(angle_i), ::sin(angle_i));
                const Vec2d v_j(::cos(angle_j), ::sin(angle_j));
                init_data.add_vertex(Vec2f(v_i.x(), v_i.y()));
                init_data.add_vertex(Vec2f(v_j.x(), v_j.y()));
                const size_t vcount = init_data.vertices_count();
                init_data.add_line(vcount - 2, vcount - 1);
#if !SLIC3R_OPENGL_ES
            }
            else {
                const float angle = float(i) * step_size;
                init_data.add_vertex(Vec2f(2.0f * ((center.x() + ::cos(angle) * radius) * cnv_inv_width - 0.5f),
                  -2.0f * ((center.y() + ::sin(angle) * radius) * cnv_inv_height - 0.5f)));
                init_data.add_index(i);
            }
#endif // !SLIC3R_OPENGL_ES
        }

        m_circle.init_from(std::move(init_data));
    }

#if SLIC3R_OPENGL_ES
    GLShaderProgram* shader = wxGetApp().get_shader("dashed_lines");
#else
    GLShaderProgram* shader = OpenGLManager::get_gl_info().is_core_profile() ? wxGetApp().get_shader("dashed_thick_lines") : wxGetApp().get_shader("flat");
#endif // SLIC3R_OPENGL_ES
    if (shader != nullptr) {
        shader->start_using();
#if !SLIC3R_OPENGL_ES
        if (OpenGLManager::get_gl_info().is_core_profile()) {
#endif // !SLIC3R_OPENGL_ES
            const Transform3d view_model_matrix = Geometry::translation_transform(Vec3d(2.0f * (center.x() * cnv_inv_width - 0.5f), -2.0f * (center.y() * cnv_inv_height - 0.5f), 0.0)) *
                Geometry::scale_transform(Vec3d(2.0f * radius * cnv_inv_width, 2.0f * radius * cnv_inv_height, 1.0f));
            shader->set_uniform("view_model_matrix", view_model_matrix);
#if !SLIC3R_OPENGL_ES
        }
        else
            shader->set_uniform("view_model_matrix", Transform3d::Identity());
#endif // !SLIC3R_OPENGL_ES
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
        m_circle.render();
        shader->stop_using();
    }

#if !SLIC3R_OPENGL_ES
    if (!OpenGLManager::get_gl_info().is_core_profile())
        glsafe(::glPopAttrib());
#endif // !SLIC3R_OPENGL_ES
    glsafe(::glEnable(GL_DEPTH_TEST));
}

void GLGizmoPainterBase::render_cursor_sphere(const Transform3d& trafo) const
{
    if (s_sphere == nullptr) {
        s_sphere = std::make_shared<GLModel>();
        s_sphere->init_from(its_make_sphere(1.0, double(PI) / 12.0));
    }

    GLShaderProgram* shader = wxGetApp().get_shader("flat");
    if (shader == nullptr)
        return;

    const Transform3d complete_scaling_matrix_inverse = Geometry::Transformation(trafo).get_scaling_factor_matrix().inverse();

    ColorRGBA render_color = { 0.0f, 0.0f, 0.0f, 0.25f };
    if (m_button_down == Button::Left)
        render_color = this->get_cursor_sphere_left_button_color();
    else if (m_button_down == Button::Right)
        render_color = this->get_cursor_sphere_right_button_color();

    shader->start_using();

    const Camera& camera = wxGetApp().plater()->get_camera();
    Transform3d view_model_matrix = camera.get_view_matrix() * trafo *
        Geometry::translation_transform(m_rr.hit.cast<double>()) * complete_scaling_matrix_inverse *
        Geometry::scale_transform(m_cursor_radius * Vec3d::Ones());

    shader->set_uniform("view_model_matrix", view_model_matrix);
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());

    const bool is_left_handed = Geometry::Transformation(view_model_matrix).is_left_handed();
    if (is_left_handed)
        glsafe(::glFrontFace(GL_CW));

    assert(s_sphere != nullptr);
    s_sphere->set_color(render_color);
    s_sphere->render();

    if (is_left_handed)
        glsafe(::glFrontFace(GL_CCW));

    shader->stop_using();
}

void GLGizmoPainterBase::render_cursor_height_range(const Transform3d &trafo) const {
    const ModelObject   &model_object   = *m_c->selection_info()->model_object();
    const Vec3f          mesh_hit_world = (trafo * m_rr.hit.cast<double>()).cast<float>();

    const std::array<float, 2> z_range = {mesh_hit_world.z() - m_height_range_z_range / 2.f,
                                          mesh_hit_world.z() + m_height_range_z_range / 2.f};

    struct SlicedPolygonsAtZ
    {
        float    z;
        Polygons polygons;
    };

    std::vector<SlicedPolygonsAtZ> sliced_polygons_per_z;
    for (const float z: z_range) {
        sliced_polygons_per_z.push_back({z, slice_mesh(model_object.volumes[m_rr.mesh_id]->mesh().its, z, MeshSlicingParams(trafo))});
    }

    const size_t max_vertices_cnt = std::accumulate(sliced_polygons_per_z.cbegin(), sliced_polygons_per_z.cend(), 0,
                                                    [](const size_t sum, const SlicedPolygonsAtZ &polygons_at_z) {
                                                        return sum + count_points(polygons_at_z.polygons);
                                                    });

    GLModel::Geometry z_range_geometry;
    z_range_geometry.format = {GLModel::Geometry::EPrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P3};
    z_range_geometry.reserve_vertices(max_vertices_cnt);
    z_range_geometry.reserve_indices(max_vertices_cnt);
    z_range_geometry.color = ColorRGBA::WHITE();

    size_t vertices_cnt = 0;
    for (const SlicedPolygonsAtZ &polygons_at_z : sliced_polygons_per_z) {
        for (const Polygon &polygon: polygons_at_z.polygons) {
            for (const Point &pt: polygon.points)
                z_range_geometry.add_vertex(Vec3f(unscaled<float>(pt.x()), unscaled<float>(pt.y()), polygons_at_z.z));

            for (size_t pt_idx = 1; pt_idx < polygon.points.size(); ++pt_idx)
                z_range_geometry.add_line(vertices_cnt + pt_idx - 1, vertices_cnt + pt_idx);

            z_range_geometry.add_line(vertices_cnt + polygon.points.size() - 1, vertices_cnt);

            vertices_cnt += polygon.points.size();
        }
    }

    GLModel z_range_model;
    if (!z_range_geometry.is_empty())
        z_range_model.init_from(std::move(z_range_geometry));

    const Camera     &camera            = wxGetApp().plater()->get_camera();
    const Transform3d view_model_matrix = camera.get_view_matrix();

    GLShaderProgram *shader = wxGetApp().get_shader("mm_contour");
    if (shader == nullptr)
        return;

    shader->start_using();
    shader->set_uniform("offset", OpenGLManager::get_gl_info().is_mesa() ? 0.0005 : 0.00001);
    shader->set_uniform("view_model_matrix", view_model_matrix);
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());
    ScopeGuard guard([shader]() { if (shader) shader->stop_using(); });

    const bool is_left_handed = Geometry::Transformation(view_model_matrix).is_left_handed();
    if (is_left_handed)
        glsafe(::glFrontFace(GL_CW));

    z_range_model.render();

    if (is_left_handed)
        glsafe(::glFrontFace(GL_CCW));

    shader->stop_using();
}

bool GLGizmoPainterBase::is_mesh_point_clipped(const Vec3d& point, const Transform3d& trafo) const
{
    if (m_c->object_clipper()->get_position() == 0.)
        return false;

    auto sel_info = m_c->selection_info();
    Vec3d transformed_point =  trafo * point;
    transformed_point(2) += sel_info->get_sla_shift();
    return m_c->object_clipper()->get_clipping_plane()->is_point_clipped(transformed_point);
}

// Interpolate points between the previous and current mouse positions, which are then projected onto the object.
// Returned projected mouse positions are grouped by mesh_idx. It may contain multiple std::vector<GLGizmoPainterBase::ProjectedMousePosition>
// with the same mesh_idx, but all items in std::vector<GLGizmoPainterBase::ProjectedMousePosition> always have the same mesh_idx.
std::vector<std::vector<GLGizmoPainterBase::ProjectedMousePosition>> GLGizmoPainterBase::get_projected_mouse_positions(const Vec2d &mouse_position, const double resolution, const std::vector<Transform3d> &trafo_matrices) const
{
    // List of mouse positions that will be used as seeds for painting.
    std::vector<Vec2d> mouse_positions{mouse_position};
    if (m_last_mouse_click != Vec2d::Zero()) {
        // In case current mouse position is far from the last one,
        // add several positions from between into the list, so there
        // are no gaps in the painted region.
        if (size_t patches_in_between = size_t((mouse_position - m_last_mouse_click).norm() / resolution); patches_in_between > 0) {
            const Vec2d diff = (m_last_mouse_click - mouse_position) / (patches_in_between + 1);
            for (size_t patch_idx = 1; patch_idx <= patches_in_between; ++patch_idx)
                mouse_positions.emplace_back(mouse_position + patch_idx * diff);
            mouse_positions.emplace_back(m_last_mouse_click);
        }
    }

    const Camera                       &camera = wxGetApp().plater()->get_camera();
    std::vector<ProjectedMousePosition> mesh_hit_points;
    mesh_hit_points.reserve(mouse_positions.size());

    // In mesh_hit_points only the last item could have mesh_id == -1, any other items mustn't.
    for (const Vec2d &mp : mouse_positions) {
        update_raycast_cache(mp, camera, trafo_matrices);
        mesh_hit_points.push_back({m_rr.hit, m_rr.mesh_id, m_rr.facet});
        if (m_rr.mesh_id == -1)
            break;
    }

    // Divide mesh_hit_points into groups with the same mesh_idx. It may contain multiple groups with the same mesh_idx.
    std::vector<std::vector<ProjectedMousePosition>> mesh_hit_points_by_mesh;
    for (size_t prev_mesh_hit_point = 0, curr_mesh_hit_point = 0; curr_mesh_hit_point < mesh_hit_points.size(); ++curr_mesh_hit_point) {
        size_t next_mesh_hit_point = curr_mesh_hit_point + 1;
        if (next_mesh_hit_point >= mesh_hit_points.size() || mesh_hit_points[curr_mesh_hit_point].mesh_idx != mesh_hit_points[next_mesh_hit_point].mesh_idx) {
            mesh_hit_points_by_mesh.emplace_back();
            mesh_hit_points_by_mesh.back().insert(mesh_hit_points_by_mesh.back().end(), mesh_hit_points.begin() + int(prev_mesh_hit_point), mesh_hit_points.begin() + int(next_mesh_hit_point));
            prev_mesh_hit_point = next_mesh_hit_point;
        }
    }

    auto on_same_facet = [](std::vector<ProjectedMousePosition> &hit_points) -> bool {
        for (const ProjectedMousePosition &mesh_hit_point : hit_points)
            if (mesh_hit_point.facet_idx != hit_points.front().facet_idx)
                return false;
        return true;
    };

    struct Plane
    {
        Vec3d origin;
        Vec3d first_axis;
        Vec3d second_axis;
    };
    auto find_plane = [](std::vector<ProjectedMousePosition> &hit_points) -> std::optional<Plane> {
        assert(hit_points.size() >= 3);
        for (size_t third_idx = 2; third_idx < hit_points.size(); ++third_idx) {
            const Vec3d &first_point  = hit_points[third_idx - 2].mesh_hit.cast<double>();
            const Vec3d &second_point = hit_points[third_idx - 1].mesh_hit.cast<double>();
            const Vec3d &third_point  = hit_points[third_idx].mesh_hit.cast<double>();

            const Vec3d  first_vec    = first_point - second_point;
            const Vec3d  second_vec   = third_point - second_point;

            // If three points aren't collinear, then there exists only one plane going through all points.
            if (first_vec.cross(second_vec).squaredNorm() > sqr(EPSILON)) {
                const Vec3d first_axis_vec_n = first_vec.normalized();
                // Make second_vec perpendicular to first_axis_vec_n using Gram–Schmidt orthogonalization process
                const Vec3d second_axis_vec_n = (second_vec - (first_vec.dot(second_vec) / first_vec.dot(first_vec)) * first_vec).normalized();
                return Plane{second_point, first_axis_vec_n, second_axis_vec_n};
            }
        }

        return std::nullopt;
    };

    for(std::vector<ProjectedMousePosition> &hit_points : mesh_hit_points_by_mesh) {
        assert(!hit_points.empty());
        if (hit_points.back().mesh_idx == -1)
            break;

        if (hit_points.size() <= 2)
            continue;

        if (on_same_facet(hit_points)) {
            hit_points = {hit_points.front(), hit_points.back()};
        } else if (std::optional<Plane> plane = find_plane(hit_points); plane) {
            Polyline polyline;
            polyline.points.reserve(hit_points.size());
            // Project hit_points into its plane to simplified them in the next step.
            for (auto &hit_point : hit_points) {
                const Vec3d &point  = hit_point.mesh_hit.cast<double>();
                const double x_cord = plane->first_axis.dot(point - plane->origin);
                const double y_cord = plane->second_axis.dot(point - plane->origin);
                polyline.points.emplace_back(scale_(x_cord), scale_(y_cord));
            }

            polyline.simplify(scale_(m_cursor_radius) / 10.);

            const int                           mesh_idx = hit_points.front().mesh_idx;
            std::vector<ProjectedMousePosition> new_hit_points;
            new_hit_points.reserve(polyline.points.size());
            // Project 2D simplified hit_points beck to 3D.
            for (const Point &point : polyline.points) {
                const double x_cord        = unscale<double>(point.x());
                const double y_cord        = unscale<double>(point.y());
                const Vec3d  new_hit_point = plane->origin + x_cord * plane->first_axis + y_cord * plane->second_axis;
                const int    facet_idx     = m_c->raycaster()->raycasters()[mesh_idx]->get_closest_facet(new_hit_point.cast<float>());
                new_hit_points.push_back({new_hit_point.cast<float>(), mesh_idx, size_t(facet_idx)});
            }

            hit_points = new_hit_points;
        } else {
            hit_points = {hit_points.front(), hit_points.back()};
        }
    }

    return mesh_hit_points_by_mesh;
}

// Following function is called from GLCanvas3D to inform the gizmo about a mouse/keyboard event.
// The gizmo has an opportunity to react - if it does, it should return true so that the Canvas3D is
// aware that the event was reacted to and stops trying to make different sense of it. If the gizmo
// concludes that the event was not intended for it, it should return false.
bool GLGizmoPainterBase::gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down)
{
    if (action == SLAGizmoEventType::MouseWheelUp || action == SLAGizmoEventType::MouseWheelDown) {
        // On Windows Right ALT could be reported as Left ALT + Control.
        // In such cases, we want to prioritize ALT over Control.
        if (!alt_down && control_down) {
            double pos = m_c->object_clipper()->get_position();
            pos = action == SLAGizmoEventType::MouseWheelDown
                      ? std::max(0., pos - 0.01)
                      : std::min(1., pos + 0.01);
            m_c->object_clipper()->set_position_by_ratio(pos, true);
            return true;
        } else if (alt_down) {
            if (m_tool_type == ToolType::BRUSH && (m_cursor_type == TriangleSelector::CursorType::SPHERE || m_cursor_type == TriangleSelector::CursorType::CIRCLE)) {
                m_cursor_radius = action == SLAGizmoEventType::MouseWheelDown ? std::max(m_cursor_radius - this->get_cursor_radius_step(), this->get_cursor_radius_min())
                                                                              : std::min(m_cursor_radius + this->get_cursor_radius_step(), this->get_cursor_radius_max());
                m_parent.set_as_dirty();
                return true;
            } else if (m_tool_type == ToolType::SMART_FILL || m_tool_type == ToolType::BUCKET_FILL) {
                float &fill_angle = (m_tool_type == ToolType::SMART_FILL) ? m_smart_fill_angle : m_bucket_fill_angle;
                fill_angle        = (action == SLAGizmoEventType::MouseWheelDown) ? std::max(fill_angle - SmartFillAngleStep, SmartFillAngleMin)
                                                                                  : std::min(fill_angle + SmartFillAngleStep, SmartFillAngleMax);

                m_parent.set_as_dirty();
                if (m_rr.mesh_id != -1) {
                    const Selection                       &selection      = m_parent.get_selection();
                    const ModelObject                     *mo             = m_c->selection_info()->model_object();
                    const ModelInstance                   *mi             = mo->instances[selection.get_instance_idx()];
                    const Transform3d                      trafo_matrix   = mi->get_transformation().get_matrix() * mo->volumes[m_rr.mesh_id]->get_matrix();
                    const TriangleSelector::ClippingPlane &clipping_plane = this->get_clipping_plane_in_volume_coordinates(trafo_matrix);

                    if (m_tool_type == ToolType::SMART_FILL) {
                        const Transform3d trafo_matrix_not_translate = mi->get_transformation().get_matrix_no_offset() * mo->volumes[m_rr.mesh_id]->get_matrix_no_offset();
                        m_triangle_selectors[m_rr.mesh_id]->seed_fill_select_triangles(m_rr.hit, int(m_rr.facet), trafo_matrix_not_translate, clipping_plane, m_smart_fill_angle, SmartFillGapArea,
                                                                                       m_paint_on_overhangs_only ? m_highlight_by_angle_threshold_deg : 0.f, TriangleSelector::ForceReselection::YES);
                    } else {
                        assert(m_tool_type == ToolType::BUCKET_FILL);
                        m_triangle_selectors[m_rr.mesh_id]->bucket_fill_select_triangles(m_rr.hit, int(m_rr.facet), clipping_plane, m_bucket_fill_angle, BucketFillGapArea,
                                                                                         TriangleSelector::BucketFillPropagate::YES, TriangleSelector::ForceReselection::YES);
                    }

                    m_triangle_selectors[m_rr.mesh_id]->request_update_render_data();
                    m_seed_fill_last_mesh_id = m_rr.mesh_id;
                }
                return true;
            } else if (m_tool_type == ToolType::HEIGHT_RANGE) {
                m_height_range_z_range = action == SLAGizmoEventType::MouseWheelDown ? std::max(m_height_range_z_range - HeightRangeZRangeStep, HeightRangeZRangeMin)
                                                                                     : std::min(m_height_range_z_range + HeightRangeZRangeStep, HeightRangeZRangeMax);
                m_parent.set_as_dirty();
                return true;
            }

            return false;
        }
    }

    if (action == SLAGizmoEventType::ResetClippingPlane) {
        m_c->object_clipper()->set_position_by_ratio(-1., false);
        return true;
    }

    if (action == SLAGizmoEventType::LeftDown
     || action == SLAGizmoEventType::RightDown
    || (action == SLAGizmoEventType::Dragging && m_button_down != Button::None)) {

        if (m_triangle_selectors.empty())
            return false;

        TriangleStateType new_state = TriangleStateType::NONE;
        if (! shift_down) {
            if (action == SLAGizmoEventType::Dragging)
                new_state = m_button_down == Button::Left ? this->get_left_button_state_type() : this->get_right_button_state_type();
            else
                new_state = action == SLAGizmoEventType::LeftDown ? this->get_left_button_state_type() : this->get_right_button_state_type();
        }

        const Camera        &camera                      = wxGetApp().plater()->get_camera();
        const Selection     &selection                   = m_parent.get_selection();
        const ModelObject   *mo                          = m_c->selection_info()->model_object();
        const ModelInstance *mi                          = mo->instances[selection.get_instance_idx()];
        const Transform3d   instance_trafo               = mi->get_transformation().get_matrix();
        const Transform3d   instance_trafo_not_translate = mi->get_transformation().get_matrix_no_offset();

        // Precalculate transformations of individual meshes.
        std::vector<Transform3d> trafo_matrices;
        std::vector<Transform3d> trafo_matrices_not_translate;
        for (const ModelVolume *mv : mo->volumes)
            if (mv->is_model_part()) {
                trafo_matrices.emplace_back(instance_trafo * mv->get_matrix());
                trafo_matrices_not_translate.emplace_back(instance_trafo_not_translate * mv->get_matrix_no_offset());
            }

        std::vector<std::vector<ProjectedMousePosition>> projected_mouse_positions_by_mesh = get_projected_mouse_positions(mouse_position, 1., trafo_matrices);
        m_last_mouse_click = Vec2d::Zero(); // only actual hits should be saved

        for (const std::vector<ProjectedMousePosition> &projected_mouse_positions : projected_mouse_positions_by_mesh) {
            assert(!projected_mouse_positions.empty());
            const int  mesh_idx                = projected_mouse_positions.front().mesh_idx;
            const bool dragging_while_painting = (action == SLAGizmoEventType::Dragging && m_button_down != Button::None);

            // The mouse button click detection is enabled when there is a valid hit.
            // Missing the object entirely
            // shall not capture the mouse.
            if (mesh_idx != -1)
                if (m_button_down == Button::None)
                    m_button_down = ((action == SLAGizmoEventType::LeftDown) ? Button::Left : Button::Right);

            // In case we have no valid hit, we can return. The event will be stopped when
            // dragging while painting (to prevent scene rotations and moving the object)
            if (mesh_idx == -1)
                return dragging_while_painting;

            const Transform3d &trafo_matrix               = trafo_matrices[mesh_idx];
            const Transform3d &trafo_matrix_not_translate = trafo_matrices_not_translate[mesh_idx];

            // Calculate direction from camera to the hit (in mesh coords):
            Vec3f camera_pos = (trafo_matrix.inverse() * camera.get_position()).cast<float>();

            assert(mesh_idx < int(m_triangle_selectors.size()));
            const TriangleSelector::ClippingPlane &clp = this->get_clipping_plane_in_volume_coordinates(trafo_matrix);
            if (m_tool_type == ToolType::SMART_FILL || m_tool_type == ToolType::BUCKET_FILL || (m_tool_type == ToolType::BRUSH && m_cursor_type == TriangleSelector::CursorType::POINTER)) {
                for (const ProjectedMousePosition &projected_mouse_position: projected_mouse_positions) {
                    assert(projected_mouse_position.mesh_idx == mesh_idx);
                    const Vec3f mesh_hit  = projected_mouse_position.mesh_hit;
                    const int   facet_idx = int(projected_mouse_position.facet_idx);

                    m_triangle_selectors[mesh_idx]->seed_fill_apply_on_triangles(new_state);

                    if (m_tool_type == ToolType::SMART_FILL) {
                        m_triangle_selectors[mesh_idx]->seed_fill_select_triangles(mesh_hit, facet_idx, trafo_matrix_not_translate, clp, m_smart_fill_angle, SmartFillGapArea,
                                                                                   (m_paint_on_overhangs_only ? m_highlight_by_angle_threshold_deg : 0.f), TriangleSelector::ForceReselection::YES);
                    } else if (m_tool_type == ToolType::BRUSH && m_cursor_type == TriangleSelector::CursorType::POINTER) {
                        m_triangle_selectors[mesh_idx]->bucket_fill_select_triangles(mesh_hit, facet_idx, clp, m_bucket_fill_angle, BucketFillGapArea, TriangleSelector::BucketFillPropagate::NO, TriangleSelector::ForceReselection::YES);
                    } else if (m_tool_type == ToolType::BUCKET_FILL) {
                        m_triangle_selectors[mesh_idx]->bucket_fill_select_triangles(mesh_hit, facet_idx, clp, m_bucket_fill_angle, BucketFillGapArea, TriangleSelector::BucketFillPropagate::YES, TriangleSelector::ForceReselection::YES);
                    }

                    m_seed_fill_last_mesh_id = -1;
                }
            } else if (m_tool_type == ToolType::BRUSH) {
                assert(m_cursor_type == TriangleSelector::CursorType::CIRCLE || m_cursor_type == TriangleSelector::CursorType::SPHERE);

                if (projected_mouse_positions.size() == 1) {
                    const ProjectedMousePosition             &first_position = projected_mouse_positions.front();
                    std::unique_ptr<TriangleSelector::Cursor> cursor         = TriangleSelector::SinglePointCursor::cursor_factory(first_position.mesh_hit,
                                                                                                                                   camera_pos, m_cursor_radius,
                                                                                                                                   m_cursor_type, trafo_matrix, clp);
                    m_triangle_selectors[mesh_idx]->select_patch(int(first_position.facet_idx), std::move(cursor), new_state, trafo_matrix_not_translate,
                                                                 m_triangle_splitting_enabled, m_paint_on_overhangs_only ? m_highlight_by_angle_threshold_deg : 0.f);
                } else {
                    for (auto first_position_it = projected_mouse_positions.cbegin(); first_position_it != projected_mouse_positions.cend() - 1; ++first_position_it) {
                        auto second_position_it = first_position_it + 1;
                        std::unique_ptr<TriangleSelector::Cursor> cursor = TriangleSelector::DoublePointCursor::cursor_factory(first_position_it->mesh_hit, second_position_it->mesh_hit, camera_pos, m_cursor_radius, m_cursor_type, trafo_matrix, clp);
                        m_triangle_selectors[mesh_idx]->select_patch(int(first_position_it->facet_idx), std::move(cursor), new_state, trafo_matrix_not_translate, m_triangle_splitting_enabled, m_paint_on_overhangs_only ? m_highlight_by_angle_threshold_deg : 0.f);
                    }
                }
            } else if (m_tool_type == ToolType::HEIGHT_RANGE) {
                for (const ProjectedMousePosition &projected_mouse_position: projected_mouse_positions) {
                    const Vec3f         &mesh_hit  = projected_mouse_position.mesh_hit;
                    const int            facet_idx = int(projected_mouse_position.facet_idx);
                    const BoundingBoxf3  mesh_bbox = mo->volumes[projected_mouse_position.mesh_idx]->mesh().bounding_box();

                    std::unique_ptr<TriangleSelector::Cursor> cursor = std::make_unique<TriangleSelector::HeightRange>(mesh_hit, mesh_bbox, m_height_range_z_range, trafo_matrix, clp);
                    m_triangle_selectors[mesh_idx]->select_patch(facet_idx, std::move(cursor), new_state, trafo_matrix_not_translate,
                                                                 m_triangle_splitting_enabled, m_paint_on_overhangs_only ? m_highlight_by_angle_threshold_deg : 0.f);
                }
            }

            m_triangle_selectors[mesh_idx]->request_update_render_data();
            m_last_mouse_click = mouse_position;
        }

        return true;
    }

    if (action == SLAGizmoEventType::Moving && (m_tool_type == ToolType::SMART_FILL || m_tool_type == ToolType::BUCKET_FILL || (m_tool_type == ToolType::BRUSH && m_cursor_type == TriangleSelector::CursorType::POINTER))) {
        if (m_triangle_selectors.empty())
            return false;

        const Camera        &camera                       = wxGetApp().plater()->get_camera();
        const Selection     &selection                    = m_parent.get_selection();
        const ModelObject   *mo                           = m_c->selection_info()->model_object();
        const ModelInstance *mi                           = mo->instances[selection.get_instance_idx()];
        const Transform3d    instance_trafo               = mi->get_transformation().get_matrix();
        const Transform3d    instance_trafo_not_translate = mi->get_transformation().get_matrix_no_offset();

        // Precalculate transformations of individual meshes.
        std::vector<Transform3d> trafo_matrices;
        std::vector<Transform3d> trafo_matrices_not_translate;
        for (const ModelVolume *mv : mo->volumes)
            if (mv->is_model_part()) {
                trafo_matrices.emplace_back(instance_trafo * mv->get_matrix());
                trafo_matrices_not_translate.emplace_back(instance_trafo_not_translate* mv->get_matrix_no_offset());
            }

        // Now "click" into all the prepared points and spill paint around them.
        update_raycast_cache(mouse_position, camera, trafo_matrices);

        auto seed_fill_unselect_all = [this]() {
            for (auto &triangle_selector : m_triangle_selectors) {
                triangle_selector->seed_fill_unselect_all_triangles();
                triangle_selector->request_update_render_data();
            }
        };

        if (m_rr.mesh_id == -1) {
            // Clean selected by seed fill for all triangles in all meshes when a mouse isn't pointing on any mesh.
            seed_fill_unselect_all();
            m_seed_fill_last_mesh_id = -1;

            // In case we have no valid hit, we can return.
            return false;
        }

        // The mouse moved from one object's volume to another one. So it is needed to unselect all triangles selected by seed fill.
        if(m_rr.mesh_id != m_seed_fill_last_mesh_id)
            seed_fill_unselect_all();

        const Transform3d &trafo_matrix = trafo_matrices[m_rr.mesh_id];
        const Transform3d &trafo_matrix_not_translate = trafo_matrices_not_translate[m_rr.mesh_id];

        assert(m_rr.mesh_id < int(m_triangle_selectors.size()));
        const TriangleSelector::ClippingPlane &clp = this->get_clipping_plane_in_volume_coordinates(trafo_matrix);
        if (m_tool_type == ToolType::SMART_FILL)
            m_triangle_selectors[m_rr.mesh_id]->seed_fill_select_triangles(m_rr.hit, int(m_rr.facet), trafo_matrix_not_translate, clp, m_smart_fill_angle, SmartFillGapArea,
                                                                           m_paint_on_overhangs_only ? m_highlight_by_angle_threshold_deg : 0.f);
        else if (m_tool_type == ToolType::BRUSH && m_cursor_type == TriangleSelector::CursorType::POINTER)
            m_triangle_selectors[m_rr.mesh_id]->bucket_fill_select_triangles(m_rr.hit, int(m_rr.facet), clp, m_bucket_fill_angle, BucketFillGapArea, TriangleSelector::BucketFillPropagate::NO);
        else if (m_tool_type == ToolType::BUCKET_FILL)
            m_triangle_selectors[m_rr.mesh_id]->bucket_fill_select_triangles(m_rr.hit, int(m_rr.facet), clp, m_bucket_fill_angle, BucketFillGapArea, TriangleSelector::BucketFillPropagate::YES);
        m_triangle_selectors[m_rr.mesh_id]->request_update_render_data();
        m_seed_fill_last_mesh_id = m_rr.mesh_id;
        return true;
    }

    if ((action == SLAGizmoEventType::LeftUp || action == SLAGizmoEventType::RightUp)
      && m_button_down != Button::None) {
        // Take snapshot and update ModelVolume data.
        wxString action_name = this->handle_snapshot_action_name(shift_down, m_button_down);
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), action_name, UndoRedo::SnapshotType::GizmoAction);
        update_model_object();

        m_button_down = Button::None;
        m_last_mouse_click = Vec2d::Zero();
        return true;
    }

    return false;
}

bool GLGizmoPainterBase::on_mouse(const wxMouseEvent &mouse_event)
{
    // wxCoord == int --> wx/types.h
    Vec2i mouse_coord(mouse_event.GetX(), mouse_event.GetY());
    Vec2d mouse_pos = mouse_coord.cast<double>();

    if (mouse_event.Moving()) {
        gizmo_event(SLAGizmoEventType::Moving, mouse_pos, mouse_event.ShiftDown(), mouse_event.AltDown(), false);
        return false;
    }

    // when control is down we allow scene pan and rotation even when clicking
    // over some object
    bool control_down           = mouse_event.CmdDown();
    bool grabber_contains_mouse = (get_hover_id() != -1);

    const Selection &selection = m_parent.get_selection();
    int selected_object_idx = selection.get_object_idx();
    if (mouse_event.LeftDown()) {
        if ((!control_down || grabber_contains_mouse) &&            
            gizmo_event(SLAGizmoEventType::LeftDown, mouse_pos, mouse_event.ShiftDown(), mouse_event.AltDown(), false))
            // the gizmo got the event and took some action, there is no need
            // to do anything more
            return true;
    } else if (mouse_event.RightDown()){
        if (!control_down && selected_object_idx != -1 &&
            gizmo_event(SLAGizmoEventType::RightDown, mouse_pos, false, false, false)) 
            // event was taken care of
            return true;
    } else if (mouse_event.Dragging()) {
        if (m_parent.get_move_volume_id() != -1)
            // don't allow dragging objects with the Sla gizmo on
            return true;
        if (!control_down && gizmo_event(SLAGizmoEventType::Dragging,
                                         mouse_pos, mouse_event.ShiftDown(),
                                         mouse_event.AltDown(), false)) {
            // the gizmo got the event and took some action, no need to do
            // anything more here
            m_parent.set_as_dirty();
            return true;
        }
        if(control_down && (mouse_event.LeftIsDown() || mouse_event.RightIsDown()))
        {
            // CTRL has been pressed while already dragging -> stop current action
            if (mouse_event.LeftIsDown())
                gizmo_event(SLAGizmoEventType::LeftUp, mouse_pos, mouse_event.ShiftDown(), mouse_event.AltDown(), true);
            else if (mouse_event.RightIsDown())
                gizmo_event(SLAGizmoEventType::RightUp, mouse_pos, mouse_event.ShiftDown(), mouse_event.AltDown(), true);
            return false;
        }
    } else if (mouse_event.LeftUp()) {
        if (!m_parent.is_mouse_dragging()) {
            // in case SLA/FDM gizmo is selected, we just pass the LeftUp
            // event and stop processing - neither object moving or selecting
            // is suppressed in that case
            gizmo_event(SLAGizmoEventType::LeftUp, mouse_pos, mouse_event.ShiftDown(), mouse_event.AltDown(), control_down);
            return true;
        }
    } else if (mouse_event.RightUp()) {
        if (!m_parent.is_mouse_dragging()) {
            gizmo_event(SLAGizmoEventType::RightUp, mouse_pos, mouse_event.ShiftDown(), mouse_event.AltDown(), control_down);
            return true;
        }
    }
    return false;
}

void GLGizmoPainterBase::update_raycast_cache(const Vec2d& mouse_position,
                                              const Camera& camera,
                                              const std::vector<Transform3d>& trafo_matrices) const
{
    if (m_rr.mouse_position == mouse_position) {
        // Same query as last time - the answer is already in the cache.
        return;
    }

    Vec3f normal =  Vec3f::Zero();
    Vec3f hit = Vec3f::Zero();
    size_t facet = 0;
    Vec3f closest_hit = Vec3f::Zero();
    double closest_hit_squared_distance = std::numeric_limits<double>::max();
    size_t closest_facet = 0;
    int closest_hit_mesh_id = -1;

    // Cast a ray on all meshes, pick the closest hit and save it for the respective mesh
    for (int mesh_id = 0; mesh_id < int(trafo_matrices.size()); ++mesh_id) {

        if (m_c->raycaster()->raycasters()[mesh_id]->unproject_on_mesh(
                mouse_position, trafo_matrices[mesh_id], camera, hit, normal,
                m_c->object_clipper()->get_clipping_plane(), &facet, false
            ))
        {
            // In case this hit is clipped, skip it.
            if (is_mesh_point_clipped(hit.cast<double>(), trafo_matrices[mesh_id]))
                continue;

            // Is this hit the closest to the camera so far?
            double hit_squared_distance = (camera.get_position()-trafo_matrices[mesh_id]*hit.cast<double>()).squaredNorm();
            if (hit_squared_distance < closest_hit_squared_distance) {
                closest_hit_squared_distance = hit_squared_distance;
                closest_facet = facet;
                closest_hit_mesh_id = mesh_id;
                closest_hit = hit;
            }
        }
    }

    m_rr = {mouse_position, closest_hit_mesh_id, closest_hit, closest_facet};
}

bool GLGizmoPainterBase::on_is_activable() const
{
    const Selection& selection = m_parent.get_selection();

    if (wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() != ptFFF
        || !selection.is_single_full_instance() || wxGetApp().get_mode() == comSimple)
        return false;

    // Check that none of the selected volumes is outside. Only SLA auxiliaries (supports) are allowed outside.
    const Selection::IndicesList& list = selection.get_volume_idxs();
    return std::all_of(list.cbegin(), list.cend(), [&selection](unsigned int idx) { return !selection.get_volume(idx)->is_outside; });
}

bool GLGizmoPainterBase::on_is_selectable() const
{
    return (wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() == ptFFF
         && wxGetApp().get_mode() != comSimple );
}


CommonGizmosDataID GLGizmoPainterBase::on_get_requirements() const
{
    return CommonGizmosDataID(
                int(CommonGizmosDataID::SelectionInfo)
              | int(CommonGizmosDataID::InstancesHider)
              | int(CommonGizmosDataID::Raycaster)
              | int(CommonGizmosDataID::ObjectClipper));
}


void GLGizmoPainterBase::on_set_state()
{
    if (m_state == m_old_state)
        return;

    if (m_state == On && m_old_state != On) { // the gizmo was just turned on
        on_opening();
    }
    if (m_state == Off && m_old_state != Off) { // the gizmo was just turned Off
        // we are actually shutting down
        on_shutdown();
        m_old_mo_id = -1;
        //m_iva.release_geometry();
        m_triangle_selectors.clear();
    }
    m_old_state = m_state;
}



void GLGizmoPainterBase::on_load(cereal::BinaryInputArchive&)
{
    // We should update the gizmo from current ModelObject, but it is not
    // possible at this point. That would require having updated selection and
    // common gizmos data, which is not done at this point. Instead, save
    // a flag to do the update in set_painter_gizmo_data, which will be called
    // soon after.
    m_schedule_update = true;
}

TriangleSelector::ClippingPlane GLGizmoPainterBase::get_clipping_plane_in_volume_coordinates(const Transform3d &trafo) const {
    const ::Slic3r::GUI::ClippingPlane *const clipping_plane = m_c->object_clipper()->get_clipping_plane();
    if (clipping_plane == nullptr || !clipping_plane->is_active())
        return {};

    const Vec3d  clp_normal = clipping_plane->get_normal();
    const double clp_offset = clipping_plane->get_offset();

    const Transform3d trafo_normal = Transform3d(trafo.linear().transpose());
    const Transform3d trafo_inv    = trafo.inverse();

    Vec3d point_on_plane             = clp_normal * clp_offset;
    Vec3d point_on_plane_transformed = trafo_inv * point_on_plane;
    Vec3d normal_transformed         = trafo_normal * clp_normal;
    auto offset_transformed          = float(point_on_plane_transformed.dot(normal_transformed));

    return TriangleSelector::ClippingPlane({float(normal_transformed.x()), float(normal_transformed.y()), float(normal_transformed.z()), offset_transformed});
}

ColorRGBA TriangleSelectorGUI::get_seed_fill_color(const ColorRGBA& base_color)
{
    return saturate(base_color, 0.75f);
}

void TriangleSelectorGUI::render(ImGuiWrapper* imgui, const Transform3d& matrix)
{
    static const ColorRGBA enforcers_color = { 0.47f, 0.47f, 1.0f, 1.0f };
    static const ColorRGBA blockers_color  = { 1.0f, 0.44f, 0.44f, 1.0f };

    if (m_update_render_data) {
        update_render_data();
        m_update_render_data = false;
    }

    auto* shader = wxGetApp().get_current_shader();
    if (! shader)
        return;

    assert(shader->get_name() == "gouraud");

    for (auto iva : {std::make_pair(&m_iva_enforcers, enforcers_color),
                     std::make_pair(&m_iva_blockers, blockers_color)}) {
        iva.first->set_color(iva.second);
        iva.first->render();
    }

    for (auto& iva : m_iva_seed_fills) {
        size_t           color_idx = &iva - &m_iva_seed_fills.front();
        const ColorRGBA& color     = TriangleSelectorGUI::get_seed_fill_color(color_idx == 1 ? enforcers_color :
            color_idx == 2 ? blockers_color :
            GLVolume::NEUTRAL_COLOR);
        iva.set_color(color);
        iva.render();
    }

    render_paint_contour(matrix);

#ifdef PRUSASLICER_TRIANGLE_SELECTOR_DEBUG
    if (imgui)
        render_debug(imgui);
    else
        assert(false); // If you want debug output, pass ptr to ImGuiWrapper.
#endif // PRUSASLICER_TRIANGLE_SELECTOR_DEBUG
}

void TriangleSelectorGUI::update_render_data()
{
    int              enf_cnt = 0;
    int              blc_cnt = 0;
    std::vector<int> seed_fill_cnt(m_iva_seed_fills.size(), 0);

    for (auto* iva : { &m_iva_enforcers, &m_iva_blockers }) {
        iva->reset();
    }

    for (auto& iva : m_iva_seed_fills) {
        iva.reset();
    }

    GLModel::Geometry iva_enforcers_data;
    iva_enforcers_data.format = { GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3N3 };
    GLModel::Geometry iva_blockers_data;
    iva_blockers_data.format = { GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3N3 };
    std::array<GLModel::Geometry, 3> iva_seed_fills_data;
    for (auto& data : iva_seed_fills_data)
        data.format = { GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3N3 };

    // small value used to offset triangles along their normal to avoid z-fighting
    static const float offset = 0.001f;

    for (const Triangle &tr : m_triangles) {
        if (!tr.valid() || tr.is_split() || (tr.get_state() == TriangleStateType::NONE && !tr.is_selected_by_seed_fill()))
            continue;

        int tr_state = int(tr.get_state());
        GLModel::Geometry &iva = tr.is_selected_by_seed_fill()                 ? iva_seed_fills_data[tr_state] :
                                 tr.get_state() == TriangleStateType::ENFORCER ? iva_enforcers_data :
                                                                                 iva_blockers_data;
        int                  &cnt = tr.is_selected_by_seed_fill()                 ? seed_fill_cnt[tr_state] :
                                    tr.get_state() == TriangleStateType::ENFORCER ? enf_cnt :
                                                                                    blc_cnt;
        const Vec3f          &v0  = m_vertices[tr.verts_idxs[0]].v;
        const Vec3f          &v1  = m_vertices[tr.verts_idxs[1]].v;
        const Vec3f          &v2  = m_vertices[tr.verts_idxs[2]].v;
        //FIXME the normal may likely be pulled from m_triangle_selectors, but it may not be worth the effort
        // or the current implementation may be more cache friendly.
        const Vec3f           n   = (v1 - v0).cross(v2 - v1).normalized();
        // small value used to offset triangles along their normal to avoid z-fighting
        const Vec3f    offset_n   = offset * n;
        iva.add_vertex(v0 + offset_n, n);
        iva.add_vertex(v1 + offset_n, n);
        iva.add_vertex(v2 + offset_n, n);
        iva.add_triangle((unsigned int)cnt, (unsigned int)cnt + 1, (unsigned int)cnt + 2);
        cnt += 3;
    }

    if (!iva_enforcers_data.is_empty())
        m_iva_enforcers.init_from(std::move(iva_enforcers_data));
    if (!iva_blockers_data.is_empty())
        m_iva_blockers.init_from(std::move(iva_blockers_data));
    for (size_t i = 0; i < m_iva_seed_fills.size(); ++i) {
        if (!iva_seed_fills_data[i].is_empty())
            m_iva_seed_fills[i].init_from(std::move(iva_seed_fills_data[i]));
    }

    update_paint_contour();
}

#ifdef PRUSASLICER_TRIANGLE_SELECTOR_DEBUG
void TriangleSelectorGUI::render_debug(ImGuiWrapper* imgui)
{
    imgui->begin(std::string("TriangleSelector dialog (DEV ONLY)"),
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);
    static float edge_limit = 1.f;
    imgui->text("Edge limit (mm): ");
    imgui->slider_float("", &edge_limit, 0.1f, 8.f);
    set_edge_limit(edge_limit);
    imgui->checkbox("Show split triangles: ", m_show_triangles);
    imgui->checkbox("Show invalid triangles: ", m_show_invalid);

    int valid_triangles = m_triangles.size() - m_invalid_triangles;
    imgui->text("Valid triangles: " + std::to_string(valid_triangles) +
                  "/" + std::to_string(m_triangles.size()));
    imgui->text("Vertices: " + std::to_string(m_vertices.size()));
    if (imgui->button("Force garbage collection"))
        garbage_collect();

    if (imgui->button("Serialize - deserialize")) {
        auto map = serialize();
        deserialize(map);
    }

    imgui->end();

    if (! m_show_triangles)
        return;

    enum vtype {
        ORIGINAL = 0,
        SPLIT,
        INVALID
    };

    for (auto& va : m_varrays)
        va.reset();

    std::array<int, 3> cnts;

    ::glScalef(1.01f, 1.01f, 1.01f);

    std::array<GLModel::Geometry, 3> varrays_data;
    for (auto& data : varrays_data)
        data.format = { GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3N3, GLModel::Geometry::EIndexType::UINT };

    for (int tr_id=0; tr_id<int(m_triangles.size()); ++tr_id) {
        const Triangle& tr = m_triangles[tr_id];
        GLModel::Geometry* va = nullptr;
        int* cnt = nullptr;
        if (tr_id < m_orig_size_indices) {
            va = &varrays_data[ORIGINAL];
            cnt = &cnts[ORIGINAL];
        }
        else if (tr.valid()) {
            va = &varrays_data[SPLIT];
            cnt = &cnts[SPLIT];
        }
        else {
            if (! m_show_invalid)
                continue;
            va = &varrays_data[INVALID];
            cnt = &cnts[INVALID];
        }

        for (int i = 0; i < 3; ++i) {
            va->add_vertex(m_vertices[tr.verts_idxs[i]].v, Vec3f(0.0f, 0.0f, 1.0f));
        }
        va->add_uint_triangle((unsigned int)*cnt, (unsigned int)*cnt + 1, (unsigned int)*cnt + 2);
        *cnt += 3;
    }

    for (int i = 0; i < 3; ++i) {
        if (!varrays_data[i].is_empty())
            m_varrays[i].init_from(std::move(varrays_data[i]));
    }

    GLShaderProgram* curr_shader = wxGetApp().get_current_shader();
    if (curr_shader != nullptr)
        curr_shader->stop_using();

    GLShaderProgram* shader = wxGetApp().get_shader("flat");
    if (shader != nullptr) {
        shader->start_using();

        const Camera& camera = wxGetApp().plater()->get_camera();
        shader->set_uniform("view_model_matrix", camera.get_view_matrix());
        shader->set_uniform("projection_matrix", camera.get_projection_matrix());

    ::glPolygonMode( GL_FRONT_AND_BACK, GL_LINE );
    for (vtype i : {ORIGINAL, SPLIT, INVALID}) {
        GLModel& va = m_varrays[i];
        switch (i) {
        case ORIGINAL: va.set_color({ 0.0f, 0.0f, 1.0f, 1.0f }); break;
        case SPLIT:    va.set_color({ 1.0f, 0.0f, 0.0f, 1.0f }); break;
        case INVALID:  va.set_color({ 1.0f, 1.0f, 0.0f, 1.0f }); break;
        }
        va.render();
    }
    ::glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );

        shader->stop_using();
    }

    if (curr_shader != nullptr)
        curr_shader->start_using();
}
#endif // PRUSASLICER_TRIANGLE_SELECTOR_DEBUG

void TriangleSelectorGUI::update_paint_contour()
{
    m_paint_contour.reset();

    GLModel::Geometry init_data;
    const std::vector<Vec2i> contour_edges = this->get_seed_fill_contour();
    init_data.format = { GLModel::Geometry::EPrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P3 };
    init_data.reserve_vertices(2 * contour_edges.size());
    init_data.reserve_indices(2 * contour_edges.size());
    init_data.color = ColorRGBA::WHITE();
 
    // vertices + indices
    unsigned int vertices_count = 0;
    for (const Vec2i& edge : contour_edges) {
        init_data.add_vertex(m_vertices[edge(0)].v);
        init_data.add_vertex(m_vertices[edge(1)].v);
        vertices_count += 2;
        init_data.add_line(vertices_count - 2, vertices_count - 1);
    }

    if (!init_data.is_empty())
        m_paint_contour.init_from(std::move(init_data));
}

void TriangleSelectorGUI::render_paint_contour(const Transform3d& matrix)
{
    auto* curr_shader = wxGetApp().get_current_shader();
    if (curr_shader != nullptr)
        curr_shader->stop_using();

    auto* contour_shader = wxGetApp().get_shader("mm_contour");
    if (contour_shader != nullptr) {
        contour_shader->start_using();

        contour_shader->set_uniform("offset", OpenGLManager::get_gl_info().is_mesa() ? 0.0005 : 0.00001);
        const Camera& camera = wxGetApp().plater()->get_camera();
        contour_shader->set_uniform("view_model_matrix", camera.get_view_matrix() * matrix);
        contour_shader->set_uniform("projection_matrix", camera.get_projection_matrix());

        m_paint_contour.render();
        contour_shader->stop_using();
    }

    if (curr_shader != nullptr)
        curr_shader->start_using();
}

} // namespace Slic3r::GUI
