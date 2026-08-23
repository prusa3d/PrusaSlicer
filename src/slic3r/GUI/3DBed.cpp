///|/ Copyright (c) Prusa Research 2019 - 2023 Enrico Turri @enricoturri1966, Vojtěch Bubník @bubnikv, Filip Sykala @Jony01, Lukáš Matěna @lukasmatena, Oleksandra Iushchenko @YuSanka
///|/ Copyright (c) 2022 Michael Kirsch
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "libslic3r/libslic3r.h"

#include "3DBed.hpp"

#include "libslic3r/Polygon.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Geometry/Circle.hpp"
#include "libslic3r/Tesselate.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/MultipleBeds.hpp"

#include "GUI_App.hpp"
#include "GLCanvas3D.hpp"
#include "Plater.hpp"
#include "Camera.hpp"

#include <GL/glew.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/log/trivial.hpp>

#include <imgui/imgui.h>

#include <numeric>

static const float GROUND_Z = -0.02f;
static const Slic3r::ColorRGBA DEFAULT_MODEL_COLOR             = Slic3r::ColorRGBA::DARK_GRAY();
static const Slic3r::ColorRGBA PICKING_MODEL_COLOR             = Slic3r::ColorRGBA::BLACK();
static const Slic3r::ColorRGBA DEFAULT_SOLID_GRID_COLOR        = { 0.9f, 0.9f, 0.9f, 1.0f };
static const Slic3r::ColorRGBA DEFAULT_TRANSPARENT_GRID_COLOR  = { 0.9f, 0.9f, 0.9f, 0.6f };
static const Slic3r::ColorRGBA DISABLED_MODEL_COLOR            = { 0.6f, 0.6f, 0.6f, 0.75f };

