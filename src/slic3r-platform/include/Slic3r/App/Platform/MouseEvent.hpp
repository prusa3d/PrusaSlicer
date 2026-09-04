#pragma once
#include <cstdint>
#include <type_traits>

#include "KeyModifers.hpp"

namespace Slic3r::App::Platform {

#ifdef None
// There is a special place in hell for people who create common word defines
// without prefixes like the Xlib devs
#undef None
#endif

enum class MouseButton : uint8_t {
    NoButton = 0,
    Left = 1,
    Middle = 1 << 1,
    Right = 1 << 2,
};

using MouseButtons = std::underlying_type_t<MouseButton>;

class MouseEvent {
public:
    enum class Type : uint8_t {
        Move = 0,
        ButtonDown,
        ButtonUp,
        DoubleClick,
        Wheel,
        Enter,
        Leave
    };

    MouseEvent(Type type, MouseButton button, int x, int y, float wheel_delta_x, float wheel_delta_y, KeyModifiers key_modifiers)
        : m_type(type), m_button(button), m_x(x), m_y(y), m_wheel_delta_x(wheel_delta_x), m_wheel_delta_y(wheel_delta_y), m_key_modifiers(key_modifiers)
    {}

    MouseEvent(const MouseEvent& other) = default;
    MouseEvent(MouseEvent&& other) = default;

    MouseEvent& operator=(const MouseEvent& other) = default;
    MouseEvent& operator=(MouseEvent&& other) = default;

    void set_imgui_captured(bool imgui_captured) { m_imgui_captured = imgui_captured; }

    [[nodiscard]] Type type() const { return m_type; }
    [[nodiscard]] MouseButton button() const { return m_button; }
    [[nodiscard]] int x() const { return m_x; }
    [[nodiscard]] int y() const { return m_y; }
    [[nodiscard]] float wheel_delta_x() const { return m_wheel_delta_x; }
    [[nodiscard]] float wheel_delta_y() const { return m_wheel_delta_y; }
    [[nodiscard]] KeyModifiers key_modifiers() const { return m_key_modifiers; }
    [[nodiscard]] bool is_imgui_captured() const { return m_imgui_captured; }
private:
    Type m_type;
    MouseButton m_button;
    int m_x{0};
    int m_y{0};
    float m_wheel_delta_x{0};
    float m_wheel_delta_y{0};
    KeyModifiers m_key_modifiers;
    bool m_imgui_captured{false};
};

}
