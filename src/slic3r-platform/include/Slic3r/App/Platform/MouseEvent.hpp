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

    /**
     * @brief Scroll payload of a Wheel event, in three different units.
     *
     * A classic notched mouse wheel and a macOS trackpad are very different input
     * devices, and consumers need different units from them:
     *
     * - @c delta is a *continuous* amount of wheel detents. A notched wheel produces
     *   exactly +/-1 per click; a precise device (trackpad, Magic Mouse) produces
     *   small fractions many times per second. Use it for continuous actions such as
     *   camera zoom.
     * - @c pixels is the physical distance the content should travel, in logical
     *   pixels. Precise devices report this directly (which is what makes their
     *   scrolling — including the momentum phase synthesised by the OS — feel
     *   right), notched wheels have it synthesised from @c delta.
     * - @c steps is a *discrete* detent count, accumulated by the platform layer so
     *   that precise devices still produce one step per detent-worth of movement.
     *   Use it for quantised actions such as changing a brush size, which would
     *   otherwise run away on a trackpad.
     */
    struct Scroll
    {
        float delta_x{0};  ///< Continuous wheel detents, horizontal.
        float delta_y{0};  ///< Continuous wheel detents, vertical.
        float pixels_x{0}; ///< Logical pixels of content travel, horizontal.
        float pixels_y{0}; ///< Logical pixels of content travel, vertical.
        int steps_x{0};    ///< Whole detents crossed since the last event, horizontal.
        int steps_y{0};    ///< Whole detents crossed since the last event, vertical.
        bool precise{false};  ///< Device reports sub-detent deltas (trackpad, Magic Mouse).
        bool momentum{false}; ///< Event belongs to the OS-generated inertial phase.
    };

    MouseEvent(Type type, MouseButton button, int x, int y, float wheel_delta_x, float wheel_delta_y, KeyModifiers key_modifiers)
        : m_type(type), m_button(button), m_x(x), m_y(y), m_key_modifiers(key_modifiers)
    {
        m_scroll.delta_x = wheel_delta_x;
        m_scroll.delta_y = wheel_delta_y;
        m_scroll.steps_x = static_cast<int>(wheel_delta_x);
        m_scroll.steps_y = static_cast<int>(wheel_delta_y);
    }

    MouseEvent(Type type, MouseButton button, int x, int y, const Scroll& scroll, KeyModifiers key_modifiers)
        : m_type(type), m_button(button), m_x(x), m_y(y), m_scroll(scroll), m_key_modifiers(key_modifiers)
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
    [[nodiscard]] const Scroll& scroll() const { return m_scroll; }
    [[nodiscard]] float wheel_delta_x() const { return m_scroll.delta_x; }
    [[nodiscard]] float wheel_delta_y() const { return m_scroll.delta_y; }
    [[nodiscard]] int wheel_steps_x() const { return m_scroll.steps_x; }
    [[nodiscard]] int wheel_steps_y() const { return m_scroll.steps_y; }
    [[nodiscard]] bool is_precise_scroll() const { return m_scroll.precise; }
    [[nodiscard]] KeyModifiers key_modifiers() const { return m_key_modifiers; }
    [[nodiscard]] bool is_imgui_captured() const { return m_imgui_captured; }
private:
    Type m_type;
    MouseButton m_button;
    int m_x{0};
    int m_y{0};
    Scroll m_scroll{};
    KeyModifiers m_key_modifiers;
    bool m_imgui_captured{false};
};

}
