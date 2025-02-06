///|/ Copyright (c) Prusa Research 2016 - 2023 Lukáš Matěna @lukasmatena, Enrico Turri @enricoturri1966, Oleksandra Iushchenko @YuSanka, Tomáš Mészáros @tamasmeszaros, Vojtěch Bubník @bubnikv, Filip Sykala @Jony01, Lukáš Hejl @hejllukas, David Kocík @kocikdav, Vojtěch Král @vojtechkral
///|/ Copyright (c) 2017 Eyal Soha @eyal0
///|/ Copyright (c) Slic3r 2015 Alessandro Ranellucci @alranel
///|/
///|/ ported from lib/Slic3r/GUI/3DScene.pm:
///|/ Copyright (c) Prusa Research 2016 - 2019 Vojtěch Bubník @bubnikv, Enrico Turri @enricoturri1966, Oleksandra Iushchenko @YuSanka
///|/ Copyright (c) Slic3r 2013 - 2016 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2013 Guillaume Seguin @iXce
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include <GL/glew.h>

#include "3DScene.hpp"
#include "GLShader.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "BitmapCache.hpp"
#include "Camera.hpp"

#include "Gizmos/GLGizmoMmuSegmentation.hpp"

#include "libslic3r/BuildVolume.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/SLAPrint.hpp"
#include "libslic3r/Slicing.hpp"
#include "libslic3r/Format/STL.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Tesselate.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/MultipleBeds.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <boost/log/trivial.hpp>

#include <boost/filesystem/operations.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <Eigen/Dense>

#ifdef HAS_GLSAFE
void glAssertRecentCallImpl(const char* file_name, unsigned int line, const char* function_name)
{
#if defined(NDEBUG)
    // In release mode, only show OpenGL errors if sufficiently high loglevel.
    if (Slic3r::get_logging_level() < 5)
        return;
#endif // NDEBUG

    GLenum err = glGetError();
    if (err == GL_NO_ERROR)
        return;
    const char* sErr = 0;
    switch (err) {
    case GL_INVALID_ENUM:       sErr = "Invalid Enum";      break;
    case GL_INVALID_VALUE:      sErr = "Invalid Value";     break;
    // be aware that GL_INVALID_OPERATION is generated if glGetError is executed between the execution of glBegin and the corresponding execution of glEnd 
    case GL_INVALID_OPERATION:  sErr = "Invalid Operation"; break;
    case GL_STACK_OVERFLOW:     sErr = "Stack Overflow";    break;
    case GL_STACK_UNDERFLOW:    sErr = "Stack Underflow";   break;
    case GL_OUT_OF_MEMORY:      sErr = "Out Of Memory";     break;
    default:                    sErr = "Unknown";           break;
    }
    BOOST_LOG_TRIVIAL(error) << "OpenGL error in " << file_name << ":" << line << ", function " << function_name << "() : " << (int)err << " - " << sErr;
    assert(false);
}
#endif // HAS_GLSAFE

namespace Slic3r {

const float GLVolume::SinkingContours::HalfWidth = 0.25f;

void GLVolume::SinkingContours::render()
{
    update();

    GLShaderProgram* shader = GUI::wxGetApp().get_current_shader();
    if (shader == nullptr)
        return;

    const GUI::Camera& camera = GUI::wxGetApp().plater()->get_camera();
    shader->set_uniform("view_model_matrix", camera.get_view_matrix() * Geometry::translation_transform(m_shift));
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());
    m_model.render();
}

void GLVolume::SinkingContours::update()
{
    const int object_idx = m_parent.object_idx();
    const Model& model = GUI::wxGetApp().plater()->model();

    if (object_idx < 0 ||
        object_idx >= int(model.objects.size()) ||
        !m_parent.is_sinking() ||
        m_parent.is_below_printbed()){
        m_model.reset();
        return;    
    }

    const BoundingBoxf3& box = m_parent.transformed_convex_hull_bounding_box();
    if (m_old_box.size().isApprox(box.size()) &&
        m_old_box.min.z() == box.min.z()){
        // Fix it !!! It is not working all the time
        m_shift = box.center() - m_old_box.center();
        return;
    }    
    
    m_old_box = box;
    m_shift = Vec3d::Zero();

    const TriangleMesh& mesh = model.objects[object_idx]->volumes[m_parent.volume_idx()]->mesh();

    m_model.reset();
    GUI::GLModel::Geometry init_data;
    init_data.format = { GUI::GLModel::Geometry::EPrimitiveType::Triangles, GUI::GLModel::Geometry::EVertexLayout::P3 };
    init_data.color = ColorRGBA::WHITE();
    unsigned int vertices_counter = 0;
    MeshSlicingParams slicing_params;
    slicing_params.trafo = m_parent.world_matrix();
    const Polygons polygons = union_(slice_mesh(mesh.its, 0.0f, slicing_params));
    if (polygons.empty()) return;

    for (const ExPolygon& expoly : diff_ex(expand(polygons, float(scale_(HalfWidth))), shrink(polygons, float(scale_(HalfWidth))))) {
        const std::vector<Vec3d> triangulation = triangulate_expolygon_3d(expoly);
        init_data.reserve_vertices(init_data.vertices_count() + triangulation.size());
        init_data.reserve_indices(init_data.indices_count() + triangulation.size());
        for (const Vec3d& v : triangulation) {
            init_data.add_vertex((Vec3f)(v.cast<float>() + 0.015f * Vec3f::UnitZ())); // add a small positive z to avoid z-fighting
            ++vertices_counter;
            if (vertices_counter % 3 == 0)
                init_data.add_triangle(vertices_counter - 3, vertices_counter - 2, vertices_counter - 1);
        }
    }

    if (init_data.vertices_count() > 0)
      m_model.init_from(std::move(init_data));
}

void GLVolume::NonManifoldEdges::render()
{
    update();

#if !SLIC3R_OPENGL_ES
    if (!GUI::OpenGLManager::get_gl_info().is_core_profile())
        glsafe(::glLineWidth(2.0f));
#endif // !SLIC3R_OPENGL_ES

    GLShaderProgram* shader = GUI::wxGetApp().get_current_shader();
    if (shader == nullptr)
        return;

    const GUI::Camera& camera = GUI::wxGetApp().plater()->get_camera();
    shader->set_uniform("view_model_matrix", camera.get_view_matrix() * m_parent.world_matrix());
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());
#if !SLIC3R_OPENGL_ES
    if (GUI::OpenGLManager::get_gl_info().is_core_profile()) {
#endif // !SLIC3R_OPENGL_ES
        const std::array<int, 4>& viewport = camera.get_viewport();
        shader->set_uniform("viewport_size", Vec2d(double(viewport[2]), double(viewport[3])));
        shader->set_uniform("width", 0.5f);
        shader->set_uniform("gap_size", 0.0f);
#if !SLIC3R_OPENGL_ES
    }
#endif // !SLIC3R_OPENGL_ES
    m_model.set_color(complementary(m_parent.render_color));
    m_model.render();
}

void GLVolume::NonManifoldEdges::update()
{
    if (!m_update_needed)
        return;

    m_model.reset();
    const int object_idx = m_parent.object_idx();
    const Model& model = GUI::wxGetApp().plater()->model();
    if (0 <= object_idx && object_idx < int(model.objects.size())) {
        const ModelObject* model_object = model.objects[object_idx];
        const int volume_idx = m_parent.volume_idx();
        if (0 <= volume_idx && volume_idx < int(model_object->volumes.size())) {
            const ModelVolume* model_volume = model_object->volumes[volume_idx];
            const TriangleMesh& mesh = model_volume->mesh();
            const std::vector<std::pair<int, int>> edges = its_get_open_edges(mesh.its);
            if (!edges.empty()) {
                GUI::GLModel::Geometry init_data;
                init_data.format = { GUI::GLModel::Geometry::EPrimitiveType::Lines, GUI::GLModel::Geometry::EVertexLayout::P3 };
                init_data.reserve_vertices(2 * edges.size());
                init_data.reserve_indices(2 * edges.size());

                // vertices + indices
                unsigned int vertices_count = 0;
                for (const std::pair<int, int>& edge : edges) {
                    init_data.add_vertex((Vec3f)mesh.its.vertices[edge.first].cast<float>());
                    init_data.add_vertex((Vec3f)mesh.its.vertices[edge.second].cast<float>());
                    vertices_count += 2;
                    init_data.add_line(vertices_count - 2, vertices_count - 1);
                }
                m_model.init_from(std::move(init_data));
            }
        }
    }

    m_update_needed = false;
}

