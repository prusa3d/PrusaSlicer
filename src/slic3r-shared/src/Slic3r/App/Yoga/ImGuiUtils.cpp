#include "Slic3r/App/Yoga/ImGuiUtils.hpp"
#include <imgui_internal.h>

namespace Slic3r::App::Yoga {

/**
 * @note copied from imgui internals, we need our custom styling
 */
void YGRenderArrow(ImDrawList* draw_list, ImVec2 pos, ImVec2 size, ImU32 col, ImGuiDir dir, float scale)
{
    // clang-format off
    float r = GImGui->FontSize * 0.35f * scale;

    const float w = size.x * scale;
    const float h = size.y * scale;
    
    ImVec2 center = pos + ImVec2(w * 0.50f, h * 0.5f);
    float center_shift = 0.5f * r * 0.75f;

    ImVec2 a, b, c;
    switch (dir)
    {
    case ImGuiDir_Up:
    case ImGuiDir_Down:
        if (dir == ImGuiDir_Up) {
            r = -r;
            center.y += center_shift;
        }
        else {
            center.y -= center_shift;
        }
        b = ImVec2(+0.000f,+0.750f) * r;
        a = ImVec2(-0.750f,-0.00f) * r;
        c = ImVec2(+0.750f,-0.00f) * r;
        break;
    case ImGuiDir_Left:
    case ImGuiDir_Right:
        if (dir == ImGuiDir_Left) {
            r = -r;
            center.x += center_shift;
        }
        else {
            center.x -= center_shift;
        }
        b = ImVec2(+0.750f,+0.000f) * r;
        c = ImVec2(-0.000f,+0.750f) * r;
        a = ImVec2(-0.000f,-0.750f) * r;
        break;
    case ImGuiDir_None:
    case ImGuiDir_COUNT:
        IM_ASSERT(0);
        break;
    }

    if ((col & IM_COL32_A_MASK) == 0)
        return;

    draw_list->PathLineTo(center + a);
    draw_list->PathLineTo(center + b);
    draw_list->PathLineTo(center + c);
    draw_list->PathStroke(col, false, 1.f);
    // clang-format on
}

} // namespace Slic3r::App::Yoga
