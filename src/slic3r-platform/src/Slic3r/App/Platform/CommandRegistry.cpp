#include "Slic3r/App/Platform/CommandRegistry.hpp"
#include "Slic3r/Assert.hpp"

namespace Slic3r::App::Platform {

CommandRegistry& CommandRegistry::register_command(std::unique_ptr<ICommand> command)
{
    ASSERT(command);
    const char* name = ASSERT_VAL(command->name());
    ASSERT(m_commands_by_id.count(name) == 0, "Command with same name already registered");
    m_commands_by_id[name] = std::move(command);
    return *this;
}

bool CommandRegistry::process_keyboard_event(const KeyboardEvent& e)
{
    if (e.type() != KeyboardEvent::Type::KeyDown)
        return false;

    for (const auto& cmd : std::as_const(m_commands_by_id)) {
        const auto shortcuts = cmd.second->keyboard_shortcuts();
        if (!shortcuts.has_value() || !cmd.second->enabled()) {
            continue;
        }

        const std::vector<KeyboardShortcut>& kb_shortcuts = shortcuts.value();
        for (const KeyboardShortcut& kbs : kb_shortcuts) {
            if (e.key_modifiers() == kbs.modifiers && e.code() == kbs.key) {
                cmd.second->execute();
                return true;
            }
        }
    }

    return false;
}

const ICommand& CommandRegistry::command(const char* name) const
{
    ASSERT(m_commands_by_id.contains(name), "Non-existed command");
    return *m_commands_by_id.at(name).get();
}

bool CommandRegistry::has_command(const char* name) const
{
    return m_commands_by_id.contains(name);
}

bool CommandRegistry::remove_command(const char* name)
{
    auto it = m_commands_by_id.find(name);
    if (it != m_commands_by_id.end()) {
        m_commands_by_id.erase(it);
        return true;
    }
    return false;
}

} // namespace Slic3r::App::Platform
