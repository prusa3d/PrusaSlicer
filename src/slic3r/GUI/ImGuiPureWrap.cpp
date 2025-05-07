///|/ Copyright (c) Prusa Research 2018 - 2023 Oleksandra Iushchenko @YuSanka, Lukáš Matěna @lukasmatena, Enrico Turri @enricoturri1966, David Kocík @kocikdav, Vojtěch Bubník @bubnikv, Tomáš Mészáros @tamasmeszaros, Filip Sykala @Jony01, Lukáš Hejl @hejllukas, Vojtěch Král @vojtechkral
///|/ Copyright (c) 2019 Jason Tibbitts @jasontibbitts
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "ImGuiPureWrap.hpp"

#include <boost/algorithm/string/predicate.hpp>
#include <boost/nowide/convert.hpp>

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui/imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <cassert>
#include <cinttypes>
#include <cstddef>

namespace ImGuiPureWrap {

void set_display_size(float w, float h)
{
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(w, h);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
}

ImVec2 calc_text_size(std::string_view text,
                                    bool  hide_text_after_double_hash,
                                    float wrap_width)
{
    return ImGui::CalcTextSize(text.data(), text.data() + text.length(),
                               hide_text_after_double_hash, wrap_width);
}

ImVec2 calc_text_size(const std::string& text,
                                    bool  hide_text_after_double_hash,
                                    float wrap_width)
{
    return ImGui::CalcTextSize(text.c_str(), NULL, hide_text_after_double_hash, wrap_width);
}

ImVec2 calc_button_size(const std::string &text, const ImVec2 &button_size)
{
    const ImVec2        text_size = calc_text_size(text);
    const ImGuiContext &g         = *GImGui;
    const ImGuiStyle   &style     = g.Style;

    return ImGui::CalcItemSize(button_size, text_size.x + style.FramePadding.x * 2.0f, text_size.y + style.FramePadding.y * 2.0f);
}

ImVec2 calc_button_size(const std::wstring& wtext, const ImVec2& button_size)
{
    const std::string text = boost::nowide::narrow(wtext);
    return calc_button_size(text, button_size);
}

ImVec2 get_item_spacing()
{
    const ImGuiContext &g     = *GImGui;
    const ImGuiStyle   &style = g.Style;
    return style.ItemSpacing;
}

float get_slider_float_height()
{
    const ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    return g.FontSize + style.FramePadding.y * 2.0f + style.ItemSpacing.y;
}

void set_next_window_pos(float x, float y, int flag, float pivot_x, float pivot_y)
{
    ImGui::SetNextWindowPos(ImVec2(x, y), (ImGuiCond)flag, ImVec2(pivot_x, pivot_y));
    ImGui::SetNextWindowSize(ImVec2(0.0, 0.0));
}

void set_next_window_bg_alpha(float alpha)
{
    ImGui::SetNextWindowBgAlpha(alpha);
}

void set_next_window_size(float x, float y, ImGuiCond cond)
{
	ImGui::SetNextWindowSize(ImVec2(x, y), cond);
}

bool begin(const std::string &name, int flags)
{
    return ImGui::Begin(name.c_str(), nullptr, (ImGuiWindowFlags)flags);
}

bool begin(const std::string& name, bool* close, int flags)
{
    return ImGui::Begin(name.c_str(), close, (ImGuiWindowFlags)flags);
}

void end()
{
    ImGui::End();
}

bool button(const std::string & label_utf8, const std::string& tooltip)
{
    const bool ret = ImGui::Button(label_utf8.c_str());

    if (!tooltip.empty() && ImGui::IsItemHovered()) {
        auto tooltip_utf8 = tooltip;
        ImGui::SetTooltip(tooltip_utf8.c_str(), nullptr);
    }

    return ret;
}

bool button(const std::string& label_utf8, float width, float height)
{
	return ImGui::Button(label_utf8.c_str(), ImVec2(width, height));
}

bool button(const std::wstring& wlabel, float width, float height)
{
    const std::string label = boost::nowide::narrow(wlabel);
    return button(label, width, height);
}

bool radio_button(const std::string& label_utf8, bool active)
{
    return ImGui::RadioButton(label_utf8.c_str(), active);
}

bool draw_radio_button(const std::string& name, float size, bool active,
    std::function<void(ImGuiWindow& window, const ImVec2& pos, float size)> draw_callback)
{
    ImGuiWindow& window = *ImGui::GetCurrentWindow();
    if (window.SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window.GetID(name.c_str());

    const ImVec2 pos = window.DC.CursorPos;
    const ImRect total_bb(pos, pos + ImVec2(size, size + style.FramePadding.y * 2.0f));
    ImGui::ItemSize(total_bb, style.FramePadding.y);
    if (!ImGui::ItemAdd(total_bb, id))
        return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(total_bb, id, &hovered, &held);
    if (pressed)
        ImGui::MarkItemEdited(id);

    if (hovered)
        window.DrawList->AddRect({ pos.x - 1.0f, pos.y - 1.0f }, { pos.x + size + 1.0f, pos.y + size + 1.0f }, ImGui::GetColorU32(ImGuiCol_CheckMark));

    if (active)
        window.DrawList->AddRect(pos, { pos.x + size, pos.y + size }, ImGui::GetColorU32(ImGuiCol_CheckMark));

    draw_callback(window, pos, size);

    IMGUI_TEST_ENGINE_ITEM_INFO(id, label, window.DC.LastItemStatusFlags);
    return pressed;
}

bool checkbox(const std::string& label_utf8, bool &value)
{
    return ImGui::Checkbox(label_utf8.c_str(), &value);
}

void text(const char *label)
{
    ImGui::Text("%s", label);
}

void text(const std::string &label)
{
    text(label.c_str());
}

void text(const std::wstring& wlabel)
{
    const std::string label = boost::nowide::narrow(wlabel);
    text(label.c_str());
}

void text_colored(const ImVec4& color, const char* label)
{
    ImGui::TextColored(color, "%s", label);
}

void text_colored(const ImVec4& color, const std::string& label)
{
    text_colored(color, label.c_str());
}

void text_wrapped(const char *label, float wrap_width)
{
    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + wrap_width);
    text(label);
    ImGui::PopTextWrapPos();
}

void text_wrapped(const std::string &label, float wrap_width)
{
    text_wrapped(label.c_str(), wrap_width);
}

void tooltip(const char *label, float wrap_width)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 8.0f, 8.0f });
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(wrap_width);
    ImGui::TextUnformatted(label);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
    ImGui::PopStyleVar(3);
}