namespace Slic3r {
namespace GUI {

bool Bed3D::set_shape(const Pointfs& bed_shape, const double max_print_height, const std::string& custom_texture, const std::string& custom_model, bool force_as_custom)
{
    auto check_texture = [](const std::string& texture) {
        boost::system::error_code ec; // so the exists call does not throw (e.g. after a permission problem)
        return !texture.empty() && (boost::algorithm::iends_with(texture, ".png") || boost::algorithm::iends_with(texture, ".svg")) && boost::filesystem::exists(texture, ec);
    };

    auto check_model = [](const std::string& model) {
        boost::system::error_code ec;
        return !model.empty() && boost::algorithm::iends_with(model, ".stl") && boost::filesystem::exists(model, ec);
    };

    Type type;
    std::string model;
    std::string texture;
    if (force_as_custom)
        type = Type::Custom;
    else {
        auto [new_type, system_model, system_texture] = detect_type(bed_shape);
        type = new_type;
        model = system_model;
        texture = system_texture;
    }

    std::string texture_filename = custom_texture.empty() ? texture : custom_texture;
    if (! texture_filename.empty() && ! check_texture(texture_filename)) {
        BOOST_LOG_TRIVIAL(error) << "Unable to load bed texture: " << texture_filename;
        texture_filename.clear();
    }

    std::string model_filename = custom_model.empty() ? model : custom_model;
    if (! model_filename.empty() && ! check_model(model_filename)) {
        BOOST_LOG_TRIVIAL(error) << "Unable to load bed model: " << model_filename;
        model_filename.clear();
    }

    
    if (m_build_volume.bed_shape() == bed_shape && m_build_volume.max_print_height() == max_print_height && m_type == type && m_texture_filename == texture_filename && m_model_filename == model_filename)
        // No change, no need to update the UI.
        return false;

    m_type = type;
    m_build_volume = BuildVolume { bed_shape, max_print_height };
    m_texture_filename = texture_filename;
    m_model_filename = model_filename;
    m_extended_bounding_box = this->calc_extended_bounding_box();

    m_contour = ExPolygon(Polygon::new_scale(bed_shape));
    const BoundingBox bbox = m_contour.contour.bounding_box();
    if (!bbox.defined)
        throw RuntimeError(std::string("Invalid bed shape"));

    m_triangles.reset();
    m_gridlines.reset();
    m_contourlines.reset();
    m_texture.reset();
    m_model.reset();

    // unregister from picking
    wxGetApp().plater()->canvas3D()->remove_raycasters_for_picking(SceneRaycaster::EType::Bed);

    init_internal_model_from_file();
    init_triangles();

    s_multiple_beds.update_build_volume(m_build_volume.bounding_volume2d());

    m_models_overlap = false;
    if (! m_model_filename.empty()) {
        // Calculate bb of the bed model and figure out if the models would overlap when rendered next to each other.
        const BoundingBoxf3& mdl_bb3 = m_model.model.get_bounding_box();
        const BoundingBoxf model_bb(Vec2d(mdl_bb3.min.x(), mdl_bb3.min.y()), Vec2d(mdl_bb3.max.x(), mdl_bb3.max.y()));
        BoundingBoxf bed_bb = m_build_volume.bounding_volume2d();
        bed_bb.translate(-m_model_offset.x(), -m_model_offset.y());
        Vec2d gap = unscale(s_multiple_beds.get_bed_gap());
        m_models_overlap = (model_bb.size().x() - bed_bb.size().x() > 2 * gap.x() || model_bb.size().y() - bed_bb.size().y() > 2 * gap.y());
    }

    // Set the origin and size for rendering the coordinate system axes.
    m_axes.set_origin({ 0.0, 0.0, static_cast<double>(GROUND_Z) });
    m_axes.set_stem_length(0.1f * static_cast<float>(m_build_volume.bounding_volume().max_size()));

    // Let the calee to update the UI.
    return true;
}

void Bed3D::render(GLCanvas3D& canvas, const Transform3d& view_matrix, const Transform3d& projection_matrix, bool bottom, float scale_factor, bool show_texture)
{
    bool is_thumbnail = s_multiple_beds.get_thumbnail_bed_idx() != -1;
    bool is_preview = wxGetApp().plater()->is_preview_shown();
    int  bed_to_highlight = s_multiple_beds.get_active_bed();

    static std::vector<int> beds_to_render;
    beds_to_render.clear();
    if (is_thumbnail)
        beds_to_render.push_back(s_multiple_beds.get_thumbnail_bed_idx());
    else if (is_preview)
        beds_to_render.push_back(s_multiple_beds.get_active_bed());
    else {
        beds_to_render.resize(s_multiple_beds.get_number_of_beds() + int(s_multiple_beds.should_show_next_bed()));
        std::iota(beds_to_render.begin(), beds_to_render.end(), 0);
    }

    for (int i : beds_to_render) {
        Transform3d mat = view_matrix;
        mat.translate(s_multiple_beds.get_bed_translation(i));
        render_internal(canvas, mat, projection_matrix, bottom, scale_factor, show_texture, false, is_thumbnail || i == bed_to_highlight);
    }

    if (m_digits_models.empty()) {
        for (size_t i = 0; i < 10; ++i) {
            GLModel::Geometry g;
            g.format.vertex_layout = GLModel::Geometry::EVertexLayout::P3T2;
            const double digit_part = 94./1024.;
            g.add_vertex(Vec3f(0, 0, 0), Vec2f(digit_part * i, 1.));
            g.add_vertex(Vec3f(1, 0, 0), Vec2f(digit_part * (i+1), 1.));
            g.add_vertex(Vec3f(1, 1, 0), Vec2f(digit_part * (i+1), 0));
            g.add_vertex(Vec3f(0, 1, 0), Vec2f(digit_part * i, 0));
            g.add_triangle(0, 1, 3);
            g.add_triangle(3, 1, 2);
            m_digits_models.emplace_back(std::make_unique<GLModel>());
            m_digits_models.back()->init_from(std::move(g));
            m_digits_models.back()->set_color(ColorRGBA(0.5f, 0.5f, 0.5f, 0.5f));
        }
        m_digits_texture = std::make_unique<GLTexture>();
        m_digits_texture->load_from_file(resources_dir() + "/icons/numbers.png", true, GLTexture::ECompressionType::None, false);
        m_digits_texture->send_compressed_data_to_gpu();
    }
    if (!is_thumbnail && s_multiple_beds.get_number_of_beds() > 1) {
        GLShaderProgram* shader = wxGetApp().get_shader("flat_texture");
        shader->start_using();
        shader->set_uniform("projection_matrix", projection_matrix);
        glsafe(::glEnable(GL_BLEND));
        glsafe(::glEnable(GL_DEPTH_TEST));
        glsafe(::glDepthMask(GL_FALSE));
        const bool old_cullface = ::glIsEnabled(GL_CULL_FACE);
        glsafe(::glDisable(GL_CULL_FACE));
        glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
        glsafe(::glBindTexture(GL_TEXTURE_2D, m_digits_texture->get_id()));

        const BoundingBoxf bb = this->build_volume().bounding_volume2d();

        for (int i : beds_to_render) {
            if (i + 1 >= m_digits_models.size())
                break;

            double size_x = std::max(10., std::min(bb.size().x(), bb.size().y()) * 0.11);
            double aspect = 1.2;
            Transform3d mat = view_matrix;
            mat.translate(Vec3d(bb.min.x(), bb.min.y(), 0.));
            mat.translate(s_multiple_beds.get_bed_translation(i));
            if (build_volume().type() != BuildVolume::Type::Circle)
                mat.translate(Vec3d(0.3 * size_x, 0.3 * size_x, 0.));
            mat.translate(Vec3d(0., 0., 0.5 * GROUND_Z));            
            mat.scale(Vec3d(size_x, size_x * aspect, 1.));

            shader->set_uniform("view_model_matrix", mat);
            m_digits_models[i + 1]->render();
        }
        glsafe(::glBindTexture(GL_TEXTURE_2D, 0));
        if (old_cullface)
            glsafe(::glEnable(GL_CULL_FACE));
        glsafe(::glDepthMask(GL_TRUE));
        glsafe(::glDisable(GL_DEPTH_TEST));
        shader->stop_using();
    }
}

void Bed3D::render_for_picking(GLCanvas3D& canvas, const Transform3d& view_matrix, const Transform3d& projection_matrix, bool bottom, float scale_factor)
{
    render_internal(canvas, view_matrix, projection_matrix, bottom, scale_factor, false, true, false);
}

void Bed3D::render_internal(GLCanvas3D& canvas, const Transform3d& view_matrix, const Transform3d& projection_matrix, bool bottom, float scale_factor,
    bool show_texture, bool picking, bool active)
{
    if (m_show_mesh_overlay) {
        static int ri_count = 0;
        if (ri_count < 5) { fprintf(stderr, "render_internal called with overlay ON, valid=%d picking=%d\n", (int)m_mesh_data.is_valid(), (int)picking); fflush(stderr); ri_count++; }
    }
    m_scale_factor = scale_factor;

    glsafe(::glEnable(GL_DEPTH_TEST));

    m_model.model.set_color(picking ? PICKING_MODEL_COLOR : DEFAULT_MODEL_COLOR);
    m_triangles.set_color(picking ? PICKING_MODEL_COLOR : DEFAULT_MODEL_COLOR);
    if (!picking && !active) {
        m_model.model.set_color(DISABLED_MODEL_COLOR);
        m_triangles.set_color(DISABLED_MODEL_COLOR);
    }

    switch (m_type)
    {
    case Type::System: { render_system(canvas, view_matrix, projection_matrix, bottom, show_texture, active); break; }
    default:
    case Type::Custom: { render_custom(canvas, view_matrix, projection_matrix, bottom, show_texture, picking, active); break; }
    }

    if (m_show_mesh_overlay && m_mesh_data.is_valid() && !picking)
        render_mesh_overlay(view_matrix, projection_matrix);

    glsafe(::glDisable(GL_DEPTH_TEST));
}

// Calculate an extended bounding box from axes and current model for visualization purposes.
BoundingBoxf3 Bed3D::calc_extended_bounding_box() const
{
    BoundingBoxf3 out { m_build_volume.bounding_volume() };
    const Vec3d size = out.size();
    // ensures that the bounding box is set as defined or the following calls to merge() will not work as intented
    if (size.x() > 0.0 && size.y() > 0.0 && !out.defined)
        out.defined = true;
    // Reset the build volume Z, we don't want to zoom to the top of the build volume if it is empty.
    out.min.z() = 0.0;
    out.max.z() = 0.0;
    // extend to origin in case origin is off bed
    out.merge(m_axes.get_origin());
    // extend to contain axes
    out.merge(m_axes.get_origin() + m_axes.get_total_length() * Vec3d::Ones());
    out.merge(out.min + Vec3d(-m_axes.get_tip_radius(), -m_axes.get_tip_radius(), out.max.z()));
    // extend to contain model, if any
    BoundingBoxf3 model_bb = m_model.model.get_bounding_box();
    if (model_bb.defined) {
        model_bb.translate(m_model_offset);
        out.merge(model_bb);
    }
    return out;
}

void Bed3D::init_triangles()
{
    if (m_triangles.is_initialized())
        return;

    if (m_contour.empty())
        return;

    const std::vector<Vec2f> triangles = triangulate_expolygon_2f(m_contour, NORMALS_UP);
    if (triangles.empty() || triangles.size() % 3 != 0)
        return;

    GLModel::Geometry init_data;
    init_data.format = { GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3T2 };
    init_data.reserve_vertices(triangles.size());
    init_data.reserve_indices(triangles.size() / 3);

    Vec2f min = triangles.front();
    Vec2f max = min;
    for (const Vec2f& v : triangles) {
        min = min.cwiseMin(v).eval();
        max = max.cwiseMax(v).eval();
    }

    const Vec2f size = max - min;
    if (size.x() <= 0.0f || size.y() <= 0.0f)
        return;

    Vec2f inv_size = size.cwiseInverse();
    inv_size.y() *= -1.0f;

    // vertices + indices
    unsigned int vertices_counter = 0;
    for (const Vec2f& v : triangles) {
        const Vec3f p = { v.x(), v.y(), GROUND_Z };
        init_data.add_vertex(p, (Vec2f)(v - min).cwiseProduct(inv_size).eval());
        ++vertices_counter;
        if (vertices_counter % 3 == 0)
            init_data.add_triangle(vertices_counter - 3, vertices_counter - 2, vertices_counter - 1);
    }

    if (m_model.model.get_filename().empty() && m_model.mesh_raycaster == nullptr)
        // register for picking
        register_raycasters_for_picking(init_data, Transform3d::Identity());

    m_triangles.init_from(std::move(init_data));
}

void Bed3D::init_gridlines()
{
    if (m_gridlines.is_initialized())
        return;

    if (m_contour.empty())
        return;

    const BoundingBox& bed_bbox = m_contour.contour.bounding_box();
    const coord_t step = scale_(10.0);

    Polylines axes_lines;
    for (coord_t x = bed_bbox.min.x(); x <= bed_bbox.max.x(); x += step) {
        Polyline line;
        line.append(Point(x, bed_bbox.min.y()));
        line.append(Point(x, bed_bbox.max.y()));
        axes_lines.push_back(line);
    }
    for (coord_t y = bed_bbox.min.y(); y <= bed_bbox.max.y(); y += step) {
        Polyline line;
        line.append(Point(bed_bbox.min.x(), y));
        line.append(Point(bed_bbox.max.x(), y));
        axes_lines.push_back(line);
    }

    // clip with a slightly grown expolygon because our lines lay on the contours and may get erroneously clipped
    Lines gridlines = to_lines(intersection_pl(axes_lines, offset(m_contour, float(SCALED_EPSILON))));

    // append bed contours
    Lines contour_lines = to_lines(m_contour);
    std::copy(contour_lines.begin(), contour_lines.end(), std::back_inserter(gridlines));

    GLModel::Geometry init_data;
    init_data.format = { GLModel::Geometry::EPrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P3 };
    init_data.reserve_vertices(2 * gridlines.size());
    init_data.reserve_indices(2 * gridlines.size());

    for (const Slic3r::Line& l : gridlines) {
        init_data.add_vertex(Vec3f(unscale<float>(l.a.x()), unscale<float>(l.a.y()), GROUND_Z));
        init_data.add_vertex(Vec3f(unscale<float>(l.b.x()), unscale<float>(l.b.y()), GROUND_Z));
        const unsigned int vertices_counter = (unsigned int)init_data.vertices_count();
        init_data.add_line(vertices_counter - 2, vertices_counter - 1);
    }

    m_gridlines.init_from(std::move(init_data));
}

void Bed3D::init_contourlines()
{
    if (m_contourlines.is_initialized())
        return;

    if (m_contour.empty())
        return;

    const Lines contour_lines = to_lines(m_contour);

    GLModel::Geometry init_data;
    init_data.format = { GLModel::Geometry::EPrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P3 };
    init_data.reserve_vertices(2 * contour_lines.size());
    init_data.reserve_indices(2 * contour_lines.size());

    for (const Slic3r::Line& l : contour_lines) {
        init_data.add_vertex(Vec3f(unscale<float>(l.a.x()), unscale<float>(l.a.y()), GROUND_Z));
        init_data.add_vertex(Vec3f(unscale<float>(l.b.x()), unscale<float>(l.b.y()), GROUND_Z));
        const unsigned int vertices_counter = (unsigned int)init_data.vertices_count();
        init_data.add_line(vertices_counter - 2, vertices_counter - 1);
    }

    m_contourlines.init_from(std::move(init_data));
    m_contourlines.set_color({ 1.0f, 1.0f, 1.0f, 0.5f });
}

// Try to match the print bed shape with the shape of an active profile. If such a match exists,
// return the print bed model.
std::tuple<Bed3D::Type, std::string, std::string> Bed3D::detect_type(const Pointfs& shape)
{
    auto bundle = wxGetApp().preset_bundle;
    if (bundle != nullptr) {
        const Preset* curr = &bundle->printers.get_selected_preset();
        while (curr != nullptr) {
            if (curr->config.has("bed_shape")) {
                if (shape == dynamic_cast<const ConfigOptionPoints*>(curr->config.option("bed_shape"))->values) {
                    std::string model_filename = PresetUtils::system_printer_bed_model(*curr);
                    std::string texture_filename = PresetUtils::system_printer_bed_texture(*curr);
                    if (!model_filename.empty() && !texture_filename.empty())
                        return { Type::System, model_filename, texture_filename };
                }
            }

            curr = bundle->printers.get_preset_parent(*curr);
        }
    }

    return { Type::Custom, {}, {} };
}

void Bed3D::render_axes()
{
    if (m_build_volume.valid())
        m_axes.render(Transform3d::Identity(), 0.25f);
}

void Bed3D::render_system(GLCanvas3D& canvas, const Transform3d& view_matrix, const Transform3d& projection_matrix, bool bottom, bool show_texture, bool is_active)
{
    if (m_models_overlap && s_multiple_beds.get_number_of_beds() + int(s_multiple_beds.should_show_next_bed()) > 1) {
        render_default(bottom, false, show_texture, view_matrix, projection_matrix);
        return;
    }

    if (!bottom)
        render_model(view_matrix, projection_matrix);

    if (show_texture)
        render_texture(bottom, canvas, view_matrix, projection_matrix, is_active);
    else if (bottom)
        render_contour(view_matrix, projection_matrix);
}

void Bed3D::render_texture(bool bottom, GLCanvas3D& canvas, const Transform3d& view_matrix, const Transform3d& projection_matrix, bool is_active)
{
    if (m_texture_filename.empty()) {
        m_texture.reset();
        render_default(bottom, false, true, view_matrix, projection_matrix);
        return;
    }

    if (m_texture.get_id() == 0 || m_texture.get_source() != m_texture_filename) {
        m_texture.reset();

        if (boost::algorithm::iends_with(m_texture_filename, ".svg")) {
            // use higher resolution images if graphic card and opengl version allow
            GLint max_tex_size = OpenGLManager::get_gl_info().get_max_tex_size();
            if (m_temp_texture.get_id() == 0 || m_temp_texture.get_source() != m_texture_filename) {
                // generate a temporary lower resolution texture to show while no main texture levels have been compressed
                if (!m_temp_texture.load_from_svg_file(m_texture_filename, false, false, false, max_tex_size / 8)) {
                    render_default(bottom, false, true, view_matrix, projection_matrix);
                    return;
                }
                canvas.request_extra_frame();
            }

            // starts generating the main texture, compression will run asynchronously
            if (!m_texture.load_from_svg_file(m_texture_filename, true, true, true, max_tex_size)) {
                render_default(bottom, false, true, view_matrix, projection_matrix);
                return;
            }
        } 
        else if (boost::algorithm::iends_with(m_texture_filename, ".png")) {
            // generate a temporary lower resolution texture to show while no main texture levels have been compressed
            if (m_temp_texture.get_id() == 0 || m_temp_texture.get_source() != m_texture_filename) {
                if (!m_temp_texture.load_from_file(m_texture_filename, false, GLTexture::None, false)) {
                    render_default(bottom, false, true, view_matrix, projection_matrix);
                    return;
                }
                canvas.request_extra_frame();
            }

            // starts generating the main texture, compression will run asynchronously
            if (!m_texture.load_from_file(m_texture_filename, true, GLTexture::MultiThreaded, true)) {
                render_default(bottom, false, true, view_matrix, projection_matrix);
                return;
            }
        }
        else {
            render_default(bottom, false, true, view_matrix, projection_matrix);
            return;
        }
    }
    else if (m_texture.unsent_compressed_data_available()) {
        // sends to gpu the already available compressed levels of the main texture
        m_texture.send_compressed_data_to_gpu();
        wxQueueEvent(wxGetApp().plater(), new SimpleEvent(EVT_REGENERATE_BED_THUMBNAILS));

        // the temporary texture is not needed anymore, reset it
        if (m_temp_texture.get_id() != 0)
            m_temp_texture.reset();

        canvas.request_extra_frame();
    }

    init_triangles();

    GLShaderProgram* shader = wxGetApp().get_shader("printbed");
    if (shader != nullptr) {
        shader->start_using();
        shader->set_uniform("view_model_matrix", view_matrix);
        shader->set_uniform("projection_matrix", projection_matrix);
        shader->set_uniform("transparent_background", bottom || ! is_active);
        shader->set_uniform("svg_source", boost::algorithm::iends_with(m_texture.get_source(), ".svg"));

        glsafe(::glEnable(GL_DEPTH_TEST));
        if (bottom)
            glsafe(::glDepthMask(GL_FALSE));

        glsafe(::glEnable(GL_BLEND));
        glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

        if (bottom)
            glsafe(::glFrontFace(GL_CW));

        // show the temporary texture while no compressed data is available
        GLuint tex_id = (GLuint)m_temp_texture.get_id();
        if (tex_id == 0)
            tex_id = (GLuint)m_texture.get_id();

        glsafe(::glBindTexture(GL_TEXTURE_2D, tex_id));
        m_triangles.render();
        glsafe(::glBindTexture(GL_TEXTURE_2D, 0));

        if (bottom)
            glsafe(::glFrontFace(GL_CCW));

        glsafe(::glDisable(GL_BLEND));
        if (bottom)
            glsafe(::glDepthMask(GL_TRUE));

        shader->stop_using();
    }
}

void Bed3D::init_internal_model_from_file()
{
    if (m_model_filename.empty())
        return;

    if (m_model.model.get_filename() != m_model_filename && m_model.model.init_from_file(m_model_filename)) {
        m_model.model.set_color(DEFAULT_MODEL_COLOR);

        // move the model so that its origin (0.0, 0.0, 0.0) goes into the bed shape center and a bit down to avoid z-fighting with the texture quad
        m_model_offset = to_3d(m_build_volume.bounding_volume2d().center(), -0.03);

        // register for picking
        const std::vector<std::shared_ptr<SceneRaycasterItem>>* const raycaster = wxGetApp().plater()->canvas3D()->get_raycasters_for_picking(SceneRaycaster::EType::Bed);
        if (!raycaster->empty()) {
            // The raycaster may have been set by the call to init_triangles() made from render_texture() if the printbed was
            // changed while the camera was pointing upward.
            // In this case we need to remove it before creating a new using the model geometry
            wxGetApp().plater()->canvas3D()->remove_raycasters_for_picking(SceneRaycaster::EType::Bed);
            m_model.mesh_raycaster.reset();
        }
        register_raycasters_for_picking(m_model.model.get_geometry(), Geometry::translation_transform(m_model_offset));

        // update extended bounding box
        m_extended_bounding_box = this->calc_extended_bounding_box();
    }
}

void Bed3D::render_model(const Transform3d& view_matrix, const Transform3d& projection_matrix)
{
    if (m_model_filename.empty())
        return;

    init_internal_model_from_file();

    if (!m_model.model.get_filename().empty()) {
        GLShaderProgram* shader = wxGetApp().get_shader("gouraud_light");
        if (shader != nullptr) {
            shader->start_using();
            shader->set_uniform("emission_factor", 0.0f);
            const Transform3d model_matrix = Geometry::translation_transform(m_model_offset);
            shader->set_uniform("view_model_matrix", view_matrix * model_matrix);
            shader->set_uniform("projection_matrix", projection_matrix);
            const Matrix3d view_normal_matrix = view_matrix.matrix().block(0, 0, 3, 3) * model_matrix.matrix().block(0, 0, 3, 3).inverse().transpose();
            shader->set_uniform("view_normal_matrix", view_normal_matrix);
            m_model.model.render();
            shader->stop_using();
        }
    }
}

void Bed3D::render_custom(GLCanvas3D& canvas, const Transform3d& view_matrix, const Transform3d& projection_matrix, bool bottom, bool show_texture, bool picking, bool is_active)
{
    if ((m_texture_filename.empty() && m_model_filename.empty())
     || (m_models_overlap && s_multiple_beds.get_number_of_beds() + int(s_multiple_beds.should_show_next_bed()) > 1)) {
        render_default(bottom, picking, show_texture, view_matrix, projection_matrix);
        return;
    }

    if (!bottom)
        render_model(view_matrix, projection_matrix);

    if (show_texture)
        render_texture(bottom, canvas, view_matrix, projection_matrix, is_active);
    else if (bottom)
        render_contour(view_matrix, projection_matrix);
}

void Bed3D::render_default(bool bottom, bool picking, bool show_texture, const Transform3d& view_matrix, const Transform3d& projection_matrix)
{
    m_texture.reset();

    init_gridlines();
    init_triangles();

    GLShaderProgram* shader = wxGetApp().get_shader("flat");
    if (shader != nullptr) {
        shader->start_using();

        shader->set_uniform("view_model_matrix", view_matrix);
        shader->set_uniform("projection_matrix", projection_matrix);

        glsafe(::glEnable(GL_DEPTH_TEST));
        glsafe(::glEnable(GL_BLEND));
        glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

        const bool has_model = !m_model.model.get_filename().empty() && ! m_models_overlap;
        if (!has_model && !bottom) {
            // draw background
            glsafe(::glDepthMask(GL_FALSE));
            m_triangles.render();
            glsafe(::glDepthMask(GL_TRUE));
        }

        if (show_texture) {
            // draw grid
#if !SLIC3R_OPENGL_ES
            if (!OpenGLManager::get_gl_info().is_core_profile())
                glsafe(::glLineWidth(1.5f * m_scale_factor));
#endif // !SLIC3R_OPENGL_ES
            m_gridlines.set_color(has_model && !bottom ? DEFAULT_SOLID_GRID_COLOR : DEFAULT_TRANSPARENT_GRID_COLOR);
            m_gridlines.render();
        }
        else
            render_contour(view_matrix, projection_matrix);

        glsafe(::glDisable(GL_BLEND));

        shader->stop_using();
    }
}

void Bed3D::render_contour(const Transform3d& view_matrix, const Transform3d& projection_matrix)
{
    init_contourlines();

    GLShaderProgram* shader = wxGetApp().get_shader("flat");
    if (shader != nullptr) {
        shader->start_using();
        shader->set_uniform("view_model_matrix", view_matrix);
        shader->set_uniform("projection_matrix", projection_matrix);

        glsafe(::glEnable(GL_DEPTH_TEST));
        glsafe(::glEnable(GL_BLEND));
        glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

        // draw contour
#if !SLIC3R_OPENGL_ES
        if (!OpenGLManager::get_gl_info().is_core_profile())
            glsafe(::glLineWidth(1.5f * m_scale_factor));
#endif // !SLIC3R_OPENGL_ES
        m_contourlines.render();

        glsafe(::glDisable(GL_BLEND));

        shader->stop_using();
    }
}

void Bed3D::register_raycasters_for_picking(const GLModel::Geometry& geometry, const Transform3d& trafo)
{
    assert(m_model.mesh_raycaster == nullptr);

    indexed_triangle_set its;
    its.vertices.reserve(geometry.vertices_count());
    for (size_t i = 0; i < geometry.vertices_count(); ++i) {
        its.vertices.emplace_back(geometry.extract_position_3(i));
    }
    its.indices.reserve(geometry.indices_count() / 3);
    for (size_t i = 0; i < geometry.indices_count() / 3; ++i) {
        const size_t tri_id = i * 3;
        its.indices.emplace_back(geometry.extract_index(tri_id), geometry.extract_index(tri_id + 1), geometry.extract_index(tri_id + 2));
    }

    m_model.mesh_raycaster = std::make_unique<MeshRaycaster>(std::make_shared<const TriangleMesh>(std::move(its)));
    wxGetApp().plater()->canvas3D()->add_raycaster_for_picking(SceneRaycaster::EType::Bed, 0, *m_model.mesh_raycaster, trafo);
}

void Bed3D::set_mesh_data(const BedMeshData& data)
{
    m_mesh_data = data;
    // Loading a new primary mesh invalidates any previous compare baseline —
    // the delta would no longer correspond to the user's visible view — and
    // any stale per-tool vector from an earlier multi-tool probe.
    m_mesh_per_tool.clear();
    m_mesh_tool_index = 0;
    clear_mesh_compare();
    invalidate_mesh_overlay();
}

void Bed3D::set_mesh_compare(BedMeshData baseline, std::string baseline_name, BedMeshData delta)
{
    m_mesh_baseline      = std::move(baseline);
    m_mesh_delta         = std::move(delta);
    m_mesh_compare_name  = std::move(baseline_name);
    m_mesh_compare_active = true;
    // Delta view is clearest with Zero reference (white = no change).
    m_mesh_reference = BedMeshData::Reference::Zero;
    invalidate_mesh_overlay();
}

void Bed3D::clear_mesh_compare()
{
    if (!m_mesh_compare_active) return;
    m_mesh_compare_active = false;
    m_mesh_baseline      = {};
    m_mesh_delta         = {};
    m_mesh_compare_name.clear();
    invalidate_mesh_overlay();
}

void Bed3D::set_mesh_data_per_tool(std::vector<BedMeshData> meshes)
{
    if (meshes.empty()) {
        m_mesh_per_tool.clear();
        m_mesh_tool_index = 0;
        return;
    }
    m_mesh_per_tool = std::move(meshes);
    m_mesh_tool_index = 0;
    // Mirror T0 into m_mesh_data so the existing render path just works.
    m_mesh_data = m_mesh_per_tool.front();
    clear_mesh_compare();
    invalidate_mesh_overlay();
}

void Bed3D::set_active_mesh_tool(int tool_index)
{
    if (m_mesh_per_tool.empty()) return;
    if (tool_index < 0) tool_index = 0;
    if (tool_index >= int(m_mesh_per_tool.size()))
        tool_index = int(m_mesh_per_tool.size()) - 1;
    if (tool_index == m_mesh_tool_index) return;
    m_mesh_tool_index = tool_index;
    m_mesh_data = m_mesh_per_tool[tool_index];
    // Switching the primary mesh invalidates any comparison that was built
    // against a different tool's data.
    clear_mesh_compare();
    invalidate_mesh_overlay();
}

void Bed3D::init_mesh_overlay()
{
    if (m_mesh_overlay.is_initialized())
        return;

    // Pick the mesh to render: the precomputed delta when compare mode is on,
    // otherwise the current mesh. Both must be valid for rendering.
    const BedMeshData& src = m_mesh_compare_active ? m_mesh_delta : m_mesh_data;
    if (!src.is_valid())
        return;

    const size_t rows = src.rows;
    const size_t cols = src.cols;

    // Subdivide each data quad into `sub × sub` sub-quads with bilinearly
    // interpolated Z values. The original probe points are preserved exactly
    // (they sit on fine-grid vertices whose s,t are integers), but iso-lines
    // no longer show the triangulation kinks that a coarse 1-triangle-per-
    // side tessellation produces. Heavier tessellation also makes the
    // diffuse+specular shading smoother. 4 is a good sweet spot visually —
    // even a 21×21 firmware grid only becomes 81×81 = ~13k triangles, which
    // is nothing for the GPU.
    const int sub = m_mesh_subdivision;
    const size_t fine_rows = (rows - 1) * size_t(sub) + 1;
    const size_t fine_cols = (cols - 1) * size_t(sub) + 1;
    const size_t num_quads     = (fine_rows - 1) * (fine_cols - 1);
    const size_t num_triangles = num_quads * 2;
    const size_t num_vertices  = fine_rows * fine_cols;

    GLModel::Geometry init_data;
    init_data.format = { GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3N3E3 };
    init_data.reserve_vertices(num_vertices);
    init_data.reserve_indices(num_triangles * 3);

    const float z_mean = (src.z_min + src.z_max) * 0.5f;
    const float z_ref = (m_mesh_reference == BedMeshData::Reference::Mean)
        ? src.mean() : 0.f;

    // Bilinear sample of the source grid at fractional coordinates (s,t),
    // where s ∈ [0, cols-1] and t ∈ [0, rows-1]. Clamps to the grid edges.
    // Delegated to BedMeshData::sample_bilinear so tests can exercise the
    // sampling logic without an OpenGL context.
    auto sample_z = [&](double s, double t) -> float {
        return src.sample_bilinear(s, t);
    };

    const double sub_d   = double(sub);
    const float  dx_mm_h = float(src.spacing.x() / sub_d); // width of one fine cell
    const float  dy_mm_h = float(src.spacing.y() / sub_d);

    for (size_t fr = 0; fr < fine_rows; ++fr) {
        const double t = double(fr) / sub_d;
        for (size_t fc = 0; fc < fine_cols; ++fc) {
            const double s = double(fc) / sub_d;

            const float z_raw = sample_z(s, t);
            const float x = float(src.origin.x() + s * src.spacing.x());
            const float y = float(src.origin.y() + t * src.spacing.y());
            const float z = 20.0f + (z_raw - z_mean) * m_mesh_z_scale;

            // Normal from centered differences on the fine grid.
            float dzdx, dzdy;
            if (fc == 0)
                dzdx = (sample_z(s + 1.0 / sub_d, t) - z_raw) * m_mesh_z_scale / dx_mm_h;
            else if (fc == fine_cols - 1)
                dzdx = (z_raw - sample_z(s - 1.0 / sub_d, t)) * m_mesh_z_scale / dx_mm_h;
            else
                dzdx = (sample_z(s + 1.0 / sub_d, t) - sample_z(s - 1.0 / sub_d, t))
                       * m_mesh_z_scale / (2.0f * dx_mm_h);
            if (fr == 0)
                dzdy = (sample_z(s, t + 1.0 / sub_d) - z_raw) * m_mesh_z_scale / dy_mm_h;
            else if (fr == fine_rows - 1)
                dzdy = (z_raw - sample_z(s, t - 1.0 / sub_d)) * m_mesh_z_scale / dy_mm_h;
            else
                dzdy = (sample_z(s, t + 1.0 / sub_d) - sample_z(s, t - 1.0 / sub_d))
                       * m_mesh_z_scale / (2.0f * dy_mm_h);

            const Vec3f normal = Vec3f(-dzdx, -dzdy, 1.0f).normalized();
            const ColorRGBA color = z_deviation_to_color(z_raw, src.z_min, src.z_max, z_ref);
            init_data.add_vertex(Vec3f(x, y, z), normal,
                                 Vec3f(color.r(), color.g(), color.b()));
        }
    }

    // Triangles on the fine grid — two per sub-quad.
    for (size_t fr = 0; fr < fine_rows - 1; ++fr) {
        for (size_t fc = 0; fc < fine_cols - 1; ++fc) {
            const unsigned int tl = static_cast<unsigned int>(fr * fine_cols + fc);
            const unsigned int tr = tl + 1;
            const unsigned int bl = static_cast<unsigned int>((fr + 1) * fine_cols + fc);
            const unsigned int br = bl + 1;
            init_data.add_triangle(tl, bl, tr);
            init_data.add_triangle(tr, bl, br);
        }
    }

    m_mesh_overlay.init_from(std::move(init_data));
}

void Bed3D::render_mesh_overlay(const Transform3d& view_matrix, const Transform3d& projection_matrix)
{
    init_mesh_overlay();

    if (!m_mesh_overlay.is_initialized())
        return;

    GLShaderProgram* shader = wxGetApp().get_shader("bed_mesh_overlay");
    if (shader == nullptr) {
        shader = wxGetApp().get_shader("gouraud_light");
        if (shader == nullptr)
            return;
    }

    shader->start_using();
    shader->set_uniform("view_model_matrix", view_matrix);
    shader->set_uniform("projection_matrix", projection_matrix);
    const Matrix3d view_normal_matrix = view_matrix.matrix().block(0, 0, 3, 3).inverse().transpose();
    shader->set_uniform("view_normal_matrix", view_normal_matrix);
    shader->set_uniform("overlay_alpha", 0.85f);

    // Pass the parameters needed by the FS to reconstruct raw mm-space Z
    // values at each fragment for contour drawing. Must match the encoding
    // in init_mesh_overlay (z = 20 + (raw - mean) * scale).
    const BedMeshData& src = m_mesh_compare_active ? m_mesh_delta : m_mesh_data;
    const float z_mean = (src.z_min + src.z_max) * 0.5f;
    shader->set_uniform("u_z_mean",  z_mean);
    shader->set_uniform("u_z_scale", m_mesh_z_scale);
    shader->set_uniform("u_z_base",  20.0f);
    shader->set_uniform("u_contour_interval",
        m_mesh_show_contours ? m_mesh_contour_interval : 0.f);
    shader->set_uniform("u_contour_darkness", 0.55f);

    glsafe(::glDisable(GL_CULL_FACE));
    glsafe(::glDisable(GL_DEPTH_TEST));
    glsafe(::glEnable(GL_BLEND));
    glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

    m_mesh_overlay.render();

    glsafe(::glDisable(GL_BLEND));
    glsafe(::glEnable(GL_DEPTH_TEST));
    glsafe(::glEnable(GL_CULL_FACE));

    shader->stop_using();
}

void Bed3D::render_mesh_legend()
{
    if (!m_show_mesh_overlay || !m_mesh_data.is_valid())
        return;

    const BedMeshData& displayed = m_mesh_compare_active ? m_mesh_delta : m_mesh_data;

    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::SetNextWindowPos(ImVec2(10.f, 10.f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.8f);

    if (!ImGui::Begin("Bed Mesh", nullptr, flags)) {
        ImGui::End();
        return;
    }

    if (m_mesh_compare_active) {
        ImGui::Text(u8"Δ from %s", m_mesh_compare_name.c_str());
        ImGui::Text("Grid: %zux%zu", displayed.cols, displayed.rows);
        if (ImGui::SmallButton("Clear comparison"))
            clear_mesh_compare();
    } else {
        ImGui::Text("Bed Mesh (%zux%zu)", displayed.cols, displayed.rows);
    }

    // Per-tool picker (XL).
    if (!m_mesh_per_tool.empty() && !m_mesh_compare_active) {
        ImGui::Text("Tool:");
        ImGui::SameLine();
        for (int t = 0; t < int(m_mesh_per_tool.size()); ++t) {
            if (t > 0) ImGui::SameLine();
            char label[8];
            std::snprintf(label, sizeof(label), "T%d", t);
            const bool active = (t == m_mesh_tool_index);
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.55f, 0.85f, 1.f));
            if (ImGui::SmallButton(label))
                set_active_mesh_tool(t);
            if (active) ImGui::PopStyleColor();
        }
    }

