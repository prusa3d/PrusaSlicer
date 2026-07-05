///|/ Copyright (c) Prusa Research 2026 — CLI thumbnail renderer
///|/
///|/ The render logic below mirrors what the GUI does in
///|/ GLCanvas3D::_render_thumbnail_internal(): pick a scene-fitting ortho
///|/ camera, bind gouraud_light, and draw each printable ModelVolume with its
///|/ world transform. We reuse PrusaSlicer's stock `gouraud_light` shaders
///|/ (version 140) rather than introducing a new one so output matches the GUI
///|/ as closely as practical for a CLI path.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "CLIThumbnailRenderer.hpp"
#include "OffscreenGLContext.hpp"

#include <GL/glew.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Color.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/TriangleMesh.hpp"

namespace Slic3r {
namespace CLIThumbnails {
namespace {

std::string read_file(const std::string &path)
{
    boost::nowide::ifstream f(path.c_str(), std::ios::binary);
    if (!f.good()) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

GLuint compile_shader(GLenum type, const std::string &src, const char *tag)
{
    GLuint id = glCreateShader(type);
    const char *ptr = src.c_str();
    const GLint  len = static_cast<GLint>(src.size());
    glShaderSource(id, 1, &ptr, &len);
    glCompileShader(id);
    GLint ok = 0;
    glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char    log[4096];
        GLsizei log_len = 0;
        glGetShaderInfoLog(id, sizeof(log), &log_len, log);
        BOOST_LOG_TRIVIAL(error) << "CLI thumbnail " << tag
                                 << " shader compile failed: "
                                 << std::string(log, log_len);
        glDeleteShader(id);
        return 0;
    }
    return id;
}

GLuint link_program(GLuint vs, GLuint fs)
{
    GLuint id = glCreateProgram();
    glAttachShader(id, vs);
    glAttachShader(id, fs);
    glLinkProgram(id);
    GLint ok = 0;
    glGetProgramiv(id, GL_LINK_STATUS, &ok);
    if (!ok) {
        char    log[4096];
        GLsizei log_len = 0;
        glGetProgramInfoLog(id, sizeof(log), &log_len, log);
        BOOST_LOG_TRIVIAL(error) << "CLI thumbnail program link failed: "
                                 << std::string(log, log_len);
        glDeleteProgram(id);
        return 0;
    }
    return id;
}

struct MeshBuffers
{
    GLuint        vao         = 0;
    GLuint        vbo         = 0;
    GLuint        ibo         = 0;
    GLsizei       index_count = 0;
    Transform3d   world       = Transform3d::Identity();
    ColorRGBA     color       = ColorRGBA::GRAY();
    BoundingBoxf3 local_box;
};

MeshBuffers upload_mesh(const TriangleMesh &mesh, GLuint prog)
{
    const indexed_triangle_set &its = mesh.its;
    const size_t                nv  = its.vertices.size();
    const size_t                nt  = its.indices.size();

    MeshBuffers mb;
    if (nv == 0 || nt == 0) return mb;

    // Smooth vertex normals: each vertex gets the normalized sum of the
    // face normals of the triangles it belongs to. Use `auto` throughout
    // because stl_vertex / stl_triangle_vertex_indices are Eigen matrix
    // types with DontAlign that don't bind to plain Vec3f / Vec3i refs.
    std::vector<Vec3f> normals(nv, Vec3f::Zero());
    for (const auto &tri : its.indices) {
        const auto &a = its.vertices[tri[0]];
        const auto &b = its.vertices[tri[1]];
        const auto &c = its.vertices[tri[2]];
        Vec3f n = (Vec3f(b) - Vec3f(a)).cross(Vec3f(c) - Vec3f(a));
        const float m = n.norm();
        if (m > 0.0f) n /= m;
        normals[tri[0]] += n;
        normals[tri[1]] += n;
        normals[tri[2]] += n;
    }
    for (Vec3f &n : normals) {
        const float m = n.norm();
        n = (m > 0.0f) ? (n / m) : Vec3f(0.0f, 0.0f, 1.0f);
    }

    std::vector<float> verts(nv * 6);
    for (size_t i = 0; i < nv; ++i) {
        const auto &v = its.vertices[i];
        verts[i * 6 + 0] = v.x();
        verts[i * 6 + 1] = v.y();
        verts[i * 6 + 2] = v.z();
        verts[i * 6 + 3] = normals[i].x();
        verts[i * 6 + 4] = normals[i].y();
        verts[i * 6 + 5] = normals[i].z();
    }
    std::vector<uint32_t> idx(nt * 3);
    for (size_t i = 0; i < nt; ++i) {
        const auto &t = its.indices[i];
        idx[i * 3 + 0] = static_cast<uint32_t>(t[0]);
        idx[i * 3 + 1] = static_cast<uint32_t>(t[1]);
        idx[i * 3 + 2] = static_cast<uint32_t>(t[2]);
    }

    mb.index_count = static_cast<GLsizei>(nt * 3);

    glGenVertexArrays(1, &mb.vao);
    glBindVertexArray(mb.vao);

    glGenBuffers(1, &mb.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, mb.vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                 verts.data(), GL_STATIC_DRAW);

    glGenBuffers(1, &mb.ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mb.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(idx.size() * sizeof(uint32_t)),
                 idx.data(), GL_STATIC_DRAW);

    const GLint loc_pos = glGetAttribLocation(prog, "v_position");
    const GLint loc_nrm = glGetAttribLocation(prog, "v_normal");
    if (loc_pos >= 0) {
        glEnableVertexAttribArray(loc_pos);
        glVertexAttribPointer(loc_pos, 3, GL_FLOAT, GL_FALSE,
                              6 * sizeof(float), reinterpret_cast<void *>(0));
    }
    if (loc_nrm >= 0) {
        glEnableVertexAttribArray(loc_nrm);
        glVertexAttribPointer(loc_nrm, 3, GL_FLOAT, GL_FALSE,
                              6 * sizeof(float),
                              reinterpret_cast<void *>(3 * sizeof(float)));
    }
    glBindVertexArray(0);

    mb.local_box = mesh.bounding_box();
    return mb;
}

// Build the same extruder color table the GUI uses (Plater.cpp
// ::get_extruder_colors_from_plater_config): start with `extruder_colour`,
// fall back to `filament_colour` for any empty entries. Unparseable values
// fall through to `ColorRGBA::GRAY()`.
std::vector<ColorRGBA> build_extruder_colors(const DynamicPrintConfig &cfg)
{
    std::vector<ColorRGBA> out;
    std::vector<std::string> extruder_strs;
    if (const auto *opt = cfg.option<ConfigOptionStrings>("extruder_colour"))
        extruder_strs = opt->values;
    std::vector<std::string> filament_strs;
    if (const auto *opt = cfg.option<ConfigOptionStrings>("filament_colour"))
        filament_strs = opt->values;

    const size_t n = std::max(extruder_strs.size(), filament_strs.size());
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const std::string src =
            (i < extruder_strs.size() && !extruder_strs[i].empty())
                ? extruder_strs[i]
                : (i < filament_strs.size() ? filament_strs[i] : std::string{});
        ColorRGBA c = ColorRGBA::GRAY();
        if (!src.empty() && !decode_color(src, c))
            c = ColorRGBA::GRAY();
        out.push_back(c);
    }
    if (out.empty()) out.push_back(ColorRGBA::GRAY());
    return out;
}

// Extruder index used by a given volume, 1-based (PrusaSlicer convention).
// Falls back up the chain: volume.config -> object.config -> 1.
// ModelConfig is not a ConfigBase subclass — it wraps DynamicPrintConfig
// internally and only exposes the non-templated option() — so we cast
// explicitly here instead of using the template form from ConfigOptionResolver.
int extruder_id_for_volume(const ModelObject &obj, const ModelVolume &vol)
{
    if (const auto *opt = vol.config.option("extruder"))
        if (const auto *v = dynamic_cast<const ConfigOptionInt *>(opt))
            return std::max(1, v->value);
    if (const auto *opt = obj.config.option("extruder"))
        if (const auto *v = dynamic_cast<const ConfigOptionInt *>(opt))
            return std::max(1, v->value);
    return 1;
}

Transform3d look_at(const Vec3d &eye, const Vec3d &target, const Vec3d &up_hint)
{
    const Vec3d f = (target - eye).normalized();
    const Vec3d r = f.cross(up_hint).normalized();
    const Vec3d u = r.cross(f);

    Transform3d m = Transform3d::Identity();
    m(0, 0) = r.x();  m(0, 1) = r.y();  m(0, 2) = r.z();  m(0, 3) = -r.dot(eye);
    m(1, 0) = u.x();  m(1, 1) = u.y();  m(1, 2) = u.z();  m(1, 3) = -u.dot(eye);
    m(2, 0) = -f.x(); m(2, 1) = -f.y(); m(2, 2) = -f.z(); m(2, 3) =  f.dot(eye);
    return m;
}

Transform3d ortho(double l, double r, double b, double t, double n, double fa)
{
    Transform3d m = Transform3d::Identity();
    m(0, 0) = 2.0 / (r - l);
    m(1, 1) = 2.0 / (t - b);
    m(2, 2) = -2.0 / (fa - n);
    m(0, 3) = -(r + l) / (r - l);
    m(1, 3) = -(t + b) / (t - b);
    m(2, 3) = -(fa + n) / (fa - n);
    return m;
}

} // namespace

ThumbnailsList render_thumbnails(const Model &model,
                                 const DynamicPrintConfig &print_config,
                                 const ThumbnailsParams &params,
                                 const std::string      &resources_dir)
{
    if (params.sizes.empty()) return {};

    int max_w = 0, max_h = 0;
    for (const Vec2d &s : params.sizes) {
        max_w = std::max(max_w, static_cast<int>(std::lround(s.x())));
        max_h = std::max(max_h, static_cast<int>(std::lround(s.y())));
    }
    if (max_w <= 0 || max_h <= 0) return {};

    std::string err;
    std::unique_ptr<OffscreenGLContext> ctx =
        OffscreenGLContext::create(max_w, max_h, &err);
    if (!ctx) {
        BOOST_LOG_TRIVIAL(warning)
            << "CLI thumbnails skipped: offscreen GL unavailable (" << err << ")";
        return {};
    }
    if (!ctx->make_current()) {
        BOOST_LOG_TRIVIAL(warning) << "CLI thumbnails skipped: make_current failed";
        return {};
    }
    BOOST_LOG_TRIVIAL(info) << "CLI thumbnails: offscreen backend = "
                            << ctx->backend_name();

    glewExperimental = GL_TRUE;
    const GLenum ge = glewInit();
    // GLEW returns a non-OK error on EGL core-profile contexts because
    // glGetString(GL_EXTENSIONS) is removed in core profiles; despite the
    // error, glewExperimental causes GLEW to load function pointers via
    // glGetStringi(). We verify that by checking a core 3.2 function pointer
    // we actually use, and only bail if it's genuinely unavailable.
    while (glGetError() != GL_NO_ERROR) { /* drain glewInit-spurious error */ }
    if (ge != GLEW_OK && glCreateShader == nullptr) {
        BOOST_LOG_TRIVIAL(warning)
            << "CLI thumbnails skipped: glewInit failed and core GL not usable: "
            << glewGetErrorString(ge);
        return {};
    }
    if (ge != GLEW_OK) {
        BOOST_LOG_TRIVIAL(debug)
            << "glewInit returned non-OK (" << glewGetErrorString(ge)
            << ") but core GL function pointers loaded — proceeding";
    }

    const std::string shader_dir = resources_dir + "/shaders/140/";
    const std::string vs_src = read_file(shader_dir + "gouraud_light.vs");
    const std::string fs_src = read_file(shader_dir + "gouraud_light.fs");
    if (vs_src.empty() || fs_src.empty()) {
        BOOST_LOG_TRIVIAL(warning) << "CLI thumbnails skipped: could not read shaders from "
                                   << shader_dir;
        return {};
    }

    const GLuint vs   = compile_shader(GL_VERTEX_SHADER,   vs_src, "vertex");
    const GLuint fs   = compile_shader(GL_FRAGMENT_SHADER, fs_src, "fragment");
    const GLuint prog = (vs && fs) ? link_program(vs, fs) : 0;
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);
    if (!prog) return {};

