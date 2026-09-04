#include "Slic3r/App/Platform/ICommand.hpp"

#include <set>
#include <fmt/format.h>
#include <fmt/ranges.h>

namespace Slic3r::App::Platform {

std::string ICommand::keyboard_shortcut_string(KeyboardShortcut::Translator translator) const
{
    if (keyboard_shortcuts()) {
        const size_t shortcut_cnt = keyboard_shortcuts().value().size();
        if (shortcut_cnt == 1) {
            return keyboard_shortcuts().value().front().to_string(translator);
        }

        std::set<std::string> unique_shortcuts;
        for (size_t id = 0; id < shortcut_cnt; id++) {
            unique_shortcuts.emplace(keyboard_shortcuts().value().at(id).to_string(translator));
        }
        return fmt::format("{}", fmt::join(unique_shortcuts, ", "));
    }
    return std::string();
}

std::vector<std::string> ICommand::keyboard_shortcut_accel_string() const
{
    std::vector<std::string> ret;
    if (keyboard_shortcuts()) {
        KeyboardShortcuts kb_shortcuts = keyboard_shortcuts().value();
        for (const KeyboardShortcut& kb_shortcut : kb_shortcuts) {
            ret.emplace_back(kb_shortcut.to_accel_table_string());
        }
    }
    return ret;
}

} // namespace Slic3r::App::Platform
