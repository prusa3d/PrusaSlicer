#include "Slic3r/App/Config/FavoriteButton.hpp"
#include "Slic3r/App/Yoga/Item.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"

namespace Slic3r::App {

FavoriteButton::FavoriteButton() : LayoutButton(std::string(), Render::Icon::Star)
{
    set_object_name("FavoriteButton");
    set_min_width(16);
    set_min_height(16);
    set_content_padding(0.f);
    set_background_color(Platform::Color::ButtonTransparent);
    set_background_color_checked(m_theme->color_imgui(Platform::Color::Transparent));
    set_tooltip(Biz::_u8L("Add to favorites"));
}

bool FavoriteButton::show_only_on_hover() const
{
    return m_show_only_on_hover;
}

void FavoriteButton::set_show_only_on_hover(bool show_only_on_hover)
{
    m_show_only_on_hover = show_only_on_hover;
    checked_updated_internal();
}

void FavoriteButton::checked_updated_internal()
{
    set_icon(
        (m_show_only_on_hover && !hovered()) ? Render::Icon::None :
            checked()                        ? Render::Icon::StarSolid :
                                               Render::Icon::Star
    );
    set_tooltip(checked() ? Biz::_u8L("Remove from favorites") : Biz::_u8L("Add to favorites"));
}

void FavoriteButton::hovered_updated_internal()
{
    if (m_show_only_on_hover) {
        set_icon(
            hovered() ? (checked() ? Render::Icon::StarSolid : Render::Icon::Star) :
                        Render::Icon::None
        );
    }
}
} // namespace Slic3r::App