void tooltip(const std::string& label, float wrap_width)
{
    tooltip(label.c_str(), wrap_width);
}

ImVec2 get_slider_icon_size()
{
    return calc_button_size(std::wstring(&ImGui::SliderFloatEditBtnIcon, 1));
}

static bool image_button_ex(ImGuiID id, ImTextureID texture_id, const ImVec2& size, const ImVec2& uv0, const ImVec2& uv1, const ImVec2& padding, const ImVec4& bg_col, const ImVec4& tint_col, ImGuiButtonFlags flags)
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    const ImRect bb(window->DC.CursorPos, window->DC.CursorPos + size + padding * 2);
    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, id))
        return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, flags);

    // Render
    const ImU32 col = ImGui::GetColorU32((held && hovered) ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
    ImGui::RenderNavHighlight(bb, id);
    ImGui::RenderFrame(bb.Min, bb.Max, col, true, ImClamp((float)ImMin(padding.x, padding.y), 0.0f, g.Style.FrameRounding));
    if (bg_col.w > 0.0f)
        window->DrawList->AddRectFilled(bb.Min + padding, bb.Max - padding, ImGui::GetColorU32(bg_col));
    window->DrawList->AddImage(texture_id, bb.Min + padding, bb.Max - padding, uv0, uv1, ImGui::GetColorU32(tint_col));

    return pressed;
}

bool image_button(ImTextureID user_texture_id, const ImVec2& size, const ImVec2& uv0, const ImVec2& uv1, int frame_padding, const ImVec4& bg_col, const ImVec4& tint_col, ImGuiButtonFlags flags)
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;
    if (window->SkipItems)
        return false;

    // Default to using texture ID as ID. User can still push string/integer prefixes.
    ImGui::PushID((void*)(intptr_t)user_texture_id);
    const ImGuiID id = window->GetID("#image");
    ImGui::PopID();

    const ImVec2 padding = (frame_padding >= 0) ? ImVec2((float)frame_padding, (float)frame_padding) : g.Style.FramePadding;
    return image_button_ex(id, user_texture_id, size, uv0, uv1, padding, bg_col, tint_col, flags);
}

