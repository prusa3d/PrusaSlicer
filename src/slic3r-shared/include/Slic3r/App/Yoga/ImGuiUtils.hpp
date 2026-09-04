#pragma once

#include <imgui.h>

#include <cstddef>

namespace Slic3r::App::Yoga {

/**
 * @note copied from imgui internals, we need our custom styling
 */
void YGRenderArrow(ImDrawList* draw_list, ImVec2 pos, ImVec2 size, ImU32 col, ImGuiDir dir, float scale);

/// Applies the styling every root item is rendered with, for the lifetime of the instance.
struct SetOurStyleVars
{
    SetOurStyleVars()
    {
        PushStyleVar(ImGuiStyleVar_WindowRounding, 5.f);
        PushStyleVar(ImGuiStyleVar_FrameRounding, 3.f);
        PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.f, 0.f));
        PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.f, 6.f));
        PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(2.f, 0.f));
    }

    ~SetOurStyleVars()
    {
        ImGui::PopStyleVar(static_cast<int>(m_vars_cnt));
    }

private:
    void PushStyleVar(ImGuiStyleVar idx, float val)
    {
        ImGui::PushStyleVar(idx, val);
        m_vars_cnt++;
    }

    void PushStyleVar(ImGuiStyleVar idx, const ImVec2& val)
    {
        ImGui::PushStyleVar(idx, val);
        m_vars_cnt++;
    }

    size_t m_vars_cnt{0};
};

} // namespace Slic3r::App::Yoga
