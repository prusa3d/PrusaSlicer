#include "Slic3r/App/Preview/GCodeDisplay.hpp"

#include "Slic3r/App/Preview/GCodeWindow.hpp"
#include <Slic3r/App/libvgcode/FdmViewer.hpp>

#include "Slic3r/Biz/I18N/I18N.hpp"

#include <imgui/imgui_internal.h>

namespace Slic3r::App::Preview {

static ImVec4 id_color() { return { 0.99f, 0.41f, 0.2f, 1.0f }; } // color from SidebarAfterSlice::render()
static ImVec4 cmd_color() { return { 0.8f, 0.8f, 0.0f, 1.0f }; }
static ImVec4 params_color() { return ImGui::GetStyleColorVec4(ImGuiCol_Text); }
static ImVec4 comment_color() { return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled); }
static ImVec4 ellipsis_color() { return { 0.0f, 0.7f, 0.0f, 1.0f }; }
static ImVec4 highlight_bg_color() { return { 0.4f, 0.4f, 0.4f, 1.0f }; }
static ImVec4 no_highlight_bg_color() { return ImGui::GetStyleColorVec4(ImGuiCol_TableRowBg); }

static std::string_view reduce_string(const std::string_view src, size_t current_length)
{
    static const size_t LENGTH_THRESHOLD = 60;
    return (current_length + src.length() > LENGTH_THRESHOLD) ? src.substr(0, LENGTH_THRESHOLD - current_length) : src;
}

GCodeDisplay::GCodeDisplay(libvgcode::FdmViewer* viewer, GCodeWindowData* data) :
    Yoga::Item(),
    m_viewer(viewer),
    m_data(data)
{
    set_object_name("GCodeDisplay");
}

void GCodeDisplay::render(const Yoga::Vec2f& pos, const Yoga::Vec2f& size)
{
    render_item_begin(pos, size);

    ImGuiWindow* window = ImGui::GetCurrentWindow();

    if (!m_data->has_data()) {
        static std::string msg = Biz::_u8L("No data available");
        ImGui::RenderText(to_im(pos), msg.c_str());
    }
    else {
        ImGui::SetCursorScreenPos(to_im(pos));
        float line_height = ImGui::GetTextLineHeight();
        const ImGuiStyle& style = ImGui::GetStyle();
        float cell_height = line_height + 2.0f * style.CellPadding.y;
        float available_height = size.y();

        size_t curr_line_id = size_t(m_viewer->current_vertex().gcode_id);

        const uint32_t visible_lines_count = uint32_t(available_height / cell_height);
        if (visible_lines_count == 0)
            return;

        // visible range
        GCodeWindowData::Range visible_range;
        m_data->resize_range(visible_range, visible_lines_count, curr_line_id);

        float top_padding_y = available_height - float(visible_lines_count) * cell_height;
        window->DC.CursorPos.y += top_padding_y;

        if (ImGui::BeginTable("GCodeLines", 2, ImGuiTableFlags_SizingFixedFit)) {
            float max_id_width = ImGui::CalcTextSize(std::to_string(*visible_range.max).c_str()).x;
            for (uint32_t id = *visible_range.min; id <= *visible_range.max; ++id) {
                ImGui::TableNextRow();
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                    ImGui::GetColorU32((id == curr_line_id) ? highlight_bg_color() : no_highlight_bg_color()));

                ImGui::TableSetColumnIndex(0);
                // spacer to right align text
                float id_width = ImGui::CalcTextSize(std::to_string(id).c_str()).x;
                if (id_width < max_id_width) {
                    ImGui::Dummy({ max_id_width - id_width, line_height });
                    ImGui::SameLine(0.0f, 0.0f);
                }
                ImGui::TextColored(id_color(), "%u", id);

                const GCodeWindowData::Line& line = m_data->line_at(id - 1);
                if (!line.empty()) {
                    ImGui::TableSetColumnIndex(1);

                    size_t current_length = 0;
                    bool show_ellipsis = false;
                    if (!line.command.empty()) {
                        std::string_view str = m_clip_text ? reduce_string(line.command, current_length) : line.command;
                        ImGui::TextColored(cmd_color(), "%s", std::string(str).c_str());
                        current_length += str.length() + 1;
                        if (m_clip_text) show_ellipsis = str != line.command;
                    }
                    if (!show_ellipsis && !line.parameters.empty()) {
                        if (current_length > 0)
                            ImGui::SameLine(0.0f, 0.0f);
                        std::string_view str = m_clip_text ? reduce_string(line.parameters, current_length) : line.parameters;
                        ImGui::TextColored(params_color(), "%s", std::string(str).c_str());
                        current_length += str.length() + 1;
                        if (m_clip_text) show_ellipsis = str != line.parameters;
                    }
                    if (!show_ellipsis && !line.comment.empty()) {
                        if (current_length > 0)
                            ImGui::SameLine(0.0f, 0.0f);
                        std::string_view str = m_clip_text ? reduce_string(line.comment, current_length) : line.comment;
                        ImGui::TextColored(comment_color(), "%s", std::string(str).c_str());
                        if (m_clip_text) show_ellipsis = str != line.comment;
                    }
                    if (show_ellipsis) {
                        ImGui::SameLine(0.0f, 0.0f);
                        ImGui::TextColored(ellipsis_color(), "...");
                    }
                }
            }

            ImGui::EndTable();
        }
    }

    render_item_end(pos, size);
}

void GCodeDisplay::set_clip_text(bool clip_text) { m_clip_text = clip_text; }

} // namespace Slic3r::App::Preview
