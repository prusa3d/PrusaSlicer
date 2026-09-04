#include "Slic3r/App/Platform/KeyboardShortcut.hpp"

namespace Slic3r::App::Platform {

std::string KeyboardShortcut::to_string(Translator translator) const
{
    std::string shortcut;
    if (std::string str_modifiers = Platform::to_string(modifiers); !str_modifiers.empty()) {
        shortcut += str_modifiers;
    }
    shortcut += translator(Platform::to_string(key));

    return shortcut;
}

std::string KeyboardShortcut::to_accel_table_string() const
{
    std::string shortcut;
    if (std::string str_modifiers = Platform::to_accel_table_string(modifiers); !str_modifiers.empty()) {
        shortcut += str_modifiers;
    }
    shortcut += Platform::to_accel_table_string(key);

    return shortcut;
}

} // namespace Slic3r::App::Platform