    const std::vector<ColorRGBA> extruder_colors = build_extruder_colors(print_config);

    std::vector<MeshBuffers> meshes;
    BoundingBoxf3            scene_box;
    for (const ModelObject *obj : model.objects) {
        if (!obj) continue;
        for (const ModelVolume *vol : obj->volumes) {
            if (!vol || !vol->is_model_part()) continue;
            const int extruder_id = extruder_id_for_volume(*obj, *vol);
            const ColorRGBA color = extruder_colors[std::min<size_t>(
                static_cast<size_t>(extruder_id - 1), extruder_colors.size() - 1)];
            for (const ModelInstance *inst : obj->instances) {
                if (!inst || !inst->printable) continue;
                MeshBuffers mb = upload_mesh(vol->mesh(), prog);
                if (mb.index_count == 0) continue;
                mb.world = inst->get_matrix() * vol->get_matrix();
                mb.color = color;
                scene_box.merge(mb.local_box.transformed(mb.world));
                meshes.push_back(std::move(mb));
            }
        }
    }

    auto cleanup_and_return = [&](ThumbnailsList result) -> ThumbnailsList {
        for (const MeshBuffers &mb : meshes) {
            if (mb.ibo) glDeleteBuffers(1, &mb.ibo);
            if (mb.vbo) glDeleteBuffers(1, &mb.vbo);
            if (mb.vao) glDeleteVertexArrays(1, &mb.vao);
        }
        glDeleteProgram(prog);
        ctx->release();
        return result;
    };

