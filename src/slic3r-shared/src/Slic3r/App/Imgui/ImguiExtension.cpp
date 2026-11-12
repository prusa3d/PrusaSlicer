#include "Slic3r/App/Imgui/ImguiExtension.hpp"

#include "Slic3r/App/Imgui/DoubleSlider.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/Domain/Color.hpp"

#include <boost/nowide/convert.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace Slic3r::App::Imgui {

static constexpr float TWO_PI = 2.0f * float(IM_PI);

void UnifiedWindowStyle::push()
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {8.0f, 8.0f});
    ImGui::SetNextWindowBgAlpha(DEFAULT_WINDOW_BG_ALPHA);
}

void UnifiedWindowStyle::pop()
{
    ImGui::PopStyleVar(3);
}

void tooltip(const char* label, float wrap_width)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {6.0f, 6.0f});
    ImGui::SetNextWindowBgAlpha(DEFAULT_WINDOW_BG_ALPHA);
    ImGui::BeginTooltip();
    if (wrap_width > 0.0f)
        ImGui::PushTextWrapPos(wrap_width);
    ImGui::Text("%s", label);
    if (wrap_width > 0.0f)
        ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
    ImGui::PopStyleVar(2);
}

void tooltip(const std::string& label, float wrap_width)
{
    tooltip(label.c_str(), wrap_width);
}

static bool IsRootOfOpenMenuSet()
{
    ImGuiContext& g     = *ImGui::GetCurrentContext();
    ImGuiWindow* window = g.CurrentWindow;
    if ((g.OpenPopupStack.Size <= g.BeginPopupStack.Size)
        || (window->Flags & ImGuiWindowFlags_ChildMenu))
        return false;

    // Initially we used 'upper_popup->OpenParentId == window->IDStack.back()' to differentiate
    // multiple menu sets from each others (e.g. inside menu bar vs loose menu items) based on
    // parent ID. This would however prevent the use of e.g. PushID() user code submitting menus. Previously
    // this worked between popup and a first child menu because the first child menu always had the
    // _ChildWindow flag, making hovering on parent popup possible while first child menu was
    // focused - but this was generally a bug with other side effects. Instead we don't treat Popup
    // specifically (in order to consistently support menu features in them), maybe the first child
    // menu of a Popup doesn't have the _ChildWindow flag, and we rely on this IsRootOfOpenMenuSet()
    // check to allow hovering between root window/popup and first child menu. In the end, lack of
    // ID check made it so we could no longer differentiate between separate menu sets. To
    // compensate for that, we at least check parent window nav layer. This fixes the most common
    // case of menu opening on hover when moving between window content and menu bar. Multiple
    // different menu sets in same nav layer would still open on hover, but that should be a lesser
    // problem, because if such menus are close in proximity in window content then it won't feel
    // weird and if they are far apart it likely won't be a problem anyone runs into.
    const ImGuiPopupData* upper_popup = &g.OpenPopupStack[g.BeginPopupStack.Size];
    if (window->DC.NavLayerCurrent != upper_popup->ParentNavLayer)
        return false;
    return upper_popup->Window
        && (upper_popup->Window->Flags & ImGuiWindowFlags_ChildMenu)
        && ImGui::IsWindowChildOf(upper_popup->Window, window, true);
}