const ColorRGBA GLVolume::SELECTED_COLOR         = ColorRGBA::GREEN();
const ColorRGBA GLVolume::HOVER_SELECT_COLOR     = { 0.4f, 0.9f, 0.1f, 1.0f };
const ColorRGBA GLVolume::HOVER_DESELECT_COLOR   = { 1.0f, 0.75f, 0.75f, 1.0f };
const ColorRGBA GLVolume::OUTSIDE_COLOR          = { 0.0f, 0.38f, 0.8f, 1.0f };
const ColorRGBA GLVolume::SELECTED_OUTSIDE_COLOR = { 0.19f, 0.58f, 1.0f, 1.0f };
const ColorRGBA GLVolume::DISABLED_COLOR         = ColorRGBA::DARK_GRAY();
const ColorRGBA GLVolume::SLA_SUPPORT_COLOR      = ColorRGBA::LIGHT_GRAY();
const ColorRGBA GLVolume::SLA_PAD_COLOR          = { 0.0f, 0.2f, 0.0f, 1.0f };
const ColorRGBA GLVolume::NEUTRAL_COLOR          = { 0.9f, 0.9f, 0.9f, 1.0f };
const std::array<ColorRGBA, 4> GLVolume::MODEL_COLOR = { {
    ColorRGBA::YELLOW(),
    { 1.0f, 0.5f, 0.5f, 1.0f },
    { 0.5f, 1.0f, 0.5f, 1.0f },
    { 0.5f, 0.5f, 1.0f, 1.0f }
} };
const ColorRGBA GLVolume::NEGATIVE_VOLUME_COLOR     = { 0.2f, 0.2f, 0.2f, 0.5f };
const ColorRGBA GLVolume::PARAMETER_MODIFIER_COLOR  = { 1.0, 1.0f, 0.2f, 0.5f };
const ColorRGBA GLVolume::SUPPORT_BLOCKER_COLOR     = { 1.0f, 0.2f, 0.2f, 0.5f };
const ColorRGBA GLVolume::SUPPORT_ENFORCER_COLOR    = { 0.2f, 0.2f, 1.0f, 0.5f };

GLVolume::GLVolume(float r, float g, float b, float a)
    : m_sla_shift_z(0.0)
    , m_sinking_contours(*this)
    , m_non_manifold_edges(*this)
    // geometry_id == 0 -> invalid
    , geometry_id(std::pair<size_t, size_t>(0, 0))
    , extruder_id(0)
    , selected(false)
    , disabled(false)
    , printable(true)
    , is_active(true)
    , zoom_to_volumes(true)
    , shader_outside_printer_detection_enabled(false)
    , is_outside(false)
    , hover(HS_None)
    , is_modifier(false)
    , is_extrusion_path(false)
    , force_native_color(false)
    , force_neutral_color(false)
    , force_sinking_contours(false)
    , tverts_range(0, size_t(-1))
{
    color = { r, g, b, a };
    set_render_color(color);
}

void GLVolume::set_render_color(bool force_transparent)
{
    bool outside = is_outside || (!is_modifier && is_below_printbed());

    if (force_native_color || force_neutral_color) {
        if (outside && shader_outside_printer_detection_enabled)
            set_render_color(OUTSIDE_COLOR);
        else {
            if (force_native_color)
                set_render_color(color);
            else
                set_render_color(NEUTRAL_COLOR);
        }
    }
    else {
        if (hover == HS_Deselect)
            set_render_color(HOVER_DESELECT_COLOR);
        else if (hover == HS_Select || selected) {
            const ColorRGBA rc = outside ? SELECTED_OUTSIDE_COLOR : SELECTED_COLOR;
            if (color == NEGATIVE_VOLUME_COLOR || color == PARAMETER_MODIFIER_COLOR || color == SUPPORT_BLOCKER_COLOR || color == SUPPORT_ENFORCER_COLOR)
                set_render_color(ColorRGBA(rc.r() * color.r(), rc.g() * color.g(), rc.b() * color.b(), rc.a() * color.a()));
            else
                set_render_color(rc);
        }
        else if (disabled)
            set_render_color(DISABLED_COLOR);
        else if (outside && shader_outside_printer_detection_enabled)
            set_render_color(OUTSIDE_COLOR);
        else
            set_render_color(color);
    }

    if (!printable)
        render_color = saturate(render_color, 0.25f);

    if (force_transparent)
        render_color.a(color.a());
}

ColorRGBA color_from_model_volume(const ModelVolume& model_volume)
{
    ColorRGBA color;
    if (model_volume.is_negative_volume())
        color = GLVolume::NEGATIVE_VOLUME_COLOR;
    else if (model_volume.is_modifier())
        color = GLVolume::PARAMETER_MODIFIER_COLOR;
    else if (model_volume.is_support_blocker())
        color = GLVolume::SUPPORT_BLOCKER_COLOR;
    else if (model_volume.is_support_enforcer())
        color = GLVolume::SUPPORT_ENFORCER_COLOR;

    return color;
}

Transform3d GLVolume::world_matrix() const
{
    Transform3d m = m_instance_transformation.get_matrix() * m_volume_transformation.get_matrix();
    m.translation()(2) += m_sla_shift_z;
    return m;
}

bool GLVolume::is_left_handed() const
{
    const Vec3d &m1 = m_instance_transformation.get_mirror();
    const Vec3d &m2 = m_volume_transformation.get_mirror();
    return m1.x() * m1.y() * m1.z() * m2.x() * m2.y() * m2.z() < 0.;
}

const BoundingBoxf3& GLVolume::transformed_bounding_box() const
{
    if (!m_transformed_bounding_box.has_value()) {
        const BoundingBoxf3& box = bounding_box();
        assert(box.defined || box.min.x() >= box.max.x() || box.min.y() >= box.max.y() || box.min.z() >= box.max.z());
        std::optional<BoundingBoxf3>* trans_box = const_cast<std::optional<BoundingBoxf3>*>(&m_transformed_bounding_box);
        *trans_box = box.transformed(world_matrix());
    }
    return *m_transformed_bounding_box;
}

const BoundingBoxf3& GLVolume::transformed_convex_hull_bounding_box() const
{
    if (!m_transformed_convex_hull_bounding_box.has_value()) {
        std::optional<BoundingBoxf3>* trans_box = const_cast<std::optional<BoundingBoxf3>*>(&m_transformed_convex_hull_bounding_box);
        *trans_box = transformed_convex_hull_bounding_box(world_matrix());
    }
    return *m_transformed_convex_hull_bounding_box;
}

BoundingBoxf3 GLVolume::transformed_convex_hull_bounding_box(const Transform3d &trafo) const
{
	return (m_convex_hull && ! m_convex_hull->empty()) ?
		m_convex_hull->transformed_bounding_box(trafo) :
        bounding_box().transformed(trafo);
}

BoundingBoxf3 GLVolume::transformed_non_sinking_bounding_box(const Transform3d& trafo) const
{
    return GUI::wxGetApp().plater()->model().objects[object_idx()]->volumes[volume_idx()]->mesh().transformed_bounding_box(trafo, 0.0);
}

const BoundingBoxf3& GLVolume::transformed_non_sinking_bounding_box() const
{
    if (!m_transformed_non_sinking_bounding_box.has_value()) {
        std::optional<BoundingBoxf3>* trans_box = const_cast<std::optional<BoundingBoxf3>*>(&m_transformed_non_sinking_bounding_box);
        const Transform3d& trafo = world_matrix();
        *trans_box = transformed_non_sinking_bounding_box(trafo);
    }
    return *m_transformed_non_sinking_bounding_box;
}

void GLVolume::set_range(double min_z, double max_z)
{
    this->tverts_range.first = 0;
    this->tverts_range.second = this->model.indices_count();

    if (!this->print_zs.empty()) {
        // The Z layer range is specified.
        // First test whether the Z span of this object is not out of (min_z, max_z) completely.
        if (this->print_zs.front() > max_z || this->print_zs.back() < min_z)
            this->tverts_range.second = 0;
        else {
            // Then find the lowest layer to be displayed.
            size_t i = 0;
            for (; i < this->print_zs.size() && this->print_zs[i] < min_z; ++i);
            if (i == this->print_zs.size())
                // This shall not happen.
                this->tverts_range.second = 0;
            else {
                // Remember start of the layer.
                this->tverts_range.first = this->offsets[i];
                // Some layers are above $min_z. Which?
                for (; i < this->print_zs.size() && this->print_zs[i] <= max_z; ++i);
                if (i < this->print_zs.size())
                    this->tverts_range.second = this->offsets[i];
            }
        }
    }
}

void GLVolume::render()
{
    if (!is_active)
        return;

    GLShaderProgram* shader = GUI::wxGetApp().get_current_shader();
    if (shader == nullptr)
        return;
    
    const bool is_left_handed = this->is_left_handed();

    if (is_left_handed)
        glsafe(::glFrontFace(GL_CW));
    glsafe(::glCullFace(GL_BACK));

    if (tverts_range == std::make_pair<size_t, size_t>(0, -1))
        model.render();
    else
        model.render(this->tverts_range);

    if (is_left_handed)
        glsafe(::glFrontFace(GL_CCW));
}

bool GLVolume::is_sla_support() const { return this->composite_id.volume_id == -int(slaposSupportTree); }
bool GLVolume::is_sla_pad() const { return this->composite_id.volume_id == -int(slaposPad); }

bool GLVolume::is_sinking() const
{
    if (is_modifier || GUI::wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() == ptSLA)
        return false;
    const BoundingBoxf3& box = transformed_convex_hull_bounding_box();
    return box.min.z() < SINKING_Z_THRESHOLD && box.max.z() >= SINKING_Z_THRESHOLD;
}

bool GLVolume::is_below_printbed() const
{
    return transformed_convex_hull_bounding_box().max.z() < 0.0;
}

void GLVolume::render_sinking_contours()
{
    m_sinking_contours.render();
}

void GLVolume::render_non_manifold_edges()
{
    m_non_manifold_edges.render();
}