bool combo(const std::string& label, const std::vector<std::string>& options, int& selection, ImGuiComboFlags flags/* = 0*/, float label_width/* = 0.0f*/, float item_width/* = 0.0f*/)
{
    // this is to force the label to the left of the widget:
    const bool hidden_label = boost::starts_with(label, "##");
    if (!label.empty() && !hidden_label) {
        text(label);
        ImGui::SameLine(label_width);
    }
    ImGui::PushItemWidth(item_width);

    int selection_out = selection;
    bool res = false;

    const char *selection_str = selection < int(options.size()) && selection >= 0 ? options[selection].c_str() : "";
    if (ImGui::BeginCombo(hidden_label ? label.c_str() : ("##" + label).c_str(), selection_str, flags)) {
        for (int i = 0; i < (int)options.size(); i++) {
            if (ImGui::Selectable(options[i].c_str(), i == selection)) {
                selection_out = i;
                res = true;
            }
        }

        ImGui::EndCombo();
    }

    selection = selection_out;
    return res;
}

void draw_hexagon(const ImVec2& center, float radius, ImU32 col, float start_angle, float rounding)
{
    if ((col & IM_COL32_A_MASK) == 0)
        return;

    ImGuiWindow* window = ImGui::GetCurrentWindow();

    float a_min = start_angle;
    float a_max = start_angle + 2.f * IM_PI;

    if (rounding <= 0) {
        window->DrawList->PathArcTo(center, radius, a_min, a_max, 6);
    }
    else {
        const float a_delta = IM_PI / 4.f;
        radius -= rounding;

        for (int i = 0; i <= 6; i++) {
            float a = a_min + ((float)i / (float)6) * (a_max - a_min);
            if (a >= 2.f * IM_PI)
                a -= 2.f * IM_PI;
            ImVec2 pos = ImVec2(center.x + ImCos(a) * radius, center.y + ImSin(a) * radius);
            window->DrawList->PathArcTo(pos, rounding, a - a_delta, a + a_delta, 5);
        }
    }
    window->DrawList->PathFillConvex(col);
}

// Scroll up for one item 
void scroll_up()
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;

    float item_size_y = window->DC.PrevLineSize.y + g.Style.ItemSpacing.y;
    float win_top = window->Scroll.y;

    ImGui::SetScrollY(win_top - item_size_y);
}

// Scroll down for one item 
void scroll_down()
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;

    float item_size_y = window->DC.PrevLineSize.y + g.Style.ItemSpacing.y;
    float win_top = window->Scroll.y;

    ImGui::SetScrollY(win_top + item_size_y);
}

void process_mouse_wheel(int& mouse_wheel)
{
    if (mouse_wheel > 0)
        scroll_up();
    else if (mouse_wheel < 0)
        scroll_down();
    mouse_wheel = 0;
}

bool undo_redo_list(const ImVec2& size, const bool is_undo, bool (*items_getter)(const bool , int , const char**), int& hovered, int& selected, int& mouse_wheel)
{
    bool is_hovered = false;
    ImGui::ListBoxHeader("", size);

    int i=0;
    const char* item_text;
    while (items_getter(is_undo, i, &item_text)) {
        ImGui::Selectable(item_text, i < hovered);

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", item_text);
            hovered = i;
            is_hovered = true;
        }

        if (ImGui::IsItemClicked())
            selected = i;
        i++;
    }

    if (is_hovered)
        process_mouse_wheel(mouse_wheel);

    ImGui::ListBoxFooter();
    return is_hovered;
}

void title(const std::string& str)
{
    text(str);
    ImGui::Separator();
}

bool want_mouse()
{
    return ImGui::GetIO().WantCaptureMouse;
}

bool want_keyboard()
{
    return ImGui::GetIO().WantCaptureKeyboard;
}

bool want_text_input()
{
    return ImGui::GetIO().WantTextInput;
}

bool want_any_input()
{
    const auto io = ImGui::GetIO();
    return io.WantCaptureMouse || io.WantCaptureKeyboard || io.WantTextInput;
}

void disable_background_fadeout_animation()
{
    GImGui->DimBgRatio = 1.0f;
}

