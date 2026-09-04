#pragma once

#include <string>
#include <unordered_map>

#include "Slic3r/App/Render/Shader.hpp"
#include "Slic3r/App/Render/UniformValue.hpp"
#include "Slic3r/App/Render/Texture.hpp"
#ifdef SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
#include "Slic3r/App/Render/Buffer.hpp"
#include <Slic3r/Assert.hpp>
#endif // SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED

namespace Slic3r::App::Render {

using MaterialTextures = std::unordered_map<size_t, TexturePtr>;
#ifdef SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
using MaterialTextureBuffers = std::unordered_map<size_t, const TextureBuffer*>;
#endif // SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
using MaterialUniforms = std::unordered_map<std::string, UniformValue>;

class Material
{
public:
    Material() = default;
    Material(const Material&) = default;
    Material(Material&&) = default;

    Material& operator=(const Material&) = default;
    Material& operator=(Material&&) = default;

    explicit Material(const Shader* shader) : m_shader(shader) {}

    const Shader* shader() const { return m_shader; }
    Material& set_shader(const Shader* shader) { m_shader = shader; return *this; }

    const MaterialTextures& textures() const { return m_textures; }

#ifdef SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
    const MaterialTextureBuffers& texture_buffers() const { return m_texture_buffers; }
#endif // SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED

    const MaterialUniforms& uniforms() const { return m_uniforms; }
    MaterialUniforms& uniforms() { return m_uniforms; }

    Material& set_uniform(const std::string& name, const UniformValue& value)
    {
        m_uniforms[name] = value;
        return *this;
    }

    Material& set_texture(size_t slot, TexturePtr texture)
    {
#ifdef SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
        DEBUG_ASSERT(m_texture_buffers.find(slot) == m_texture_buffers.end());
#endif // SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
        m_textures[slot] = texture;
        return *this;
    }

#ifdef SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
    Material& set_texture_buffer(size_t slot, const TextureBuffer* texture_buffer)
    {
        DEBUG_ASSERT(m_textures.find(slot) == m_textures.end());
        m_texture_buffers[slot] = texture_buffer;
        return *this;
    }
#endif // SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED

    bool transparent() const { return m_transparent.has_value() ? *m_transparent : false; }
    Material& set_transparent(bool transparent)
    {
        m_transparent = transparent;
        return *this;
    }

    void update(const Material& override);
private:
    const Shader* m_shader{nullptr};
    MaterialUniforms m_uniforms;
    MaterialTextures m_textures;
#ifdef SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
    MaterialTextureBuffers m_texture_buffers;
#endif // SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
    std::optional<bool> m_transparent{};
};

}

