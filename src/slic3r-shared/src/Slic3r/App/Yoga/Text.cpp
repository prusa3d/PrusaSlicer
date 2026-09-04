#include "Slic3r/App/Yoga/Text.hpp"

#include "Slic3r/App/Render/ImguiRender.hpp"

#include "Slic3r/Log.hpp"

#include <imgui_internal.h>

namespace Slic3r::App::Yoga {

static bool utf8_last_byte_is_ascii_space(std::string_view s)
{
    return !s.empty() && std::isspace(static_cast<unsigned char>(s.back()));
}

static void utf8_remove_last_codepoint(std::string_view& s)
{
    if (s.empty()) {
        return;
    }

    size_t i = s.size() - 1;
    while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) {
        --i;
    }
    s.remove_suffix(s.size() - i);
}

static ImRect align_rectangle(const ImVec2& space, const ImVec2& aligned_size, const Align& align)
{
    ImVec2 pos{0.0f, 0.0f};

    switch (align.horizontal) {
    case AlignH::Left:
        pos.x = 0.0f;
        break;
    case AlignH::Center:
        pos.x = (space.x - aligned_size.x) * 0.5f;
        break;
    case AlignH::Right:
        pos.x = space.x - aligned_size.x;
        break;
    }

    switch (align.vertical) {
    case AlignV::Top:
        pos.y = 0.0f;
        break;
    case AlignV::Center:
        pos.y = (space.y - aligned_size.y) * 0.5f;
        break;
    case AlignV::Bottom:
        pos.y = space.y - aligned_size.y;
        break;
    }

    return ImRect(pos, pos + aligned_size);
}

Text::Text(const std::string& text, Render::ImguiFontType font_type) :
    m_text_color(m_theme->color_imgui(Platform::Color::Text)),
    m_font_type(font_type)
{
    set_object_name("Text");
    set_text(text);
}

void Text::render(const Vec2f& pos, const Vec2f& size)
{
    render_item_begin(pos, size);

    const ImVec2 text_pos = align_rectangle(to_im(size), m_taken_size, m_align).GetTL();
    ImGui::SetCursorScreenPos(to_im(pos) + text_pos);

    ImGui::PushFont(m_imgui_render->font(m_font_type), used_font_size());
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        enabled() ?
            ImVec4(m_text_color) :
            ImVec4(m_theme->color_imgui(Platform::Color::Text, Platform::ColorGroup::Disabled))
    );

    switch (m_wrap_mode) {
    case Text::WrapMode::Wrap:
    case Text::WrapMode::WrapElide:
        // When text wrapping is applied, the text gets truncated on the right due to its offset from the parent window.
        // To fix this, we need to increase wrapping_size by ImGui::GetCursorPos().x.
        // Note: This feels like a hack, but a similar use of PushTextWrapPos can be found in imgui_demo.cpp (line 1281).
        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + size.x());
        ImGui::TextUnformatted(m_rendered_text.c_str());
        ImGui::PopTextWrapPos();
        break;
    case Text::WrapMode::NoWrap:
        ImGui::TextUnformatted(m_rendered_text.c_str());
        break;
    }

    ImGui::PopStyleColor();
    ImGui::PopFont();

    render_item_end(pos, size);
}

const std::string& Text::text() const
{
    return m_source_text;
}

void Text::set_text(const std::string& text)
{
    if (m_source_text != text) {
        m_source_text = text;
        invalidate_min_size_calculation();
    }
}

const Align& Text::align() const
{
    return m_align;
}

void Text::set_align(const Align& align)
{
    if (m_align != align) {
        m_align = align;
        invalidate_min_size_calculation();
    }
}

void Text::on_resized()
{
    invalidate_min_size_calculation();
}