template <typename T, typename Func> 
static bool input_optional(std::optional<T> &v, Func& f, std::function<bool(const T&)> is_default, const T& def_val)
{
    if (v.has_value()) {
        if (f(*v)) {
            if (is_default(*v)) v.reset();
            return true;
        }
    } else {
        T val = def_val;
        if (f(val)) {
            if (!is_default(val)) v = val;
            return true;
        }
    }
    return false;
}

bool input_optional_int(const char *        label,
                                      std::optional<int>& v,
                                      int                 step,
                                      int                 step_fast,
                                      ImGuiInputTextFlags flags,
                                      int                 def_val)
{
    auto func = [&](int &value) {
        return ImGui::InputInt(label, &value, step, step_fast, flags);
    };
    std::function<bool(const int &)> is_default = 
        [def_val](const int &value) -> bool { return value == def_val; };
    return input_optional(v, func, is_default, def_val);
}

bool input_optional_float(const char *          label,
                                        std::optional<float> &v,
                                        float                 step,
                                        float                 step_fast,
                                        const char *          format,
                                        ImGuiInputTextFlags   flags,
                                        float                 def_val)
{
    auto func = [&](float &value) {
        return ImGui::InputFloat(label, &value, step, step_fast, format, flags);
    };
    std::function<bool(const float &)> is_default =
        [def_val](const float &value) -> bool {
        return std::fabs(value-def_val) <= std::numeric_limits<float>::epsilon();
    };
    return input_optional(v, func, is_default, def_val);
}

bool drag_optional_float(const char *          label,
                                       std::optional<float> &v,
                                       float                 v_speed,
                                       float                 v_min,
                                       float                 v_max,
                                       const char *          format,
                                       float                 power,
                                       float                 def_val)
{
    auto func = [&](float &value) {
        return ImGui::DragFloat(label, &value, v_speed, v_min, v_max, format, power);
    };
    std::function<bool(const float &)> is_default =
        [def_val](const float &value) -> bool {
        return std::fabs(value-def_val) <= std::numeric_limits<float>::epsilon();
    };
    return input_optional(v, func, is_default, def_val);
}

std::optional<ImVec2> change_window_position(const char *window_name, bool try_to_fix) {
    ImGuiWindow *window = ImGui::FindWindowByName(window_name);
    // is window just created
    if (window == NULL)
        return {};

    // position of window on screen
    ImVec2 position = window->Pos;
    ImVec2 size     = window->SizeFull;

    // screen size
    ImVec2 screen = ImGui::GetMainViewport()->Size;

    std::optional<ImVec2> output_window_offset;
    if (position.x < 0) {
        if (position.y < 0)
            // top left 
            output_window_offset = ImVec2(0, 0); 
        else
            // only left
            output_window_offset = ImVec2(0, position.y); 
    } else if (position.y < 0) {
        // only top
        output_window_offset = ImVec2(position.x, 0); 
    } else if (screen.x < (position.x + size.x)) {
        if (screen.y < (position.y + size.y))
            // right bottom
            output_window_offset = ImVec2(screen.x - size.x, screen.y - size.y);
        else
            // only right
            output_window_offset = ImVec2(screen.x - size.x, position.y);
    } else if (screen.y < (position.y + size.y)) {
        // only bottom
        output_window_offset = ImVec2(position.x, screen.y - size.y);
    }

    if (!try_to_fix && output_window_offset.has_value())
        output_window_offset = ImVec2(-1, -1); // Put on default position

    return output_window_offset;
}

void left_inputs() {
    ImGui::ClearActiveID(); 
}

std::string trunc(const std::string &text,
                                float              width,
                                const char *       tail)
{
    float text_width = ImGui::CalcTextSize(text.c_str()).x;
    if (text_width < width) return text;
    float tail_width = ImGui::CalcTextSize(tail).x;
    assert(width > tail_width);
    if (width <= tail_width) return "Error: Can't add tail and not be under wanted width.";
    float allowed_width = width - tail_width;
    
    // guess approx count of letter
    float average_letter_width = calc_text_size(std::string_view("n")).x; // average letter width
    unsigned count_letter  = static_cast<unsigned>(allowed_width / average_letter_width);

    std::string_view text_ = text;
    std::string_view result_text = text_.substr(0, count_letter);
    text_width = calc_text_size(result_text).x;
    if (text_width < allowed_width) {
        // increase letter count
        while (count_letter < text.length()) {
            ++count_letter;
            std::string_view act_text = text_.substr(0, count_letter);
            text_width = calc_text_size(act_text).x;
            if (text_width > allowed_width) break;
            result_text = act_text;
        }
    } else {
        // decrease letter count
        while (count_letter > 1) {
            --count_letter;
            result_text = text_.substr(0, count_letter);
            text_width  = calc_text_size(result_text).x;
            if (text_width < allowed_width) break;            
        } 
    }
    return std::string(result_text) + tail;
}