    if (meshes.empty() || !scene_box.defined) {
        BOOST_LOG_TRIVIAL(info) << "CLI thumbnails: no printable volumes";
        return cleanup_and_return({});
    }

    const Vec3d  center  = scene_box.center();
    const double radius  = std::max(1.0, 0.5 * scene_box.size().norm());
    const Vec3d  eye_dir = Vec3d(1.0, -1.0, 1.0).normalized();
    const Vec3d  eye     = center + eye_dir * (radius * 4.0);
    const Transform3d view = look_at(eye, center, Vec3d(0, 0, 1));

    // Project the 8 scene bbox corners through the view matrix to get the
    // actual 2D extents of the content in view space. Fitting against this
    // box rather than the bounding sphere handles non-square thumbnails
    // (notably portrait, where aspect < 1) without clipping the model.
    double content_min_x =  std::numeric_limits<double>::max();
    double content_max_x = -std::numeric_limits<double>::max();
    double content_min_y =  std::numeric_limits<double>::max();
    double content_max_y = -std::numeric_limits<double>::max();
    for (int i = 0; i < 8; ++i) {
        const Vec3d corner(
            (i & 1) ? scene_box.max.x() : scene_box.min.x(),
            (i & 2) ? scene_box.max.y() : scene_box.min.y(),
            (i & 4) ? scene_box.max.z() : scene_box.min.z());
        const Vec3d v = view * corner;
        content_min_x = std::min(content_min_x, v.x());
        content_max_x = std::max(content_max_x, v.x());
        content_min_y = std::min(content_min_y, v.y());
        content_max_y = std::max(content_max_y, v.y());
    }
    const double content_half_w = 0.5 * (content_max_x - content_min_x);
    const double content_half_h = 0.5 * (content_max_y - content_min_y);
    const double content_cx     = 0.5 * (content_min_x + content_max_x);
    const double content_cy     = 0.5 * (content_min_y + content_max_y);

