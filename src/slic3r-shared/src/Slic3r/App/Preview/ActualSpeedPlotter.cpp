#include "Slic3r/App/Preview/ActualSpeedPlotter.hpp"
#include "Slic3r/App/Imgui/ImguiExtension.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Domain/Color.hpp"

namespace Slic3r::App::Preview {

static const ImVec4 GRID_MAIN_COLOR      = { 0.5f, 0.5f, 0.5f, 0.5f };
static const ImVec4 GRID_SECONDARY_COLOR = { 0.0f, 0.0f, 0.5f, 0.5f };
static const ImVec4 PROFILE_BASE_COLOR   = { 0.8f, 0.8f, 0.8f, 1.0f };

int plot_actual_speed_profile(const ActualSpeedPlotData& data, const ImVec2& size)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return -1;

    static const char* title = "##ActualSpeedProfile";

    Imgui::ScopedGroup group((std::string(window->Name) + "ActualSpeedProfilePlot").c_str());

    const ImGuiStyle& style = ImGui::GetStyle();
    const ImGuiIO& io = ImGui::GetIO();

    float label_height = ImGui::GetTextLineHeight();
    ImVec2 internal_frame_size = size;
    internal_frame_size = ImGui::CalcItemSize(internal_frame_size, ImGui::CalcItemWidth(), 
        label_height + style.FramePadding.y * 2.0f);

    ImRect frame_bb(window->DC.CursorPos, window->DC.CursorPos + internal_frame_size);
    ImRect inner_bb(frame_bb.Min + style.FramePadding, frame_bb.Max - style.FramePadding);
    ImRect total_bb(frame_bb.Min, frame_bb.Max);
    ImGui::ItemSize(total_bb, style.FramePadding.y);
    if (!ImGui::ItemAdd(total_bb, 0, &frame_bb))
        return -1;

    ImGuiID id = window->GetID(title);
    bool hovered = ImGui::ItemHoverable(frame_bb, id, ImGuiItemFlags_None);

    ImGui::RenderFrame(frame_bb.Min, frame_bb.Max, ImGui::GetColorU32(ImGuiCol_FrameBg), true,
        style.FrameRounding);

    int values_count = int(data.data.size());
    int idx_hovered = -1;

    if (values_count < 2 || data.data.back().position - data.data.front().position <= 0.0f) {
        static const std::string msg = Biz::_u8L("No data available");
        ImGui::RenderText(frame_bb.Min + (frame_bb.GetSize() - ImGui::CalcTextSize(msg.c_str())) * 0.5f, msg.c_str());
    }
    else {
        ImVec2 offset(10.0f, 0.0f);
        float size_y = data.y_range[1] - data.y_range[0];
        float size_x = data.data.back().position - data.data.front().position;

        float inv_scale_y = (size_y == 0.0f) ? 0.0f : 1.0f / size_y;
        float inv_scale_x = 1.0f / size_x;
        float x0 = data.data.front().position;
        float y0 = data.y_range[0];

        ImU32 grid_main_color = ImGui::GetColorU32(GRID_MAIN_COLOR);
        ImU32 grid_secondary_color = ImGui::GetColorU32(GRID_SECONDARY_COLOR);

        // horizontal levels
        for (const auto& [level, color] : data.levels) {
            float y = 1.0f - ImSaturate((level - data.y_range[0]) * inv_scale_y);

            window->DrawList->AddLine(ImLerp(inner_bb.Min, { inner_bb.Min.x + offset.x, inner_bb.Max.y }, { 0.1f, y }),
                ImLerp(inner_bb.Min, { inner_bb.Min.x + offset.x, inner_bb.Max.y }, { 0.9f, y }),
                    App::Imgui::to_ImU32(color, 255), 5.0f);

            window->DrawList->AddLine(ImLerp(inner_bb.Min + offset, inner_bb.Max, { 0.0f, y }),
                ImLerp(inner_bb.Min + offset, inner_bb.Max, { 1.0f, y }), grid_main_color);
        }

        // vertical positions
        for (int n = 0; n < values_count - 1; ++n) {
            float x = ImSaturate((data.data[n].position - x0) * inv_scale_x);
            window->DrawList->AddLine(ImLerp(inner_bb.Min + offset, inner_bb.Max, { x, 0.0f }),
                ImLerp(inner_bb.Min + offset, inner_bb.Max, { x, 1.0f }),
                    data.data[n].is_internal ? grid_secondary_color : grid_main_color);
        }
        window->DrawList->AddLine(ImLerp(inner_bb.Min + offset, inner_bb.Max, { 1.0f, 0.0f }),
            ImLerp(inner_bb.Min + offset, inner_bb.Max, { 1.0f, 1.0f }), grid_main_color);

        // profile
        ImU32 col_base = ImGui::GetColorU32(ImGuiCol_PlotLines);
        ImU32 col_hovered = ImGui::GetColorU32(ImGuiCol_PlotLinesHovered);
        for (int n = 0; n < values_count - 1; ++n) {
            ImVec2 tp1(ImSaturate((data.data[n].position - x0) * inv_scale_x),
                1.0f - ImSaturate((data.data[n].speed - y0) * inv_scale_y));
            ImVec2 tp2(ImSaturate((data.data[n + 1].position - x0) * inv_scale_x),
                1.0f - ImSaturate((data.data[n + 1].speed - y0) * inv_scale_y));
            if (hovered && inner_bb.Contains(io.MousePos)) {
                float t = ImClamp((io.MousePos.x - inner_bb.Min.x - offset.x) / (inner_bb.Max.x - inner_bb.Min.x - offset.x),
                    0.0f, 0.9999f);
                if (tp1.x < t && t < tp2.x)
                    idx_hovered = n;
            }
            window->DrawList->AddLine(ImLerp(inner_bb.Min + offset, inner_bb.Max, tp1),
                ImLerp(inner_bb.Min + offset, inner_bb.Max, tp2),
                    idx_hovered == n ? col_hovered : col_base, 2.0f);
        }
    }

    return idx_hovered;
}

} // namespace Slic3r::App::Preview