std::vector<int> GLVolumeCollection::load_object(
    const ModelObject*      model_object,
    int                     obj_idx,
    const std::vector<int>& instance_idxs)
{
    std::vector<int> volumes_idx;
    for (int volume_idx = 0; volume_idx < int(model_object->volumes.size()); ++volume_idx)
        for (int instance_idx : instance_idxs)
           volumes_idx.emplace_back(this->GLVolumeCollection::load_object_volume(model_object, obj_idx, volume_idx, instance_idx));
    return volumes_idx;
}

int GLVolumeCollection::load_object_volume(
    const ModelObject* model_object,
    int                obj_idx,
    int                volume_idx,
    int                instance_idx)
{
    const ModelVolume   *model_volume = model_object->volumes[volume_idx];
    const int            extruder_id  = model_volume->extruder_id();
    const ModelInstance *instance 	  = model_object->instances[instance_idx];
    std::shared_ptr<const TriangleMesh> mesh = model_volume->mesh_ptr();
    this->volumes.emplace_back(new GLVolume());
    GLVolume& v = *this->volumes.back();
    v.set_color(color_from_model_volume(*model_volume));
    // apply printable value from the instance
    v.printable = instance->printable;
#if ENABLE_SMOOTH_NORMALS
    v.model.init_from(*mesh, true);
    if (m_use_raycasters)
      v.mesh_raycaster = std::make_unique<GUI::MeshRaycaster>(mesh);
#else
    v.model.init_from(*mesh);
    if (m_use_raycasters)
      v.mesh_raycaster = std::make_unique<GUI::MeshRaycaster>(mesh);
#endif // ENABLE_SMOOTH_NORMALS
    v.composite_id = GLVolume::CompositeID(obj_idx, volume_idx, instance_idx);
    if (model_volume->is_model_part()) {
        // GLVolume will reference a convex hull from model_volume!
        v.set_convex_hull(model_volume->get_convex_hull_shared_ptr());
        if (extruder_id != -1)
            v.extruder_id = extruder_id;
    }
    v.is_modifier = !model_volume->is_model_part();
    v.shader_outside_printer_detection_enabled = model_volume->is_model_part();
    v.set_instance_transformation(instance->get_transformation());
    v.set_volume_transformation(model_volume->get_transformation());

    return int(this->volumes.size() - 1);
}

#if SLIC3R_OPENGL_ES
GLVolume* GLVolumeCollection::load_wipe_tower_preview(
    float pos_x, float pos_y, float width, float depth, const std::vector<std::pair<float, float>>& z_and_depth_pairs, float height, float cone_angle,
    float rotation_angle, bool size_unknown, float brim_width, size_t idx, TriangleMesh* out_mesh)
#else
GLVolume* GLVolumeCollection::load_wipe_tower_preview(
    float pos_x, float pos_y, float width, float depth, const std::vector<std::pair<float, float>>& z_and_depth_pairs, float height, float cone_angle,
    float rotation_angle, bool size_unknown, float brim_width, size_t idx)
#endif // SLIC3R_OPENGL_ES
{
    if (height == 0.0f)
        height = 0.1f;

    // Because the GLVolume is also used for arrangement, it must be safely larger
    // than the actual extruded tower, otherwise the arranged tower ends up out of bed.
    const float offset = 0.3f;
    pos_x -= offset;
    pos_y -= offset;
    width += 2.f * offset;
    depth += 2.f * offset;
    brim_width += offset;

    static const float brim_height = 0.2f;
//    const float scaled_brim_height = brim_height / height;

    TriangleMesh mesh;
    ColorRGBA color = ColorRGBA::DARK_YELLOW();

    // In case we don't know precise dimensions of the wipe tower yet, we'll draw
    // the box with different color with one side jagged:
    if (size_unknown) {
        color.r(0.9f);
        color.g(0.6f);

        // Too narrow tower would interfere with the teeth. The estimate is not precise anyway.
        depth = std::max(depth, 10.f);
        float min_width = 30.f;

        // We'll now create the box with jagged edge. y-coordinates of the pre-generated model
        // are shifted so that the front edge has y=0 and centerline of the back edge has y=depth:
        float out_points_idx[][3] = { { 0, -depth, 0 }, { 0, 0, 0 }, { 38.453f, 0, 0 }, { 61.547f, 0, 0 }, { 100.0f, 0, 0 }, { 100.0f, -depth, 0 }, { 55.7735f, -10.0f, 0 }, { 44.2265f, 10.0f, 0 },
            { 38.453f, 0, 1 }, { 0, 0, 1 }, { 0, -depth, 1 }, { 100.0f, -depth, 1 }, { 100.0f, 0, 1 }, { 61.547f, 0, 1 }, { 55.7735f, -10.0f, 1 }, { 44.2265f, 10.0f, 1 } };
        static constexpr const int out_facets_idx[][3] = {
            { 0, 1, 2 }, { 3, 4, 5 }, { 6, 5, 0 }, { 3, 5, 6 }, { 6, 2, 7 }, { 6, 0, 2 }, { 8, 9, 10 }, { 11, 12, 13 }, { 10, 11, 14 }, { 14, 11, 13 }, { 15, 8, 14 },
            { 8, 10, 14 }, { 3, 12, 4 }, { 3, 13, 12 }, { 6, 13, 3 }, { 6, 14, 13 }, { 7, 14, 6 }, { 7, 15, 14 }, { 2, 15, 7 }, { 2, 8, 15 }, { 1, 8, 2 }, { 1, 9, 8 },
            { 0, 9, 1 }, { 0, 10, 9 }, { 5, 10, 0 }, { 5, 11, 10 }, { 4, 11, 5 }, { 4, 12, 11 } };
        indexed_triangle_set its;
        for (int i = 0; i < 16; ++i)
            its.vertices.emplace_back(out_points_idx[i][0] / (100.f / min_width), out_points_idx[i][1] + depth, out_points_idx[i][2]);
        its.indices.reserve(28);
        for (const int* face : out_facets_idx)
            its.indices.emplace_back(face);
        TriangleMesh tooth_mesh(std::move(its));

        // We have the mesh ready. It has one tooth and width of min_width. We will now
        // append several of these together until we are close to the required width
        // of the block. Than we can scale it precisely.
        size_t n = std::max(1, int(width / min_width)); // How many shall be merged?
        for (size_t i = 0; i < n; ++i) {
            mesh.merge(tooth_mesh);
            tooth_mesh.translate(min_width, 0.f, 0.f);
        }

        mesh.scale(Vec3f(width / (n * min_width), 1.f, height)); // Scaling to proper width
    }
    else {
        for (size_t i=1; i<z_and_depth_pairs.size(); ++i) {
            TriangleMesh m = make_cube(width, z_and_depth_pairs[i-1].second, z_and_depth_pairs[i].first-z_and_depth_pairs[i-1].first);
            m.translate(0.f, -z_and_depth_pairs[i-1].second/2.f + z_and_depth_pairs[0].second/2.f, z_and_depth_pairs[i-1].first);
            mesh.merge(m);
        }
    }

    // We'll make another mesh to show the brim (fixed layer height):
    TriangleMesh brim_mesh = make_cube(width + 2.f * brim_width, depth + 2.f * brim_width, 0.2f);
    brim_mesh.translate(-brim_width, -brim_width, 0.f);
    mesh.merge(brim_mesh);

    // Now the stabilization cone and its base.
    const auto [R, scale_x] = WipeTower::get_wipe_tower_cone_base(width, height, depth, cone_angle);
    if (R > 0.) {
        TriangleMesh cone_mesh(its_make_cone(R, height));
        cone_mesh.scale(Vec3f(1.f/scale_x, 1.f, 1.f));

        TriangleMesh disk_mesh(its_make_cylinder(R, brim_height));
        disk_mesh.scale(Vec3f(1. / scale_x, 1., 1.)); // Now it matches the base, which may be elliptic.
        disk_mesh.scale(Vec3f(1.f + scale_x*brim_width/R, 1.f + brim_width/R, 1.f)); // Scale so the brim is not deformed.
        cone_mesh.merge(disk_mesh);
        cone_mesh.translate(width / 2., depth / 2., 0.);
        mesh.merge(cone_mesh);
    }

    GLVolume* result{new GLVolume(color)};
    GLVolume& v = *result;
#if SLIC3R_OPENGL_ES
    if (out_mesh != nullptr)
        *out_mesh = mesh;
#endif // SLIC3R_OPENGL_ES
    v.model.init_from(mesh);
    v.model.set_color(color);
    v.mesh_raycaster = std::make_unique<GUI::MeshRaycaster>(std::make_shared<const TriangleMesh>(mesh));
    v.set_convex_hull(mesh.convex_hull_3d());
    v.set_volume_offset(Vec3d(pos_x, pos_y, 0.0));
    v.set_volume_rotation(Vec3d(0., 0., (M_PI / 180.) * rotation_angle));
    v.composite_id = GLVolume::CompositeID(INT_MAX - idx, 0, 0);
    v.geometry_id.first = 0;
    v.geometry_id.second = wipe_tower_instance_id(idx).id;
    v.wipe_tower_bed_index = idx;
    v.shader_outside_printer_detection_enabled = !size_unknown;

    return result;
}