    GLuint fbo = 0, color_rb = 0, depth_rb = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenRenderbuffers(1, &color_rb);
    glBindRenderbuffer(GL_RENDERBUFFER, color_rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, max_w, max_h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_RENDERBUFFER, color_rb);
    glGenRenderbuffers(1, &depth_rb);
    glBindRenderbuffer(GL_RENDERBUFFER, depth_rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, max_w, max_h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, depth_rb);

    const GLenum fbs = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (fbs != GL_FRAMEBUFFER_COMPLETE) {
        BOOST_LOG_TRIVIAL(warning)
            << "CLI thumbnails skipped: framebuffer incomplete: 0x"
            << std::hex << fbs;
        glDeleteRenderbuffers(1, &color_rb);
        glDeleteRenderbuffers(1, &depth_rb);
        glDeleteFramebuffers(1, &fbo);
        return cleanup_and_return({});
    }

    glUseProgram(prog);
    const GLint loc_proj     = glGetUniformLocation(prog, "projection_matrix");
    const GLint loc_vm       = glGetUniformLocation(prog, "view_model_matrix");
    const GLint loc_vn       = glGetUniformLocation(prog, "view_normal_matrix");
    const GLint loc_color    = glGetUniformLocation(prog, "uniform_color");
    const GLint loc_emission = glGetUniformLocation(prog, "emission_factor");

