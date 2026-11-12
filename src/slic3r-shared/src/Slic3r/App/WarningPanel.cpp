#include "Slic3r/App/WarningPanel.hpp"

#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/Icon.hpp"
#include "Slic3r/App/Yoga/Separator.hpp"

#include <numeric>

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

WarningPanel::WarningPanel(Platform::Color color)
{
    const ImColor warning_color = m_theme->color_imgui(color);
    ImColor warning_color_fill  = warning_color;
    warning_color_fill.Value.w  = 0.15f; // 15%
    set_fill(warning_color_fill);
    set_rounding(0);

    set_orientation(Orientation::Vertical);

    emplace_back<Separator>();

    Item* content_row = emplace_back<Item>();
    content_row->set_gap(15_fpx);
    content_row->set_padding(15_fpx);

    m_warning_icon = content_row->emplace_back<Icon>(Render::Icon::ExclamationTriangle);
    m_warning_icon->set_width(16_fpx);
    m_warning_icon->set_height(16_fpx);
    m_warning_icon->set_aspect_ratio(1);
    m_warning_icon->set_fill_mode(Icon::FillMode::PreservedAspectCentered);
    m_warning_icon->set_self_align(YGAlign::YGAlignFlexStart);
    m_warning_icon->set_preserve_colors(true);
    m_warning_icon->set_tint(warning_color);

    Item* text_column = content_row->emplace_back<Item>();
    text_column->set_gap(5_fpx);
    text_column->set_flex_grow(1);
    text_column->set_orientation(Orientation::Vertical);

    m_title = text_column->emplace_back<Text>(std::string{});
    m_title->set_font_type(Render::ImguiFontType::Bold);
    m_title->set_text_color(warning_color);
    m_title->set_height(16_fpx);
    m_title->set_align(Align{AlignH::Left, AlignV::Center});

    m_text = text_column->emplace_back<Text>(std::string{});
    m_text->set_wrap_mode(Text::WrapMode::Wrap);
    m_text->set_text_color(warning_color);

    emplace_back<Separator>();
}

void WarningPanel::set_warning(const std::string& title, const std::string& text)
{
    m_title->set_text(title);
    m_text->set_text(text);
}

void WarningPanel::set_warning(const std::string& title, const std::vector<std::string>& errors)
{
    m_title->set_text(title);

    m_text->set_text(
        std::accumulate(
            std::next(errors.begin()),
            errors.end(),
            errors.empty() ? std::string{} : errors.front(),
            [](std::string sum, const std::string& error) { return sum + '\n' + error; }
        )
    );
}

} // namespace Slic3r::App
