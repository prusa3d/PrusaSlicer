// ReSharper disable CppMemberFunctionMayBeStatic
// NOLINTBEGIN(*-convert-member-functions-to-static)
#include "Slic3r/App/Render/Shader.hpp"
#include "Slic3r/App/Render/GL/commonGL.hpp"
#include "Slic3r/App/Render/GL/GLShaderInternal.hpp"
#include "Slic3r/Domain/Color.hpp"
#include "Slic3r/Domain/Types.hpp"

#include <Slic3r/Log.hpp>
#include <Slic3r/Assert.hpp>
#include <Slic3r/Directories.hpp>


#include <boost/nowide/fstream.hpp>
#include <GL/glew.h>


using Slic3r::Domain::ColorRGB;
using Slic3r::Domain::ColorRGBA;
using Slic3r::Domain::SquareMatrix3d;
using Slic3r::Domain::SquareMatrix3f;
using Slic3r::Domain::SquareMatrix4d;
using Slic3r::Domain::SquareMatrix4f;
using Slic3r::Domain::Transform3d;
using Slic3r::Domain::Transform3f;
using Slic3r::Domain::Vec2d;
using Slic3r::Domain::Vec2f;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::Vec3f;
using Slic3r::Domain::Vec4f;

namespace Slic3r::App::Render {

Shader::Shader(Device& device)
    : WithInternal(InternalType<GL::GLShaderInternal>()), m_device(device)
{}

Shader::~Shader()
{
    auto& self = get_internal_as<GL::GLShaderInternal>();
    if (self.m_id > 0) {
        glDeleteProgram(self.m_id);
        glCheck();
    }
}

bool Shader::init_from_files(const std::string& name, const ShaderFilenames& filenames, const std::initializer_list<std::string_view> &defines)
{
    // Load a shader program from file, prepend defs block.
    auto load_from_file = [](const std::string& filename, const std::string &defs) {
        std::string path = resources_dir() + "/shaders/" + filename;
        boost::nowide::ifstream s(path, boost::nowide::ifstream::binary);
        if (!s.good()) {
            SPDLOG_ERROR("Couldn't open file: '{}'", path);
            return std::string();
        }

        s.seekg(0, boost::nowide::ifstream::end);
        int file_length = static_cast<int>(s.tellg());
        s.seekg(0, boost::nowide::ifstream::beg);
        std::string source(defs.size() + file_length, '\0');
        memcpy(source.data(), defs.c_str(), defs.size());
        s.read(source.data() + defs.size(), file_length);
        if (!s.good()) {
            SPDLOG_ERROR("Error while loading file: {}", path);
            return std::string();
        }
        s.close();

        if (! defs.empty()) {
            // Extract the version and flip the order of "defines" and version in the source block.
            size_t idx = source.find('\n', defs.size());
            if (idx != std::string::npos && strncmp(source.c_str() + defs.size(), "#version", 8) == 0) {
                // Swap the version line with the defines.
                size_t len = idx - defs.size() + 1;
                memmove(source.data(), source.c_str() + defs.size(), len);
                memcpy(source.data() + len, defs.c_str(), defs.size());
            }
        }

        return source;
    };

    // Create a block of C "defines" from list of symbols.
    std::string defines_program;
    for (std::string_view def : defines)
        // Our shaders are stored with "\r\n", thus replicate the same here for consistency. Likely "\n" would suffice, 
        // but we don't know all the OpenGL shader compilers around.
        defines_program += std::string("#define ") + std::string(def) + "\r\n";

    ShaderSources sources = {};
    for (size_t i = 0; i < static_cast<size_t>(ShaderType::Count); ++i) {
        sources[i] = filenames[i].empty() ? std::string() : load_from_file(filenames[i], defines_program);
    }

    bool valid = !sources[static_cast<size_t>(ShaderType::Vertex)].empty() && !sources[static_cast<size_t>(ShaderType::Fragment)].empty() && sources[static_cast<size_t>(ShaderType::Compute)].empty();
    valid |= !sources[static_cast<size_t>(ShaderType::Compute)].empty() && sources[static_cast<size_t>(ShaderType::Vertex)].empty() && sources[static_cast<size_t>(ShaderType::Fragment)].empty() &&
              sources[static_cast<size_t>(ShaderType::Geometry)].empty() && sources[static_cast<size_t>(ShaderType::TessEvaluation)].empty() && sources[static_cast<size_t>(ShaderType::TessControl)].empty();

    return valid ? init_from_texts(name, sources) : false;
}

bool Shader::init_from_texts(const std::string& name, const ShaderSources& sources)
{
    auto shader_type_as_string = [](ShaderType type) {
        switch (type)
        {
        case ShaderType::Vertex:         { return "vertex"; }
        case ShaderType::Fragment:       { return "fragment"; }
        case ShaderType::Geometry:       { return "geometry"; }
        case ShaderType::TessEvaluation: { return "tesselation evaluation"; }
        case ShaderType::TessControl:    { return "tesselation control"; }
        case ShaderType::Compute:        { return "compute"; }
        default:                          { return "unknown"; }
        }
    };

    auto create_shader = [](ShaderType type) {
        GLuint id = 0;
        switch (type)
        {
        case ShaderType::Vertex:         { id = glCreateShader(GL_VERTEX_SHADER); glCheck(); break; }
        case ShaderType::Fragment:       { id = glCreateShader(GL_FRAGMENT_SHADER); glCheck(); break; }
        case ShaderType::Geometry:       { id = glCreateShader(GL_GEOMETRY_SHADER); glCheck(); break; }
        case ShaderType::TessEvaluation: { id = glCreateShader(GL_TESS_EVALUATION_SHADER); glCheck(); break; }
        case ShaderType::TessControl:    { id = glCreateShader(GL_TESS_CONTROL_SHADER); glCheck(); break; }
        case ShaderType::Compute:        { id = glCreateShader(GL_COMPUTE_SHADER); glCheck(); break; }
        default:                          { break; }
        }
           
        return (id == 0) ? std::make_pair<bool, GLuint>(false, 0) : std::make_pair(true, id);
    };

    auto release_shaders = [](const std::array<GLuint, static_cast<size_t>(ShaderType::Count)>& shader_ids) {
        for (size_t i = 0; i < static_cast<size_t>(ShaderType::Count); ++i) {
            if (shader_ids[i] > 0) {
                glDeleteShader(shader_ids[i]);
                glCheck();

            }
        }
    };

    auto& self = get_internal_as<GL::GLShaderInternal>();
    DEBUG_ASSERT(self.m_id == 0);

    m_name = name;

    std::array<GLuint, static_cast<size_t>(ShaderType::Count)> shader_ids = { 0 };

    for (size_t i = 0; i < static_cast<size_t>(ShaderType::Count); ++i) {
        const std::string& source = sources[i];
        if (!source.empty()) {
            ShaderType type = static_cast<ShaderType>(i);
            auto [result, id] = create_shader(type);
            if (result)
                shader_ids[i] = id;
            else {
                SPDLOG_ERROR("glCreateShader() failed for {} shader of shader program '{}'", shader_type_as_string(type), name);

                // release shaders
                release_shaders(shader_ids);
                return false;
            }

            const char* source_ptr = source.c_str();
            glShaderSource(id, 1, &source_ptr, nullptr);
            glCheck();
            glCompileShader(id);
            glCheck();
            GLint params;
            glGetShaderiv(id, GL_COMPILE_STATUS, &params);
            glCheck();
            if (params == GL_FALSE) {
                // Compilation failed. 
                glGetShaderiv(id, GL_INFO_LOG_LENGTH, &params);
                glCheck();
                std::vector<char> msg(params);
                glGetShaderInfoLog(id, params, &params, msg.data());
                glCheck();
                SPDLOG_ERROR(
                    "Unable to compile {} shader of shader program '{}':\n{}", shader_type_as_string(type), name, msg.data());

                // release shaders
                release_shaders(shader_ids);
                return false;
            }
        }
    }

    self.m_id = glCreateProgram();
    glCheck();
    if (self.m_id == 0) {
        SPDLOG_ERROR("glCreateProgram() failed for shader program '{}'", name);

        // release shaders
        release_shaders(shader_ids);
        return false;
    }

    for (size_t i = 0; i < static_cast<size_t>(ShaderType::Count); ++i) {
        if (shader_ids[i] > 0) {
            glAttachShader(self.m_id, shader_ids[i]);
            glCheck();
        }
    }

    glLinkProgram(self.m_id);
    glCheck();
    GLint params;
    glGetProgramiv(self.m_id, GL_LINK_STATUS, &params);
    glCheck();
    if (params == GL_FALSE) {
        // Linking failed. 
        glGetProgramiv(self.m_id, GL_INFO_LOG_LENGTH, &params);
        glCheck();
        std::vector<char> msg(params);
        glGetProgramInfoLog(self.m_id, params, &params, msg.data());
        glCheck();
        SPDLOG_ERROR("Unable to link shader program '{}':\n{}", name, msg.data());

        // release shaders
        release_shaders(shader_ids);

        // release shader program
        glDeleteProgram(self.m_id);
        glCheck();
        self.m_id = 0;

        return false;
    }

    // release shaders, they are no more needed
    release_shaders(shader_ids);

    return true;
}

void Shader::set_uniform(int id, int value) const
{
    if (id >= 0) {
        glUniform1i(id, value);
        glCheck();
    }
}

void Shader::set_uniform(int id, bool value) const
{
    set_uniform(id, value ? 1 : 0);
}

void Shader::set_uniform(int id, float value) const
{
    if (id >= 0) {
        glUniform1f(id, value);
        glCheck();
    }
}

void Shader::set_uniform(int id, double value) const
{
    set_uniform(id, static_cast<float>(value));
}

void Shader::set_uniform(int id, const std::array<int, 2>& value) const
{
    if (id >= 0) {
        glUniform2iv(id, 1, value.data());
        glCheck();
    }
}

void Shader::set_uniform(int id, const std::array<int, 3>& value) const
{
    if (id >= 0) {
        glUniform3iv(id, 1, value.data());
        glCheck();
    }
}

void Shader::set_uniform(int id, const std::array<int, 4>& value) const
{
    if (id >= 0) {
        glUniform4iv(id, 1, value.data());
        glCheck();
    }
}

void Shader::set_uniform(int id, const std::array<float, 2>& value) const
{
    if (id >= 0) {
        glUniform2fv(id, 1, value.data());
        glCheck();
    }
}

void Shader::set_uniform(int id, const std::array<float, 3>& value) const
{
    if (id >= 0) {
        glUniform3fv(id, 1, value.data());
        glCheck();
    }
}

void Shader::set_uniform(int id, const std::array<float, 4>& value) const
{
    if (id >= 0) {
        glUniform4fv(id, 1, value.data());
        glCheck();
    }
}

void Shader::set_uniform(int id, const std::array<double, 4>& value) const
{
    const std::array<float, 4> f_value = {
        static_cast<float>(value[0]),
        static_cast<float>(value[1]),
        static_cast<float>(value[2]),
        static_cast<float>(value[3])
    };
    set_uniform(id, f_value);
}

void Shader::set_uniform(int id, const float* value, size_t size) const
{
    if (id >= 0) {
        if (size == 1)
            set_uniform(id, value[0]);
        else if (size == 2) {
            glUniform2fv(id, 1, value);
            glCheck();
        }
        else if (size == 3) {
            glUniform3fv(id, 1, value);
            glCheck();
        }
        else if (size == 4) {
            glUniform4fv(id, 1, value);
            glCheck();
        }
    }
}

void Shader::set_uniform(int id, const Transform3f& value) const
{
    if (id >= 0) {
        glUniformMatrix4fv(id, 1, GL_FALSE, value.matrix().data());
        glCheck();
    }
}

void Shader::set_uniform(int id, const Transform3d& value) const
{
    set_uniform(id, value.cast<float>());
}

void Shader::set_uniform(int id, const SquareMatrix3f& value) const
{
    if (id >= 0) {
        glUniformMatrix3fv(id, 1, GL_FALSE, value.data());
        glCheck();
    }
}

void Shader::set_uniform(int id, const SquareMatrix3d& value) const
{
    set_uniform(id, static_cast<SquareMatrix3f>(value.cast<float>()));
}

void Shader::set_uniform(int id, const SquareMatrix4f& value) const
{
    if (id >= 0) {
        glUniformMatrix4fv(id, 1, GL_FALSE, value.data());
        glCheck();
    }
}

void Shader::set_uniform(int id, const SquareMatrix4d& value) const
{
    set_uniform(id, static_cast<SquareMatrix4f>(value.cast<float>()));
}

void Shader::set_uniform(int id, const Vec2f& value) const
{
    if (id >= 0) {
        glUniform2fv(id, 1, value.data());
        glCheck();
    }
}

void Shader::set_uniform(int id, const Vec2d& value) const
{
    set_uniform(id, static_cast<Vec2f>(value.cast<float>()));
}

void Shader::set_uniform(int id, const Vec3f& value) const
{
    if (id >= 0) {
        glUniform3fv(id, 1, value.data());
        glCheck();
    }
}

void Shader::set_uniform(int id, const Vec4f& value) const
{
    if (id >= 0) {
        glUniform4fv(id, 1, value.data());
        glCheck();
    }
}

void Shader::set_uniform(int id, const Vec3d& value) const
{
    set_uniform(id, static_cast<Vec3f>(value.cast<float>()));
}

void Shader::set_uniform(int id, const ColorRGB& value) const
{
    set_uniform(id, value.data(), 3);
}

void Shader::set_uniform(int id, const ColorRGBA& value) const
{
    set_uniform(id, value.data(), 4);
}

int Shader::get_attrib_location(const char* name) const
{
    auto& self = get_internal_as<GL::GLShaderInternal>();
    DEBUG_ASSERT(self.m_id > 0);

    if (self.m_id <= 0)
        // Shader program not loaded. This should not happen.
        return -1;

    auto it = std::find_if(m_attrib_location_cache.begin(), m_attrib_location_cache.end(), [name](const auto& p) { return p.first == name; });
    if (it != m_attrib_location_cache.end())
        // Attrib ID cached.
        return it->second;

    int id = glGetAttribLocation(self.m_id, name);
    this->m_attrib_location_cache.emplace_back(name, id );
    return id;
}

int Shader::get_uniform_location(const char* name) const
{
    auto& self = get_internal_as<GL::GLShaderInternal>();
    DEBUG_ASSERT(self.m_id > 0);

    if (self.m_id <= 0)
        // Shader program not loaded. This should not happen.
        return -1;

    auto it = std::find_if(m_uniform_location_cache.begin(), m_uniform_location_cache.end(), [name](const auto &p) { return p.first == name; });
    if (it != m_uniform_location_cache.end())
        // Uniform ID cached.
        return it->second;

    int id = glGetUniformLocation(self.m_id, name);
    this->m_uniform_location_cache.emplace_back(name, id );
    return id;
}


}

// NOLINTEND(*-convert-member-functions-to-static)
