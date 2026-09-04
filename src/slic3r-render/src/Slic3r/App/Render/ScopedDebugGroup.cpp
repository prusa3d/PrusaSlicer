#include "Slic3r/App/Render/ScopedDebugGroup.hpp"

namespace Slic3r::App::Render {
ScopedDebugGroup::ScopedDebugGroup(const std::string& message, CommandBuffer& commandBuffer) : m_commandBuffer(commandBuffer) {
    m_commandBuffer.begin_debug_group(message);
}

ScopedDebugGroup::~ScopedDebugGroup()
{
    m_commandBuffer.end_debug_group();
}

}