// see as reference: bool ImGui::MenuItemEx() in imgui_widgets.cpp
bool menu_item_with_icon(
    const char* label,
    const char* shortcut,
    ImU32 icon_color /* = 0*/,
    bool selected /* = false*/,
    bool enabled /* = true*/
)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g   = *ImGui::GetCurrentContext();
    ImGuiStyle& style = g.Style;
    ImVec2 pos        = window->DC.CursorPos;
    ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);

    // See BeginMenuEx() for comments about this.
    bool menuset_is_open = IsRootOfOpenMenuSet();
    if (menuset_is_open)
        ImGui::PushItemFlag(ImGuiItemFlags_NoWindowHoverableCheck, true);

    // We've been using the equivalent of ImGuiSelectableFlags_SetNavIdOnHover on all Selectable()
    // since early Nav system days (commit 43ee5d73), but I am unsure whether this should be kept at
    // all. For now moved it to be an opt-in feature used by menus only.
    bool pressed = false;
    ImGui::PushID(label);
    if (!enabled)
        ImGui::BeginDisabled();

    // We use ImGuiSelectableFlags_NoSetKeyOwner to allow down on one menu item, move, up on another.
    ImGuiSelectableFlags selectable_flags = ImGuiSelectableFlags_SelectOnRelease
        | ImGuiSelectableFlags_NoSetKeyOwner
        | ImGuiSelectableFlags_SetNavIdOnHover;
    ImGuiMenuColumns* offsets = &window->DC.MenuColumns;
    if (window->DC.LayoutType == ImGuiLayoutType_Horizontal) {
        DEBUG_ASSERT(false); // not implemented yet
        //// Mimic the exact layout spacing of BeginMenu() to allow MenuItem() inside a menu bar,
        /// which is a little misleading but may be useful / Note that in this situation: we don't
        /// render the shortcut, we render a highlight instead of the selected tick mark.
        // float w = label_size.x;
        // window->DC.CursorPos.x += IM_TRUNC(style.ItemSpacing.x * 0.5f);
        // ImVec2 text_pos(window->DC.CursorPos.x + offsets->OffsetLabel, window->DC.CursorPos.y +
        // window->DC.CurrLineTextBaseOffset); ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
        // ImVec2(style.ItemSpacing.x * 2.0f, style.ItemSpacing.y)); pressed = ImGui::Selectable("",
        // selected, selectable_flags, ImVec2(w, 0.0f)); ImGui::PopStyleVar(); if
        // (g.LastItemData.StatusFlags & ImGuiItemStatusFlags_Visible)
        // ImGui::RenderText(text_pos, label);
        // window->DC.CursorPos.x += IM_TRUNC(style.ItemSpacing.x * (-1.0f + 0.5f)); // -1 spacing
        // to compensate the spacing added when Selectable() did a SameLine(). It would also work to
        // call SameLine() ourselves after the PopStyleVar().
    } else {
        // Menu item inside a vertical menu
        // (In a typical menu window where all items are BeginMenu() or MenuItem() calls, extra_w
        // will always be 0.0f.
        // Only when they are other items sticking out we're going to add spacing, yet only
        // register minimum width into the layout system.
        float icon_w    = (icon_color == 0) ? 0.0f : ImGui::GetTextLineHeight();
        float icon_size = (icon_w > 0.0f) ? icon_w + style.ItemInnerSpacing.x : 0.0f;
        float shortcut_w =
            (shortcut && shortcut[0]) ? ImGui::CalcTextSize(shortcut, nullptr).x : 0.0f;
        float checkmark_w = selected ? IM_TRUNC(g.FontSize * 1.20f) : 0.0f;
        float min_w       = window->DC.MenuColumns.DeclColumns(
            icon_size,
            label_size.x,
            shortcut_w,
            checkmark_w
        ); // Feedback for next frame
        float stretch_w              = ImMax(0.0f, ImGui::GetContentRegionAvail().x - min_w);
        unsigned int spacing_counter = 0;
        if (icon_w > 0.0f)
            ++spacing_counter;
        if (shortcut != nullptr)
            ++spacing_counter;
        if (selected)
            ++spacing_counter;
        ImVec2 item_size(
            label_size.x
                + icon_w
                + shortcut_w
                + checkmark_w
                + float(spacing_counter) * style.FramePadding.x,
            0.0f
        );
        pressed = ImGui::Selectable(
            "",
            false,
            selectable_flags | ImGuiSelectableFlags_SpanAvailWidth,
            ImVec2(min_w, label_size.y)
        );
        if (g.LastItemData.StatusFlags & ImGuiItemStatusFlags_Visible) {
            if (icon_w > 0.0f)
                ImGui::RenderFrame(pos, pos + ImVec2(icon_w, icon_w), icon_color);

            ImGui::RenderText(pos + ImVec2(offsets->OffsetLabel, 0.0f), label);

            if (shortcut_w > 0.0f) {
                ImGui::PushStyleColor(ImGuiCol_Text, g.Style.Colors[ImGuiCol_TextDisabled]);
                ImGui::RenderText(
                    pos + ImVec2(offsets->OffsetShortcut + stretch_w, 0.0f),
                    shortcut,
                    NULL,
                    false
                );
                ImGui::PopStyleColor();
            }
            if (selected) {
                ImGui::RenderCheckMark(
                    window->DrawList,
                    pos
                        + ImVec2(
                            offsets->OffsetMark + stretch_w + g.FontSize * 0.40f,
                            g.FontSize * 0.134f * 0.5f
                        ),
                    ImGui::GetColorU32(enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled),
                    g.FontSize * 0.866f
                );
            }
        }
    }
    IMGUI_TEST_ENGINE_ITEM_INFO(
        g.LastItemData.ID,
        label,
        g.LastItemData.StatusFlags
            | ImGuiItemStatusFlags_Checkable
            | (selected ? ImGuiItemStatusFlags_Checked : 0)
    );
    if (!enabled)
        ImGui::EndDisabled();
    ImGui::PopID();
    if (menuset_is_open)
        ImGui::PopItemFlag();

    return pressed;
}

