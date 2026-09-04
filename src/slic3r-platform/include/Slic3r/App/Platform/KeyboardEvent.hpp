#pragma once

#include <cstdint>
#include "KeyCode.hpp"
#include "KeyModifers.hpp"

namespace Slic3r::App::Platform {

class KeyboardEvent
{
public:
    enum class Type : uint8_t {
        KeyDown = 0,
        KeyUp = 1,
        COUNT = KeyUp
    };

    KeyboardEvent(Type type, KeyCode code, KeyModifiers modifiers, bool repeat)
        : m_type(type), m_key_modifiers(modifiers), m_code(code), m_repeat(repeat)
    {}

    Type type() const { return m_type; }
    KeyCode code() const { return m_code; }
    KeyModifiers key_modifiers() const { return m_key_modifiers; }
    bool is_repeat() const { return m_repeat; }

private:
    Type m_type;
    KeyModifiers m_key_modifiers;
    KeyCode m_code;
    bool m_repeat;
};

} // namespace Slic3r::App::Platform
