#include "Slic3r/App/Plater/HeightRangeButton.hpp"

#include "Slic3r/App/Yoga/Text.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App::Plater {

HeightRangeButton::HeightRangeButton()
{
    set_object_name("HeightRangeButton");
    set_content_padding({8.f, 4.f, 8.f, 4.f});
    set_flex_grow(1);

    m_range_label = emplace_back<Text>(std::string{});
    m_range_label->set_font_type(Render::ImguiFontType::Bold);
    m_range_label->set_flex_grow(1);

    m_height_label = emplace_back<Text>(std::string{});
}

void HeightRangeButton::set_range_label(const std::string& range)
{
    m_range_label->set_text(range);
}

void HeightRangeButton::set_height_label(const std::string& height)
{
    m_height_label->set_text(height);
}

void HeightRangeButton::set_highlighted(bool highlighted)
{
    if (m_highlighted != highlighted) {
        m_highlighted = highlighted;
        update_colors();
    }
}

void HeightRangeButton::update_colors()
{
    set_background_color(
        m_theme->color_imgui(
            Platform::Color::Button,
            m_highlighted ? Platform::ColorGroup::Active : Platform::ColorGroup::Default
        ),
        false
    );
}

} // namespace Slic3r::App::Plater
