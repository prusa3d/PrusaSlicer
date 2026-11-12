#pragma once

#include <vector>
#include <cstddef>

#include "Slic3r/Domain/PixelFormat.hpp"
#include "WithInternal.hpp"
#include "Types.hpp"

namespace Slic3r::App::Render {

class Context;
class Texture;
class Buffer;
class VertexBuffer;
class IndexBuffer;
#ifdef SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
class TextureBuffer;
#endif // SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
class Renderbuffer;
class Shader;
class CommandBuffer;
class Framebuffer;
struct FramebufferCreationData;

class Device : public WithInternal
{
    friend class Context;
    explicit Device(Context& context);

    Device(Device&&) = default;
public:
    Context& context() { return m_context; }
    const Context& context() const { return m_context; }

    void load_state();

    std::unique_ptr<Texture> create_texture();
    std::unique_ptr<VertexBuffer> create_vertex_buffer();
    std::unique_ptr<IndexBuffer> create_index_buffer();
#ifdef SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
    std::unique_ptr<TextureBuffer> create_texture_buffer(Domain::PixelFormat format);
#endif // SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
    std::unique_ptr<Renderbuffer> create_render_buffer(Domain::PixelFormat format);
    std::unique_ptr<CommandBuffer> create_command_buffer();
    std::unique_ptr<Framebuffer> create_framebuffer(const FramebufferCreationData& data);

    void bind_buffer(const Buffer& b);
    void unbind_buffer(const Buffer& b);

    void* map_buffer(const Buffer& b, BufferAccess access);
    void unmap_buffer(const Buffer& b);

    void bind_framebuffer(const Framebuffer& b);
    void unbind_framebuffer(const Framebuffer& b);

    void bind_renderbuffer(const Renderbuffer& b);
    void unbind_renderbuffer(const Renderbuffer& b);

private:
    Context& m_context;
};

}