void icon_image(Render::Icon icon, const ImVec2& size, bool disabled)
{
    ImFontBaked* font = ImGui::GetFontBaked();
    float h           = ImGui::GetTextLineHeight();
    ImVec2 rect       = size;
    if (rect.x == 0.0f) {
        rect.x = h;
    }
    if (rect.y == 0.0f) {
        rect.y = h;
    }
    const ImFontGlyph* glyph = font->FindGlyph(static_cast<wchar_t>(icon));
    if (glyph != nullptr)
        ImGui::ImageWithBg(
            ImGui::GetFont()->OwnerAtlas->TexData->GetTexRef(),
            rect,
            {glyph->U0, glyph->V0},
            {glyph->U1, glyph->V1},
            {0, 0, 0, 0},
            {1, 1, 1, disabled ? 0.6f : 1.f}
        );
}

bool icon_button(Render::Icon icon, const ImVec2& size, const std::string& id)
{
    ImFontBaked* font = ImGui::GetFontBaked();
    float h           = ImGui::GetTextLineHeight();
    ImVec2 rect       = size;
    if (rect.x == 0.0f) {
        rect.x = h;
    }
    if (rect.y == 0.0f) {
        rect.y = h;
    }
    const ImFontGlyph* glyph = font->FindGlyph(static_cast<wchar_t>(icon));
    return (glyph != nullptr) ? ImGui::ImageButton(
                                    ("##btn" + id).c_str(),
                                    ImGui::GetFont()->OwnerAtlas->TexData->GetTexRef(),
                                    rect,
                                    {glyph->U0, glyph->V0},
                                    {glyph->U1, glyph->V1}
                                ) :
                                false;
}

