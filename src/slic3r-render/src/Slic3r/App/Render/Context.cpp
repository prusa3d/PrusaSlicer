#include "Slic3r/App/Render/Context.hpp"

#include "Slic3r/App/Render/GL/commonGL.hpp"
#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/App/Render/ShaderManager.hpp"
#include "Slic3r/App/Render/TextureManager.hpp"
#ifdef SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
#include "Slic3r/App/Render/TextureBufferManager.hpp"
#endif // SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
#include "Slic3r/App/Render/FramebufferManager.hpp"

#include <Slic3r/Log.hpp>

#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>

#include <sstream>

namespace Slic3r::App::Render {

const char* getGlString(GLenum name)
{
    const char* val = reinterpret_cast<const char*>(glGetString(name));
    glCheck();
    return val;
}


std::string getGlString(GLenum name, const std::string& defaultValue)
{
    const char* val = reinterpret_cast<const char*>(glGetString(name));
    glCheck();
    return val == nullptr ? defaultValue : val;
}

static const std::string VERSION_NA = "n/a";

Semver parse_version(const std::string& s)
{
    if (s == VERSION_NA)
        return Semver::invalid();
    std::vector<std::string> tokens;
    boost::split(tokens, s, boost::is_any_of(" "), boost::token_compress_on);

    if (tokens.empty())
        return Semver::invalid();

#if SLIC3R_OPENGL_ES
    const std::string version_container = (tokens.size() > 1 && boost::istarts_with(tokens[1], "ES")
                                          ) ?
        tokens[2] :
        tokens[0];
#endif // SLIC3R_OPENGL_ES

    std::vector<std::string> numbers;
#if SLIC3R_OPENGL_ES
    boost::split(numbers, version_container, boost::is_any_of("."), boost::token_compress_on);
#else
    boost::split(numbers, tokens[0], boost::is_any_of("."), boost::token_compress_on);
#endif // SLIC3R_OPENGL_ES

    unsigned int gl_major = 0;
    unsigned int gl_minor = 0;

    if (numbers.size() > 0)
        gl_major = ::atoi(numbers[0].c_str());

    if (numbers.size() > 1)
        gl_minor = ::atoi(numbers[1].c_str());

    return Semver(gl_major, gl_minor, 0);
}

Context::Context()
{
    m_gl_vendor_string = getGlString(GL_VENDOR);
    m_gl_version_string = getGlString(GL_VERSION, VERSION_NA);
    m_opengl_version = parse_version(m_gl_version_string);
    m_core_profile = !GLEW_ARB_compatibility;
    m_gl_core_profile_string = m_core_profile ? "Yes" : "No";
    m_glsl_version_string = getGlString(GL_SHADING_LANGUAGE_VERSION, VERSION_NA);
    m_glsl_version = parse_version(m_glsl_version_string);
    m_gl_renderer_string = getGlString(GL_RENDERER);

    GLint max_texture_units = 0;
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &max_texture_units);
    glCheck();
    m_max_texture_units = max_texture_units;

    GLint max_texture_size = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
    glCheck();
    m_max_texture_size = max_texture_size;

#ifdef EMSCRIPTEN
    m_vao_available = GLEW_OES_vertex_array_object;
#else
    m_vao_available = true;
#endif // EMSCRIPTEN
    m_device.reset(new Device(*this));
    m_shader_manager.reset(new ShaderManager(*this));
    m_texture_manager.reset(new TextureManager(*m_device));
#ifdef SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
    m_texture_buffer_manager.reset(new TextureBufferManager(*m_device));
#endif // SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
    m_framebuffer_manager.reset(new FramebufferManager(*m_device));
}

Context::~Context()
{
    SPDLOG_DEBUG("Releasing context");
}

void Context::log_gl_info() const
{
    SPDLOG_INFO("OpenGL Vendor: {}", m_gl_vendor_string);
    SPDLOG_INFO("OpenGL Version: {}", m_gl_version_string);
    SPDLOG_INFO("Core profile: {}", m_gl_core_profile_string);
    SPDLOG_INFO("GLSL Version: {}", m_glsl_version_string);
    SPDLOG_INFO("OpenGL Renderer: {}", m_gl_renderer_string);
#ifdef EMSCRIPTEN
    SPDLOG_INFO("OpenGL Extensions: {}", getGlString(GL_EXTENSIONS));
#else
    if (is_core_profile()) {
        int n;
        glGetIntegerv(GL_NUM_EXTENSIONS, &n);

        std::ostringstream oss;
        for (int i = 0; i < n; i++) {
            if (i > 0)
                oss << " ";
            oss << reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, i));
        }
        SPDLOG_INFO("OpenGL Extensions: {}", oss.str());
    } else
        SPDLOG_INFO("OpenGL Extensions: {}", getGlString(GL_EXTENSIONS));
#endif // EMSCRIPTEN
}

void Context::release_resources()
{
    if (m_shader_manager) m_shader_manager->shutdown();
    if (m_texture_manager) m_texture_manager->shutdown();
#ifdef SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
    if (m_texture_buffer_manager) m_texture_buffer_manager->shutdown();
#endif // SLIC3R_RENDER_TEXTURE_BUFFER_SUPPORTED
    if (m_framebuffer_manager) m_framebuffer_manager->shutdown();
}

Context& Context::instance()
{
    static Context inst;
    return inst;
}

} // namespace Slic3r::App::Render
