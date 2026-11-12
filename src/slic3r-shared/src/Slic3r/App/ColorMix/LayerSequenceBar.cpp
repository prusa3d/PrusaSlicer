#include "Slic3r/App/ColorMix/LayerSequenceBar.hpp"

#include <algorithm>
#include <utility>

using namespace Slic3r::App::Yoga;

namespace Slic3r::App::ColorMix {

// Short cycles are repeated to at least this many cells so that the pattern reads.
const constexpr int MIN_CELL_COUNT = 28;

LayerSequenceBar::LayerSequenceBar() :
    AbstractButton({}, "LayerSequenceBar"),
    m_cell_gap{1_fpx},
    m_bar_rounding{3_fpx},
    m_border_thickness{1_fpx}
{
    this->set_min_width(240_fpx);
    this->set_height(16_fpx);
    this->set_flex_shrink(0);
}

void LayerSequenceBar::size_info_changed(const Yoga::SizeInfo& info_size)
{
    m_cell_gap.evaluate(info_size);
    m_bar_rounding.evaluate(info_size);
    m_border_thickness.evaluate(info_size);
}

void LayerSequenceBar::set_cycle(std::vector<ImColor> cycle)
{
    m_cycle = std::move(cycle);
    this->set_style_dirty();
}

void LayerSequenceBar::render(const Vec2f& pos, const Vec2f& size)
{
    const float cell_gap         = m_cell_gap.result;
    const float bar_rounding     = m_bar_rounding.result;
    const float border_thickness = m_border_thickness.result;

    AbstractButton::render(pos, size);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ASSERT(draw_list);

    const ImVec2 rect_min = to_im(pos);
    const ImVec2 rect_max = to_im(pos + size);

    if (m_cycle.empty()) {
        draw_list->AddRectFilled(
            rect_min,
            rect_max,
            m_theme->color_imgui(Platform::Color::Button, Platform::ColorGroup::Disabled),
            bar_rounding
        );
    } else {
        const int cell_count = std::max<int>(MIN_CELL_COUNT, static_cast<int>(m_cycle.size()));
        for (int i = 0; i < cell_count; ++i) {
            const bool is_first = i == 0;
            const bool is_last  = i + 1 == cell_count;
            const float x0 =
                rect_min.x + static_cast<float>(i) * size.x() / static_cast<float>(cell_count);
            const float x1 =
                rect_min.x + static_cast<float>(i + 1) * size.x() / static_cast<float>(cell_count);
            draw_list->AddRectFilled(
                ImVec2(x0, rect_min.y),
                ImVec2(is_last ? rect_max.x : std::max(x0, x1 - cell_gap), rect_max.y),
                m_cycle[static_cast<size_t>(i) % m_cycle.size()],
                is_first || is_last ? bar_rounding : 0.f,
                is_first ? ImDrawFlags_RoundCornersLeft :
                           (is_last ? ImDrawFlags_RoundCornersRight : ImDrawFlags_RoundCornersNone)
            );
        }
    }

    const ImColor border_color =
        m_theme->color_imgui(Platform::Color::Text, Platform::ColorGroup::Disabled);

    draw_list->AddRect(rect_min, rect_max, border_color, bar_rounding, 0, border_thickness);
}

} // namespace Slic3r::App::ColorMix