    // show_bed is honoured by the GUI but not by this CLI path: PrusaSlicer's
    // bed model is loaded through the GUI layer, and dragging wxWidgets into
    // the CLI just to render a background plate is out of scope for this
    // change. Log once per call when it was requested so users have a
    // breadcrumb if the CLI thumbnail differs from their GUI expectation.
    if (params.show_bed)
        BOOST_LOG_TRIVIAL(debug) << "CLI thumbnails: show_bed requested but not supported (ignored).";

    ThumbnailsList out;
    out.reserve(params.sizes.size());

    for (const Vec2d &sz : params.sizes) {
        const int w = static_cast<int>(std::lround(sz.x()));
        const int h = static_cast<int>(std::lround(sz.y()));
        if (w <= 0 || h <= 0) continue;

        glViewport(0, 0, w, h);

        // Fit the content bbox (in view space) to the thumbnail aspect so
        // portrait and landscape aspects both cover the whole model.
        const double margin = 1.1;
        const double aspect = static_cast<double>(w) / static_cast<double>(h);
        double half_w, half_h;
        if (content_half_h <= 0.0 || content_half_w / content_half_h > aspect) {
            // Content is wider than the viewport — fit width.
            half_w = content_half_w * margin;
            half_h = half_w / std::max(aspect, 1e-6);
        } else {
            // Content is taller than the viewport — fit height.
            half_h = content_half_h * margin;
            half_w = half_h * aspect;
        }
        const double near_z = 0.1;
        const double far_z  = radius * 16.0;
        const Transform3d proj = ortho(
            content_cx - half_w, content_cx + half_w,
            content_cy - half_h, content_cy + half_h,
            near_z, far_z);

        if (params.transparent_background)
            // Match the GUI's convention: alpha 0 but RGB 0.4 so viewers that
            // drop the alpha channel show gray instead of solid black.
            glClearColor(0.4f, 0.4f, 0.4f, 0.0f);
        else
            glClearColor(0.4f, 0.4f, 0.4f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);

        if (loc_proj >= 0) {
            const Eigen::Matrix4f proj_f = proj.matrix().cast<float>();
            glUniformMatrix4fv(loc_proj, 1, GL_FALSE, proj_f.data());
        }
        if (loc_emission >= 0) glUniform1f(loc_emission, 0.0f);

        for (const MeshBuffers &mb : meshes) {
            const Transform3d      vm   = view * mb.world;
            const Eigen::Matrix4f  vm_f = vm.matrix().cast<float>();
            const Eigen::Matrix3d  vn_d = view.linear()
                * mb.world.linear().inverse().transpose();
            const Eigen::Matrix3f  vn_f = vn_d.cast<float>();

            if (loc_vm    >= 0) glUniformMatrix4fv(loc_vm, 1, GL_FALSE, vm_f.data());
            if (loc_vn    >= 0) glUniformMatrix3fv(loc_vn, 1, GL_FALSE, vn_f.data());
            if (loc_color >= 0)
                glUniform4f(loc_color, mb.color.r(), mb.color.g(),
                            mb.color.b(), mb.color.a());

            glBindVertexArray(mb.vao);
            glDrawElements(GL_TRIANGLES, mb.index_count, GL_UNSIGNED_INT, nullptr);
        }

        glFinish();

        ThumbnailData data;
        data.set(w, h);
        // Matches the GUI's convention (see GLCanvas3D::_render_thumbnail_*):
        // glReadPixels writes bottom-up; the PNG / QOI / JPEG encoders in
        // GCodeThumbnails all flip vertically during encoding, so we must NOT
        // flip here or the embedded thumbnails end up upside down.
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, data.pixels.data());
        out.push_back(std::move(data));
    }

    glDeleteRenderbuffers(1, &color_rb);
    glDeleteRenderbuffers(1, &depth_rb);
    glDeleteFramebuffers(1, &fbo);
    return cleanup_and_return(std::move(out));
}

} // namespace CLIThumbnails
} // namespace Slic3r
