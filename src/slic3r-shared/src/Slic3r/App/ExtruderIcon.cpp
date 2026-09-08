#include "Slic3r/App/ExtruderIcon.hpp"

#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Imgui/ImguiExtension.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

ExtruderIcon::ExtruderIcon()
{
    set_object_name("ExtruderButton");
    set_aspect_ratio(1);

    m_label = emplace_back<Text>(std::string{});
    m_label->set_align({AlignH::Center, AlignV::Center});
    m_label->set_flex_grow(1);
    m_label->set_font_type(Render::ImguiFontType::Bold);
    m_label->set_margin(Margins{0, -1, -1, 0});
    m_label->set_visible(m_extruder_index_visible);

    set_extruder_color(m_theme->color_imgui(Platform::Color::AccentPrimary));
    set_extruder_index(1);
    set_border_color(m_theme->color_imgui(Platform::Color::Text, Platform::ColorGroup::Disabled));
    set_border_color_disabled(
        m_theme->color_imgui(Platform::Color::Text, Platform::ColorGroup::Disabled)
    );
}

void ExtruderIcon::set_extruder_color(const ImColor& color)
{
    set_colors({color});
    m_label->set_text_color(Imgui::contrast_color(color));
    update_color();
}

void ExtruderIcon::set_extruders_colors(const std::vector<ImColor>& colors)
{
    set_colors(colors);
    set_deflate(0.f);
    set_border_width(0);
    m_label->set_visible(m_extruder_index_visible && colors.size() == 1);
}

void ExtruderIcon::set_extruder_index(const size_t index)
{
    set_extruder_index_visible(true);
    m_label->set_text(std::to_string(index));
}

void ExtruderIcon::set_extruder_index_visible(bool visible)
{
    m_extruder_index_visible = visible;
    m_label->set_visible(m_extruder_index_visible);
}

void ExtruderIcon::set_invalid()
{
    set_deflate(2.f);
    set_colors({m_theme->color_imgui(Platform::Color::Transparent)});
    set_border_width(2);
    set_border_color(m_theme->color_imgui(Platform::Color::Error));

    set_extruder_index_visible(false);
}

void ExtruderIcon::enabled_updated_internal()
{
    update_color();
}

void ExtruderIcon::update_color()
{
    if (enabled()) {
        set_border_width(0);
        set_fill(m_fill_collor);
    } else {
        set_border_width(2);
        set_fill(m_theme->color_imgui(Platform::Color::Transparent));
    }
}

} // namespace Slic3r::App
