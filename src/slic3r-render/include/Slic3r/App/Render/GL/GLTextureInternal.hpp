#pragma once

#include "commonGL.hpp"
#include "Slic3r/App/Render/Texture.hpp"

namespace Slic3r::App::Render::GL {

struct GLTextureInternal : public Texture::Internal
{
    GLuint m_id{0};
    GLenum m_target{GL_TEXTURE_2D};
    static constexpr uint8_t UNBOUND = 255;
    uint8_t m_bound_unit{UNBOUND};
};

}