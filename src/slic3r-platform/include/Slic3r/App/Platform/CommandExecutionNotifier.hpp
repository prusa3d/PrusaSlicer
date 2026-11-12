#pragma once

#include "Slic3r/Biz/Platform/WithListeners.hpp"

namespace Slic3r::App::Platform {

struct ICommandExecutedListener
{
    virtual ~ICommandExecutedListener() = default;
    virtual void on_command_executed()  = 0;
};

/// Process-wide notifier of executed commands. Singleton.
struct CommandExecutionNotifier final : public WithListeners<ICommandExecutedListener>
{
    static CommandExecutionNotifier& instance();
    void notify_command_executed();
};

} // namespace Slic3r::App::Platform
