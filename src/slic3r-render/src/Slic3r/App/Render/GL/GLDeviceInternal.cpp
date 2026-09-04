#include "Slic3r/App/Render/GL/GLDeviceInternal.hpp"

#include "Slic3r/App/Render/Context.hpp"
#include "Slic3r/App/Render/Geometry.hpp"
#include "Slic3r/App/Render/Shader.hpp"

#include "Slic3r/App/Render/GL/commonGL.hpp"
#include "Slic3r/App/Render/GL/GLTypes.hpp"
#include "Slic3r/App/Render/GL/GLBufferInternal.hpp"
#include "Slic3r/App/Render/GL/GLGeometryInternal.hpp"
#include "Slic3r/App/Render/GL/GLShaderInternal.hpp"
#include "Slic3r/App/Render/GL/GLTextureInternal.hpp"
#include "Slic3r/App/Render/GL/GLFramebufferInternal.hpp"
#include "Slic3r/App/Render/GL/GLRenderbufferInternal.hpp"

#include "Slic3r/Assert.hpp"
#include "GL/glew.h"

#define RENDER_TRACE_LOG 0
#define RENDER_TRACE_DRAW 0

namespace Slic3r::App::Render::GL {

GLDeviceInternal::GLDeviceInternal(Context& context): m_context(context)
{
    m_bound_textures.resize(context.max_texture_units(), 0);
}

void GLDeviceInternal::load_state()
{
    glGetIntegerv(GL_CURRENT_PROGRAM, reinterpret_cast<GLint*>(&m_bound_shader));
    glCheck();
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, reinterpret_cast<GLint*>(&m_bound_vertex_buffer));
    glCheck();
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, reinterpret_cast<GLint*>(&m_bound_index_buffer));
    glCheck();
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, reinterpret_cast<GLint*>(&m_bound_vao));
    glCheck();

    GLint active_texture_unit;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active_texture_unit);
    glCheck();
    m_active_texture_unit = static_cast<uint8_t>(active_texture_unit - GL_TEXTURE0);

    for (auto& tex : m_bound_textures)
        tex = 0;

#if RENDER_TRACE_LOG
    SPDLOG_INFO(
        "loaded GL state: shader {}  VB {}  IB {}  VAO {}", m_bound_shader, m_bound_vertex_buffer,
        m_bound_index_buffer, m_bound_vao
    );
#endif
}

void GLDeviceInternal::activate_texture_unit(uint8_t unit)
{
    if (m_active_texture_unit == unit)
        return;
    glActiveTexture(GL_TEXTURE0 + unit);
    glCheck();
    m_active_texture_unit = unit;
}

void GLDeviceInternal::bind_texture(uint8_t unit, const Texture& t)
{
    const auto& tex = t.get_internal_as<GLTextureInternal>();
    if (m_bound_textures[unit] == tex.m_id)
        return;
    activate_texture_unit(unit);
    glBindTexture(tex.m_target, tex.m_id);
    glCheck();
    m_bound_textures[unit] = tex.m_id;
}

void GLDeviceInternal::unbind_texture(uint8_t unit, const Texture& t)
{
    const auto& tex = t.get_internal_as<GLTextureInternal>();
    if (m_bound_textures[unit] == 0)
        return;
    activate_texture_unit(unit);
    glBindTexture(tex.m_target, 0);
    glCheck();
    m_bound_textures[unit] = 0;
}

#ifdef SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
void GLDeviceInternal::bind_texture_buffer(uint8_t unit, const TextureBuffer& tb)
{
    const auto& buf = tb.get_internal_as<GLBufferInternal>();

    activate_texture_unit(unit);
    glBindTexture(GL_TEXTURE_BUFFER, buf.m_tex_id);
    glCheck();

    glTexBuffer(GL_TEXTURE_BUFFER, texture_internal_format(tb.format()), buf.m_id);
    glCheck();
}
#endif // SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED

void GLDeviceInternal::bind_renderbuffer(const Renderbuffer& b)
{
    GLuint id = b.get_internal_as<GL::GLRenderbufferInternal>().m_id;

#if RENDER_TRACE_LOG
    SPDLOG_INFO("Binding RB {}", id);
#endif // RENDER_TRACE_LOG
    if (m_bound_renderbuffer == id)
        return;
#if RENDER_TRACE_LOG
    SPDLOG_INFO("Bound RB {}", id);
#endif // RENDER_TRACE_LOG
    glBindRenderbuffer(GL_RENDERBUFFER, id);
    glCheck();
    m_bound_renderbuffer = id;
#if RENDER_TRACE_LOG
    SPDLOG_INFO("(bind_renderbuffer) Setting bound RB {}", id);
#endif // RENDER_TRACE_LOG
}

