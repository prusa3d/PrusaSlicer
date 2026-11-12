#include "Slic3r/App/Yoga/ColorPickerButton.hpp"

#include "Slic3r/App/Yoga/ContextPopup.hpp"
#include "Slic3r/App/Yoga/ColorPicker.hpp"

namespace Slic3r::App::Yoga {

ColorPickerButton::ColorPickerButton(const std::string& name)
{
    set_object_name(name.empty() ? "ColorPickerButton" : name);
    set_width(30);
    set_height(25);
    set_background_border_width(2);
    set_background_color(m_theme->color_imgui(Platform::Color::WindowBg));

    m_popup = emplace_back<ContextPopup>("ColorPickerPopup");
    m_popup->set_orientation(Orientation::Vertical);
    m_popup->set_padding(5);
    m_color_picker = m_popup->emplace_back<ColorPicker>();
    m_color_picker->set_flex_grow(1);
    m_color_picker->callbacks().color_edited = [this](const ImColor& color)
    { on_color_edited(color); };
    m_popup->set_position(Position::Right);
    m_popup->callbacks().closed = [this]
    {
        if (m_delayed_update && m_callbacks.color_edited) {
            m_callbacks.color_edited(m_color);
        }
    };

    AbstractButton::callbacks().action = [this]
    {
        if (m_popup->opened()) {
            m_popup->close();
        } else {
            m_popup->open();
        }
    };

    set_color(m_color);
}

ColorPickerButton::Callbacks& ColorPickerButton::callbacks()
{
    return m_callbacks;
}

const ImColor& ColorPickerButton::color() const
{
    return m_color;
}

void ColorPickerButton::set_color(const ImColor& color)
{
    m_color = color;
    m_color_picker->set_color(color);
    set_background_color(color, false);
}

ImGuiColorEditFlags ColorPickerButton::flags() const
{
    return m_color_picker->flags();
}

void ColorPickerButton::set_flags(ImGuiColorEditFlags flags)
{
    return m_color_picker->set_flags(flags);
}

bool ColorPickerButton::delayed_update() const
{
    return m_delayed_update;
}

void ColorPickerButton::set_delayed_update(bool delayed_update)
{
    m_delayed_update = delayed_update;
}

void ColorPickerButton::on_color_edited(const ImColor& color)
{
    m_color = color;
    set_background_color(color);
    if (!m_delayed_update && m_callbacks.color_edited) {
        m_callbacks.color_edited(m_color);
    }
}

void ColorPickerButton::hovered_updated_internal()
{
    set_background_border_width(hovered() ? 2 : 1);
}

} // namespace Slic3r::App::Yoga
