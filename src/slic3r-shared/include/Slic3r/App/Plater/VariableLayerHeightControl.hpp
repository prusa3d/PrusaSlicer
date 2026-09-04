#pragma once

#include "Slic3r/App/Plater/LayerHeightProfileControl.hpp"

#include <functional>

namespace Slic3r::App::Plater {

class VariableLayerHeightControl : public LayerHeightProfileControl
{
public:
    enum class Button
    {
        None,
        Left,
        Right
    };

    struct Callbacks
    {
        std::function<void(std::optional<float> cursor_normalized_position)> on_mouse_move =
            [](std::optional<float>) {};
        std::function<void(
            float cursor_normalized_position,
            bool shift_down,
            bool ctrl_down,
            Button mouse_button
        )>
            on_mouse_down = [](float, bool, bool, Button) {};
        std::function<void(
            float cursor_normalized_position,
            bool shift_down,
            bool ctrl_down,
            Button mouse_button
        )>
            on_mouse_drag                                    = [](float, bool, bool, Button) {};
        std::function<void(Button mouse_button)> on_mouse_up = [](Button) {};
        std::function<void(float mouse_wheel_delta, bool ctrl_down)> on_mouse_wheel = [](float,
                                                                                         bool) {};
        std::function<void()> on_height_range_click                                 = []() {};
    };

    VariableLayerHeightControl();

    Callbacks& callbacks();

    void set_cursor_band_width(float width);
    void set_cursor_normalized_position(float normalized_position);
    void reset_cursor_position();

    void render(const Domain::Vec2f& pos, const Domain::Vec2f& size) override;

private:
    void render_cursor(const Domain::Vec2f& pos, const Domain::Vec2f& size) const;
    void render_height_range_tooltip() const;

    void process_input(
        const Domain::Vec2f& pos,
        const Domain::Vec2f& size,
        const Domain::Vec2f& profile_area_position,
        const Domain::Vec2f& profile_area_size
    );

    float m_cursor_band_width = 0.f;

    std::optional<float> m_cursor_normalized_position;

    Callbacks m_callbacks;

    Button m_mouse_button_down = Button::None;
    bool m_was_active          = false;
    bool m_was_hovered         = false;
};

} // namespace Slic3r::App::Plater