void toggle_button(const std::string& label, bool* on, bool right_align)
{
    DEBUG_ASSERT(on != nullptr);

    // see: https://github.com/ocornut/imgui/issues/1537#issuecomment-355569554 for reference

    float txt_height         = ImGui::GetTextLineHeight();
    float switch_height      = 0.8f * txt_height;
    float switch_width       = switch_height * 2.0f;
    float switch_radius      = switch_height * 0.50f;
    float switch_total_width = switch_width + switch_radius;

    ImVec2 select_size = {switch_total_width + ImGui::CalcTextSize(label.c_str()).x, txt_height};

    if (right_align) {
        ImGui::Dummy(
            {ImGui::GetContentRegionAvail().x - select_size.x - switch_radius, txt_height}
        );
        ImGui::SameLine();
    }

    ImVec2 csp = ImGui::GetCursorScreenPos();

    ImGui::InvisibleButton(label.c_str(), select_size);
    if (ImGui::IsItemClicked())
        *on = !*on;

    float t = *on ? 1.0f : 0.0f;

    ImVec4 col_bg = ImGui::IsItemHovered() ? ImVec4(0.675f, 0.675f, 0.675f, 1.0f) :
        (t == 1.0f)                        ? ImVec4(0.85f, 0.85f, 0.85f, 1.0f) :
                                             ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
    ImVec4 col_knob =
        (t == 1.0f) ? ImVec4(0.31f, 0.51f, 0.97f, 1.0f) : ImVec4(0.2f, 0.2f, 0.2f, 1.0f);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 p              = csp + ImVec2(0.0f, 0.5f * txt_height);
    draw_list->AddRectFilled(
        p,
        ImVec2(p.x + switch_width, p.y + switch_height),
        ImGui::GetColorU32(col_bg),
        switch_height * 0.5f
    );
    ImVec2 knob_center = {
        p.x + switch_radius + t * (switch_width - switch_radius * 2.0f),
        p.y + switch_radius
    };
    draw_list->AddCircleFilled(knob_center, switch_radius - 1.75f, ImGui::GetColorU32(col_knob));

    ImGui::GetCurrentWindow()->DC.CursorPos = csp + ImVec2(switch_total_width, 0.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", label.c_str());
}

ImU32 to_ImU32(const Domain::ColorRGBA& color)
{
    return IM_COL32(color.r_uchar(), color.g_uchar(), color.b_uchar(), color.a_uchar());
}

ImU32 to_ImU32(const Domain::ColorRGB& color, uint8_t alpha)
{
    return IM_COL32(color.r_uchar(), color.g_uchar(), color.b_uchar(), alpha);
}

ImVec4 to_ImVec4(const Domain::ColorRGBA& color)
{
    return {color.r(), color.g(), color.b(), color.a()};
}

ImVec4 to_ImVec4(const Domain::ColorRGB& color, uint8_t alpha)
{
    return {color.r(), color.g(), color.b(), alpha / 255.f};
}


ImColor adjust_brightness(ImColor color, float factor)
{
    float h, s, v;

    // Convert color from RGB to HSV
    ImGui::ColorConvertRGBtoHSV(color.Value.x, color.Value.y, color.Value.z, h, s, v);

    if (factor < 1.0f) {
        // Darken: original behavior.
        v *= factor;
    } else {
        // Brighten: increase V first, (it may clip above 1.0 resulting in no change).
        float adjusted_v = v * factor;

        if (adjusted_v <= 1.0f) {
            v = adjusted_v;
        } else {
            // V clipped, so continue brightening by reducing saturation.
            v = 1.0f;

            float diff = adjusted_v - 1.0f;
            s          = std::clamp(s * (1.0f - diff), 0.0f, 1.0f);
        }
    }

    v = std::clamp(v, 0.0f, 1.0f);

    // Convert back to RGB
    ImVec4 adjustedColor;
    ImGui::ColorConvertHSVtoRGB(h, s, v, adjustedColor.x, adjustedColor.y, adjustedColor.z);
    adjustedColor.w = color.Value.w; // Preserve original alpha

    return ImColor(adjustedColor);
}

void text_with_bg_aligned(float align_x, const std::string& label, ImVec4 bg_color)
{
    using namespace ImGui;
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return;

    ImGuiContext& g         = *GImGui;
    const ImGuiStyle& style = g.Style;

    const bool render_bg = bg_color != ImVec4();
    const float avail_x  = GetContentRegionAvail().x;

    const char *text, *text_end;
    ImFormatStringToTempBuffer(&text, &text_end, "%s", label.c_str());
    const ImVec2 text_size = CalcTextSize(text, text_end);

    ImVec2 pos(window->DC.CursorPos.x, window->DC.CursorPos.y + window->DC.CurrLineTextBaseOffset);
    ImVec2 pos_max(pos.x + avail_x, window->ClipRect.Max.y);
    ImVec2 size(ImMin(avail_x, text_size.x), text_size.y);
    window->DC.CursorMaxPos.x = ImMax(window->DC.CursorMaxPos.x, pos.x + text_size.x);
    window->DC.IdealMaxPos.x  = ImMax(window->DC.IdealMaxPos.x, pos.x + text_size.x);
    if (align_x > 0.0f && text_size.x < avail_x) {
        pos.x += ImTrunc((avail_x - text_size.x) * align_x);
        if (render_bg)
            pos.x -= style.FramePadding.x;
        window->DC.CursorPos = pos;
    }

    if (render_bg) {
        ImRect frame_bb(pos, pos + size);
        frame_bb.Expand(style.FramePadding);
        ImGui::RenderFrame(
            frame_bb.Min,
            frame_bb.Max,
            ImGui::ColorConvertFloat4ToU32(bg_color),
            true,
            style.FrameRounding
        );
    }

    RenderTextEllipsis(window->DrawList, pos, pos_max, pos_max.x, text, text_end, &text_size);

    const ImVec2 backup_max_pos = window->DC.CursorMaxPos;
    ItemSize(size);
    ItemAdd(ImRect(pos, pos + size), 0);
    window->DC.CursorMaxPos.x =
        backup_max_pos
            .x; // Cancel out extending content size because right-aligned text would otherwise mess it up.
}

bool button_aligned(
    float align_x,
    const std::string& label_str,
    const ImVec2& size_arg,
    ImGuiButtonFlags flags
)
{
    using namespace ImGui;
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    float avail_x     = GetContentRegionAvail().x;
    const char* label = label_str.c_str();

    ImGuiContext& g         = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id        = window->GetID(label);
    const ImVec2 label_size = CalcTextSize(label, NULL, true);

    ImVec2 pos = window->DC.CursorPos;
    if (
        (flags & ImGuiButtonFlags_AlignTextBaseLine)
        && style.FramePadding.y < window->DC.CurrLineTextBaseOffset
    ) // Try to vertically align buttons that are smaller/have no padding so that text baseline matches (bit hacky, since it shouldn't be a flag)
        pos.y += window->DC.CurrLineTextBaseOffset - style.FramePadding.y;
    ImVec2 size = CalcItemSize(
        ImVec2(ImMin(avail_x, size_arg.x), size_arg.y),
        label_size.x + style.FramePadding.x * 2.0f,
        label_size.y + style.FramePadding.y * 2.0f
    );

    if (align_x > 0.0f && size.x < avail_x) {
        pos.x += ImTrunc((avail_x - size.x) * align_x);
        window->DC.CursorPos = pos;
    }

    const ImRect bb(pos, pos + size);
    ItemSize(size, style.FramePadding.y);
    if (!ItemAdd(bb, id))
        return false;

    bool hovered, held;
    bool pressed = ButtonBehavior(bb, id, &hovered, &held, flags);

    const ImVec2 backup_max_pos = window->DC.CursorMaxPos;
    // Render
    const ImU32 col = GetColorU32(
        (held && hovered) ? ImGuiCol_ButtonActive :
            hovered       ? ImGuiCol_ButtonHovered :
                            ImGuiCol_Button
    );
    RenderNavCursor(bb, id);
    RenderFrame(bb.Min, bb.Max, col, true, style.FrameRounding);

    if (g.LogEnabled)
        LogSetNextTextDecoration("[", "]");
    RenderTextClipped(
        bb.Min + style.FramePadding,
        bb.Max - style.FramePadding,
        label,
        NULL,
        &label_size,
        style.ButtonTextAlign,
        &bb
    );

    window->DC.CursorMaxPos.x =
        backup_max_pos
            .x; // Cancel out extending content size because right-aligned text would otherwise mess it up.

    IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags);
    return pressed;
}

