#pragma once

#include "Slic3r/App/Plater/GizmoWindowWithLeftSidePanel.hpp"
#include "Slic3r/App/Yoga/Slider.hpp"
#include "Slic3r/App/Yoga/ToggleButton.hpp"
#include "Slic3r/App/Plater/VariableLayerHeightControl.hpp"
#include "Slic3r/Domain/LayerHeightProfile.hpp"

namespace Slic3r::App::Yoga {
class LayoutButton;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App::Plater {

class VariableLayerHeightDialog : public GizmoWindowWithLeftSidePanel
{
public:
    struct Callbacks
    {
        std::function<void(double)> smart_resolution_changed = [](double) {};
        std::function<void(int)> blend_distance_changed      = [](int) {};
        std::function<void(bool)> lock_high_detail_changed   = [](bool) {};
        std::function<void()> auto_calculate_clicked         = []() {};
        std::function<void()> smooth_clicked                 = []() {};
        std::function<void()> reset_clicked                  = []() {};

        std::function<void(std::optional<float> cursor_normalized_position)>
            layer_profile_mouse_move = [](std::optional<float>) {};
        std::function<void(
            float cursor_normalized_position,
            bool shift_down,
            bool ctrl_down,
            VariableLayerHeightControl::Button mouse_button
        )>
            layer_profile_mouse_down = [](float, bool, bool, VariableLayerHeightControl::Button) {};
        std::function<void(
            float cursor_normalized_position,
            bool shift_down,
            bool ctrl_down,
            VariableLayerHeightControl::Button mouse_button
        )>
            layer_profile_mouse_drag = [](float, bool, bool, VariableLayerHeightControl::Button) {};
        std::function<void(VariableLayerHeightControl::Button mouse_button)>
            layer_profile_mouse_up = [](VariableLayerHeightControl::Button) {};
        std::function<void(float mouse_wheel_delta, bool ctrl_down)> layer_profile_mouse_wheel =
            [](float, bool) {};
        std::function<void()> on_height_range_click = []() {};
    };

    VariableLayerHeightDialog();

    Callbacks& callbacks();

    void set_smart_resolution(double smart_resolution);
    void set_blend_distance(int blend_distance);
    void set_lock_high_detail(bool lock_high_detail);
    void set_layer_height_title(double layer_height);

    void set_object_max_z(float object_max_z);
    void set_min_layer_height(float min_layer_height);
    void set_max_layer_height(float max_layer_height);
    void set_default_layer_height(float default_layer_height);
    void set_cursor_band_width(float band_width);
    void set_cursor_normalized_position(float normalized_position);
    void reset_cursor_position();

    void set_layer_height_profile(const Domain::ZHeightPairs& layer_height_profile);
    void set_height_ranges(const HeightRangeEntries& height_ranges);

private:
    Yoga::Passthrough<Yoga::Slider> m_blend_distance_slider;
    Yoga::Passthrough<Yoga::Slider> m_smart_resolution_slider;

    Yoga::ToggleButton* m_lock_high_detail_toggle = nullptr;

    Yoga::LayoutButton* m_auto_calculate_button = nullptr;
    Yoga::LayoutButton* m_smooth_button         = nullptr;

    VariableLayerHeightControl* m_layer_height_profile_control = nullptr;

    Callbacks m_callbacks;

    void add_smart_resolution_section(Item* item);
    void add_blend_distance_section(Item* item);
    void add_help_section(Item* item);
    void init_layer_height_profile_control();
};

} // namespace Slic3r::App::Plater
