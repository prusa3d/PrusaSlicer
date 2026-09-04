#include "Slic3r/App/ToolBar/ToolBarSwitchButton.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

ToolBarSwitchButton::ToolBarSwitchButton(
    SwitchPosition switch_position,
    Render::Icon icon,
    const std::string& label,
    const std::string& tooltip
) :
    ToolBarButton(icon, tooltip),
    m_switch_position(switch_position),
    m_label(label)
{
    set_label(label);

    set_background_color(Platform::Color::Button);

    switch (switch_position) {
    case SwitchPosition::Left:
        set_draw_flags(ImDrawFlags_RoundCornersLeft);
        break;
    case SwitchPosition::Center:
        set_draw_flags(ImDrawFlags_None);
        break;
    case SwitchPosition::Right:
        set_draw_flags(ImDrawFlags_RoundCornersRight);
        break;
    }
}

void ToolBarSwitchButton::set_show_label(bool show_label)
{
    if (show_label) {
        set_label(m_label);
    } else {
        set_label(std::string{});
    }
}

void ToolBarSwitchButton::style_node()
{
    ToolBarButton::style_node();
    if (!label().empty()) {
        m_width_with_label = width();
    }
}

float ToolBarSwitchButton::width_with_label() const
{
    return m_width_with_label;
}

} // namespace Slic3r::App
