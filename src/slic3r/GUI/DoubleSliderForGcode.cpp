///|/ Copyright (c) Prusa Research 2020 - 2023 Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv, Tomáš Mészáros @tamasmeszaros, Lukáš Matěna @lukasmatena, Enrico Turri @enricoturri1966
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "DoubleSliderForGcode.hpp"

#include <algorithm>

namespace DoubleSlider {

static const float LEFT_MARGIN             = 13.0f + 100.0f;  // avoid thumbnail toolbar
static const float HORIZONTAL_SLIDER_HEIGHT = 40.0f;

void DSForGcode::Render(const int canvas_width, const int canvas_height, float extra_scale/* = 0.1f*/, float offset/* = 0.f*/)
{
    if (!m_ctrl.IsShown())
        return;
    m_scale = extra_scale * 0.1f * m_em;

    ImVec2 pos  = ImVec2{std::max(LEFT_MARGIN, 0.2f * canvas_width), canvas_height - HORIZONTAL_SLIDER_HEIGHT * m_scale};
    ImVec2 size = ImVec2(canvas_width - 2 * pos.x, HORIZONTAL_SLIDER_HEIGHT * m_scale);

    m_ctrl.Init(pos, size, m_scale);
    if (m_ctrl.render())
        process_thumb_move();
}

} // DoubleSlider


