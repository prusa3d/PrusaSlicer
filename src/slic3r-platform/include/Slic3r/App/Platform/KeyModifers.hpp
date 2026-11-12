#pragma once

#include <cstdint>
#include <string>
#include <type_traits>

namespace Slic3r::App::Platform {

enum class KeyModifier : uint8_t
{
    None = 0,
    Alt = 1 << 0,
    Shift = 1 << 1,
    Meta = 1 << 2,
    Ctrl = 1 << 3,
};

typedef std::underlying_type_t<KeyModifier> KeyModifiers;

inline std::string to_string(KeyModifier modifier)
{
    switch (modifier) {
    case KeyModifier::Alt:
#ifdef __APPLE__
        return "⌥";
#else
        return "Alt+";
#endif
    case KeyModifier::Shift:
#ifdef __APPLE__
        return "⇧";
#else
        return "Shift+";
#endif
    case KeyModifier::Ctrl:
#ifdef __APPLE__
        return "⌘";
#else
        return "Ctrl+";
#endif
    default:
        return std::string();
    }
}

inline std::string to_accel_table_string(KeyModifier modifier)
{
    switch (modifier) {
    case KeyModifier::Alt:
        return "Alt+";
    case KeyModifier::Shift:
        return "Shift+";
    case KeyModifier::Ctrl:
        return "Ctrl+";
    default:
        return std::string();
    }
}

inline bool shift_down(KeyModifiers modifiers)
{
    return (modifiers & KeyModifiers(KeyModifier::Shift)) != 0;
}
inline bool ctrl_down(KeyModifiers modifiers) {
    return (modifiers & KeyModifiers(KeyModifier::Ctrl)) != 0;
}
inline    bool alt_down(KeyModifiers modifiers) {
    return (modifiers & KeyModifiers(KeyModifier::Alt)) != 0;
}

inline std::string to_string(KeyModifiers modifiers)
{
    std::string ret;
    if (shift_down(modifiers)) {
        ret += to_string(KeyModifier::Shift);
    }
    if (ctrl_down(modifiers)) {
        ret += to_string(KeyModifier::Ctrl);
    }
    if (alt_down(modifiers)) {
        ret += to_string(KeyModifier::Alt);
    }
    return ret;
}

inline std::string to_accel_table_string(KeyModifiers modifiers)
{
    std::string ret;
    if (shift_down(modifiers)) {
        ret += to_accel_table_string(KeyModifier::Shift);
    }
    if (ctrl_down(modifiers)) {
        ret += to_accel_table_string(KeyModifier::Ctrl);
    }
    if (alt_down(modifiers)) {
        ret += to_accel_table_string(KeyModifier::Alt);
    }
    return ret;
}

}