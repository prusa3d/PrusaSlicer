#pragma once

#include "commonGL.hpp"
#include "GLTypes.hpp"
#include "Slic3r/App/Render/Framebuffer.hpp"

namespace Slic3r::App::Render::GL {

struct GLFramebufferInternal : public Framebuffer::Internal
{
    GLenum m_target{GL_FRAMEBUFFER};
    GLuint m_id{0};
    size_t color_attachments_count{ 0 };
    bool depth{ false };
    bool stencil{ false };
    uint8_t num_samples{ 1 };

    explicit GLFramebufferInternal(FramebufferTarget target) : m_target(type(target)) {}
};

} // Slic3r::App::Render::GL