// Load SLA auxiliary GLVolumes (for support trees or pad).
// This function produces volumes for multiple instances in a single shot,
// as some object specific mesh conversions may be expensive.
void GLVolumeCollection::load_object_auxiliary(
    const SLAPrintObject*           print_object,
    int                             obj_idx,
    // pairs of <instance_idx, print_instance_idx>
    const std::vector<std::pair<size_t, size_t>>& instances,
    SLAPrintObjectStep              milestone,
    // Timestamp of the last change of the milestone
    size_t                          timestamp)
{
    if (print_object->get_mesh_to_print() == nullptr)
        return;
    const Transform3d mesh_trafo_inv = print_object->trafo().inverse();

    auto add_volume = [this, timestamp](int obj_idx, int inst_idx, const ModelInstance& model_instance, SLAPrintObjectStep step,
        const TriangleMesh& mesh, const ColorRGBA& color, std::optional<const TriangleMesh> convex_hull = std::nullopt) {
        if (mesh.empty())
            return;

        GLVolume& v = *this->volumes.emplace_back(new GLVolume(color));
#if ENABLE_SMOOTH_NORMALS
        v.model.init_from(mesh, true);
#else
        v.model.init_from(mesh);
        v.model.set_color(color);
        v.mesh_raycaster = std::make_unique<GUI::MeshRaycaster>(std::make_shared<const TriangleMesh>(mesh));
#endif // ENABLE_SMOOTH_NORMALS
        v.composite_id = GLVolume::CompositeID(obj_idx, -int(step), inst_idx);
        v.geometry_id = std::pair<size_t, size_t>(timestamp, model_instance.id().id);
        if (convex_hull.has_value())
            v.set_convex_hull(*convex_hull);
        v.is_modifier = false;
        v.shader_outside_printer_detection_enabled = (step == slaposSupportTree || step == slaposDrillHoles);
        v.set_instance_transformation(model_instance.get_transformation());
    };
 
    if (milestone == SLAPrintObjectStep::slaposDrillHoles) {
        if (print_object->get_parts_to_slice().size() > 1) {
            // Get the mesh.
            TriangleMesh backend_mesh;
            std::shared_ptr<const indexed_triangle_set> preview_mesh_ptr = print_object->get_mesh_to_print();
            if (preview_mesh_ptr != nullptr)
                backend_mesh = TriangleMesh(*preview_mesh_ptr);
            backend_mesh.translate(s_multiple_beds.get_bed_translation(s_multiple_beds.get_active_bed()).cast<float>());
            if (!backend_mesh.empty()) {
                backend_mesh.transform(mesh_trafo_inv);
                TriangleMesh convex_hull = backend_mesh.convex_hull_3d();
                for (const std::pair<size_t, size_t>& instance_idx : instances) {
                    const ModelInstance& model_instance = *print_object->model_object()->instances[instance_idx.first];
                    add_volume(obj_idx, (int)instance_idx.first, model_instance, slaposDrillHoles, backend_mesh, GLVolume::MODEL_COLOR[0], convex_hull);
                }
            }
        }
    }

    // Get the support mesh.
    if (milestone == SLAPrintObjectStep::slaposSupportTree) {
        TriangleMesh supports_mesh = print_object->support_mesh();
        supports_mesh.translate(s_multiple_beds.get_bed_translation(s_multiple_beds.get_active_bed()).cast<float>());
        if (!supports_mesh.empty()) {
            supports_mesh.transform(mesh_trafo_inv);
            TriangleMesh convex_hull = supports_mesh.convex_hull_3d();
            for (const std::pair<size_t, size_t>& instance_idx : instances) {
                const ModelInstance& model_instance = *print_object->model_object()->instances[instance_idx.first];
                add_volume(obj_idx, (int)instance_idx.first, model_instance, slaposSupportTree, supports_mesh, GLVolume::SLA_SUPPORT_COLOR, convex_hull);
            }
        }
    }

    // Get the pad mesh.
    if (milestone == SLAPrintObjectStep::slaposPad) {
        TriangleMesh pad_mesh = print_object->pad_mesh();
        pad_mesh.translate(s_multiple_beds.get_bed_translation(s_multiple_beds.get_active_bed()).cast<float>());
        if (!pad_mesh.empty()) {
            pad_mesh.transform(mesh_trafo_inv);
            TriangleMesh convex_hull = pad_mesh.convex_hull_3d();
            for (const std::pair<size_t, size_t>& instance_idx : instances) {
                const ModelInstance& model_instance = *print_object->model_object()->instances[instance_idx.first];
                add_volume(obj_idx, (int)instance_idx.first, model_instance, slaposPad, pad_mesh, GLVolume::SLA_PAD_COLOR, convex_hull);
            }
        }
    }
}

GLVolume* GLVolumeCollection::new_toolpath_volume(const ColorRGBA& rgba)
{
    GLVolume* out = new_nontoolpath_volume(rgba);
    out->is_extrusion_path = true;
    return out;
}

GLVolume* GLVolumeCollection::new_nontoolpath_volume(const ColorRGBA& rgba)
{
    GLVolume* out = new GLVolume(rgba);
    out->is_extrusion_path = false;
    this->volumes.emplace_back(out);
    return out;
}

GLVolumeWithIdAndZList volumes_to_render(const GLVolumePtrs& volumes, GLVolumeCollection::ERenderType type, const Transform3d& view_matrix, std::function<bool(const GLVolume&)> filter_func)
{
    GLVolumeWithIdAndZList list;
    list.reserve(volumes.size());

    for (unsigned int i = 0; i < (unsigned int)volumes.size(); ++i) {
        GLVolume* volume = volumes[i];
        bool is_transparent = volume->render_color.is_transparent();
        if (((type == GLVolumeCollection::ERenderType::Opaque && !is_transparent) ||
             (type == GLVolumeCollection::ERenderType::Transparent && is_transparent) ||
             type == GLVolumeCollection::ERenderType::All) &&
            (! filter_func || filter_func(*volume)))
            list.emplace_back(std::make_pair(volume, std::make_pair(i, 0.0)));
    }

    if (type == GLVolumeCollection::ERenderType::Transparent && list.size() > 1) {
        for (GLVolumeWithIdAndZ& volume : list) {
            volume.second.second = volume.first->bounding_box().transformed(view_matrix * volume.first->world_matrix()).max(2);
        }

        std::sort(list.begin(), list.end(),
            [](const GLVolumeWithIdAndZ& v1, const GLVolumeWithIdAndZ& v2) -> bool { return v1.second.second < v2.second.second; }
        );
    }
    else if (type == GLVolumeCollection::ERenderType::Opaque && list.size() > 1) {
        std::sort(list.begin(), list.end(),
            [](const GLVolumeWithIdAndZ& v1, const GLVolumeWithIdAndZ& v2) -> bool { return v1.first->selected && !v2.first->selected; }
        );
    }

    return list;
}

