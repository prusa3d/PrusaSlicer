#include "Slic3r/App/Render/CommandBuffer.hpp"

#include "Slic3r/App/Render/Geometry.hpp"

#include "Slic3r/App/Render/GL/commonGL.hpp"
#include "Slic3r/App/Render/GL/GLTypes.hpp"
#include "Slic3r/App/Render/GL/GLCommandBufferInternal.hpp"
#include "Slic3r/App/Render/GL/GLDeviceInternal.hpp"

// ReSharper disable CppMemberFunctionMayBeConst
// ReSharper disable CppMemberFunctionMayBeStatic
// NOLINT_BEGIN(*-convert-member-functions-to-static)

namespace Slic3r::App::Render {

namespace GL {


inline void setEnabled(GLenum flag, bool enabled)
{
    if (enabled)
        glEnable(flag);
    else
        glDisable(flag);
    glCheck();
}

} // namespace GL

CommandBuffer::CommandBuffer(Device& device)
    : WithInternal(InternalType<GL::GLCommandBufferInternal>()), m_device(device)
{}

CommandBuffer::~CommandBuffer() noexcept
{
    if (m_needs_submit)
        submit();
}

void CommandBuffer::set_clear_values(const RgbaF& clear_color, double clear_depth)
{
    glClearColor(clear_color.r, clear_color.g, clear_color.b, clear_color.a);
    glCheck();
    glClearDepth(clear_depth);
    glCheck();
}

void CommandBuffer::clear_buffers(bool color, bool depth)
{
    DEBUG_ASSERT(color || depth);
    GLenum buffer_mask = 0;
    if (color)
        buffer_mask |= GL_COLOR_BUFFER_BIT;
    if (depth)
        buffer_mask |= GL_DEPTH_BUFFER_BIT;
    if (buffer_mask) {
        glClear(buffer_mask);
        glCheck();
    }
}

void CommandBuffer::set_viewport(const Rect& viewport)
{
    glViewport(viewport.x, viewport.y, viewport.width, viewport.height);
    glCheck();
}

void CommandBuffer::set_scissor(const Rect& scissor)
{
    glScissor(scissor.x, scissor.y, scissor.width, scissor.height);
    glCheck();
}

void CommandBuffer::set_scissor_enabled(bool enabled)
{
    GL::setEnabled(GL_SCISSOR_TEST, enabled);
}

void CommandBuffer::set_blending(const Blending& blending)
{
    glBlendEquation(GL::type(blending.equation));
    glBlendFuncSeparate(
        GL::type(blending.rgb.src), GL::type(blending.rgb.dst),
        GL::type(blending.alpha.src), GL::type(blending.alpha.dst)
    );
    glCheck();
}

void CommandBuffer::set_blending_enabled(bool enabled)
{
    GL::setEnabled(GL_BLEND, enabled);
}

void CommandBuffer::set_depth_test_enabled(bool enabled)
{
    GL::setEnabled(GL_DEPTH_TEST, enabled);
}

void CommandBuffer::set_cull_face_enabled(bool enabled)
{
    GL::setEnabled(GL_CULL_FACE, enabled);
}

void CommandBuffer::set_stencil_test_enabled(bool enabled)
{
    GL::setEnabled(GL_STENCIL_TEST, enabled);
}

void CommandBuffer::set_depth_write_enabled(bool enabled)
{
    glDepthMask(enabled ? GL_TRUE : GL_FALSE);
    glCheck();
}

void CommandBuffer::set_cull_face_mode(CullFaceMode mode)
{
    glCullFace(GL::type(mode));
    glCheck();
}

void CommandBuffer::set_multisampling_enabled(bool enabled)
{
    GL::setEnabled(GL_MULTISAMPLE, enabled);
}

void CommandBuffer::bind_texture(uint8_t unit, const Texture& t)
{
    auto& device = m_device.get_internal_as<GL::GLDeviceInternal>();
    device.bind_texture(unit, t);
}

void CommandBuffer::unbind_texture(uint8_t unit, const Texture& t)
{
    auto& device = m_device.get_internal_as<GL::GLDeviceInternal>();
    device.unbind_texture(unit, t);
}

#ifdef SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
void CommandBuffer::bind_texture_buffer(uint8_t unit, const TextureBuffer& tb)
{
    auto& device = m_device.get_internal_as<GL::GLDeviceInternal>();
    device.bind_texture_buffer(unit, tb);
}
#endif // SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED

void CommandBuffer::bind_framebuffer(const Framebuffer& fb)
{
    auto& device = m_device.get_internal_as<GL::GLDeviceInternal>();
    device.bind_framebuffer(fb);
}

void CommandBuffer::unbind_framebuffer(const Framebuffer& fb)
{
    auto& device = m_device.get_internal_as<GL::GLDeviceInternal>();
    device.unbind_framebuffer(fb);
}

void CommandBuffer::bind_shader(const Shader& s)
{
    auto& device = m_device.get_internal_as<GL::GLDeviceInternal>();
    device.bind_shader(s);
}

void CommandBuffer::bind_geometry(const Geometry& g, const Shader& s)
{
    auto& device = m_device.get_internal_as<GL::GLDeviceInternal>();
    device.bind_geometry(g, s);
    m_bound_geometry_element_size = g.index_count();
    if (m_bound_geometry_element_size == 0)
        m_bound_geometry_element_size = g.vertex_count();
    m_needs_submit = true;
}

void CommandBuffer::bind_pull_geometry(const PullGeometry& g) {
    auto& device = m_device.get_internal_as<GL::GLDeviceInternal>();
    device.bind_pull_geometry(g);
    m_needs_submit = true;
}

void CommandBuffer::blit_framebuffer(const Framebuffer& src_fb, Framebuffer& dst_fb, int x, int y, int width, int height,
    BlitFramebufferMask mask, BlitFramebufferFilter filter)
{
    m_device.get_internal_as<GL::GLDeviceInternal>().blit_framebuffer(src_fb, dst_fb, x, y, width, height, mask, filter);
}

void CommandBuffer::blit_to_draw_framebuffer(const Framebuffer& fb, int width, int height, BlitFramebufferMask mask,
    BlitFramebufferFilter filter)
{
    m_device.get_internal_as<GL::GLDeviceInternal>().blit_to_draw_framebuffer(fb, width, height, mask, filter);
}

void CommandBuffer::read_pixels(const Framebuffer& fb, int x, int y, int width, int height, Domain::PixelFormat format, void* pixels)
{
    m_device.get_internal_as<GL::GLDeviceInternal>().read_pixels(fb, x, y, width, height, format, pixels);
}

void CommandBuffer::draw(PrimitiveType primitive, size_t offset, size_t count)
{
    auto& device = m_device.get_internal_as<GL::GLDeviceInternal>();
    device.draw(primitive, offset, count == 0 ? m_bound_geometry_element_size : count);
}

void CommandBuffer::draw_instanced(PrimitiveType primitive, size_t offset, size_t count, size_t instances_count)
{
    auto& device = m_device.get_internal_as<GL::GLDeviceInternal>();
    device.draw_instanced(primitive, offset, count == 0 ? m_bound_geometry_element_size : count, instances_count);
}

void CommandBuffer::submit()
{
    auto& device = m_device.get_internal_as<GL::GLDeviceInternal>();
    device.unbind_geometry();
}

void CommandBuffer::begin_debug_group(const std::string& message) {
    // glPushDebugGroup is OpenGL 4.3, OpenGL version @ Mac is 4.1
    if (glPushDebugGroup == nullptr)
        return;
    // The cost of this should be negligible
    static int id = 0;
    glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, id++, message.size(), message.data());
}

void CommandBuffer::end_debug_group() {
    // glPopDebugGroup is OpenGL 4.3, OpenGL version @ Mac is 4.1
    if (glPopDebugGroup == nullptr)
        return;
    glPopDebugGroup();
}

}
