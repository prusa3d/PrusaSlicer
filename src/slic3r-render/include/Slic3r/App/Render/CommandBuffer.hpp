#pragma once

#include <cstddef>
#include <string>
#include "Slic3r/App/Render/Types.hpp"
#include "Slic3r/App/Render/DrawCommand.hpp"
#include "Slic3r/App/Render/WithInternal.hpp"

namespace Slic3r::App::Render {

class Geometry;
class PullGeometry;
class Shader;
class Texture;
#ifdef SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
class TextureBuffer;
#endif // SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
class Framebuffer;
class Device;

class CommandBuffer final : public WithInternal
{
public:
    explicit CommandBuffer(Device& device);
    ~CommandBuffer() noexcept override;

    void set_clear_values(const RgbaF& clear_color, double clear_depth = 1);
    void clear_buffers(bool color, bool depth);

    void set_viewport(const Rect& viewport);

    void set_scissor(const Rect& scissor);
    void set_scissor_enabled(bool enabled);

    void set_blending(const Blending& blending);
    void set_blending_enabled(bool enabled);

    void set_depth_test_enabled(bool enabled);
    void set_depth_write_enabled(bool enabled);
    void set_cull_face_enabled(bool enabled);
    void set_stencil_test_enabled(bool enabled);

    void set_cull_face_mode(CullFaceMode mode);

    void set_multisampling_enabled(bool enabled);

    void bind_shader(const Shader& s);
    void bind_geometry(const Geometry& g, const Shader& s);
    void bind_pull_geometry(const PullGeometry& g);
    void bind_texture(uint8_t unit, const Texture& t);
    void unbind_texture(uint8_t unit, const Texture& t);
#ifdef SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
    void bind_texture_buffer(uint8_t unit, const TextureBuffer& tb);
#endif // SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
    void bind_framebuffer(const Framebuffer& fb);
    void unbind_framebuffer(const Framebuffer& fb);

    void bind_material(const Material& material);
    void unbind_material(const Material& material);

    void blit_framebuffer(const Framebuffer& src_fb, Framebuffer& dst_fb, int x, int y, int width, int height,
        BlitFramebufferMask mask, BlitFramebufferFilter filter);
    void blit_to_draw_framebuffer(const Framebuffer& fb, int width, int height, BlitFramebufferMask mask, BlitFramebufferFilter filter);

    void read_pixels(const Framebuffer& fb, int x, int y, int width, int height, Domain::PixelFormat format, void* pixels);

    void draw(PrimitiveType primitive, size_t offset, size_t count);
    void draw(const DrawCommand& cmd);
    void draw(const DrawCommands::const_iterator first, const DrawCommands::const_iterator last);
    void draw(const DrawCommands& cmds) { draw(cmds.begin(), cmds.end()); }

    void draw_instanced(PrimitiveType primitive, size_t offset, size_t count, size_t instances_count);
    void draw_instanced(const DrawCommand& cmd, size_t instances_count);
    void draw_instanced(const DrawCommands::const_iterator first, const DrawCommands::const_iterator last, size_t instances_count);
    void draw_instanced(const DrawCommands& cmds, size_t instances_count) { draw(cmds.begin(), cmds.end()); }

    void bind_and_draw(const Geometry& g, const Material& material_override);
    void bind_and_draw_instanced(const Geometry& g, const Material& material_override, size_t instances_count);
    void bind_and_draw_vertex_pulled(const PullGeometry& g, const DrawCommand& command, const Material& material_override);

    void begin_debug_group(const std::string& message);
    void end_debug_group();

    void submit();
private:
    Device& m_device;
    bool m_needs_submit{false};
    size_t m_bound_geometry_element_size{0};
};

}

