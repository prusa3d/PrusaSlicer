#include "Slic3r/App/Render/Renderbuffer.hpp"
#include "Slic3r/App/Render/GL/commonGL.hpp"
#include "Slic3r/App/Render/GL/GLRenderbufferInternal.hpp"

namespace Slic3r::App::Render {

Renderbuffer::Renderbuffer(Domain::PixelFormat format) :
    WithInternal(InternalType<GL::GLRenderbufferInternal>()),
    m_format(format)
{
    auto& self = get_internal_as<GL::GLRenderbufferInternal>();
    glGenRenderbuffers(1, &self.m_id);
    glCheck();
}

Renderbuffer::~Renderbuffer()
{
    auto& self = get_internal_as<GL::GLRenderbufferInternal>();
    if (self.m_id) {
        glDeleteRenderbuffers(1, &self.m_id);
        glCheck();
    }
}

} // namespace Slic3r::App::Render
