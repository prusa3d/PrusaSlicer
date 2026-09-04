#pragma once

#include "Slic3r/App/Platform/KeyCode.hpp"
#include "Slic3r/App/Platform/KeyModifers.hpp"

#include <string>
#include <functional>

namespace Slic3r::App::Platform {

struct KeyboardShortcut
{
    KeyModifiers modifiers;
    KeyCode key;

    using Translator = std::function<std::string(const std::string&)>;

    // Produces a human-readable shortcut string (e.g. for menus, tooltips).
    // Translation is delegated to the provided translator.
    std::string to_string(Translator translator) const;

    // Produces a platform-specific shortcut string suitable for accelerator tables.
    std::string to_accel_table_string() const;
};

} // namespace Slic3r::App::Platform