void GLVolumeCollection::render(GLVolumeCollection::ERenderType type, bool disable_cullface, const Transform3d& view_matrix, const Transform3d& projection_matrix,
    std::function<bool(const GLVolume&)> filter_func) const
{
    GLVolumeWithIdAndZList to_render = volumes_to_render(volumes, type, view_matrix, filter_func);
    if (to_render.empty())
        return;

    GLShaderProgram* curr_shader = GUI::wxGetApp().get_current_shader();
    GLShaderProgram* sink_shader = GUI::wxGetApp().get_shader("flat");
#if SLIC3R_OPENGL_ES
    GLShaderProgram* edges_shader = GUI::wxGetApp().get_shader("dashed_lines");
#else
    GLShaderProgram* edges_shader = GUI::OpenGLManager::get_gl_info().is_core_profile() ? GUI::wxGetApp().get_shader("dashed_thick_lines") : GUI::wxGetApp().get_shader("flat");
#endif // SLIC3R_OPENGL_ES
    GLShaderProgram* mmu_painted_shader = GUI::wxGetApp().get_shader("mm_gouraud");
    if (curr_shader == nullptr || sink_shader == nullptr || edges_shader == nullptr || mmu_painted_shader == nullptr)
        return;

    GLShaderProgram* shader = curr_shader;
    shader->stop_using();

    if (type == ERenderType::Transparent) {
        glsafe(::glEnable(GL_BLEND));
        glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
        glsafe(::glDepthMask(false));
    }

    glsafe(::glCullFace(GL_BACK));
    if (disable_cullface)
        glsafe(::glDisable(GL_CULL_FACE));


    // This block is here to render the pained triangles. It is not very nice, but it works.
    // There is a cache that holds the OpenGL models of the painted areas to render, one for
    // each ModelVolume. The cache is invalidated based on changes in extruder_colors,
    // default extruder idx and timestamp of the painted data. The data belonging to objects
    // // which no longer exist are removed from the cache periodically.
    const ModelObjectPtrs& model_objects = GUI::wxGetApp().model().objects;
    const std::vector<ColorRGBA> extruders_colors = GUI::wxGetApp().plater()->get_extruder_colors_from_plater_config();
    const bool is_render_as_mmu_painted_enabled = !model_objects.empty() && !extruders_colors.empty();

    if (m_mm_paint_cache.extruders_colors != extruders_colors) {
        m_mm_paint_cache.extruders_colors = extruders_colors;
        m_mm_paint_cache.volume_data.clear();
    }
    auto time_now = std::chrono::system_clock::now();




    for (GLVolumeWithIdAndZ& volume : to_render) {
        if (!volume.first->is_active)
            continue;

        const Transform3d world_matrix = volume.first->world_matrix();
        const Matrix3d world_matrix_inv_transp = world_matrix.linear().inverse().transpose();
        const Matrix3d view_normal_matrix = view_matrix.linear() * world_matrix_inv_transp;
        const int obj_idx = volume.first->object_idx();
        const int vol_idx = volume.first->volume_idx();
        const bool render_as_mmu_painted = is_render_as_mmu_painted_enabled && !volume.first->selected &&
            !volume.first->is_outside && volume.first->hover == GLVolume::HS_None && !volume.first->is_wipe_tower() && obj_idx >= 0 && vol_idx >= 0 &&
            !model_objects[obj_idx]->volumes[vol_idx]->mm_segmentation_facets.empty() &&
            type != GLVolumeCollection::ERenderType::Transparent; // to filter out shells (not very nice)
        volume.first->set_render_color(true);

        // render sinking contours of non-hovered volumes
        if (sink_shader != nullptr) {
            sink_shader->start_using();
            if (m_show_sinking_contours) {
                if (volume.first->is_sinking() && !volume.first->is_below_printbed() &&
                    volume.first->hover == GLVolume::HS_None && !volume.first->force_sinking_contours) {
                    volume.first->render_sinking_contours();
                }
            }
            sink_shader->stop_using();
        }

        if (render_as_mmu_painted && shader != mmu_painted_shader)
            shader = mmu_painted_shader;
        else if (!render_as_mmu_painted && shader != curr_shader)
            shader = curr_shader;

        if (render_as_mmu_painted) {
            shader->start_using();
            const std::array<float, 4> clp_data = { 0.0f, 0.0f, 1.0f, FLT_MAX };
            const std::array<float, 2> z_range = { -FLT_MAX, FLT_MAX };
            const bool is_left_handed = volume.first->is_left_handed();
            shader->set_uniform("volume_world_matrix", world_matrix);
            shader->set_uniform("volume_mirrored", is_left_handed);
            shader->set_uniform("clipping_plane", clp_data);
            shader->set_uniform("z_range", z_range);
            shader->set_uniform("view_model_matrix", view_matrix * world_matrix);
            shader->set_uniform("projection_matrix", projection_matrix);
            shader->set_uniform("view_normal_matrix", view_normal_matrix);

            if (is_left_handed)
                glsafe(::glFrontFace(GL_CW));

            const ModelVolume& model_volume = *model_objects[obj_idx]->volumes[vol_idx];
            const size_t extruder_idx = ModelVolume::get_extruder_color_idx(model_volume, GUI::wxGetApp().extruders_edited_cnt());


            // This block retrieves the painted geometry from the cache or adds it to it.
            ObjectID vol_id = model_volume.id();
            auto it = m_mm_paint_cache.volume_data.find(vol_id);
            GUI::TriangleSelectorMmGui* ts = nullptr;
            uint64_t timestamp = model_volume.mm_segmentation_facets.timestamp();
            if (it == m_mm_paint_cache.volume_data.end() || it->second.extruder_id != extruder_idx || timestamp != it->second.mm_timestamp) {
                auto ts_uptr = std::make_unique<GUI::TriangleSelectorMmGui>(model_volume.mesh(), m_mm_paint_cache.extruders_colors, m_mm_paint_cache.extruders_colors[extruder_idx]);
                ts = ts_uptr.get();
                ts->deserialize(model_volume.mm_segmentation_facets.get_data(), true);
                ts->request_update_render_data();
                m_mm_paint_cache.volume_data[vol_id] = MMPaintCachePerVolume{ extruder_idx, std::move(ts_uptr), std::chrono::system_clock::now(), timestamp };
            }
            else {
                ts = it->second.triangle_selector_mm.get();
                it->second.time_used = time_now;
            }

            
            ts->render(nullptr, world_matrix);

            if (is_left_handed)
                glsafe(::glFrontFace(GL_CCW));

            shader->stop_using();
        }
        else {
            shader->start_using();
            shader->set_uniform("z_range", m_z_range);
            shader->set_uniform("clipping_plane", m_clipping_plane);
            shader->set_uniform("use_color_clip_plane", m_use_color_clip_plane);
            shader->set_uniform("color_clip_plane", m_color_clip_plane);
            shader->set_uniform("uniform_color_clip_plane_1", m_color_clip_plane_colors[0]);
            shader->set_uniform("uniform_color_clip_plane_2", m_color_clip_plane_colors[1]);
            shader->set_uniform("print_volume.type", static_cast<int>(m_print_volume.type));
            shader->set_uniform("print_volume.xy_data", m_print_volume.data);
            shader->set_uniform("print_volume.z_data", m_print_volume.zs);
            shader->set_uniform("volume_world_matrix", world_matrix);
            shader->set_uniform("slope.actived", m_slope.active && !volume.first->is_modifier && !volume.first->is_wipe_tower());
            shader->set_uniform("slope.volume_world_normal_matrix", static_cast<Matrix3f>(world_matrix_inv_transp.cast<float>()));
            shader->set_uniform("slope.normal_z", m_slope.normal_z);

#if ENABLE_ENVIRONMENT_MAP
            unsigned int environment_texture_id = GUI::wxGetApp().plater()->get_environment_texture_id();
            bool use_environment_texture = environment_texture_id > 0 && GUI::wxGetApp().app_config->get_bool("use_environment_map");
            shader->set_uniform("use_environment_tex", use_environment_texture);
            if (use_environment_texture)
                glsafe(::glBindTexture(GL_TEXTURE_2D, environment_texture_id));
#endif // ENABLE_ENVIRONMENT_MAP
            glcheck();

            volume.first->model.set_color(volume.first->render_color);
            shader->set_uniform("view_model_matrix", view_matrix * world_matrix);
            shader->set_uniform("projection_matrix", projection_matrix);
            shader->set_uniform("view_normal_matrix", view_normal_matrix);
            volume.first->render();

#if ENABLE_ENVIRONMENT_MAP
            if (use_environment_texture)
                glsafe(::glBindTexture(GL_TEXTURE_2D, 0));
#endif // ENABLE_ENVIRONMENT_MAP

            glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
            glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
            shader->stop_using();
        }
    }


    // Purge the painted triangles cache from everything that was not used for some time.
    // Only do this occasionally (once a second).
    using namespace std::chrono_literals;
    static auto time_since_last_check = time_now;
    if (time_now - time_since_last_check > 1000ms)
        for (auto it = m_mm_paint_cache.volume_data.begin(); it != m_mm_paint_cache.volume_data.end(); ) {
            auto it_delete = it; // The iterator to the deleted element will be invalidated, the others will not.
            ++it;
            if (time_now - it_delete->second.time_used > 5000ms)
                m_mm_paint_cache.volume_data.erase(it_delete);
    }


    if (m_show_sinking_contours) {
        if (sink_shader != nullptr) {
            sink_shader->start_using();
            for (GLVolumeWithIdAndZ& volume : to_render) {
                // render sinking contours of hovered/displaced volumes
                if (volume.first->is_sinking() && !volume.first->is_below_printbed() &&
                    (volume.first->hover != GLVolume::HS_None || volume.first->force_sinking_contours)) {
                    glsafe(::glDepthFunc(GL_ALWAYS));
                    volume.first->render_sinking_contours();
                    glsafe(::glDepthFunc(GL_LESS));
                }
            }
            sink_shader->stop_using();
        }
    }

    if (edges_shader != nullptr) {
        edges_shader->start_using();
        if (m_show_non_manifold_edges && GUI::wxGetApp().app_config->get_bool("non_manifold_edges")) {
            for (GLVolumeWithIdAndZ& volume : to_render) {
                volume.first->render_non_manifold_edges();
            }
        }
        edges_shader->stop_using();
    }

    curr_shader->start_using();

    if (disable_cullface)
        glsafe(::glEnable(GL_CULL_FACE));

    if (type == ERenderType::Transparent) {
        glsafe(::glDisable(GL_BLEND));
        glsafe(::glDepthMask(true));
    }
}

void GLVolumeCollection::reset_outside_state()
{
    for (GLVolume* volume : this->volumes) {
        if (volume != nullptr)
            volume->is_outside = false;
    }
}

void GLVolumeCollection::update_colors_by_extruder(const DynamicPrintConfig* config)
{
    using ColorItem = std::pair<std::string, ColorRGB>;
    std::vector<ColorItem> colors;

    if (static_cast<PrinterTechnology>(config->opt_int("printer_technology")) == ptSLA) {
        const std::string& txt_color = config->opt_string("material_colour").empty() ? 
                                       print_config_def.get("material_colour")->get_default_value<ConfigOptionString>()->value : 
                                       config->opt_string("material_colour");
        ColorRGB rgb;
        if (decode_color(txt_color, rgb))
            colors.push_back({ txt_color, rgb });
    }
    else {
        const ConfigOptionStrings* extruders_opt = dynamic_cast<const ConfigOptionStrings*>(config->option("extruder_colour"));
        if (extruders_opt == nullptr)
            return;

        const ConfigOptionStrings* filamemts_opt = dynamic_cast<const ConfigOptionStrings*>(config->option("filament_colour"));
        if (filamemts_opt == nullptr)
            return;

        size_t colors_count = std::max(extruders_opt->values.size(), filamemts_opt->values.size());
        if (colors_count == 0)
            return;
        colors.resize(colors_count);

        for (unsigned int i = 0; i < colors_count; ++i) {
            const std::string& ext_color = config->opt_string("extruder_colour", i);
            ColorRGB rgb;
            if (decode_color(ext_color, rgb))
                colors[i] = { ext_color, rgb };
            else {
                const std::string& fil_color = config->opt_string("filament_colour", i);
                if (decode_color(fil_color, rgb))
                    colors[i] = { fil_color, rgb };
            }
        }
    }

    for (GLVolume* volume : volumes) {
        if (volume == nullptr || volume->is_modifier || volume->is_wipe_tower() || volume->is_sla_pad() || volume->is_sla_support())
            continue;

        int extruder_id = volume->extruder_id - 1;
        if (extruder_id < 0 || (int)colors.size() <= extruder_id)
            extruder_id = 0;

        const ColorItem& color = colors[extruder_id];
        if (!color.first.empty())
            volume->color = to_rgba(color.second, volume->color.a());
    }
}

