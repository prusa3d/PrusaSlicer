#include "Slic3r/App/ColorMix/BlendRatioBar.hpp"

#include <algorithm>
#include <cmath>

using namespace Slic3r::App::Yoga;

namespace Slic3r::App::ColorMix {

constexpr Unit HANDLE_OVERHANG = 3_fpx;
constexpr Unit HANDLE_WIDTH    = 5_fpx;
constexpr Unit BAR_HEIGHT      = 30_fpx;
constexpr Unit BAR_ROUNDING    = 6_fpx;
const constexpr int STEP       = 5;
const constexpr int MIN_RATIO  = 5;
const constexpr int MAX_RATIO  = 95;

namespace {
int snap_to_step(const int value)
{
    const int snapped = ((value + STEP / 2) / STEP) * STEP;
    return std::clamp(snapped, MIN_RATIO, MAX_RATIO);
}
} // namespace

BlendRatioBar::BlendRatioBar() :
    AbstractButton({}, "BlendRatioBar"),
    m_handle_overhang{HANDLE_OVERHANG},
    m_handle_width{HANDLE_WIDTH},
    m_handle_rounding{3_fpx},
    m_handle_shadow_offset{1_fpx},
    m_bar_rounding{BAR_ROUNDING},
    m_border_thickness{1_fpx}
{
    this->set_min_width(160_fpx);
    this->set_height(BAR_HEIGHT + 2.f * HANDLE_OVERHANG);
    this->set_flex_shrink(0);
}

void BlendRatioBar::size_info_changed(const Yoga::SizeInfo& info_size)
{
    m_handle_overhang.evaluate(info_size);
    m_handle_width.evaluate(info_size);
    m_handle_rounding.evaluate(info_size);
    m_handle_shadow_offset.evaluate(info_size);
    m_bar_rounding.evaluate(info_size);
    m_border_thickness.evaluate(info_size);
}

BlendRatioBar::Callbacks& BlendRatioBar::callbacks()
{
    return m_callbacks;
}

void BlendRatioBar::set_colors(const ImColor& color_a, const ImColor& color_b)
{
    m_color_a = color_a;
    m_color_b = color_b;
    this->set_style_dirty();
}

void BlendRatioBar::set_ratio(const int ratio)
{
    m_ratio = snap_to_step(std::clamp(ratio, MIN_RATIO, MAX_RATIO));
    this->set_style_dirty();
}

float BlendRatioBar::marker_x_for_ratio(const float width) const
{
    return std::max(1.f, width) * static_cast<float>(100 - m_ratio) / 100.f;
}

bool BlendRatioBar::update_from_x(const float width, const float local_x)
{
    const float usable = std::max(1.f, width);
    const int raw_ratio =
        100 - static_cast<int>(std::round(std::clamp(local_x, 0.f, usable) * 100.f / usable));
    const int new_ratio = snap_to_step(raw_ratio);
    if (new_ratio == m_ratio) {
        return false;
    }

    m_ratio = new_ratio;
    this->set_style_dirty();

    return true;
}

void BlendRatioBar::render(const Vec2f& pos, const Vec2f& size)
{
    const float bar_rounding    = m_bar_rounding.result;
    const float handle_overhang = m_handle_overhang.result;
    const float handle_width    = m_handle_width.result;
    const float handle_rounding = m_handle_rounding.result;
    const float shadow_offset   = m_handle_shadow_offset.result;

    AbstractButton::render(pos, size);

    if (this->enabled()) {
        if (this->pressed()) {
            m_dragging        = true;
            const ImGuiIO& io = ImGui::GetIO();
            if (this->update_from_x(size.x(), io.MousePos.x - pos.x())) {
                if (m_callbacks.ratio_changed) {
                    m_callbacks.ratio_changed(m_ratio);
                }
            }
        } else {
            m_dragging = false;
        }
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ASSERT(draw_list);

    const ImVec2 bar_min{pos.x(), pos.y() + handle_overhang};
    const ImVec2 bar_max{pos.x() + size.x(), pos.y() + size.y() - handle_overhang};
    const float cap_width = std::min(2.f * bar_rounding, (bar_max.x - bar_min.x) / 2.f);

    draw_list->AddRectFilled(
        bar_min,
        ImVec2(bar_min.x + cap_width, bar_max.y),
        m_color_a,
        bar_rounding,
        ImDrawFlags_RoundCornersLeft
    );
    draw_list->AddRectFilled(
        ImVec2(bar_max.x - cap_width, bar_min.y),
        bar_max,
        m_color_b,
        bar_rounding,
        ImDrawFlags_RoundCornersRight
    );
    draw_list->AddRectFilledMultiColor(
        ImVec2(bar_min.x + cap_width, bar_min.y),
        ImVec2(bar_max.x - cap_width, bar_max.y),
        m_color_a,
        m_color_b,
        m_color_b,
        m_color_a
    );

    const ImColor border_color =
        m_theme->color_imgui(Platform::Color::Text, Platform::ColorGroup::Disabled);
    draw_list->AddRect(bar_min, bar_max, border_color, bar_rounding, 0, m_border_thickness.result);

    const float marker_x = pos.x() + this->marker_x_for_ratio(size.x());
    const ImVec2 handle_min{marker_x - handle_width / 2.f, pos.y()};
    const ImVec2 handle_max{marker_x + handle_width / 2.f, pos.y() + size.y()};

    draw_list->AddRectFilled(
        ImVec2(handle_min.x + shadow_offset, handle_min.y + shadow_offset),
        ImVec2(handle_max.x + shadow_offset, handle_max.y + shadow_offset),
        ImColor(0, 0, 0, 110),
        handle_rounding
    );
    draw_list->AddRectFilled(handle_min, handle_max, ImColor(255, 255, 255), handle_rounding);
}

} // namespace Slic3r::App::ColorMix
