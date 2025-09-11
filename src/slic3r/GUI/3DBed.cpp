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

} // GUI
} // Slic3r