void GLDeviceInternal::unbind_renderbuffer(const Renderbuffer& b)
{
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glCheck();
    m_bound_renderbuffer = 0;
}

void* GLDeviceInternal::map_buffer(BufferTarget target, BufferAccess access)
{
    void* ret = glMapBuffer(type(target), type(access));
    glCheck();
    return ret;
}

void GLDeviceInternal::unmap_buffer(BufferTarget target)
{
    glUnmapBuffer(type(target));
    glCheck();
}

void GLDeviceInternal::bind_framebuffer(const Framebuffer& buffer)
{
    const auto& fb = buffer.get_internal_as<GLFramebufferInternal>();

    if (fb.m_target == GL_FRAMEBUFFER || fb.m_target == GL_DRAW_FRAMEBUFFER) {
        ResourceId id;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, reinterpret_cast<GLint*>(&id));
        glCheck();
        m_draw_framebuffer_ids.push(id);
    }

    glBindFramebuffer(fb.m_target, fb.m_id);
    glCheck();
}

void GLDeviceInternal::unbind_framebuffer(const Framebuffer& buffer)
{
    const auto& fb = buffer.get_internal_as<GLFramebufferInternal>();

    ResourceId id = 0;
    if (fb.m_target == GL_FRAMEBUFFER || fb.m_target == GL_DRAW_FRAMEBUFFER) {
        DEBUG_ASSERT(!m_draw_framebuffer_ids.empty());
        id = m_draw_framebuffer_ids.top();
        m_draw_framebuffer_ids.pop();
    }

    glBindFramebuffer(fb.m_target, id);
    glCheck();
}

void GLDeviceInternal::bind_shader(const Shader& s)
{
    ResourceId shader_id = s.get_internal_as<GLShaderInternal>().m_id;
    if (m_bound_shader == shader_id)
        return;

    glUseProgram(shader_id);
    glCheck();
    m_bound_shader = shader_id;
}

void GLDeviceInternal::bind_vertex_buffer(ResourceId vb)
{
#if RENDER_TRACE_LOG
    SPDLOG_INFO("Binding VB {}", vb);
#endif
    if (m_bound_vertex_buffer == vb)
        return;
#if RENDER_TRACE_LOG
    SPDLOG_INFO("Bound VB {}", vb);
#endif
    glBindBuffer(GL_ARRAY_BUFFER, vb);
    glCheck();
    m_bound_vertex_buffer = vb;
#if RENDER_TRACE_LOG
    SPDLOG_INFO("(bind_vertex_buffer) Setting bound VB {}", vb);
#endif
}

void GLDeviceInternal::bind_index_buffer(ResourceId ib)
{
#if RENDER_TRACE_LOG
    SPDLOG_INFO("Binding IB {}", ib);
#endif
    if (m_bound_index_buffer == ib)
        return;
#if RENDER_TRACE_LOG
    SPDLOG_INFO("Bound IB {}", ib);
#endif
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib);
    glCheck();
    m_bound_index_buffer = ib;
    m_bound_indices = ib != 0;
}

#ifdef SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
void GLDeviceInternal::bind_texture_buffer(ResourceId tb)
{
#if RENDER_TRACE_LOG
    SPDLOG_INFO("Binding TB {}", tb);
#endif // RENDER_TRACE_LOG
    if (m_bound_texture_buffer == tb)
        return;
#if RENDER_TRACE_LOG
    SPDLOG_INFO("Bound TB {}", tb);
#endif // RENDER_TRACE_LOG
    glBindBuffer(GL_TEXTURE_BUFFER, tb);
    glCheck();
    m_bound_texture_buffer = tb;
#if RENDER_TRACE_LOG
    SPDLOG_INFO("(bind_texture_buffer) Setting bound TB {}", tb);
#endif // RENDER_TRACE_LOG
}
#endif // SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED

void GLDeviceInternal::bind_vao(ResourceId vao)
{
#if RENDER_TRACE_LOG
    SPDLOG_INFO("Binding vao {}", vao);
#endif
    if (m_bound_vao == vao)
        return;
#if RENDER_TRACE_LOG
    SPDLOG_INFO("Bound vao {}", vao);
#endif
    glBindVertexArray(vao);
    glCheck();
    m_bound_vao = vao;

    if (vao == 0)
    {
        m_bound_vertex_buffer = 0;
        m_bound_index_buffer = 0;
    }
}

void GLDeviceInternal::bind_buffer(BufferTarget target, ResourceId buffer)
{
    switch (target) {
    case BufferTarget::VertexBuffer:
        bind_vertex_buffer(buffer);
        break;

    case BufferTarget::IndexBuffer:
        bind_index_buffer(buffer);
        break;

#ifdef SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
    case BufferTarget::TextureBuffer:
        bind_texture_buffer(buffer);
        break;
#endif // SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED

    default:
        // unsupported target
        PANIC("Unsupported buffer target to bind to");

    }
}