void colored_circle_marker_aligned(
    float align_x,
    const std::string& label_str,
    const std::vector<ImVec4>& colors,
    const ImVec2& size_arg
)
{
    using namespace ImGui;

    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return;

    float avail_x     = GetContentRegionAvail().x;
    const char* label = label_str.c_str();

    ImGuiContext& g         = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id        = window->GetID(label);
    const ImVec2 label_size = CalcTextSize(label, NULL, true);

    ImVec2 pos = window->DC.CursorPos;
    if (
        style.FramePadding.y < window->DC.CurrLineTextBaseOffset
    ) // Try to vertically align buttons that are smaller/have no padding so that text baseline matches (bit hacky, since it shouldn't be a flag)
        pos.y += window->DC.CurrLineTextBaseOffset - style.FramePadding.y;

    const float height = label_size.y + style.FramePadding.y * 2.0f;
    ImVec2 size = CalcItemSize(ImVec2(ImMin(avail_x, size_arg.x), size_arg.y), height, height);

    if (align_x > 0.0f && size.x < avail_x) {
        pos.x += ImTrunc((avail_x - size.x) * align_x);
        window->DC.CursorPos = pos;
    }

    const ImRect bb(pos, pos + size);
    ItemSize(size, style.FramePadding.y);
    if (!ItemAdd(bb, id))
        return;

    const ImVec2 backup_max_pos = window->DC.CursorMaxPos;
    // Render

    const ImVec2 center = bb.GetCenter();
    const float radius  = 0.5f * height;

    size_t colors_cnt = colors.size();
    float a_min       = 0.5f * float(IM_PI); // 0.f;
    float a_delta     = TWO_PI / colors_cnt;

    for (size_t i = 0; i < colors_cnt; i++) {
        if (colors_cnt != 1)
            window->DrawList->_Path.push_back(center);
        window->DrawList->PathArcTo(center, radius, a_min, a_min + a_delta, 24 / colors_cnt);
        window->DrawList->PathFillConvex(GetColorU32(colors[i]));

        a_min += a_delta;
        if (a_min >= TWO_PI)
            a_min -= TWO_PI;
    }

    if (g.LogEnabled)
        LogSetNextTextDecoration("[", "]");
    RenderTextClipped(
        bb.Min + ImVec2(style.FramePadding.x, 0.f),
        bb.Max - style.FramePadding,
        label,
        NULL,
        &label_size,
        style.ButtonTextAlign,
        &bb
    );

    window->DC.CursorMaxPos.x =
        backup_max_pos
            .x; // Cancel out extending content size because right-aligned text would otherwise mess it up.

    IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags);
}