    ImGui::Separator();

    // Stats
    ImGui::Text("Min:  %.4f mm", displayed.z_min);
    ImGui::Text("Max:  %.4f mm", displayed.z_max);
    ImGui::Text("Range: %.4f mm", displayed.z_max - displayed.z_min);
    ImGui::Text("Mean: %.4f mm", displayed.mean());
    ImGui::Text("StdDev: %.4f mm", displayed.std_dev());

    ImGui::Separator();

    // Warp report — plane fit + quality grade. Collapsible so it doesn't
    // dominate the legend for users who just want the heatmap.
    if (ImGui::CollapsingHeader("Warp report", ImGuiTreeNodeFlags_DefaultOpen)) {
        const auto pf = displayed.fit_plane();
        ImGui::Text("Tilt X: %+.1f arcmin", pf.tilt_x_arcmin);
        ImGui::Text("Tilt Y: %+.1f arcmin", pf.tilt_y_arcmin);
        ImGui::Text("Warp (RMS after plane): %.4f mm", pf.rms_after);
        const float max_dev = displayed.max_deviation_from_plane();
        ImGui::Text("Worst point: %.4f mm", max_dev);

        ImGui::Spacing();
        ImGui::Text("Threshold:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.f);
        if (ImGui::InputFloat("mm##qth", &m_mesh_quality_threshold, 0.f, 0.f, "%.2f")) {
            if (m_mesh_quality_threshold < 0.01f) m_mesh_quality_threshold = 0.01f;
            if (m_mesh_quality_threshold > 2.0f)  m_mesh_quality_threshold = 2.0f;
        }
        const auto grade = displayed.quality_grade(m_mesh_quality_threshold);
        const char*  grade_str;
        ImVec4       grade_col;
        switch (grade) {
            case BedMeshData::Quality::Excellent: grade_str = "Excellent"; grade_col = ImVec4(0.2f, 0.9f, 0.3f, 1.f); break;
            case BedMeshData::Quality::Good:      grade_str = "Good";      grade_col = ImVec4(0.3f, 0.8f, 0.4f, 1.f); break;
            case BedMeshData::Quality::Marginal:  grade_str = "Marginal";  grade_col = ImVec4(0.95f, 0.75f, 0.2f, 1.f); break;
            default:                              grade_str = "Bad";       grade_col = ImVec4(0.95f, 0.3f, 0.3f, 1.f); break;
        }
        ImGui::TextColored(grade_col, "Quality: %s", grade_str);
    }

    ImGui::Separator();

    // Reference point selector
    ImGui::Text("Reference:");
    ImGui::SameLine();
    int ref_idx = (m_mesh_reference == BedMeshData::Reference::Mean) ? 1 : 0;
    const char* ref_items[] = { "Zero", "Mean" };
    ImGui::SetNextItemWidth(80.f);
    if (ImGui::Combo("##ref", &ref_idx, ref_items, IM_ARRAYSIZE(ref_items))) {
        m_mesh_reference = (ref_idx == 1) ? BedMeshData::Reference::Mean
                                          : BedMeshData::Reference::Zero;
        invalidate_mesh_overlay(); // per-vertex colors need to be recomputed
    }

    ImGui::Separator();

    // Color scale bar
    ImGui::Text("Z Scale (mm):");
    const float bar_width = 20.f;
    const float bar_height = 120.f;
    const int   num_steps  = 32;
    const float z_ref = (m_mesh_reference == BedMeshData::Reference::Mean)
        ? displayed.mean() : 0.f;
    const float max_abs = std::max(std::abs(displayed.z_min - z_ref),
                                   std::abs(displayed.z_max - z_ref));

    ImVec2 cursor = ImGui::GetCursorScreenPos();

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    for (int i = 0; i < num_steps; ++i) {
        float t = 1.0f - float(i) / float(num_steps);            // 1 at top, 0 at bottom
        float z = z_ref + max_abs * (2.0f * t - 1.0f);           // +max above ref at top
        ColorRGBA c = z_deviation_to_color(z, displayed.z_min, displayed.z_max, z_ref);
        ImU32 col = IM_COL32(int(c.r() * 255), int(c.g() * 255), int(c.b() * 255), 255);
        float y0 = cursor.y + bar_height * float(i) / float(num_steps);
        float y1 = cursor.y + bar_height * float(i + 1) / float(num_steps);
        draw_list->AddRectFilled(ImVec2(cursor.x, y0), ImVec2(cursor.x + bar_width, y1), col);
    }

    // Labels next to the bar — absolute Z so the numbers match the stats above
    char buf[32];
    snprintf(buf, sizeof(buf), "%+.3f", z_ref + max_abs);
    draw_list->AddText(ImVec2(cursor.x + bar_width + 4.f, cursor.y - 2.f), IM_COL32(255, 255, 255, 255), buf);
    snprintf(buf, sizeof(buf), "%+.3f", z_ref);
    draw_list->AddText(ImVec2(cursor.x + bar_width + 4.f, cursor.y + bar_height * 0.5f - 6.f), IM_COL32(255, 255, 255, 255), buf);
    snprintf(buf, sizeof(buf), "%+.3f", z_ref - max_abs);
    draw_list->AddText(ImVec2(cursor.x + bar_width + 4.f, cursor.y + bar_height - 14.f), IM_COL32(255, 255, 255, 255), buf);

    // Advance cursor past the bar
    ImGui::Dummy(ImVec2(bar_width + 80.f, bar_height));

    ImGui::Separator();

    // Z exaggeration slider
    ImGui::Text("Z Exaggeration:");
    if (ImGui::SliderFloat("##z_scale", &m_mesh_z_scale, 10.f, 1000.f, "%.0fx")) {
        invalidate_mesh_overlay(); // rebuild mesh with new scale
    }

    ImGui::Separator();

    // Display options — iso-Z contour lines (via shader) and per-cell text
    // labels (via ImGui background drawlist).
    ImGui::Checkbox("Contours", &m_mesh_show_contours);
    if (m_mesh_show_contours) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.f);
        if (ImGui::InputFloat("mm##ci", &m_mesh_contour_interval, 0.f, 0.f, "%.2f")) {
            if (m_mesh_contour_interval < 0.01f) m_mesh_contour_interval = 0.01f;
            if (m_mesh_contour_interval > 1.0f)  m_mesh_contour_interval = 1.0f;
        }
    }
    ImGui::Checkbox("Cell values", &m_mesh_show_cell_values);

