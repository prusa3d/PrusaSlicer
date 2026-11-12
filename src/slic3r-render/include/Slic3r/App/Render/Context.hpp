#pragma once
#include "Slic3r/Semver.hpp"
#include <memory>

#include "ShaderManager.hpp"

namespace Slic3r::App::Render {

class ShaderManager;
class TextureManager;
#ifdef SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
class TextureBufferManager;
#endif // SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
class FramebufferManager;
class Texture;
class Device;

class Context
{
private:
    Context();
    ~Context();

public:
    static Context& instance();

    const Semver& gl_version() const { return m_opengl_version; }
    const Semver& glsl_version() const { return m_glsl_version; }

    bool is_vao_available() const
    {
        return m_vao_available;
    }

    bool is_es() const
    {
#if SLIC3R_OPENGL_ES
        return true;
#else
        return false;
#endif
    }

    uint8_t max_texture_units() const { return m_max_texture_units; }
    size_t max_texture_size() const { return m_max_texture_size; }

    bool is_core_profile() const { return m_core_profile; }
    void log_gl_info() const;

    const std::string& gl_vendor_string() const { return m_gl_vendor_string; }
    const std::string& gl_version_string() const { return m_gl_version_string; }
    const std::string& gl_core_profile_string() const { return m_gl_core_profile_string; }
    const std::string& glsl_version_string() const { return m_glsl_version_string; }
    const std::string& gl_renderer_string() const { return m_gl_renderer_string; }

    [[nodiscard]] ShaderManager& shader_manager() const { return *m_shader_manager; }
    [[nodiscard]] TextureManager& texture_manager() const { return *m_texture_manager; }
#ifdef SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
    [[nodiscard]] TextureBufferManager& texture_buffer_manager() const { return *m_texture_buffer_manager; }
#endif // SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
    [[nodiscard]] FramebufferManager& framebuffer_manager() const { return *m_framebuffer_manager; }
    [[nodiscard]] Device& device() const { return *m_device; }

    void release_resources();

private:
    Semver m_opengl_version;
    Semver m_glsl_version;
    bool m_core_profile;
    bool m_vao_available;
    uint8_t m_max_texture_units{0};
    size_t m_max_texture_size{ 0 };

    std::string m_gl_vendor_string;
    std::string m_gl_version_string;
    std::string m_gl_core_profile_string;
    std::string m_glsl_version_string;
    std::string m_gl_renderer_string;

    std::unique_ptr<Device> m_device;
    std::unique_ptr<ShaderManager> m_shader_manager;
#ifdef SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
    std::unique_ptr<TextureBufferManager> m_texture_buffer_manager;
#endif // SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
    std::unique_ptr<TextureManager> m_texture_manager;
    std::unique_ptr<FramebufferManager> m_framebuffer_manager;
};

} // namespace Slic3r::App::Render
