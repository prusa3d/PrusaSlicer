#include "Slic3r/App/Yoga/LayoutButton.hpp"

#include "Slic3r/App/Yoga/Icon.hpp"
#include "Slic3r/App/Yoga/Text.hpp"

namespace Slic3r::App::Yoga {

Slic3r::App::Yoga::LayoutButton::LayoutButton(const std::string& label) :
    LayoutButton(label, Render::Icon::None)
{}

LayoutButton::LayoutButton(const std::string& label, Render::Icon icon) :
    LayoutButton(label, icon, std::string{})
{}

Slic3r::App::Yoga::LayoutButton::LayoutButton(
    const std::string& label,
    Render::Icon icon,
    const std::string& tooltip
) :
    RectangleButton(tooltip)
{
    set_orientation(Orientation::Horizontal);

    m_icon = emplace_back<Icon>(icon);
    m_icon->set_visible(icon != Render::Icon::None);
    m_icon->set_aspect_ratio(1);
    m_icon->set_fill_mode(Icon::FillMode::PreservedAspectCentered);

    m_text = emplace_back<Text>(label);
    m_text->set_self_align(YGAlign::YGAlignCenter);
    m_text->set_visible(!label.empty());
}

const std::string& Slic3r::App::Yoga::LayoutButton::label() const
{
    return m_text->text();
}

void Slic3r::App::Yoga::LayoutButton::set_label(const std::string& label)
{
    m_text->set_text(label);
    m_text->set_visible(!label.empty());
}

Render::ImguiFontType LayoutButton::label_font_type() const
{
    return m_text->font_type();
}

void LayoutButton::set_label_font_type(Render::ImguiFontType label_font_type)
{
    m_text->set_font_type(label_font_type);
}

void LayoutButton::set_expand_label(bool expand)
{
    m_text->set_flex_grow(expand ? 1.f : 0.f);
}

const ImColor& LayoutButton::label_color() const
{
    return m_text->text_color();
}

void LayoutButton::set_label_color(const ImColor& color)
{
    m_text->set_text_color(color);
}

Render::Icon LayoutButton::icon() const
{
    return m_icon->icon();
}

void LayoutButton::set_icon(Render::Icon icon)
{
    if (m_icon->icon() != icon) {
        m_icon->set_icon(icon);
        m_icon->set_visible(icon != Render::Icon::None);
    }
}

ImColor LayoutButton::icon_tint() const
{
    return m_icon->tint();
}

void LayoutButton::set_icon_tint(const ImColor& tint)
{
    m_icon->set_tint(tint);
}

Text::WrapMode LayoutButton::text_wrap_mode() const
{
    return m_text->wrap_mode();
}

void LayoutButton::set_text_wrap_mode(Text::WrapMode wrap_mode)
{
    m_text->set_wrap_mode(wrap_mode);
}

Text* LayoutButton::label_object() const
{
    return m_text;
}

Icon* LayoutButton::icon_object() const
{
    return m_icon;
}

} // namespace Slic3r::App::Yoga
