#pragma once

#include <memory>

#include <Slic3r/Log.hpp>

#include "Types.hpp"
#include "Shader.hpp"
#include "Context.hpp"
#include "WithInternal.hpp"
#include "Slic3r/Domain/PixelFormat.hpp"

namespace Slic3r::App::Render {

class Device;

class Buffer : public WithInternal {
public:
    Buffer(Device& device, BufferTarget target);
    ~Buffer() override;

    void set_data(const void* data, size_t size, BufferUsage usage);

    BufferTarget target() const { return m_target; }

protected:
    Device& m_device;
private:
    BufferTarget m_target;
};

class VertexBuffer : public Buffer
{
public:
    explicit VertexBuffer(Device& device) : Buffer(device, BufferTarget::VertexBuffer) {}
};

class IndexBuffer : public Buffer
{
public:
    explicit IndexBuffer(Device& device) : Buffer(device, BufferTarget::IndexBuffer) {}
};

#ifdef SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
class TextureBuffer : public Buffer
{
public:
    TextureBuffer(Device& device, Domain::PixelFormat format);
    ~TextureBuffer() override;

    Domain::PixelFormat format() const { return m_format; }

private:
    Domain::PixelFormat m_format;
};
#endif // SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED

} // namespace Slic3r::App::Render
