#pragma once

#include "Slic3r/App/Plater/GizmoWindow.hpp"

#include <functional>
#include <string>

namespace Slic3r::App::Yoga {
class InputTextWithSpin;
class LayoutButton;
class SliderWithInput;
class Text;
class ToggleButton;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App::Plater {

/**
 * @brief Panel of the SLA hollowing gizmo: the hollowing settings of the object and the drain
 * holes, edited directly in the scene and sent to slicing by Apply.
 */
class SlaHollowDialog : public GizmoWindow
{
public:
    SlaHollowDialog();

    struct Callbacks
    {
        std::function<void(bool)> hollowing_enabled_changed        = [](bool) {};
        std::function<void(double)> wall_thickness_changed         = [](double) {};
        std::function<void(double)> accuracy_changed               = [](double) {};
        std::function<void(double)> closing_distance_changed       = [](double) {};
        std::function<void(double)> hole_diameter_changed          = [](double) {};
        std::function<void(double)> hole_depth_changed             = [](double) {};
        std::function<void()> remove_all                           = []() {};
        std::function<void()> apply                                = []() {};
        std::function<void(double)> clipping_of_view_value_changed = [](double) {};
        std::function<void()> clipping_of_view_reset_direction     = []() {};
    };

    Callbacks& callbacks();

    void set_status(const std::string& status);
    void set_input_enabled(bool enabled);
    void set_apply_enabled(bool enabled);
    void set_remove_all_enabled(bool enabled);
    void set_hollowing(bool enabled, double wall_thickness, double accuracy, double closing_distance);
    void set_hole_size(double diameter, double depth);
    void set_clipping_of_view_value(double value);

private:
    void update_enabled();

    Callbacks m_callbacks;

    Yoga::Text* m_status_text                                     = nullptr;
    Yoga::ToggleButton* m_hollowing_toggle                        = nullptr;
    Yoga::InputTextWithSpin* m_wall_thickness_input               = nullptr;
    Yoga::InputTextWithSpin* m_accuracy_input                     = nullptr;
    Yoga::InputTextWithSpin* m_closing_distance_input             = nullptr;
    Yoga::InputTextWithSpin* m_hole_diameter_input                = nullptr;
    Yoga::InputTextWithSpin* m_hole_depth_input                   = nullptr;
    Yoga::LayoutButton* m_remove_all_button                       = nullptr;
    Yoga::LayoutButton* m_apply_button                            = nullptr;
    Yoga::SliderWithInput* m_clipping_of_view_slider              = nullptr;
    Yoga::LayoutButton* m_clipping_of_view_reset_direction_button = nullptr;

    bool m_input_enabled     = false;
    bool m_hollowing_enabled = false;
    bool m_apply_enabled     = false;
    bool m_remove_enabled    = false;
    bool m_setting_values    = false;
};

} // namespace Slic3r::App::Plater
