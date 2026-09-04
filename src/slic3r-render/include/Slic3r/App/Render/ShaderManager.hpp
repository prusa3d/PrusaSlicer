#pragma once

#include "Shader.hpp"

#include <vector>
#include <string>
#include <memory>

namespace Slic3r::App::Render {
class Context;

class ShaderManager
{
    friend class Context;
    explicit ShaderManager(Context& context) : m_context(context) {}
public:

    std::pair<bool, std::string> init();
    // call this method before to release the OpenGL context
    void shutdown();

    // returns nullptr if not found
    Shader* shader(const std::string& shader_name);

    // returns empty string if not found
    std::string shader_name(const Shader* shader) const;

private:
    Context& m_context;
    std::vector<std::unique_ptr<Shader>> m_shaders;
};


}