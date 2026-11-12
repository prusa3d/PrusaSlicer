#pragma once

#include <string>
#include <unordered_map>

#include "Buffer.hpp"

#ifdef SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED

namespace Slic3r::App::Render {

class Context;
class Device;

class TextureBufferManager
{
public:
    TextureBuffer* get_or_create_empty(const std::string& name, Domain::PixelFormat format);

    void shutdown() { m_buffers.clear(); }

private:
    explicit TextureBufferManager(Device& device) : m_device(device) {}

private:
    using BufferMap = std::unordered_map<std::string, std::unique_ptr<TextureBuffer>>; // std::unique_ptr<TextureBuffer> ?

    Device& m_device;
    BufferMap m_buffers;

    friend class Context;
};

} // Slic3r::App::Render

#endif // SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
