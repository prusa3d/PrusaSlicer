#include "Slic3r/App/Platform/CommandExecutionNotifier.hpp"

namespace Slic3r::App::Platform {

CommandExecutionNotifier m_cen_instance;

CommandExecutionNotifier& CommandExecutionNotifier ::instance()
{
    return m_cen_instance;
}

void CommandExecutionNotifier ::notify_command_executed()
{
    invoke_listeners<ICommandExecutedListener>([](auto* listener)
                                               { listener->on_command_executed(); });
}

} // namespace Slic3r::App::Platform