std::vector<double> GLVolumeCollection::get_current_print_zs(bool active_only) const
{
    // Collect layer top positions of all volumes.
    std::vector<double> print_zs;
    for (GLVolume *vol : this->volumes)
    {
        if (!active_only || vol->is_active)
            append(print_zs, vol->print_zs);
    }
    std::sort(print_zs.begin(), print_zs.end());

    // Replace intervals of layers with similar top positions with their average value.
    int n = int(print_zs.size());
    int k = 0;
    for (int i = 0; i < n;) {
        int j = i + 1;
        coordf_t zmax = print_zs[i] + EPSILON;
        for (; j < n && print_zs[j] <= zmax; ++ j) ;
        print_zs[k ++] = (j > i + 1) ? (0.5 * (print_zs[i] + print_zs[j - 1])) : print_zs[i];
        i = j;
    }
    if (k < n)
        print_zs.erase(print_zs.begin() + k, print_zs.end());

    return print_zs;
}

size_t GLVolumeCollection::cpu_memory_used() const 
{
	size_t memsize = sizeof(*this) + this->volumes.capacity() * sizeof(GLVolume);
	for (const GLVolume *volume : this->volumes)
		memsize += volume->cpu_memory_used();
	return memsize;
}

size_t GLVolumeCollection::gpu_memory_used() const 
{
	size_t memsize = 0;
	for (const GLVolume *volume : this->volumes)
		memsize += volume->gpu_memory_used();
	return memsize;
}

std::string GLVolumeCollection::log_memory_info() const 
{ 
	return " (GLVolumeCollection RAM: " + format_memsize_MB(this->cpu_memory_used()) + " GPU: " + format_memsize_MB(this->gpu_memory_used()) + " Both: " + format_memsize_MB(this->gpu_memory_used()) + ")";
}

static void thick_lines_to_geometry(
    const Lines&               lines,
    const std::vector<double>& widths,
    const std::vector<double>& heights,
    bool                       closed,
    double                     top_z,
    GUI::GLModel::Geometry&    geometry)
{
    assert(!lines.empty());
    if (lines.empty())
        return;

    enum Direction : unsigned char
    {
        Left,
        Right,
        Top,
        Bottom
    };

    // right, left, top, bottom
    std::array<int, 4> idx_prev    = { -1, -1, -1, -1 };
    std::array<int, 4> idx_initial = { -1, -1, -1, -1 };

    double bottom_z_prev = 0.0;
    Vec2d  b1_prev(Vec2d::Zero());
    Vec2d  v_prev(Vec2d::Zero());
    double len_prev = 0.0;
    double width_initial = 0.0;
    double bottom_z_initial = 0.0;

    // Reserve for a smooth path. Likley the path will not be that smooth, but better than nothing.
    // Allocated 1.5x more data than minimum.
    // Number of indices, not triangles.
    geometry.reserve_more_indices((lines.size() * 8 * 3) * 3 / 2);
    // Number of vertices, not floats.
    geometry.reserve_more_vertices(((lines.size() + 1) * 4) * 3 / 2);

    // loop once more in case of closed loops
    const size_t lines_end = closed ? (lines.size() + 1) : lines.size();
    for (size_t ii = 0; ii < lines_end; ++ii) {
        const size_t i = (ii == lines.size()) ? 0 : ii;
        const Line& line = lines[i];
        const double bottom_z = top_z - heights[i];
        const double middle_z = 0.5 * (top_z + bottom_z);
        const double width = widths[i];

        const bool is_first = (ii == 0);
        const bool is_last = (ii == lines_end - 1);
        const bool is_closing = closed && is_last;

        const Vec2d v = unscale(line.vector()).normalized();
        const double len = unscale<double>(line.length());

        const Vec2d a = unscale(line.a);
        const Vec2d b = unscale(line.b);
        Vec2d a1 = a;
        Vec2d a2 = a;
        Vec2d b1 = b;
        Vec2d b2 = b;
        {
            const double dist = 0.5 * width;  // scaled
            const double dx = dist * v.x();
            const double dy = dist * v.y();
            a1 += Vec2d(+dy, -dx);
            a2 += Vec2d(-dy, +dx);
            b1 += Vec2d(+dy, -dx);
            b2 += Vec2d(-dy, +dx);
        }

        // calculate new XY normals
        const Vec2d xy_right_normal = unscale(line.normal()).normalized();

        std::array<int, 4> idx_a = { 0, 0, 0, 0 };
        std::array<int, 4> idx_b = { 0, 0, 0, 0 };
        int idx_last = int(geometry.vertices_count());

        const bool bottom_z_different = bottom_z_prev != bottom_z;
        bottom_z_prev = bottom_z;

        if (!is_first && bottom_z_different) {
            // Found a change of the layer thickness -> Add a cap at the end of the previous segment.
            geometry.add_triangle(idx_b[Bottom], idx_b[Left], idx_b[Top]);
            geometry.add_triangle(idx_b[Bottom], idx_b[Top], idx_b[Right]);
        }

        // Share top / bottom vertices if possible.
        if (is_first) {
            idx_a[Top] = idx_last++;
            geometry.add_vertex(Vec3f(a.x(), a.y(), top_z), Vec3f(0.0f, 0.0f, 1.0f));
        }
        else
            idx_a[Top] = idx_prev[Top];

        if (is_first || bottom_z_different) {
            // Start of the 1st line segment or a change of the layer thickness while maintaining the print_z.
            idx_a[Bottom] = idx_last++;
            geometry.add_vertex(Vec3f(a.x(), a.y(), bottom_z), Vec3f(0.0f, 0.0f, -1.0f));
            idx_a[Left] = idx_last++;
            geometry.add_vertex(Vec3f(a2.x(), a2.y(), middle_z), Vec3f(-xy_right_normal.x(), -xy_right_normal.y(), 0.0f));
            idx_a[Right] = idx_last++;
            geometry.add_vertex(Vec3f(a1.x(), a1.y(), middle_z), Vec3f(xy_right_normal.x(), xy_right_normal.y(), 0.0f));
        }
        else
            idx_a[Bottom] = idx_prev[Bottom];

        if (is_first) {
            // Start of the 1st line segment.
            width_initial = width;
            bottom_z_initial = bottom_z;
            idx_initial = idx_a;
        }
        else {
            // Continuing a previous segment.
            // Share left / right vertices if possible.
            const double v_dot = v_prev.dot(v);
            // To reduce gpu memory usage, we try to reuse vertices
            // To reduce the visual artifacts, due to averaged normals, we allow to reuse vertices only when any of two adjacent edges 
            // is longer than a fixed threshold.
            // The following value is arbitrary, it comes from tests made on a bunch of models showing the visual artifacts
            const double len_threshold = 2.5;

            // Generate new vertices if the angle between adjacent edges is greater than 45 degrees or thresholds conditions are met
            const bool sharp = (v_dot < 0.707) || (len_prev > len_threshold) || (len > len_threshold);
            if (sharp) {
                if (!bottom_z_different) {
                    // Allocate new left / right points for the start of this segment as these points will receive their own normals to indicate a sharp turn.
                    idx_a[Right] = idx_last++;
                    geometry.add_vertex(Vec3f(a1.x(), a1.y(), middle_z), Vec3f(xy_right_normal.x(), xy_right_normal.y(), 0.0f));
                    idx_a[Left] = idx_last++;
                    geometry.add_vertex(Vec3f(a2.x(), a2.y(), middle_z), Vec3f(-xy_right_normal.x(), -xy_right_normal.y(), 0.0f));
                    if (cross2(v_prev, v) > 0.0) {
                        // Right turn. Fill in the right turn wedge.
                        geometry.add_triangle(idx_prev[Right], idx_a[Right], idx_prev[Top]);
                        geometry.add_triangle(idx_prev[Right], idx_prev[Bottom], idx_a[Right]);
                    }
                    else {
                        // Left turn. Fill in the left turn wedge.
                        geometry.add_triangle(idx_prev[Left], idx_prev[Top], idx_a[Left]);
                        geometry.add_triangle(idx_prev[Left], idx_a[Left], idx_prev[Bottom]);
                    }
                }
            }
            else {
                if (!bottom_z_different) {
                    // The two successive segments are nearly collinear.
                    idx_a[Left]  = idx_prev[Left];
                    idx_a[Right] = idx_prev[Right];
                }
            }
            if (is_closing) {
                if (!sharp) {
                    if (!bottom_z_different) {
                        // Closing a loop with smooth transition. Unify the closing left / right vertices.
                        geometry.set_vertex(idx_initial[Left], geometry.extract_position_3(idx_prev[Left]), geometry.extract_normal_3(idx_prev[Left]));
                        geometry.set_vertex(idx_initial[Right], geometry.extract_position_3(idx_prev[Right]), geometry.extract_normal_3(idx_prev[Right]));
                        geometry.remove_vertex(geometry.vertices_count() - 1);
                        geometry.remove_vertex(geometry.vertices_count() - 1);
                        // Replace the left / right vertex indices to point to the start of the loop.
                        const size_t indices_count = geometry.indices_count();
                        for (size_t u = indices_count - 24; u < indices_count; ++u) {
                            const unsigned int id = geometry.extract_index(u);
                            if (id == (unsigned int)idx_prev[Left])
                                geometry.set_index(u, (unsigned int)idx_initial[Left]);
                            else if (id == (unsigned int)idx_prev[Right])
                                geometry.set_index(u, (unsigned int)idx_initial[Right]);
                        }
                    }
                }
                // This is the last iteration, only required to solve the transition.
                break;
            }
        }

        // Only new allocate top / bottom vertices, if not closing a loop.
        if (is_closing)
            idx_b[Top] = idx_initial[Top];
        else {
            idx_b[Top] = idx_last++;
            geometry.add_vertex(Vec3f(b.x(), b.y(), top_z), Vec3f(0.0f, 0.0f, 1.0f));
        }

        if (is_closing && width == width_initial && bottom_z == bottom_z_initial)
            idx_b[Bottom] = idx_initial[Bottom];
        else {
            idx_b[Bottom] = idx_last++;
            geometry.add_vertex(Vec3f(b.x(), b.y(), bottom_z), Vec3f(0.0f, 0.0f, -1.0f));
        }
        // Generate new vertices for the end of this line segment.
        idx_b[Left] = idx_last++;
        geometry.add_vertex(Vec3f(b2.x(), b2.y(), middle_z), Vec3f(-xy_right_normal.x(), -xy_right_normal.y(), 0.0f));
        idx_b[Right] = idx_last++;
        geometry.add_vertex(Vec3f(b1.x(), b1.y(), middle_z), Vec3f(xy_right_normal.x(), xy_right_normal.y(), 0.0f));

        idx_prev = idx_b;
        bottom_z_prev = bottom_z;
        b1_prev = b1;
        v_prev = v;
        len_prev = len;

        if (bottom_z_different && (closed || (!is_first && !is_last))) {
            // Found a change of the layer thickness -> Add a cap at the beginning of this segment.
            geometry.add_triangle(idx_a[Bottom], idx_a[Right], idx_a[Top]);
            geometry.add_triangle(idx_a[Bottom], idx_a[Top], idx_a[Left]);
        }

        if (!closed) {
            // Terminate open paths with caps.
            if (is_first) {
                geometry.add_triangle(idx_a[Bottom], idx_a[Right], idx_a[Top]);
                geometry.add_triangle(idx_a[Bottom], idx_a[Top], idx_a[Left]);
            }
            // We don't use 'else' because both cases are true if we have only one line.
            if (is_last) {
                geometry.add_triangle(idx_b[Bottom], idx_b[Left], idx_b[Top]);
                geometry.add_triangle(idx_b[Bottom], idx_b[Top], idx_b[Right]);
            }
        }

        // Add quads for a straight hollow tube-like segment.
        // bottom-right face
        geometry.add_triangle(idx_a[Bottom], idx_b[Bottom], idx_b[Right]);
        geometry.add_triangle(idx_a[Bottom], idx_b[Right], idx_a[Right]);
        // top-right face
        geometry.add_triangle(idx_a[Right], idx_b[Right], idx_b[Top]);
        geometry.add_triangle(idx_a[Right], idx_b[Top], idx_a[Top]);
        // top-left face
        geometry.add_triangle(idx_a[Top], idx_b[Top], idx_b[Left]);
        geometry.add_triangle(idx_a[Top], idx_b[Left], idx_a[Left]);
        // bottom-left face
        geometry.add_triangle(idx_a[Left], idx_b[Left], idx_b[Bottom]);
        geometry.add_triangle(idx_a[Left], idx_b[Bottom], idx_a[Bottom]);
    }
}

