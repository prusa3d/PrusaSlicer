#pragma once

#include "Slic3r/App/Render/CommandBuffer.hpp"

#include <string>

namespace Slic3r::App::Render {

/**
 * @brief The Event class is RAII object used for logging
 * render events in debug enviroment.
 */
class ScopedDebugGroup
{
public:
    ScopedDebugGroup(const std::string& message, CommandBuffer& commandBuffer);
    ~ScopedDebugGroup();
    ScopedDebugGroup() = delete;
    ScopedDebugGroup(const ScopedDebugGroup& event) = delete;
    ScopedDebugGroup& operator=(const ScopedDebugGroup& event) = delete;

private:
    CommandBuffer& m_commandBuffer;
};

}
