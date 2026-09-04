#include "Slic3r/App/Render/TextureBufferManager.hpp"
#include "Slic3r/App/Render/Device.hpp"

#include "Slic3r/Assert.hpp"

#ifdef SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED

namespace Slic3r::App::Render {

TextureBuffer* TextureBufferManager::get_or_create_empty(const std::string& name, Domain::PixelFormat format)
{
    BufferMap::const_iterator it = m_buffers.find(name);
    if (it != m_buffers.end())
        return it->second.get();

    auto buf = m_device.create_texture_buffer(format);
    m_buffers[name] = std::move(buf);
    return m_buffers[name].get();
}

} // Slic3r::App::Render

#endif // SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED

