#include "Slic3r/App/Yoga/StrikethroughText.hpp"

#include "Slic3r/App/Render/ImguiRender.hpp"

#include <imgui_internal.h>

namespace Slic3r::App::Yoga {

StrikethroughText::StrikethroughText(const std::string& text, Render::ImguiFontType font_type) :
    Text(text, font_type)
{
    set_object_name("StrikethroughText");
}

void StrikethroughText::render(const Vec2f& pos, const Vec2f& size)
{
    Text::render(pos, size);

    if (!m_strikethrough) {
        return;
    }

    const ImRect       text_rect      = taken_rect(size);
    const std::string& rendered       = rendered_text();
    if (rendered.empty()) {
        return;
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ASSERT(draw_list);

    ImFont*     font        = m_imgui_render->font(font_type());
    const float font_size   = used_font_size();
    const ImVec2 block_pos  = to_im(pos) + text_rect.Min;
    const float line_height = font_size;
    const float thickness   = std::max(1.f, 0.08f * line_height);

    const ImColor color = enabled() ?
        text_color() :
        m_theme->color_imgui(Platform::Color::Text, Platform::ColorGroup::Disabled);

    // The wrap width replicates Text::render (PushTextWrapPos(GetCursorPos().x + size.x()))
    const bool  word_wrap  = wrap_mode() != Text::WrapMode::NoWrap && size.x() > 0.f;
    const float wrap_width = size.x();

    ImGui::PushFont(font, font_size);

    const char*       s        = rendered.c_str();
    const char* const text_end = s + rendered.size();
    float             y        = block_pos.y;

    while (s < text_end) {
        const char* line_end;
        if (word_wrap) {
            // Same splitter as ImFont::RenderTextWrapping uses
            line_end = ImFontCalcWordWrapPositionEx(
                font, font_size, s, text_end, wrap_width, ImDrawTextFlags_None
            );
        } else {
            line_end = static_cast<const char*>(ImMemchr(s, '\n', text_end - s));
            if (!line_end) {
                line_end = text_end;
            }
        }

        const float line_width = ImGui::CalcTextSize(s, line_end).x;
        const float strike_y    = y + 0.5f * line_height;
        draw_list->AddLine(
            ImVec2(block_pos.x, strike_y),
            ImVec2(block_pos.x + line_width, strike_y),
            color,
            thickness
        );

        if (word_wrap) {
            // Wrapping skips upcoming blanks
            s = ImTextCalcWordWrapNextLineStart(line_end, text_end, ImDrawTextFlags_None);
        } else {
            s = (*line_end == '\n') ? line_end + 1 : text_end;
        }

        y += line_height;
    }

    ImGui::PopFont();
}

bool StrikethroughText::strikethrough() const
{
    return m_strikethrough;
}

void StrikethroughText::set_strikethrough(bool strikethrough)
{
    m_strikethrough = strikethrough;
}

} // namespace Slic3r::App::Yoga
