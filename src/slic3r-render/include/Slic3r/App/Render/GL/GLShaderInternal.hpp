#pragma once

#include "commonGL.hpp"
#include "Slic3r/App/Render/Shader.hpp"

namespace Slic3r::App::Render::GL {

struct GLShaderInternal : public Shader::Internal
{
    GLuint m_id;
};

}
