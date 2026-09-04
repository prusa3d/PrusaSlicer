#pragma once

#include "WithInternal.hpp"
#include "Types.hpp"
#include "Renderbuffer.hpp"

#include <vector>
#include <cstdint>

namespace Slic3r::App::Render {

class Device;

struct FramebufferColorAttachment
{
    Domain::PixelFormat format{ Domain::PixelFormat::RGBA8 };
    TextureMinFilter min_filter{ TextureMinFilter::Nearest };
    TextureMagFilter mag_filter{ TextureMagFilter::Nearest };
};

using FramebufferColorAttachments = std::vector<FramebufferColorAttachment>;

struct FramebufferCreationData
{
    FramebufferTarget target{ FramebufferTarget::Framebuffer };
    size_t width{ 0 };
    size_t height{ 0 };
    FramebufferColorAttachments color_attachments;
    bool depth{ true };
    bool stencil{ false };
    uint8_t num_samples{ 1 };
};

class Framebuffer : public WithInternal
{
public:
    Framebuffer(Device& device, const FramebufferCreationData& data);
    ~Framebuffer() override;

    FramebufferTarget target() const { return m_target; }
    const TexturePtr color_attachment(size_t id) const;
    const TexturePtr depth() const;
    const TexturePtr stencil() const;

private:
    Device& m_device;
    FramebufferTarget m_target;
    TexturePtrs m_textures;
    RenderbufferPtrs m_renderbuffers;
};

} // namespace Slic3r::App::Render
