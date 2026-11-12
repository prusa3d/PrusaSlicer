#include "Slic3r/App/Render/Framebuffer.hpp"
#include "Slic3r/App/Render/GL/GLFramebufferInternal.hpp"
#include "Slic3r/App/Render/GL/commonGL.hpp"
#include "Slic3r/App/Render/GL/GLTypes.hpp"
#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/App/Render/GL/GLDeviceInternal.hpp"
#include "Slic3r/App/Render/Texture.hpp"
#include "Slic3r/App/Render/GL/GLTextureInternal.hpp"
#include "Slic3r/App/Render/GL/GLRenderbufferInternal.hpp"
#include "Slic3r/Assert.hpp"

#include <numeric>
#include <array>
#include <algorithm>

namespace Slic3r::App::Render {

using Domain::PixelFormat;

Framebuffer::Framebuffer(Device& device, const FramebufferCreationData& data)
    : WithInternal::WithInternal(InternalType<GL::GLFramebufferInternal>(), data.target), m_device(device), m_target(data.target)
{
    DEBUG_ASSERT(data.width > 0 && data.height > 0);

    auto& self = get_internal_as<GL::GLFramebufferInternal>();
    auto& dvc = m_device.get_internal_as<GL::GLDeviceInternal>();

    self.num_samples = std::clamp(data.num_samples, uint8_t(1), uint8_t(16));

    glGenFramebuffers(1, &self.m_id);
    glCheck();
    dvc.bind_framebuffer(*this);

    self.color_attachments_count = data.color_attachments.size();
    self.depth = data.depth;
    self.stencil = data.stencil;

    size_t tex_count = self.color_attachments_count;
    if (data.depth || data.stencil)
        ++tex_count;

    DEBUG_ASSERT(tex_count > 0);

    if (self.num_samples > 1) {
        m_renderbuffers.reserve(tex_count);
        for (size_t i = 0; i < self.color_attachments_count; ++i) {
            m_renderbuffers.emplace_back(device.create_render_buffer(data.color_attachments[i].format));
        }
        if (data.depth || data.stencil)
            m_renderbuffers.emplace_back(device.create_render_buffer(PixelFormat::DepthComponent));

        for (size_t i = 0; i < self.color_attachments_count; ++i) {
            const FramebufferColorAttachment& info = data.color_attachments[i];
            Renderbuffer& rb = *m_renderbuffers[i];
            GLuint id = rb.get_internal_as<GL::GLRenderbufferInternal>().m_id;
            dvc.bind_renderbuffer(rb);
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, self.num_samples,  GL::texture_internal_format(info.format),
                data.width, data.height);
            glCheck();
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_RENDERBUFFER, id);
            glCheck();
            dvc.unbind_renderbuffer(rb);
        }
    }
    else {
        m_textures.reserve(tex_count);
        for (size_t i = 0; i < tex_count; ++i) {
            m_textures.emplace_back(device.create_texture());
        }

        for (size_t i = 0; i < data.color_attachments.size(); ++i) {
            const FramebufferColorAttachment& info = data.color_attachments[i];
            m_textures[i]->set_data(info.format, 0, data.width, data.height, nullptr, 0);
            m_textures[i]->set_filtering(info.min_filter, info.mag_filter);
            const auto& tex = m_textures[i]->get_internal_as<GL::GLTextureInternal>();
            glFramebufferTexture2D(self.m_target, GL_COLOR_ATTACHMENT0 + i, tex.m_target, tex.m_id, 0);
            glCheck();
        }
    }

    if (data.color_attachments.empty()) {
        glDrawBuffer(GL_NONE);
        glCheck();
    }
    else {
        std::vector<GLenum> buffers(data.color_attachments.size());
        std::iota(buffers.begin(), buffers.end(), GL_COLOR_ATTACHMENT0);
        glDrawBuffers(buffers.size(), buffers.data());
        glCheck();
    }

    if (data.depth && data.stencil) {
        PANIC("Not implemented yet");
    }
    else if (data.depth) {
        if (self.num_samples > 1) {
            Renderbuffer& rb = *m_renderbuffers.back();
            GLuint id = rb.get_internal_as<GL::GLRenderbufferInternal>().m_id;
            dvc.bind_renderbuffer(rb);
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, self.num_samples,  GL_DEPTH_COMPONENT24, data.width, data.height);
            glCheck();
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, id);
            glCheck();
            dvc.unbind_renderbuffer(rb);
        } 
        else {
            m_textures.back()->set_data(PixelFormat::DepthComponent, 0, data.width, data.height, nullptr, 0);
            m_textures.back()->set_filtering(TextureMinFilter::Linear, TextureMagFilter::Linear);
            std::array<float, 4> border_color = { 1.0f, 1.0f, 1.0f, 1.0f };
            m_textures.back()->set_border_color(border_color);
            m_textures.back()->set_wrap_s(TextureWrap::ClampToBorder);
            m_textures.back()->set_wrap_t(TextureWrap::ClampToBorder);
            const auto& tex = m_textures.back()->get_internal_as<GL::GLTextureInternal>();
            glFramebufferTexture2D(self.m_target, GL_DEPTH_ATTACHMENT, tex.m_target, tex.m_id, 0);
            glCheck();
        }
    }
    else if (data.stencil) {
        PANIC("Not implemented yet");
    }

    DEBUG_ASSERT(glCheckFramebufferStatus(self.m_target) == GL_FRAMEBUFFER_COMPLETE);

    dvc.unbind_framebuffer(*this);
}

Framebuffer::~Framebuffer()
{
    auto& self = get_internal_as<GL::GLFramebufferInternal>();
    glDeleteFramebuffers(1, &self.m_id);
    glCheck();
}

const TexturePtr Framebuffer::color_attachment(size_t id) const
{
    auto& self = get_internal_as<GL::GLFramebufferInternal>();
    return (id < self.color_attachments_count) ? m_textures[id] : nullptr;
}

const TexturePtr Framebuffer::depth() const
{
    auto& self = get_internal_as<GL::GLFramebufferInternal>();
    return self.depth ? m_textures[self.color_attachments_count] : nullptr;
}

const TexturePtr Framebuffer::stencil() const
{
    auto& self = get_internal_as<GL::GLFramebufferInternal>();
    return self.depth ? m_textures[self.color_attachments_count] : nullptr;
}

} // namespace Slic3r::App::Render