ScopedStyleColors::ScopedStyleColors(
    std::initializer_list<std::pair<ImGuiCol, ImColor>> initializer_list
)
{
    for (const std::pair<ImGuiCol, ImColor>& color_pair : initializer_list) {
        ImGui::PushStyleColor(color_pair.first, color_pair.second.Value);
    }
    m_count = initializer_list.size();
}

ScopedStyleColors::~ScopedStyleColors()
{
    ImGui::PopStyleColor(m_count);
}

ImVec2 calc_text_size(
    std::string_view text,
    bool hide_text_after_double_hash = false,
    float wrap_width                 = -1.f
)
{
    return ImGui::CalcTextSize(
        text.data(),
        text.data() + text.length(),
        hide_text_after_double_hash,
        wrap_width
    );
}

void move_window_to_bounds(const ImVec2& available_size, ImRect& window)
{
    {
        // clamp window size to available_size
        const ImVec2 window_size = window.GetSize();
        if (window_size.x > available_size.x) {
            window.Max.x -= window_size.x - available_size.x;
        }
        if (window_size.y > available_size.y) {
            window.Max.y -= window_size.y - available_size.y;
        }
    }

    // Move window to range [0,0]-[available_size.x, available_size.y]

    // left
    if (window.Min.x < 0) {
        window.TranslateX(abs(window.Min.x));
    }
    // right
    if (window.Max.x > available_size.x) {
        window.TranslateX(available_size.x - window.Max.x);
    }
    // top
    if (window.Min.y < 0) {
        window.TranslateY(abs(window.Min.y));
    }
    // bottom
    if (window.Max.y > available_size.y) {
        window.TranslateY(available_size.y - window.Max.y);
    }

    window.Min.x = std::round(window.Min.x);
    window.Min.y = std::round(window.Min.y);
    window.Max.x = std::round(window.Max.x);
    window.Max.y = std::round(window.Max.y);
}

bool is_dark(const ImColor color)
{
    float luminance{0.299f * color.Value.x + 0.587f * color.Value.y + 0.114f * color.Value.z};
    return luminance < 0.5f;
}

ImColor contrast_color(ImColor color)
{
    return is_dark(color) ? ImColor{240, 240, 240} : ImColor{10, 10, 10};
}

void draw_dashed_rounded_rect(
    ImDrawList* draw_list,
    const ImVec2& rect_min,
    const ImVec2& rect_max,
    ImU32 color,
    float thickness,
    float rounding,
    float dash_length,
    float gap_length
)
{
    // AddLine() shifts both endpoints by half a pixel, which is exactly the inset AddRect() applies to its minimum.
    const ImVec2 snapped_min{std::round(rect_min.x), std::round(rect_min.y)};
    const ImVec2 snapped_max{std::round(rect_max.x) - 1.f, std::round(rect_max.y) - 1.f};

    draw_list->PathClear();
    draw_list->PathRect(snapped_min, snapped_max, rounding);
    const ImVector<ImVec2>& path = draw_list->_Path;
    std::vector<ImVec2> path_points(path.Data, path.Data + path.Size);
    draw_list->PathClear();
    if (path_points.size() < 2) {
        return;
    }

    path_points.push_back(path_points.front());

    bool pen_down        = true;
    float phase_traveled = 0.f;
    for (size_t i = 0; i + 1 < path_points.size(); ++i) {
        const ImVec2 from = path_points[i];
        const ImVec2 to   = path_points[i + 1];
        const ImVec2 delta{to.x - from.x, to.y - from.y};
        const float segment_length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
        if (segment_length <= 0.f) {
            continue;
        }

        const ImVec2 direction{delta.x / segment_length, delta.y / segment_length};
        float traveled = 0.f;
        while (traveled < segment_length) {
            const float phase_length = pen_down ? dash_length : gap_length;
            const float step = std::min(phase_length - phase_traveled, segment_length - traveled);
            if (pen_down) {
                draw_list->AddLine(
                    ImVec2(from.x + direction.x * traveled, from.y + direction.y * traveled),
                    ImVec2(
                        from.x + direction.x * (traveled + step),
                        from.y + direction.y * (traveled + step)
                    ),
                    color,
                    thickness
                );
            }

            traveled += step;
            phase_traveled += step;
            if (phase_traveled >= phase_length - 1e-3f) {
                pen_down       = !pen_down;
                phase_traveled = 0.f;
            }
        }
    }
}

} // namespace Slic3r::App::Imgui
