#include "Slic3r/App/Yoga/ToggleButton.hpp"

#include "Slic3r/App/Yoga/Tooltip.hpp"
#include "Slic3r/App/Yoga/Toggler.hpp"
#include "Slic3r/App/Yoga/Text.hpp"

namespace Slic3r::App::Yoga {

ToggleButton::ToggleButton(const std::string& label, const std::string& tooltip) :
    AbstractButton(tooltip)
{
    set_orientation(Orientation::Horizontal);
    set_align_items(YGAlignCenter);

    set_checkable(true);

    m_toggler = emplace_back<Toggler>();

    m_label = emplace_back<Text>(label);
    m_label->set_margin({5.f});
    m_label->set_visible(!label.empty());

    set_tooltip_position(Position::Bottom);
}

void ToggleButton::set_label(const std::string& label)
{
    m_label->set_text(label);
    m_label->set_visible(!label.empty());
}

const std::string& ToggleButton::get_label() const
{
    return m_label->text();
}

void ToggleButton::set_font_type(Render::ImguiFontType font_type)
{
    m_label->set_font_type(font_type);
}

void ToggleButton::checked_updated_internal()
{
    AbstractButton::checked_updated_internal();
    m_toggler->set_checked(checked());
    update_revert_button();
}

void ToggleButton::hovered_updated_internal()
{
    m_toggler->set_hovered(hovered());
}

void ToggleButton::set_default(bool default_checked)
{
    m_default_checked = default_checked;
    update_revert_button();
}

bool ToggleButton::is_changed_value() const
{
    return m_default_checked != checked();
}

void ToggleButton::reset()
{
    set_checked(m_default_checked);
}

bool ToggleButton::third_state() const
{
    return m_toggler->third_state();
}

void ToggleButton::set_third_state(bool third_state)
{
    m_toggler->set_third_state(third_state);
}

} // namespace Slic3r::App::Yoga
