#include "Slic3r/App/Yoga/TwoColorRing.hpp"
#include "imgui_internal.h"
#include <numbers>

namespace Slic3r::App::Yoga {

void TwoColorRing::render(const Vec2f& pos, const Vec2f& size)
{
    render_item_begin(pos, size);

    ImRect rect{to_im(pos), to_im(pos + size)};

    ImDrawList* draw_list{ImGui::GetWindowDrawList()};
    ASSERT(draw_list);

    const ImVec2 center{rect.GetCenter()};
    const float ring_thickness{3};

    const float radius{
        0.5f * (rect.GetWidth() < rect.GetHeight() ? rect.GetWidth() : rect.GetHeight())
        - 0.5f * ring_thickness
    };
    if (radius > 0.0f) {
        const ImColor transparent{m_theme->color_imgui(Platform::Color::Transparent)};
        constexpr float quarter_pi{float(std::numbers::pi / 4.0)};

        ImColor first_color{transparent};
        ImColor second_color{transparent};

        if (primary_color && secondary_color) {
            first_color  = *primary_color;
            second_color = *secondary_color;
        } else if (primary_color) {
            first_color  = *primary_color;
            second_color = *primary_color;
        } else if (secondary_color) {
            first_color  = *secondary_color;
            second_color = *secondary_color;
        }

        draw_list->PathArcTo(center, radius, -5 * quarter_pi, -quarter_pi, 36);
        draw_list->PathStroke(first_color, ImDrawFlags_None, ring_thickness);

        draw_list->PathArcTo(center, radius, -quarter_pi, 3 * quarter_pi, 36);
        draw_list->PathStroke(second_color, ImDrawFlags_None, ring_thickness);
    }

    render_item_end(pos, size);
}

} // namespace Slic3r::App::Yoga
