#include "Slic3r/App/Yoga/InputTextField.hpp"

#include "Slic3r/App/Yoga/Validator.hpp"
#include <fmt/format.h>

#include <imgui_internal.h>

namespace Slic3r::App::Yoga {

InputTextField::InputTextField(const std::string& name)
{
    set_object_name("InputTextField");
    m_tooltip = emplace_back<Tooltip>(this, std::string{}, std::string{});
    set_padding(1);
    set_disabled_fill(
        m_theme->color_imgui(Platform::Color::Button, Platform::ColorGroup::Disabled)
    );
    m_input_text = emplace_back<InputText>(name.empty() ? "InputText" : name);
    m_input_text->set_flex_grow(1);

    callbacks().update_revert_button = [this]() { update_revert_button(); };

    m_input_text->callbacks().hovered_changed = [this](bool hovered)
    {
        if (!m_tooltip->text().empty()) {
            hovered ? m_tooltip->open() : m_tooltip->close();
        }
        update_fill();
    };
    m_input_text->callbacks().active_changed = [this](bool active) { update_fill(); };

    update_fill();
}

InputText::Callbacks& InputTextField::callbacks()
{
    return m_input_text->callbacks();
}

const std::string& InputTextField::text() const
{
    return m_input_text->text();
}

void InputTextField::set_text(const std::string& text)
{
    if (text != m_input_text->text()) {
        m_input_text->set_text(text);
        text_updated_internal();
    }
}

ImGuiInputTextFlags InputTextField::input_flags() const
{
    return m_input_text->flags();
}

void InputTextField::set_input_flags(ImGuiInputTextFlags flags)
{
    m_input_text->set_flags(flags);
}

const std::string& InputTextField::hint() const
{
    return m_input_text->hint();
}

void InputTextField::set_hint(const std::string& hint)
{
    m_input_text->set_hint(hint);
}

const std::string& InputTextField::override_label() const
{
    return m_input_text->override_label();
}

void InputTextField::set_override_label(const std::string& override_label)
{
    m_input_text->set_override_label(override_label);
}

Validator* InputTextField::validator() const
{
    return m_input_text->validator();
}

void InputTextField::set_validator(std::unique_ptr<Validator> validator)
{
    m_input_text->set_validator(std::move(validator));
}

void InputTextField::set_tooltip(const std::string& tooltip)
{
    m_tooltip->set_text(tooltip);
}

void InputTextField::set_tooltip_position(Position position)
{
    m_tooltip->set_preferred_position(position);
}

Render::ImguiFontType InputTextField::font_type() const
{
    return m_input_text->font_type();
}

void InputTextField::set_font_type(Render::ImguiFontType font_type)
{
    m_input_text->set_font_type(font_type);
}

bool InputTextField::hovered() const
{
    return m_input_text->hovered();
}

InputText* InputTextField::input_text() const
{
    return m_input_text;
}

void InputTextField::update_fill()
{
    ImColor color;
    if (hovered() || m_input_text->active()) {
        if (m_color) {
            color = m_theme->color_imgui(*m_color, Platform::ColorGroup::Hovered);
        } else {
            color = m_theme->color_imgui(Platform::Color::Button, Platform::ColorGroup::Hovered);
        }
    } else {
        if (m_color) {
            color = m_theme->color_imgui(*m_color);
        } else {
            color = m_theme->color_imgui(Platform::Color::Button);
        }
    }
    set_fill(color);
}

void InputTextField::set_default(double default_value)
{
    if (DoubleValidator* double_validator = dynamic_cast<DoubleValidator*>(validator());
        double_validator && double_validator->precision().has_value())
    {
        m_default_text =
            fmt::format("{1:.{0}f}", double_validator->precision().value(), default_value);
    } else {
        m_default_text = fmt::format("{:.10g}", default_value);
    }
    update_revert_button();
}

void InputTextField::set_default(const std::string& default_text)
{
    m_default_text = default_text;
    update_revert_button();
}

bool InputTextField::is_changed_value() const
{
    return m_input_text->text() != m_default_text;
}

void InputTextField::reset()
{
    m_input_text->set_text(m_default_text);

    if (callbacks().text_edited) {
        callbacks().text_edited();
    }
}

bool InputTextField::resizable() const
{
    return m_input_text->resizable();
}

void InputTextField::set_resizable(bool resizable)
{
    m_input_text->set_resizable(resizable);
}

std::optional<Platform::Color> InputTextField::color() const
{
    return m_color;
}

void InputTextField::set_color(std::optional<Platform::Color> color)
{
    m_color = color;
    update_fill();
}
} // namespace Slic3r::App::Yoga