void GLDeviceInternal::unbind_buffer(BufferTarget target)
{
    glBindBuffer(type(target), 0);
    glCheck();

    switch (target) {
    case BufferTarget::VertexBuffer:
        m_bound_vertex_buffer = 0;
        break;

    case BufferTarget::IndexBuffer:
        m_bound_index_buffer = 0;
        m_bound_indices = false;
        break;

#ifdef SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
    case BufferTarget::TextureBuffer:
        m_bound_texture_buffer = 0;
        break;
#endif // SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED

    default:
        // unsupported target
        PANIC("Unsupported buffer target to bind to");
    }
}

void GLDeviceInternal::bind_geometry(const Geometry& g, const Shader& shader)
{
    DEBUG_ASSERT(g.ready());
    // TODO: handle ES with VAO
    bool use_vao = m_context.is_vao_available();
    const auto& geom = g.get_internal_as<GLGeometryInternal>();

    GLuint vb_id = g.vertex_buffer()->get_internal_as<GLBufferInternal>().m_id;
    if (use_vao) {
        bind_vao(geom.m_vao_id);
    }

    m_bound_indices = geom.m_has_indices;
    GLuint shader_id = shader.get_internal_as<GLShaderInternal>().m_id;
    auto* index_buffer = g.index_buffer();
    bool needs_new_binding = !use_vao || geom.m_shader_id != shader_id;

    if (needs_new_binding) {
        bind_vertex_buffer(vb_id);

        const VertexAttribsDesc& attrs = g.vertex_format();
        const size_t stride = vertex_attribs_stride(attrs);

        for (const auto& vad : attrs) {
            int loc = shader.get_attrib_location(shader_input_name(vad.attrib_type));
            if (loc < 0)
                continue;
            glEnableVertexAttribArray(loc);
            if (GL::is_integer(vad.data_type) && !(vad.cast_to_float || vad.normalize))
                glVertexAttribIPointer(
                    loc, vad.components, type(vad.data_type), stride,
                    reinterpret_cast<void*>(vad.offset)
                );
            else
                glVertexAttribPointer(
                    loc, vad.components, type(vad.data_type), vad.normalize ? GL_TRUE : GL_FALSE,
                    stride, reinterpret_cast<void*>(vad.offset)
                );
            glCheck();
        }

        if (index_buffer) {
            bind_index_buffer(index_buffer->get_internal_as<GLBufferInternal>().m_id);
            m_bound_index_type = g.index_type();
        } else
            bind_index_buffer(0);
        geom.m_shader_id = shader_id;
    }

    if (index_buffer)
        m_bound_index_type = g.index_type();

#if RENDER_TRACE_LOG
    print_buffer_info("bind_geometry");
#endif

    if (m_context.is_es()) {
        bind_vertex_buffer(vb_id);
    }
}

void GLDeviceInternal::bind_pull_geometry(const PullGeometry& g) {
    bind_vao(g.get_internal_as<GLPullGeometryInternal>().m_vao_id);
    m_bound_indices = false;
}

void GLDeviceInternal::unbind_geometry()
{
#if RENDER_TRACE_LOG
    print_buffer_info("before unbind_geometry");
#endif

    if (m_context.is_vao_available()) {
        bind_vao(0);
        bind_vertex_buffer(0);
        bind_index_buffer(0);
        m_bound_indices = false;
#if RENDER_TRACE_LOG
        SPDLOG_INFO("(unbind_geometry) Setting bound VB {}", 0);
#endif
    } else {
        bind_vertex_buffer(0);
        bind_index_buffer(0);
    }

#if RENDER_TRACE_LOG
    print_buffer_info("after bind_geometry");
#endif
}

void GLDeviceInternal::unbing_pull_geometry(const PullGeometry& g) {
    bind_vao(0);
}

void GLDeviceInternal::blit_framebuffer(const Framebuffer& src_fb, Framebuffer& dst_fb, int x, int y, int width, int height,
    BlitFramebufferMask mask, BlitFramebufferFilter filter)
{
#if SLIC3R_OPENGL_ES
    PANIC("Not implemented yet");
#else
    ResourceId src_id = src_fb.get_internal_as<GLFramebufferInternal>().m_id;

    ResourceId read_id;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, reinterpret_cast<GLint*>(&read_id));
    glCheck();

    if (read_id != src_id) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, src_id);
        glCheck();
    }

    ResourceId dst_id = dst_fb.get_internal_as<GLFramebufferInternal>().m_id;

    ResourceId draw_id;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, reinterpret_cast<GLint*>(&draw_id));
    glCheck();

    if (draw_id != dst_id) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst_id);
        glCheck();
    }

    glBlitFramebuffer(x, y, width, height, x, y, width, height, type(mask), type(filter));
    glCheck();

    if (draw_id != dst_id) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw_id);
        glCheck();
    }

    if (read_id != src_id) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, read_id);
        glCheck();
    }
