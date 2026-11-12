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
 * @brief Panel of the SLA support points gizmo.
 *
 * There are no modes: the points are edited directly in the scene, the panel only holds the tools
 * and the Apply button, which sends the edited points to the backend.
 */
class SlaSupportPointsDialog : public GizmoWindow
{
public:
    SlaSupportPointsDialog();

    struct Callbacks
    {
        std::function<void()> auto_generate                       = []() {};
        std::function<void()> remove_all                          = []() {};
        std::function<void()> apply                               = []() {};
        std::function<void(int)> density_changed                  = [](int) {};
        std::function<void(double)> head_diameter_changed         = [](double) {};
        std::function<void(bool)> lock_islands_changed            = [](bool) {};
        std::function<void(double)> clipping_of_view_value_changed = [](double) {};
        std::function<void()> clipping_of_view_reset_direction    = []() {};
    };

    Callbacks& callbacks();

    void set_status(const std::string& status);
    void set_input_enabled(bool enabled);
    void set_apply_enabled(bool enabled);
    void set_remove_all_enabled(bool enabled);
    void set_density(int density);
    void set_head_diameter(double head_diameter, double max_head_diameter);
    void set_lock_islands(bool lock_islands);
    void set_clipping_of_view_value(double value);

private:
    Callbacks m_callbacks;

    Yoga::Text* m_status_text                                     = nullptr;
    Yoga::LayoutButton* m_auto_generate_button                    = nullptr;
    Yoga::LayoutButton* m_remove_all_button                       = nullptr;
    Yoga::InputTextWithSpin* m_density_input                      = nullptr;
    Yoga::InputTextWithSpin* m_head_diameter_input                = nullptr;
    Yoga::ToggleButton* m_lock_islands_toggle                     = nullptr;
    Yoga::LayoutButton* m_apply_button                            = nullptr;
    Yoga::SliderWithInput* m_clipping_of_view_slider              = nullptr;
    Yoga::LayoutButton* m_clipping_of_view_reset_direction_button = nullptr;

    bool m_input_enabled  = false;
    bool m_apply_enabled  = false;
    bool m_remove_enabled = false;
};

} // namespace Slic3r::App::Plater