Vec2f Text::get_item_size()
{
    Vec2f min_size;
    ImGui::PushFont(m_imgui_render ? m_imgui_render->font(m_font_type) : nullptr, used_font_size());

    ImVec2 taken_size;
    if (m_wrap_mode == WrapMode::NoWrap) {
        m_rendered_text = m_source_text;
        taken_size      = ImGui::CalcTextSize(m_source_text.c_str());
        min_size        = from_im(taken_size);
    } else if (m_wrap_mode == WrapMode::Wrap) {
        m_rendered_text = m_source_text;

        const float target_width = available_width();
        // While wrapping enforce ONLY Y axis, let as assume the width that is set by parent or
        // external sources
        if (YGFloatIsUndefined(target_width) || target_width <= 0) {
            // We should log probably something, this is an error state
            taken_size = {0, ImGui::CalcTextSize(m_source_text.c_str()).y};
        } else {
            taken_size = {
                target_width,
                from_im(ImGui::CalcTextSize(m_source_text.c_str(), nullptr, false, target_width))
                    .y()
            };
        }

        min_size = Vec2f{0, taken_size.y};
    } else if (m_wrap_mode == WrapMode::WrapElide) {
        ImVec2 target_size{available_width(), available_height()};
        if (!YGFloatIsUndefined(target_size.x)
            && !YGFloatIsUndefined(target_size.y)
            && target_size.x > 0
            && target_size.y > 0)
        {
            target_size.y = std::max(used_font_size(), target_size.y);

            std::string_view elided_text = m_source_text;
            m_rendered_text              = elided_text;
            ImVec2 current_size          = ImGui::CalcTextSize(
                elided_text.data(),
                elided_text.data() + elided_text.size(),
                false,
                target_size.x
            );
            // As long as the text do not fit target_size keep removing characters from the end
            while ((current_size.y > target_size.y || current_size.x > target_size.x)
                   && !elided_text.empty())
            {
                utf8_remove_last_codepoint(elided_text);
                // also remove trailing whitespaces
                while (!elided_text.empty() && utf8_last_byte_is_ascii_space(elided_text)) {
                    utf8_remove_last_codepoint(elided_text);
                }

                m_rendered_text = std::string(elided_text) + "…";
                current_size =
                    ImGui::CalcTextSize(m_rendered_text.c_str(), nullptr, false, target_size.x);
            }

            taken_size = current_size;
        }
        min_size = Vec2f{0, used_font_size()};
    }

    m_taken_size = taken_size;
    set_style_dirty();

    ImGui::PopFont();

    return min_size;
}

float Text::available_width() const
{
    // There may be a hidden bug, it can happen that if we query
    // width() instead of m_last_width we get wrong anser (such as 0)
    // Investigate why Yoga reports 0
    return m_last_width - padding().result_horizontal();
}

float Text::available_height() const
{
    const float avail_height = m_last_height - padding().result_vertical();
    return Domain::fuzzy_compare(0.f, avail_height) ? used_font_size() : avail_height;
}

float Text::used_font_size() const
{
    return m_font_size.has_value() ? m_font_size.value().result : ImGui::GetFontSize();
}

void Text::size_info_changed(const SizeInfo& size_info)
{
    if (m_font_size.has_value()) {
        m_font_size.value().evaluate(size_info);
    }
}

Text::WrapMode Text::wrap_mode() const
{
    return m_wrap_mode;
}

void Text::set_wrap_mode(WrapMode wrap_mode)
{
    if (m_wrap_mode != wrap_mode) {
        m_wrap_mode = wrap_mode;
        invalidate_min_size_calculation();
    }
}

const ImColor& Text::text_color() const
{
    return m_text_color;
}

void Text::set_text_color(const ImColor& text_color)
{
    m_text_color = text_color;
    set_style_dirty();
}

Render::ImguiFontType Text::font_type() const
{
    return m_font_type;
}

void Text::set_font_type(Render::ImguiFontType font_type)
{
    if (m_font_type != font_type) {
        m_font_type = font_type;
        set_style_dirty();
    }
}

Unit Text::font_size() const
{
    return m_font_size.has_value() ? m_font_size.value().source : 1_rem;
}

void Text::set_font_size(const Unit& font_size)
{
    if (!m_font_size.has_value() || m_font_size.value().source != font_size) {
        m_font_size = EvaluatedUnit{font_size};
        invalidate_min_size_calculation();
    }
}

} // namespace Slic3r::App::Yoga