void escape_double_hash(std::string &text)
{
    // add space between hashes
    const std::string search  = "##";
    const std::string replace = "# #";
    size_t pos = 0;
    while ((pos = text.find(search, pos)) != std::string::npos) 
        text.replace(pos, search.length(), replace);
}

void draw_cross_hair(const ImVec2 &position, float radius, ImU32 color, int num_segments, float thickness)
{
    auto draw_list = ImGui::GetOverlayDrawList();
    draw_list->AddCircle(position, radius, color, num_segments, thickness);
    auto dirs = {ImVec2{0, 1}, ImVec2{1, 0}, ImVec2{0, -1}, ImVec2{-1, 0}};
    for (const ImVec2 &dir : dirs) {
        ImVec2 start(position.x + dir.x * 0.5 * radius, position.y + dir.y * 0.5 * radius);
        ImVec2 end(position.x + dir.x * 1.5 * radius, position.y + dir.y * 1.5 * radius);
        draw_list->AddLine(start, end, color, thickness);
    }
}

bool contain_all_glyphs(const ImFont      *font,
                                     const std::string &text)
{
    if (font == nullptr) return false;
    if (!font->IsLoaded()) return false;
    const ImFontConfig *fc = font->ConfigData;
    if (fc == nullptr) return false;
    if (text.empty()) return true;
    return is_chars_in_ranges(fc->GlyphRanges, text.c_str());
}

bool is_char_in_ranges(const ImWchar *ranges,
                                      unsigned int   letter)
{
    for (const ImWchar *range = ranges; range[0] && range[1]; range += 2) {
        ImWchar from = range[0];
        ImWchar to   = range[1];
        if (from <= letter && letter <= to) return true;
        if (letter < to) return false; // ranges should be sorted
    }
    return false;
};

bool is_chars_in_ranges(const ImWchar *ranges,
                                       const char    *chars_ptr)
{
    while (*chars_ptr) {
        unsigned int c = 0;
        // UTF-8 to 32-bit character need imgui_internal
        int c_len = ImTextCharFromUtf8(&c, chars_ptr, NULL);
        chars_ptr += c_len;
        if (c_len == 0) break;
        if (!is_char_in_ranges(ranges, c)) return false;
    }
    return true;
}