    ImGui::End();

    // Cell-value labels — drawn on top of the 3D view via ImGui's background
    // drawlist, projected from each grid point's world-space XY.
    if (m_mesh_show_cell_values)
        render_mesh_cell_labels(displayed);
}

void Bed3D::render_mesh_cell_labels(const BedMeshData& src)
{
    if (!src.is_valid()) return;

    const Camera& camera = wxGetApp().plater()->get_camera();
    const Matrix4d pv = camera.get_projection_matrix().matrix() * camera.get_view_matrix().matrix();
    const std::array<int, 4>& viewport = camera.get_viewport();
    const double half_w = 0.5 * double(viewport[2]);
    const double h      = double(viewport[3]);
    const double half_h = 0.5 * h;

    // Text is drawn at the Z-scaled overlay height so it sits on top of
    // the heatmap; mirror the encoding from init_mesh_overlay.
    const float z_mean = (src.z_min + src.z_max) * 0.5f;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImU32 text_col = IM_COL32(0, 0, 0, 230);
    const ImU32 bg_col   = IM_COL32(255, 255, 255, 180);

    char buf[16];
    for (std::size_t r = 0; r < src.rows; ++r) {
        for (std::size_t c = 0; c < src.cols; ++c) {
            const double x = src.origin.x() + double(c) * src.spacing.x();
            const double y = src.origin.y() + double(r) * src.spacing.y();
            const double z = 20.0 + (src.get(r, c) - z_mean) * m_mesh_z_scale;
            const Vec4d clip = pv * Vec4d(x, y, z, 1.0);
            if (clip.w() <= 0.0) continue;
            const Vec3d ndc = Vec3d(clip.x(), clip.y(), clip.z()) / clip.w();
            if (ndc.x() < -1.0 || ndc.x() > 1.0 || ndc.y() < -1.0 || ndc.y() > 1.0)
                continue;
            const float sx = float(half_w * ndc.x() + double(viewport[0]) + half_w);
            const float sy = float(h - (half_h * ndc.y() + double(viewport[1]) + half_h));
            std::snprintf(buf, sizeof(buf), "%+.3f", src.get(r, c));
            const ImVec2 tsz = ImGui::CalcTextSize(buf);
            dl->AddRectFilled(ImVec2(sx - tsz.x * 0.5f - 2.f, sy - tsz.y * 0.5f - 1.f),
                              ImVec2(sx + tsz.x * 0.5f + 2.f, sy + tsz.y * 0.5f + 1.f),
                              bg_col, 2.f);
            dl->AddText(ImVec2(sx - tsz.x * 0.5f, sy - tsz.y * 0.5f),
                        text_col, buf);
        }
    }
}

} // GUI
} // Slic3r
