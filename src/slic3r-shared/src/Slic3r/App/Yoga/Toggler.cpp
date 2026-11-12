#include "Slic3r/App/Yoga/Toggler.hpp"

#include "Slic3r/App/Yoga/Circle.hpp"

namespace Slic3r::App::Yoga {

Toggler::Toggler()
{
    set_object_name("Toggler");
    set_align_items(YGAlignCenter);

    set_width(20);
    set_height(14);
    set_padding(3);

    m_knob = emplace_back<Circle>();
    m_knob->set_height_percent(100);
    m_inner_oval = emplace_back<Oval>();
    m_inner_oval->set_width_percent(100);
    m_inner_oval->set_height_percent(100);
    m_inner_oval->set_visible(false);

    update_color();
}

void Toggler::update_contents()
{
    YGJustify justify;
    if (m_third_state) {
        justify = YGJustify::YGJustifyCenter;
    } else if (m_checked) {
        justify = YGJustify::YGJustifyFlexEnd;
    } else {
        justify = YGJustify::YGJustifyFlexStart;
    }

    set_justify_content(justify);

    m_knob->set_visible(!m_third_state);
    m_inner_oval->set_visible(m_third_state);
}

void Toggler::update_color()
{
    const ImColor bg_color =
        m_theme->color_imgui(Platform::Color::RadioButtonBackground, button_bg_color_group());
    set_fill(bg_color);
    set_disabled_fill(bg_color);

    const ImColor inner_color =
        m_theme->color_imgui(Platform::Color::RadioButton, button_color_group());
    m_knob->set_fill(inner_color);
    m_knob->set_disabled_fill(inner_color);
    m_inner_oval->set_fill(inner_color);
    m_inner_oval->set_disabled_fill(inner_color);
}

Platform::ColorGroup Toggler::button_bg_color_group() const
{
    if (enabled()) {
        if (m_hovered) {
            return Platform::ColorGroup::Hovered;
        } else {
            return m_checked || m_third_state ? Platform::ColorGroup::Active :
                                                Platform::ColorGroup::Default;
        }
    } else {
        return m_checked || m_third_state ? Platform::ColorGroup::ActiveDisabled :
                                            Platform::ColorGroup::Disabled;
    }
}

Platform::ColorGroup Toggler::button_color_group() const
{
    if (enabled()) {
        return m_checked || m_third_state ? Platform::ColorGroup::Active :
                                            Platform::ColorGroup::Default;
    } else {
        return m_checked || m_third_state ? Platform::ColorGroup::ActiveDisabled :
                                            Platform::ColorGroup::Disabled;
    }
}

void Toggler::enabled_updated_internal()
{
    update_color();
}

bool Toggler::hovered() const
{
    return m_hovered;
}

void Toggler::set_hovered(bool hovered)
{
    if (m_hovered != hovered) {
        m_hovered = hovered;
        update_color();
    }
}

bool Toggler::third_state() const
{
    return m_third_state;
}

void Toggler::set_third_state(bool third_state)
{
    m_third_state = third_state;
    update_contents();
    update_color();
}

void Toggler::set_checked(bool checked)
{
    m_third_state = false;
    m_checked     = checked;
    update_contents();
    update_color();
}

} // namespace Slic3r::App::Yoga