bool begin_menu(const char* label, bool enabled)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID     id = window->GetID(label);
    bool              menu_is_open = ImGui::IsPopupOpen(id, ImGuiPopupFlags_None);

    // Sub-menus are ChildWindow so that mouse can be hovering across them (otherwise top-most popup menu would steal focus and not allow hovering on parent menu)
    ImGuiWindowFlags flags = ImGuiWindowFlags_ChildMenu | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNavFocus;
    if (window->Flags & (ImGuiWindowFlags_Popup | ImGuiWindowFlags_ChildMenu)) flags |= ImGuiWindowFlags_ChildWindow;

    // If a menu with same the ID was already submitted, we will append to it, matching the behavior of Begin().
    // We are relying on a O(N) search - so O(N log N) over the frame - which seems like the most efficient for the expected small amount of BeginMenu() calls per frame.
    // If somehow this is ever becoming a problem we can switch to use e.g. ImGuiStorage mapping key to last frame used.
    if (g.MenusIdSubmittedThisFrame.contains(id)) {
        if (menu_is_open)
            menu_is_open = ImGui::BeginPopupEx(id, flags); // menu_is_open can be 'false' when the popup is completely clipped (e.g. zero size display)
        else
            g.NextWindowData.ClearFlags(); // we behave like Begin() and need to consume those values
        return menu_is_open;
    }

    // Tag menu as used. Next time BeginMenu() with same ID is called it will append to existing menu
    g.MenusIdSubmittedThisFrame.push_back(id);

    ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
    bool   pressed;
    bool   menuset_is_open = !(window->Flags & ImGuiWindowFlags_Popup) &&
        (g.OpenPopupStack.Size > g.BeginPopupStack.Size && g.OpenPopupStack[g.BeginPopupStack.Size].OpenParentId == window->IDStack.back());
    ImGuiWindow* backed_nav_window = g.NavWindow;
    if (menuset_is_open) g.NavWindow = window; // Odd hack to allow hovering across menus of a same menu-set (otherwise we wouldn't be able to hover parent)

    // The reference position stored in popup_pos will be used by Begin() to find a suitable position for the child menu,
    // However the final position is going to be different! It is chosen by FindBestWindowPosForPopup().
    // e.g. Menus tend to overlap each other horizontally to amplify relative Z-ordering.
    ImVec2 popup_pos, pos = window->DC.CursorPos;
    if (window->DC.LayoutType == ImGuiLayoutType_Horizontal) {
        // Menu inside an horizontal menu bar
        // Selectable extend their highlight by half ItemSpacing in each direction.
        // For ChildMenu, the popup position will be overwritten by the call to FindBestWindowPosForPopup() in Begin()
        popup_pos = ImVec2(pos.x - 1.0f - IM_FLOOR(style.ItemSpacing.x * 0.5f), pos.y - style.FramePadding.y + window->MenuBarHeight());
        window->DC.CursorPos.x += IM_FLOOR(style.ItemSpacing.x * 0.5f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x * 2.0f, style.ItemSpacing.y));
        float w = label_size.x;
        pressed = /*selectable*/ImGui::Selectable(label, menu_is_open,
            ImGuiSelectableFlags_NoHoldingActiveID | ImGuiSelectableFlags_SelectOnClick | ImGuiSelectableFlags_DontClosePopups |
            (!enabled ? ImGuiSelectableFlags_Disabled : 0),
            ImVec2(w, 0.0f));
        ImGui::PopStyleVar();
        window->DC.CursorPos.x += IM_FLOOR(
            style.ItemSpacing.x *
            (-1.0f +
                0.5f)); // -1 spacing to compensate the spacing added when Selectable() did a SameLine(). It would also work to call SameLine() ourselves after the PopStyleVar().
    }
    else {
        // Menu inside a menu
        // (In a typical menu window where all items are BeginMenu() or MenuItem() calls, extra_w will always be 0.0f.
        //  Only when they are other items sticking out we're going to add spacing, yet only register minimum width into the layout system.
        popup_pos = ImVec2(pos.x, pos.y - style.WindowPadding.y);
        float min_w = window->DC.MenuColumns.DeclColumns(label_size.x, 0.0f, IM_FLOOR(g.FontSize * 1.20f)); // Feedback to next frame
        float extra_w = ImMax(0.0f, ImGui::GetContentRegionAvail().x - min_w);
        pressed = /*selectable*/ImGui::Selectable(label, menu_is_open,
            ImGuiSelectableFlags_NoHoldingActiveID | ImGuiSelectableFlags_SelectOnClick | ImGuiSelectableFlags_DontClosePopups |
            ImGuiSelectableFlags_SpanAvailWidth | (!enabled ? ImGuiSelectableFlags_Disabled : 0),
            ImVec2(min_w, 0.0f));
        ImU32 text_col = ImGui::GetColorU32(enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled);
        ImGui::RenderArrow(window->DrawList, pos + ImVec2(window->DC.MenuColumns.Pos[2] + extra_w + g.FontSize * 0.30f, 0.0f), text_col, ImGuiDir_Right);
    }

    const bool hovered = enabled && ImGui::ItemHoverable(window->DC.LastItemRect, id);
    if (menuset_is_open) g.NavWindow = backed_nav_window;

    bool want_open = false;
    bool want_close = false;
    if (window->DC.LayoutType == ImGuiLayoutType_Vertical) // (window->Flags & (ImGuiWindowFlags_Popup|ImGuiWindowFlags_ChildMenu))
    {
        // Close menu when not hovering it anymore unless we are moving roughly in the direction of the menu
        // Implement http://bjk5.com/post/44698559168/breaking-down-amazons-mega-dropdown to avoid using timers, so menus feels more reactive.
        bool moving_toward_other_child_menu = false;

        ImGuiWindow* child_menu_window = (g.BeginPopupStack.Size < g.OpenPopupStack.Size && g.OpenPopupStack[g.BeginPopupStack.Size].SourceWindow == window) ?
            g.OpenPopupStack[g.BeginPopupStack.Size].Window :
            NULL;
        if (g.HoveredWindow == window && child_menu_window != NULL && !(window->Flags & ImGuiWindowFlags_MenuBar)) {
            // FIXME-DPI: Values should be derived from a master "scale" factor.
            ImRect next_window_rect = child_menu_window->Rect();
            ImVec2 ta = g.IO.MousePos - g.IO.MouseDelta;
            ImVec2 tb = (window->Pos.x < child_menu_window->Pos.x) ? next_window_rect.GetTL() : next_window_rect.GetTR();
            ImVec2 tc = (window->Pos.x < child_menu_window->Pos.x) ? next_window_rect.GetBL() : next_window_rect.GetBR();
            float  extra = ImClamp(ImFabs(ta.x - tb.x) * 0.30f, 5.0f, 30.0f); // add a bit of extra slack.
            ta.x += (window->Pos.x < child_menu_window->Pos.x) ? -0.5f : +0.5f;          // to avoid numerical issues
            tb.y = ta.y +
                ImMax((tb.y - extra) - ta.y, -100.0f); // triangle is maximum 200 high to limit the slope and the bias toward large sub-menus // FIXME: Multiply by fb_scale?
            tc.y = ta.y + ImMin((tc.y + extra) - ta.y, +100.0f);
            moving_toward_other_child_menu = ImTriangleContainsPoint(ta, tb, tc, g.IO.MousePos);
            // GetForegroundDrawList()->AddTriangleFilled(ta, tb, tc, moving_within_opened_triangle ? IM_COL32(0,128,0,128) : IM_COL32(128,0,0,128)); // [DEBUG]
        }
        if (menu_is_open && !hovered && g.HoveredWindow == window && g.HoveredIdPreviousFrame != 0 && g.HoveredIdPreviousFrame != id && !moving_toward_other_child_menu)
            want_close = true;

        if (!menu_is_open && hovered && pressed) // Click to open
            want_open = true;
        else if (!menu_is_open && hovered && !moving_toward_other_child_menu) // Hover to open
            want_open = true;

        if (g.NavActivateId == id) {
            want_close = menu_is_open;
            want_open = !menu_is_open;
        }
        if (g.NavId == id && g.NavMoveRequest && g.NavMoveDir == ImGuiDir_Right) // Nav-Right to open
        {
            want_open = true;
            ImGui::NavMoveRequestCancel();
        }
    }
    else {
        // Menu bar
        if (menu_is_open && pressed && menuset_is_open) // Click an open menu again to close it
        {
            want_close = true;
            want_open = menu_is_open = false;
        }
        else if (pressed || (hovered && menuset_is_open && !menu_is_open)) // First click to open, then hover to open others
        {
            want_open = true;
        }
        else if (g.NavId == id && g.NavMoveRequest && g.NavMoveDir == ImGuiDir_Down) // Nav-Down to open
        {
            want_open = true;
            ImGui::NavMoveRequestCancel();
        }
    }

    if (!enabled) // explicitly close if an open menu becomes disabled, facilitate users code a lot in pattern such as 'if (BeginMenu("options", has_object)) { ..use object.. }'
        want_close = true;
    if (want_close && ImGui::IsPopupOpen(id, ImGuiPopupFlags_None)) ImGui::ClosePopupToLevel(g.BeginPopupStack.Size, true);

    IMGUI_TEST_ENGINE_ITEM_INFO(id, label, window->DC.LastItemStatusFlags | ImGuiItemStatusFlags_Openable | (menu_is_open ? ImGuiItemStatusFlags_Opened : 0));

    if (!menu_is_open && want_open && g.OpenPopupStack.Size > g.BeginPopupStack.Size) {
        // Don't recycle same menu level in the same frame, first close the other menu and yield for a frame.
        ImGui::OpenPopup(label);
        return false;
    }

    menu_is_open |= want_open;
    if (want_open) ImGui::OpenPopup(label);

    if (menu_is_open) {
        ImGui::SetNextWindowPos(popup_pos,
            ImGuiCond_Always);     // Note: this is super misleading! The value will serve as reference for FindBestWindowPosForPopup(), not actual pos.
        menu_is_open = ImGui::BeginPopupEx(id, flags); // menu_is_open can be 'false' when the popup is completely clipped (e.g. zero size display)
    }
    else {
        g.NextWindowData.ClearFlags(); // We behave like Begin() and need to consume those values
    }

    return menu_is_open;
}

