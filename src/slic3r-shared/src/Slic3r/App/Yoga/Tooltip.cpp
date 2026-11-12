#include "Slic3r/App/Yoga/Tooltip.hpp"

#include "Slic3r/App/Yoga/Text.hpp"

namespace Slic3r::App::Yoga {

Tooltip::Tooltip(
    Item* parent,
    const std::string& text,
    const std::string& shortcut,
    const std::string& window_name
)
{
    WindowPtr window = std::make_unique<Window>(window_name.empty() ? "Tooltip" : window_name);

    window->set_orientation(Orientation::Horizontal);
    window->set_flags(
        ImGuiWindowFlags_Tooltip
        | ImGuiWindowFlags_NoInputs
        | ImGuiWindowFlags_NoTitleBar
        | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoFocusOnAppearing
        | ImGuiWindowFlags_NoMove
    );

    window->set_gap(5);
    window->set_padding(4);
    window->set_alpha(0.8f);
    m_text = window->emplace_back<Text>(text);
    m_text->set_visible(!text.empty());
    m_shortcut = window->emplace_back<Text>(shortcut);
    m_shortcut->set_visible(!shortcut.empty());
    m_shortcut->set_text_color(
        m_theme->color_imgui(Platform::Color::Text, Platform::ColorGroup::Disabled)
    );

    set_content_item(std::move(window));
    set_allow_fallback_position(true);

    attach_to_item(parent);
}

const std::string& Tooltip::text() const
{
    return m_text->text();
}

void Tooltip::set_text(const std::string& text)
{
    m_text->set_text(text);
    m_text->set_visible(!text.empty());
    if (text.empty()) {
        close();
    }
}

void Tooltip::set_text_wrap(bool wrap)
{
    m_text->set_wrap_mode(Text::WrapMode::Wrap);
    m_text->set_flex_grow(wrap ? 1 : 0);
}

const std::string& Tooltip::shortcut() const
{
    return m_shortcut->text();
}

void Tooltip::set_shortcut(const std::string& shortcut)
{
    m_shortcut->set_text(shortcut);
    m_shortcut->set_visible(!shortcut.empty());
}

} // namespace Slic3r::App::Yoga