// caller is responsible for supplying NO lines with zero length
static void thick_lines_to_geometry(
    const Lines3&              lines,
    const std::vector<double>& widths,
    const std::vector<double>& heights,
    bool                       closed,
    GUI::GLModel::Geometry&    geometry)
{
    assert(!lines.empty());
    if (lines.empty())
        return;

    enum Direction : unsigned char
    {
        Left,
        Right,
        Top,
        Bottom
    };

    // left, right, top, bottom
    std::array<int, 4> idx_prev    = { -1, -1, -1, -1 };
    std::array<int, 4> idx_initial = { -1, -1, -1, -1 };

    double z_prev = 0.0;
    double len_prev = 0.0;
    Vec3d  n_right_prev = Vec3d::Zero();
    Vec3d  n_top_prev = Vec3d::Zero();
    Vec3d  unit_v_prev = Vec3d::Zero();
    double width_initial = 0.0;

    // new vertices around the line endpoints
    // left, right, top, bottom
    std::array<Vec3d, 4> a = { Vec3d::Zero(), Vec3d::Zero(), Vec3d::Zero(), Vec3d::Zero() };
    std::array<Vec3d, 4> b = { Vec3d::Zero(), Vec3d::Zero(), Vec3d::Zero(), Vec3d::Zero() };

    // loop once more in case of closed loops
    const size_t lines_end = closed ? (lines.size() + 1) : lines.size();
    for (size_t ii = 0; ii < lines_end; ++ii) {
        const size_t i = (ii == lines.size()) ? 0 : ii;

        const Line3& line = lines[i];
        const double height = heights[i];
        const double width = widths[i];

        const Vec3d unit_v = unscale(line.vector()).normalized();
        const double len = unscale<double>(line.length());

        Vec3d n_top = Vec3d::Zero();
        Vec3d n_right = Vec3d::Zero();

        if (line.a.x() == line.b.x() && line.a.y() == line.b.y()) {
            // vertical segment
            n_top = Vec3d::UnitY();
            n_right = Vec3d::UnitX();
            if (line.a.z() < line.b.z())
                n_right = -n_right;
        }
        else {
            // horizontal segment
            n_right = unit_v.cross(Vec3d::UnitZ()).normalized();
            n_top = n_right.cross(unit_v).normalized();
        }

        const Vec3d rl_displacement = 0.5 * width * n_right;
        const Vec3d tb_displacement = 0.5 * height * n_top;
        const Vec3d l_a = unscale(line.a);
        const Vec3d l_b = unscale(line.b);

        a[Right]  = l_a + rl_displacement;
        a[Left]   = l_a - rl_displacement;
        a[Top]    = l_a + tb_displacement;
        a[Bottom] = l_a - tb_displacement;
        b[Right]  = l_b + rl_displacement;
        b[Left]   = l_b - rl_displacement;
        b[Top]    = l_b + tb_displacement;
        b[Bottom] = l_b - tb_displacement;

        const Vec3d n_bottom = -n_top;
        const Vec3d n_left = -n_right;

        std::array<int, 4> idx_a = { 0, 0, 0, 0};
        std::array<int, 4> idx_b = { 0, 0, 0, 0 };
        int idx_last = int(geometry.vertices_count());

        const bool z_different = (z_prev != l_a.z());
        z_prev = l_b.z();

        // Share top / bottom vertices if possible.
        if (ii == 0) {
            idx_a[Top] = idx_last++;
            geometry.add_vertex((Vec3f)a[Top].cast<float>(), (Vec3f)n_top.cast<float>());
        }
        else
            idx_a[Top] = idx_prev[Top];

        if (ii == 0 || z_different) {
            // Start of the 1st line segment or a change of the layer thickness while maintaining the print_z.
            idx_a[Bottom] = idx_last++;
            geometry.add_vertex((Vec3f)a[Bottom].cast<float>(), (Vec3f)n_bottom.cast<float>());
            idx_a[Left] = idx_last++;
            geometry.add_vertex((Vec3f)a[Left].cast<float>(), (Vec3f)n_left.cast<float>());
            idx_a[Right] = idx_last++;
            geometry.add_vertex((Vec3f)a[Right].cast<float>(), (Vec3f)n_right.cast<float>());
        }
        else
            idx_a[Bottom] = idx_prev[Bottom];

        if (ii == 0) {
            // Start of the 1st line segment.
            width_initial = width;
            idx_initial =  idx_a;
        }
        else {
            // Continuing a previous segment.
            // Share left / right vertices if possible.
            const double v_dot = unit_v_prev.dot(unit_v);
            const bool is_right_turn = n_top_prev.dot(unit_v_prev.cross(unit_v)) > 0.0;

            // To reduce gpu memory usage, we try to reuse vertices
            // To reduce the visual artifacts, due to averaged normals, we allow to reuse vertices only when any of two adjacent edges 
            // is longer than a fixed threshold.
            // The following value is arbitrary, it comes from tests made on a bunch of models showing the visual artifacts
            const double len_threshold = 2.5;

            // Generate new vertices if the angle between adjacent edges is greater than 45 degrees or thresholds conditions are met
            const bool is_sharp = v_dot < 0.707 || len_prev > len_threshold || len > len_threshold;
            if (is_sharp) {
                // Allocate new left / right points for the start of this segment as these points will receive their own normals to indicate a sharp turn.
                idx_a[Right] = idx_last++;
                geometry.add_vertex((Vec3f)a[Right].cast<float>(), (Vec3f)n_right.cast<float>());
                idx_a[Left] = idx_last++;
                geometry.add_vertex((Vec3f)a[Left].cast<float>(), (Vec3f)n_left.cast<float>());

                if (is_right_turn) {
                    // Right turn. Fill in the right turn wedge.
                    geometry.add_triangle(idx_prev[Right], idx_a[Right], idx_prev[Top]);
                    geometry.add_triangle(idx_prev[Right], idx_prev[Bottom], idx_a[Right]);
                }
                else {
                    // Left turn. Fill in the left turn wedge.
                    geometry.add_triangle(idx_prev[Left], idx_prev[Top], idx_a[Left]);
                    geometry.add_triangle(idx_prev[Left], idx_a[Left], idx_prev[Bottom]);
                }
            }
            else {
                // The two successive segments are nearly collinear.
                idx_a[Left] = idx_prev[Left];
                idx_a[Right] = idx_prev[Right];
            }

            if (ii == lines.size()) {
                if (!is_sharp) {
                    // Closing a loop with smooth transition. Unify the closing left / right vertices.
                    geometry.set_vertex(idx_initial[Left], geometry.extract_position_3(idx_prev[Left]), geometry.extract_normal_3(idx_prev[Left]));
                    geometry.set_vertex(idx_initial[Right], geometry.extract_position_3(idx_prev[Right]), geometry.extract_normal_3(idx_prev[Right]));
                    geometry.remove_vertex(geometry.vertices_count() - 1);
                    geometry.remove_vertex(geometry.vertices_count() - 1);
                    // Replace the left / right vertex indices to point to the start of the loop.
                    const size_t indices_count = geometry.indices_count();
                    for (size_t u = indices_count - 24; u < indices_count; ++u) {
                        const unsigned int id = geometry.extract_index(u);
                        if (id == (unsigned int)idx_prev[Left])
                            geometry.set_index(u, (unsigned int)idx_initial[Left]);
                        else if (id == (unsigned int)idx_prev[Right])
                            geometry.set_index(u, (unsigned int)idx_initial[Right]);
                    }
                }

                // This is the last iteration, only required to solve the transition.
                break;
            }
        }

        // Only new allocate top / bottom vertices, if not closing a loop.
        if (closed && ii + 1 == lines.size())
            idx_b[Top] = idx_initial[Top];
        else {
            idx_b[Top] = idx_last++;
            geometry.add_vertex((Vec3f)b[Top].cast<float>(), (Vec3f)n_top.cast<float>());
        }

        if (closed && ii + 1 == lines.size() && width == width_initial)
            idx_b[Bottom] = idx_initial[Bottom];
        else {
            idx_b[Bottom] = idx_last++;
            geometry.add_vertex((Vec3f)b[Bottom].cast<float>(), (Vec3f)n_bottom.cast<float>());
        }

        // Generate new vertices for the end of this line segment.
        idx_b[Left] = idx_last++;
        geometry.add_vertex((Vec3f)b[Left].cast<float>(), (Vec3f)n_left.cast<float>());
        idx_b[Right] = idx_last++;
        geometry.add_vertex((Vec3f)b[Right].cast<float>(), (Vec3f)n_right.cast<float>());

        idx_prev = idx_b;
        n_right_prev = n_right;
        n_top_prev = n_top;
        unit_v_prev = unit_v;
        len_prev = len;

        if (!closed) {
            // Terminate open paths with caps.
            if (i == 0) {
                geometry.add_triangle(idx_a[Bottom], idx_a[Right], idx_a[Top]);
                geometry.add_triangle(idx_a[Bottom], idx_a[Top], idx_a[Left]);
            }

            // We don't use 'else' because both cases are true if we have only one line.
            if (i + 1 == lines.size()) {
                geometry.add_triangle(idx_b[Bottom], idx_b[Left], idx_b[Top]);
                geometry.add_triangle(idx_b[Bottom], idx_b[Top], idx_b[Right]);
            }
        }

        // Add quads for a straight hollow tube-like segment.
        // bottom-right face
        geometry.add_triangle(idx_a[Bottom], idx_b[Bottom], idx_b[Right]);
        geometry.add_triangle(idx_a[Bottom], idx_b[Right], idx_a[Right]);
        // top-right face
        geometry.add_triangle(idx_a[Right], idx_b[Right], idx_b[Top]);
        geometry.add_triangle(idx_a[Right], idx_b[Top], idx_a[Top]);
        // top-left face
        geometry.add_triangle(idx_a[Top], idx_b[Top], idx_b[Left]);
        geometry.add_triangle(idx_a[Top], idx_b[Left], idx_a[Left]);
        // bottom-left face
        geometry.add_triangle(idx_a[Left], idx_b[Left], idx_b[Bottom]);
        geometry.add_triangle(idx_a[Left], idx_b[Bottom], idx_a[Bottom]);
    }
}

