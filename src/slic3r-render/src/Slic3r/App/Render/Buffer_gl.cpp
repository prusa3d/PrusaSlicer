#include "Slic3r/App/Render/Buffer.hpp"
#include "Slic3r/App/Render/Device.hpp"
#ifdef SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
#include "Slic3r/App/Render/TextureManager.hpp"
#endif // SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
#include "Slic3r/App/Render/GL/commonGL.hpp"
#include "Slic3r/App/Render/GL/GLBufferInternal.hpp"
#include "Slic3r/App/Render/GL/GLDeviceInternal.hpp"

namespace Slic3r::App::Render {

Buffer::Buffer(Device& device, BufferTarget target)
    : WithInternal(InternalType<GL::GLBufferInternal>(), target), m_device(device), m_target(target)
{
    auto& self = get_internal_as<GL::GLBufferInternal>();
    glGenBuffers(1, &self.m_id);
    glCheck();
}

Buffer::~Buffer()
{
    auto& self = get_internal_as<GL::GLBufferInternal>();
    if (self.m_id)
        glDeleteBuffers(1, &self.m_id);
}

void Buffer::set_data(const void* data, size_t size, BufferUsage usage)
{
    auto& self = get_internal_as<GL::GLBufferInternal>();
    auto& device = m_device.get_internal_as<GL::GLDeviceInternal>();
    device.bind_buffer(m_target, self.m_id);
    //device.print_buffer_info("Buffer::set_data()");
    glBufferData(self.m_target, size, data, GL::type(usage));
    glCheck();
}

#ifdef SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
TextureBuffer::TextureBuffer(Device& device, Domain::PixelFormat format)
    : Buffer(device, BufferTarget::TextureBuffer)
    , m_format(format)
{
    auto& self = get_internal_as<GL::GLBufferInternal>();
    auto& dvc = m_device.get_internal_as<GL::GLDeviceInternal>();
    dvc.bind_buffer(target(), self.m_id);
    glGenTextures(1, &self.m_tex_id);
    glCheck();
}

TextureBuffer::~TextureBuffer()
{
    auto& self = get_internal_as<GL::GLBufferInternal>();
    if (self.m_tex_id) {
        glDeleteTextures(1, &self.m_tex_id);
        glCheck();
    }
}
#endif // SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED

} // namespace Slic3r::App::Render
