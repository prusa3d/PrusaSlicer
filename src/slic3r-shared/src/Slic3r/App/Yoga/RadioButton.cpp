#include "Slic3r/App/Yoga/RadioButton.hpp"

#include "Slic3r/App/Yoga/Tooltip.hpp"
#include "Slic3r/App/Yoga/Circle.hpp"
#include "Slic3r/App/Yoga/Text.hpp"

namespace Slic3r::App::Yoga {

RadioButton::RadioButton(const std::string& label, const std::string& tooltip) :
    AbstractButton(tooltip)
{
    set_object_name("RadioButton");
    set_orientation(Orientation::Horizontal);
    set_align_items(YGAlignCenter);
    set_gap(10.f);

    set_checkable(true);

    m_knob = emplace_back<Circle>();
    m_knob->set_border_width(1);
    m_knob->set_min_width(12);
    m_knob->set_min_height(12);

    m_label = emplace_back<Text>(label);
    m_label->set_visible(!label.empty());

    set_tooltip_position(Position::Bottom);

    update_colors();
}

Text* RadioButton::label() const
{
    return m_label;
}

void RadioButton::set_label(const std::string& label)
{
    m_label->set_text(label);
    m_label->set_visible(!label.empty());
}

const std::string& RadioButton::get_label() const
{
    return m_label->text();
}

void RadioButton::set_font_type(Render::ImguiFontType font_type)
{
    if (m_label)
        m_label->set_font_type(font_type);
}

void RadioButton::checked_updated_internal()
{
    AbstractButton::checked_updated_internal();

    update_colors();
    m_knob->set_border_width(checked() ? 3 : 1);
}

void RadioButton::enabled_updated_internal()
{
    update_colors();
}

void RadioButton::hovered_updated_internal()
{
    update_colors();
}

void RadioButton::update_colors()
{
    m_knob->set_fill(m_theme->color_imgui(Platform::Color::Button, button_color_group()));
    m_knob->set_disabled_fill(m_theme->color_imgui(Platform::Color::Button, button_color_group()));
}

} // namespace Slic3r::App::Yoga