void _3DScene::thick_lines_to_verts(
    const Lines&               lines,
    const std::vector<double>& widths,
    const std::vector<double>& heights,
    bool                       closed,
    double                     top_z,
    GUI::GLModel::Geometry&    geometry)
{
    thick_lines_to_geometry(lines, widths, heights, closed, top_z, geometry);
}

void _3DScene::thick_lines_to_verts(
    const Lines3&              lines,
    const std::vector<double>& widths,
    const std::vector<double>& heights,
    bool                       closed,
    GUI::GLModel::Geometry&    geometry)
{
    thick_lines_to_geometry(lines, widths, heights, closed, geometry);
}

// Fill in the qverts and tverts with quads and triangles for the extrusion_path.
void _3DScene::extrusionentity_to_verts(const ExtrusionPath& extrusion_path, float print_z, const Point& copy, GUI::GLModel::Geometry& geometry)
{
    Polyline            polyline = extrusion_path.polyline;
    polyline.remove_duplicate_points();
    polyline.translate(copy);
    const Lines               lines = polyline.lines();
    std::vector<double> widths(lines.size(), extrusion_path.width());
    std::vector<double> heights(lines.size(), extrusion_path.height());
    thick_lines_to_verts(lines, widths, heights, false, print_z, geometry);
}

// Fill in the qverts and tverts with quads and triangles for the extrusion_loop.
void _3DScene::extrusionentity_to_verts(const ExtrusionLoop& extrusion_loop, float print_z, const Point& copy, GUI::GLModel::Geometry& geometry)
{
    Lines               lines;
    std::vector<double> widths;
    std::vector<double> heights;
    for (const ExtrusionPath& extrusion_path : extrusion_loop.paths) {
        Polyline            polyline = extrusion_path.polyline;
        polyline.remove_duplicate_points();
        polyline.translate(copy);
        const Lines lines_this = polyline.lines();
        append(lines, lines_this);
        widths.insert(widths.end(), lines_this.size(), extrusion_path.width());
        heights.insert(heights.end(), lines_this.size(), extrusion_path.height());
    }
    thick_lines_to_verts(lines, widths, heights, true, print_z, geometry);
}

// Fill in the qverts and tverts with quads and triangles for the extrusion_multi_path.
void _3DScene::extrusionentity_to_verts(const ExtrusionMultiPath& extrusion_multi_path, float print_z, const Point& copy, GUI::GLModel::Geometry& geometry)
{
    Lines               lines;
    std::vector<double> widths;
    std::vector<double> heights;
    for (const ExtrusionPath& extrusion_path : extrusion_multi_path.paths) {
        Polyline            polyline = extrusion_path.polyline;
        polyline.remove_duplicate_points();
        polyline.translate(copy);
        const Lines lines_this = polyline.lines();
        append(lines, lines_this);
        widths.insert(widths.end(), lines_this.size(), extrusion_path.width());
        heights.insert(heights.end(), lines_this.size(), extrusion_path.height());
    }
    thick_lines_to_verts(lines, widths, heights, false, print_z, geometry);
}

void _3DScene::extrusionentity_to_verts(const ExtrusionEntityCollection& extrusion_entity_collection, float print_z, const Point& copy, GUI::GLModel::Geometry& geometry)
{
    for (const ExtrusionEntity* extrusion_entity : extrusion_entity_collection.entities)
        extrusionentity_to_verts(extrusion_entity, print_z, copy, geometry);
}

void _3DScene::extrusionentity_to_verts(const ExtrusionEntity* extrusion_entity, float print_z, const Point& copy, GUI::GLModel::Geometry& geometry)
{
    if (extrusion_entity != nullptr) {
        auto* extrusion_path = dynamic_cast<const ExtrusionPath*>(extrusion_entity);
        if (extrusion_path != nullptr)
            extrusionentity_to_verts(*extrusion_path, print_z, copy, geometry);
        else {
            auto* extrusion_loop = dynamic_cast<const ExtrusionLoop*>(extrusion_entity);
            if (extrusion_loop != nullptr)
                extrusionentity_to_verts(*extrusion_loop, print_z, copy, geometry);
            else {
                auto* extrusion_multi_path = dynamic_cast<const ExtrusionMultiPath*>(extrusion_entity);
                if (extrusion_multi_path != nullptr)
                    extrusionentity_to_verts(*extrusion_multi_path, print_z, copy, geometry);
                else {
                    auto* extrusion_entity_collection = dynamic_cast<const ExtrusionEntityCollection*>(extrusion_entity);
                    if (extrusion_entity_collection != nullptr)
                        extrusionentity_to_verts(*extrusion_entity_collection, print_z, copy, geometry);
                    else
                        throw Slic3r::RuntimeError("Unexpected extrusion_entity type in to_verts()");
                }
            }
        }
    }
}

} // namespace Slic3r