#endif // SLIC3R_OPENGL_ES
}

void GLDeviceInternal::blit_to_draw_framebuffer(const Framebuffer& fb, int width, int height, BlitFramebufferMask mask,
    BlitFramebufferFilter filter)
{
#if SLIC3R_OPENGL_ES
    PANIC("Not implemented yet");
#else
    ResourceId src_id = fb.get_internal_as<GLFramebufferInternal>().m_id;

    ResourceId read_id;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, reinterpret_cast<GLint*>(&read_id));
    glCheck();

    if (read_id != src_id) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, src_id);
        glCheck();
    }

    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, type(mask), type(filter));
    glCheck();

    if (read_id != src_id) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, read_id);
        glCheck();
    }
#endif // SLIC3R_OPENGL_ES
}

void GLDeviceInternal::read_pixels(const Framebuffer& fb, int x, int y, int width, int height, Domain::PixelFormat format, void* pixels)
{
    ResourceId fb_id = fb.get_internal_as<GLFramebufferInternal>().m_id;

    ResourceId read_id;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, reinterpret_cast<GLint*>(&read_id));
    glCheck();

    if (read_id != fb_id) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fb_id);
        glCheck();
    }

    glReadPixels(x, y, width, height, texture_format(format), texture_format_type(format), pixels);
    glCheck();

    if (read_id != fb_id) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, read_id);
        glCheck();
    }
}

void GLDeviceInternal::draw(PrimitiveType primitive, size_t offset, size_t count)
{
#if RENDER_TRACE_LOG || RENDER_TRACE_DRAW
    print_buffer_info(m_bound_indices ? "draw elements" : "draw vertices");
    SPDLOG_INFO("Draw offset: {}  count: {}", offset, count);
#endif

    if (m_bound_indices) {
        glDrawElements(
            GL::type(primitive), count, type(m_bound_index_type),
            reinterpret_cast<const void*>(0 + index_type_size(m_bound_index_type) * offset)
        );
    } else {
        glDrawArrays(GL::type(primitive), offset, count);
    }
    glCheck();

}

void GLDeviceInternal::draw_instanced(PrimitiveType primitive, size_t offset, size_t count, size_t instances_count)
{
    if (m_bound_indices) {
        glDrawElementsInstanced(
            GL::type(primitive), count, type(m_bound_index_type),
            reinterpret_cast<const void*>(0 + index_type_size(m_bound_index_type) * offset), instances_count
        );
    }
    else
        glDrawArraysInstanced(GL::type(primitive), offset, count, instances_count);
    glCheck();
}

void GLDeviceInternal::print_buffer_info(const char* action)
{
    GLint bound_shader = 0;
    GLint bound_vertex_buffer = 0;
    GLint bound_index_buffer = 0;
    GLint bound_vao = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, reinterpret_cast<GLint*>(&bound_shader));
    glCheck();
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, reinterpret_cast<GLint*>(&bound_vertex_buffer));
    glCheck();
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, reinterpret_cast<GLint*>(&bound_index_buffer));
    glCheck();
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, reinterpret_cast<GLint*>(&bound_vao));
    glCheck();


    if (bound_vao) {
        GLint max_attrs;
        glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &max_attrs);
        glCheck();

        SPDLOG_INFO(
            "Bound Buffers (at {}): Bound VAO {} ({}), index buffer: {} ({})  vertex buffers:",
            action, bound_vao, m_bound_vao, bound_index_buffer, m_bound_index_buffer
        );

        for (GLint i = 0; i < max_attrs; i++) {
            GLint bound_vbi = -1;
            //glGetIntegeri_v(GL_VERTEX_ARRAY_BUFFER_BINDING, i, &bound_vbi);
            glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &bound_vbi);
            glCheck();
            if (bound_vbi != 0)
                SPDLOG_INFO("attr {}: VBO {}", i, bound_vbi);
        }
    } else {
        SPDLOG_INFO(
            "Bound buffers (at {}): current state: shader {} ({})  VB {} ({})  IB {} ({})  VAO {} ({})",
            action,
            bound_shader, m_bound_shader,
            bound_vertex_buffer, m_bound_vertex_buffer,
            bound_index_buffer, m_bound_index_buffer,
            bound_vao, m_bound_vao
        );
    }

}


} // namespace Slic3r::App::Render::GL
