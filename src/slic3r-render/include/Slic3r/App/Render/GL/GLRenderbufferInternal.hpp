#pragma once

#include "Slic3r/App/Render/Renderbuffer.hpp"

namespace Slic3r::App::Render::GL {

struct GLRenderbufferInternal : public Renderbuffer::Internal
{
    GLuint m_id{0};
};

} // namespace Slic3r::App::Render::GL
