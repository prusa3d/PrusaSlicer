#pragma once

#include <unordered_map>
#include <memory>

#include "Slic3r/App/Platform/KeyboardEvent.hpp"
#include "Slic3r/App/Platform/ICommand.hpp"

namespace Slic3r::App::Platform {

/**
 * @brief Registry for commands user can invoke.
 */
class CommandRegistry
{
public:
    using CommandsMap = std::unordered_map<std::string, std::unique_ptr<ICommand>>;

    virtual ~CommandRegistry() = default;

    /**
     * @brief Register single command
     * @param command Pointer to command imeplementaiton
     */
    virtual CommandRegistry& register_command(std::unique_ptr<ICommand> command);

    /**
     * @brief Process keyboard event and eventually execute related command.
     * @param e Keyboard generated event to process.
     * @return `true` if the event was consumed (command invoked), otherwise `false`.
     */
    bool process_keyboard_event(const KeyboardEvent& e);

    const ICommand& command(const char* name) const;
    bool has_command(const char* name) const;
    const CommandsMap& commands() const { return m_commands_by_id; }

    bool remove_command(const char* name);

private:
    CommandsMap m_commands_by_id;
};

} // Slic3r::App::Platform
