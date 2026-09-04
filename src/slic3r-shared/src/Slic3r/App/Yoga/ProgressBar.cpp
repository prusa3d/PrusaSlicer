
#include "Slic3r/App/Yoga/ProgressBar.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/Log.hpp"

#include "imgui/imgui_internal.h"

namespace Slic3r::App::Yoga {
ProgressBar::ProgressBar() : Rectangle()
{
    set_object_name("ProgressBar");
    set_fill(IM_COL32_BLACK_TRANS);
    set_border_color(m_theme->color_imgui(Platform::Color::Text, Platform::ColorGroup::Disabled));
    set_border_width(1.f);
    set_justify_content(YGJustifyCenter);

    Item* wrap = emplace_back<Item>();
    wrap->set_flex_grow(1.0);
    m_progress_area = wrap->emplace_back<Rectangle>();
    m_progress_area->set_border_color(
        m_theme->color_imgui(Platform::Color::Text, Platform::ColorGroup::Disabled)
    );
    m_progress_area->set_border_width(1.f);
    m_progress_area->set_disabled_fill(m_theme->color_imgui(Platform::Color::Button, Platform::ColorGroup::Disabled));
    m_progress_area->set_justify_content(YGJustifyFlexEnd);
    m_progress_area->set_fill(
        m_theme->color_imgui(Platform::Color::Button, Platform::ColorGroup::Active)
    );

    m_overlay = emplace_back<Text>("");
    m_overlay->set_font_type(Render::ImguiFontType::Bold);
    m_overlay->set_position_type(YGPositionTypeAbsolute);
    m_overlay->set_self_align(YGAlignCenter);
    m_overlay->set_text_color(
        m_theme->color_imgui(Platform::Color::Text, Platform::ColorGroup::Disabled)
    );
}

double ProgressBar::progress() const
{
    return m_value;
}

void ProgressBar::set_progress(int progress, const std::string& overlay)
{
    if (m_value != progress) {
        // update value
        m_value = std::clamp(progress, 0, 100);
        m_progress_area->set_width(std::lerp(height(), width(), 0.01 * m_value));
        if (m_show_overlay) {
            m_overlay->set_text(overlay);
        }
    }
}

bool ProgressBar::show_overlay() const
{
    return m_show_overlay;
}

void ProgressBar::set_show_overlay(bool show_overlay)
{
    m_show_overlay = show_overlay;
}

const ImColor& ProgressBar::progress_fill() const
{
    return m_progress_area->fill();
}

const ImColor& ProgressBar::overlay_color() const
{
    return m_overlay->text_color();
}

void ProgressBar::set_progress_fill(const ImColor& fill)
{
    m_progress_area->set_fill(fill);
}

void ProgressBar::set_overlay_color(const ImColor& color) const
{
    m_overlay->set_text_color(color);
}

void ProgressBar::update_area_width() {}

void ProgressBar::on_resized()
{
    if (!m_progress_set_on_resized && width() > 0) {
        m_progress_area->set_width(std::lerp(height(), width(), 0.01 * m_value));
        m_progress_set_on_resized = true;
    }
}

} // namespace Slic3r::App::Yoga
