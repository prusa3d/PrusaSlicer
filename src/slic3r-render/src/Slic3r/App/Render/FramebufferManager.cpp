#include "Slic3r/App/Render/FramebufferManager.hpp"
#include "Slic3r/App/Render/Device.hpp"

#include <algorithm>

namespace Slic3r::App::Render {

Framebuffer* FramebufferManager::create(const FramebufferCreationData& data)
{
    auto buf = m_device.create_framebuffer(data);
    m_buffers.push_back(std::move(buf));
    return m_buffers.back().get();
}

void FramebufferManager::destroy(Framebuffer* buffer)
{
    auto it = std::find_if(m_buffers.begin(), m_buffers.end(),
        [buffer](auto& item) {
        return item.get() == buffer;
    });
    if (it != m_buffers.end())
        m_buffers.erase(it);
}

} // namespace Slic3r::App::Render
