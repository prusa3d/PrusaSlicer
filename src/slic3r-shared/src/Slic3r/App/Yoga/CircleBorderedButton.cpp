#include "Slic3r/App/Yoga/CircleBorderedButton.hpp"

#include "Slic3r/App/Yoga/Icon.hpp"

namespace Slic3r::App::Yoga {

namespace {
Unit button_width = 30_fpx;
} // namespace

CircleBorderedButton::CircleBorderedButton()
{
    set_object_name("CircleBorderedButton");

    set_checkable(true);
    set_content_padding(5_fpx);

    // Show the checked state via the border instead of a solid fill: capture the
    // default checked colors into locals first, since set_background_color_checked()
    // below overwrites them.
    const ImColor checked_border_color       = background_color_checked();
    const ImColor checked_border_color_hover = background_color_checked_hover();
    set_background_color_border(checked_border_color, checked_border_color_hover);

    set_background_color(Platform::Color::ButtonTransparent);
    set_background_color_checked(
        m_theme->color_imgui(Platform::Color::ButtonTransparent),
        m_theme->color_imgui(Platform::Color::ButtonTransparent)
    );

    set_width(button_width);
    set_height(button_width);
    set_rounding(0.5f * button_width.value);

    m_icon = emplace_back<Icon>(Render::Icon::None);
    m_icon->set_visible(false);
    m_icon->set_aspect_ratio(1);
    m_icon->set_fill_mode(Icon::FillMode::PreservedAspectCentered);
    m_icon->set_width(18_fpx);
}

void CircleBorderedButton::checked_updated_internal()
{
    RectangleButton::checked_updated_internal();
    constexpr Unit border = 3_fpx;
    set_background_border_width(checked() ? border.value : 0.f);
}

void CircleBorderedButton::set_icon(Render::Icon icon)
{
    m_icon->set_icon(icon);
    m_icon->set_visible(icon != Render::Icon::None);
}

void CircleBorderedButton::set_icon_tint(const ImColor& tint)
{
    m_icon->set_tint(tint);
}

} // namespace Slic3r::App::Yoga