void end_menu()
{
    ImGui::EndMenu();
}

bool menu_item_with_icon(const char* label, const char* shortcut, ImVec2 icon_size /* = ImVec2(0, 0)*/, ImU32 icon_color /* = 0*/, bool selected /* = false*/, bool enabled /* = true*/)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    ImGuiStyle& style = g.Style;
    ImVec2        pos = window->DC.CursorPos;
    ImVec2        label_size = ImGui::CalcTextSize(label, NULL, true);

    // We've been using the equivalent of ImGuiSelectableFlags_SetNavIdOnHover on all Selectable() since early Nav system days (commit 43ee5d73),
    // but I am unsure whether this should be kept at all. For now moved it to be an opt-in feature used by menus only.
    ImGuiSelectableFlags flags = ImGuiSelectableFlags_SelectOnRelease | ImGuiSelectableFlags_SetNavIdOnHover | (enabled ? 0 : ImGuiSelectableFlags_Disabled);
    bool                 pressed;
    if (window->DC.LayoutType == ImGuiLayoutType_Horizontal) {
        // Mimic the exact layout spacing of BeginMenu() to allow MenuItem() inside a menu bar, which is a little misleading but may be useful
        // Note that in this situation: we don't render the shortcut, we render a highlight instead of the selected tick mark.
        float w = label_size.x;
        window->DC.CursorPos.x += IM_FLOOR(style.ItemSpacing.x * 0.5f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x * 2.0f, style.ItemSpacing.y));
        pressed = ImGui::Selectable(label, selected, flags, ImVec2(w, 0.0f));
        ImGui::PopStyleVar();
        window->DC.CursorPos.x += IM_FLOOR(
            style.ItemSpacing.x *
            (-1.0f +
                0.5f)); // -1 spacing to compensate the spacing added when Selectable() did a SameLine(). It would also work to call SameLine() ourselves after the PopStyleVar().
    }
    else {
        // Menu item inside a vertical menu
        // (In a typical menu window where all items are BeginMenu() or MenuItem() calls, extra_w will always be 0.0f.
        //  Only when they are other items sticking out we're going to add spacing, yet only register minimum width into the layout system.
        float shortcut_w = shortcut ? ImGui::CalcTextSize(shortcut, NULL).x : 0.0f;
        float min_w = window->DC.MenuColumns.DeclColumns(label_size.x, shortcut_w, IM_FLOOR(g.FontSize * 1.20f)); // Feedback for next frame
        float extra_w = std::max(0.0f, ImGui::GetContentRegionAvail().x - min_w);
        pressed = /*selectable*/ImGui::Selectable(label, false, flags | ImGuiSelectableFlags_SpanAvailWidth, ImVec2(min_w, 0.0f));

        if (icon_size.x != 0 && icon_size.y != 0) {
            float selectable_pos_y = pos.y + -0.5f * style.ItemSpacing.y;
            float icon_pos_y = selectable_pos_y + (label_size.y + style.ItemSpacing.y - icon_size.y) / 2;
            float icon_pos_x = pos.x + window->DC.MenuColumns.Pos[2] + extra_w + g.FontSize * 0.40f;
            ImVec2 icon_pos = ImVec2(icon_pos_x, icon_pos_y);
            ImGui::RenderFrame(icon_pos, icon_pos + icon_size, icon_color);
        }

        if (shortcut_w > 0.0f) {
            ImGui::PushStyleColor(ImGuiCol_Text, g.Style.Colors[ImGuiCol_TextDisabled]);
            ImGui::RenderText(pos + ImVec2(window->DC.MenuColumns.Pos[1] + extra_w, 0.0f), shortcut, NULL, false);
            ImGui::PopStyleColor();
        }
        if (selected) {
            ImGui::RenderCheckMark(window->DrawList, pos + ImVec2(window->DC.MenuColumns.Pos[2] + extra_w + g.FontSize * 0.40f, g.FontSize * 0.134f * 0.5f),
                                   ImGui::GetColorU32(enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled), g.FontSize * 0.866f);
        }
    }

    IMGUI_TEST_ENGINE_ITEM_INFO(window->DC.LastItemId, label, window->DC.LastItemStatusFlags | ImGuiItemStatusFlags_Checkable | (selected ? ImGuiItemStatusFlags_Checked : 0));
    return pressed;
}

} //  ImGuiPureWrap
