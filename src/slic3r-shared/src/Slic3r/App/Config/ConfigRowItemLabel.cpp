#include "Slic3r/App/Config/ConfigRowItemLabel.hpp"

namespace Slic3r::App {

ConfigRowItemLabel::ConfigRowItemLabel(const std::string& label)
    : m_label_color(m_theme->color_imgui(Platform::Color::Text))
{
    m_text = emplace_back<Yoga::StrikethroughText>(label);
    m_text->set_wrap_mode(Yoga::Text::WrapMode::WrapElide);
    m_text->set_width_percent(100);
    m_text->set_height_percent(100);
    m_text->set_align({Yoga::AlignH::Left, Yoga::AlignV::Center});
    m_text->set_text_color(m_label_color);
}

void ConfigRowItemLabel::set_label_color(const ImColor& color)
{
    m_label_color = color;
    if (!hovered()) {
        m_text->set_text_color(m_label_color);
    }
}

Yoga::StrikethroughText* ConfigRowItemLabel::label_object() const
{
    return m_text;
}

void ConfigRowItemLabel::hovered_updated_internal()
{
    if (hovered()) {
        m_text->set_text_color(
            m_theme->color_imgui(Platform::Color::AccentSecondary, Platform::ColorGroup::Hovered));
    } else {
        m_text->set_text_color(m_label_color);
    }
}
} // namespace Slic3r::App
