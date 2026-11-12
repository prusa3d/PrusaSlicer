#pragma once

#include <string>
#include <unordered_map>

#include "Framebuffer.hpp"

namespace Slic3r::App::Render {

class Context;
class Device;

class FramebufferManager
{
public:
    Framebuffer* create(const FramebufferCreationData& data);
    void destroy(Framebuffer* buffer);

    void shutdown() { m_buffers.clear(); }

private:
    friend class Context;
    explicit FramebufferManager(Device& device) : m_device(device) {}

private:
    using Buffers = std::vector<std::unique_ptr<Framebuffer>>;

    Device& m_device;
    Buffers m_buffers;
};

} // namespace Slic3r::App::Render