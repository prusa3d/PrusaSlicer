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

void CommandBuffer::bind_material(const Material& material)
{
    bind_shader(*material.shader());

    for (const auto& [slot, texture] : material.textures())
        bind_texture(slot, *texture);

#ifdef SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
    for (const auto [slot, texture_buffer] : material.texture_buffers())
        bind_texture_buffer(slot, *texture_buffer);
#endif // SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED

    for (const auto& [name, value] : material.uniforms())
        set_uniform(*material.shader(), name.c_str(), value);
}

void CommandBuffer::unbind_material(const Material& material)
{
    for (const auto& [slot, texture] : material.textures())
        unbind_texture(slot, *texture);
}


void CommandBuffer::draw(const DrawCommand& cmd)
{
    draw(cmd.primitive, cmd.offset, cmd.count);
}

void CommandBuffer::draw(const DrawCommands::const_iterator first, const DrawCommands::const_iterator last)
{
    std::for_each(first, last, [this](const auto& cmd) { draw(cmd); });
}

void CommandBuffer::bind_and_draw(const Geometry& g, const Material& material_override)
{
    const auto& cmds = g.draw_commands();
    if (!DEBUG_ASSERT_VAL(!cmds.empty()))
        return;

    const auto* top_shader = material_override.shader();
    bool top_shader_bound = false;

    for (const auto& cmd : cmds) {
        Material material = cmd.material;
        material.update(material_override);
        // if command overrides shader
        if (const auto* shader = cmd.material.shader()) {
            bind_shader(*shader);
            bind_geometry(g, *shader);
            top_shader_bound = false;
        } else if (!top_shader_bound) {
            bind_shader(*DEBUG_ASSERT_VAL(top_shader));
            bind_geometry(g, *top_shader);
            top_shader_bound = true;
        }
        bind_material(material);
        draw(cmd);
        unbind_material(material);
    }
}

void CommandBuffer::draw_instanced(const DrawCommand& cmd, size_t instances_count)
{
    draw_instanced(cmd.primitive, cmd.offset, cmd.count, instances_count);
}

void CommandBuffer::draw_instanced(const DrawCommands::const_iterator first, const DrawCommands::const_iterator last, size_t instances_count)
{
    std::for_each(first, last, [this, instances_count](const auto& cmd) { draw_instanced(cmd, instances_count); });
}

void CommandBuffer::bind_and_draw_instanced(const Geometry& g, const Material& material_override, size_t instances_count)
{
    const auto& cmds = g.draw_commands();
    if (!DEBUG_ASSERT_VAL(!cmds.empty()))
        return;

    const auto* top_shader = material_override.shader();
    bool top_shader_bound = false;

    for (const auto& cmd : cmds) {
        Material material = cmd.material;
        material.update(material_override);
        // if command overrides shader
        if (const auto* shader = cmd.material.shader()) {
            bind_shader(*shader);
            bind_geometry(g, *shader);
            top_shader_bound = false;
        } else if (!top_shader_bound) {
            bind_shader(*DEBUG_ASSERT_VAL(top_shader));
            bind_geometry(g, *top_shader);
            top_shader_bound = true;
        }
        bind_material(material);
        draw_instanced(cmd, instances_count);
        unbind_material(material);
    }
}

void CommandBuffer::bind_and_draw_vertex_pulled(
    const PullGeometry& g,
    const DrawCommand& command,
    const Material& material_override
)
{
    const Shader* top_shader{material_override.shader()};
    bind_pull_geometry(g);

    Material material = command.material;
    material.update(material_override);
    if (const auto* shader{command.material.shader()}) {
        bind_shader(*shader);
    } else {
        bind_shader(*DEBUG_ASSERT_VAL(top_shader));
    }
    bind_material(material);
    draw(command);
    unbind_material(material);
}
} // namespace Slic3r::App::Render
