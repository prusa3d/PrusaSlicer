#pragma once

#include <vector>
#include <cstdint>
#include <stack>
#include <cstdint>

#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/App/Render/Types.hpp"

namespace Slic3r::App::Render {
class PullGeometry;
class Geometry;
class Shader;
class Context;
}

namespace Slic3r::App::Render::GL {

using ResourceId = unsigned int;
using ResourceIds = std::vector<ResourceId>;

class GLDeviceInternal : public Device::Internal
{
public:
    explicit GLDeviceInternal(Context& context);

    void load_state();

    void bind_shader(const Shader& s);
    void bind_buffer(BufferTarget target, ResourceId buffer);
    void unbind_buffer(BufferTarget target);
    void bind_geometry(const Geometry& g, const Shader& s);
    void bind_pull_geometry(const PullGeometry& g);
    void unbind_geometry();
    void unbing_pull_geometry(const PullGeometry& g);
    void bind_texture(uint8_t unit, const Texture& t);
    void unbind_texture(uint8_t unit, const Texture& t);
#ifdef SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
    void bind_texture_buffer(uint8_t unit, const TextureBuffer& tb);
#endif // SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
    void bind_renderbuffer(const Renderbuffer& b);
    void unbind_renderbuffer(const Renderbuffer& b);

    void* map_buffer(BufferTarget target, BufferAccess access);
    void unmap_buffer(BufferTarget target);

    void bind_framebuffer(const Framebuffer& buffer);
    void unbind_framebuffer(const Framebuffer& buffer);

    void blit_framebuffer(const Framebuffer& src_fb, Framebuffer& dst_fb, int x, int y, int width, int height,
        BlitFramebufferMask mask, BlitFramebufferFilter filter);
    void blit_to_draw_framebuffer(const Framebuffer& fb, int width, int height, BlitFramebufferMask mask, BlitFramebufferFilter filter);

    void read_pixels(const Framebuffer& fb, int x, int y, int width, int height, Domain::PixelFormat format, void* pixels);

    void draw(PrimitiveType primitive, size_t offset, size_t count);
    void draw_instanced(PrimitiveType primitive, size_t offset, size_t count, size_t instances_count);

    void print_buffer_info(const char* action = nullptr);
private:
    friend class ::Slic3r::App::Render::Geometry;
    friend class ::Slic3r::App::Render::PullGeometry;
    void activate_texture_unit(uint8_t unit);
    void bind_vertex_buffer(ResourceId vb);
    void bind_index_buffer(ResourceId ib);
#ifdef SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
    void bind_texture_buffer(ResourceId tb);
#endif // SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
    void bind_vao(ResourceId vao);

private:
    Context& m_context;
    // only active bound VB (not taking bound VAO into account)
    ResourceId m_bound_vertex_buffer{0};
    // only active bound IB (not taking bound VAO into account)
    ResourceId m_bound_index_buffer{0};
#ifdef SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
    // active bound TB
    ResourceId m_bound_texture_buffer{ 0 };
#endif // SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
    ResourceId m_bound_renderbuffer{ 0 };
    IndexType m_bound_index_type{IndexType::UByte};
    ResourceId m_bound_vao{0};
    ResourceId m_bound_shader{0};
    ResourceIds m_bound_textures;
    uint8_t m_active_texture_unit{0};
    // Is index buffer enabled either via bound IB or VAO
    bool m_bound_indices{false};

    std::stack<ResourceId> m_draw_framebuffer_ids;
};

} // namespace Slic3r::App::Render::GL
