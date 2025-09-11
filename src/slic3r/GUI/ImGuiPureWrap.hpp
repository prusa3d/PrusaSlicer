///|/ Copyright (c) Prusa Research 2018 - 2023 Oleksandra Iushchenko @YuSanka, Enrico Turri @enricoturri1966, Filip Sykala @Jony01, Lukáš Matěna @lukasmatena, Vojtěch Bubník @bubnikv, Lukáš Hejl @hejllukas, David Kocík @kocikdav, Vojtěch Král @vojtechkral
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_ImGuiPureWrap_hpp_
#define slic3r_ImGuiPureWrap_hpp_

#include <imgui/imgui.h>
#include <functional>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <functional>

struct IMGUI_API ImGuiWindow;

namespace ImGuiPureWrap 
{
    void set_display_size(float w, float h);

    /// <summary>
    /// Extend ImGui::CalcTextSize to use string_view
    /// </summary>
    ImVec2 calc_text_size(std::string_view text, bool  hide_text_after_double_hash = false, float wrap_width = -1.0f);
    ImVec2 calc_text_size(const std::string& text, bool  hide_text_after_double_hash = false, float wrap_width = -1.0f);

    ImVec2 calc_button_size(const std::string& text, const ImVec2& button_size = ImVec2(0, 0));
    ImVec2 calc_button_size(const std::wstring& text, const ImVec2& button_size = ImVec2(0, 0));

    ImVec2 get_slider_icon_size();

    ImVec2 get_item_spacing();
    float  get_slider_float_height();
    void set_next_window_pos(float x, float y, int flag, float pivot_x = 0.0f, float pivot_y = 0.0f);
    void set_next_window_bg_alpha(float alpha);
	void set_next_window_size(float x, float y, ImGuiCond cond);

    bool begin(const std::string &name, int flags = 0);
    bool begin(const std::string& name, bool* close, int flags = 0);
    void end();

    void title(const std::string& str);

    bool draw_radio_button(const std::string& name, float size, bool active, std::function<void(ImGuiWindow& window, const ImVec2& pos, float size)> draw_callback);
    bool button(const std::string &label, const std::string& tooltip = {});
    bool button(const std::string& label, float width, float height);
    bool button(const std::wstring& label, float width, float height);
    bool radio_button(const std::string &label, bool active);

    bool checkbox(const std::string& label, bool& value);

    // Use selection = -1 to not mark any option as selected
    bool combo(const std::string& label, const std::vector<std::string>& options, int& selection, ImGuiComboFlags flags = 0, float label_width = 0.0f, float item_width = 0.0f);

    void draw_hexagon(const ImVec2& center, float radius, ImU32 col, float start_angle = 0.f, float rounding = 0.f);

    void text(const char* label);
    void text(const std::string& label);
    void text(const std::wstring& label);
    void text_colored(const ImVec4& color, const char* label);
    void text_colored(const ImVec4& color, const std::string& label);
    void text_wrapped(const char* label, float wrap_width);
    void text_wrapped(const std::string& label, float wrap_width);
    void tooltip(const char* label, float wrap_width);
    void tooltip(const std::string& label, float wrap_width);

    bool image_button(ImTextureID user_texture_id, const ImVec2& size, const ImVec2& uv0 = ImVec2(0.0, 0.0), const ImVec2& uv1 = ImVec2(1.0, 1.0), int frame_padding = -1, const ImVec4& bg_col = ImVec4(0.0, 0.0, 0.0, 0.0), const ImVec4& tint_col = ImVec4(1.0, 1.0, 1.0, 1.0), ImGuiButtonFlags flags = 0);

    bool want_mouse();
    bool want_keyboard();
    bool want_text_input();
    bool want_any_input();

    void disable_background_fadeout_animation();

    bool undo_redo_list(const ImVec2& size, const bool is_undo, bool (*items_getter)(const bool, int, const char**), int& hovered, int& selected, int& mouse_wheel);
    void scroll_up();
    void scroll_down();
    void process_mouse_wheel(int& mouse_wheel);

    // Optional inputs are used for set up value inside of an optional, with default value
    // 
    // Extended function ImGui::InputInt to work with std::optional<int>, when value == def_val optional is released.
    bool input_optional_int(const char *label, std::optional<int>& v, int step=1, int step_fast=100, ImGuiInputTextFlags flags=0, int def_val = 0);    
    // Extended function ImGui::InputFloat to work with std::optional<float> value near def_val cause release of optional
    bool input_optional_float(const char* label, std::optional<float> &v, float step = 0.0f, float step_fast = 0.0f, const char* format = "%.3f", ImGuiInputTextFlags flags = 0, float def_val = .0f);
    // Extended function ImGui::DragFloat to work with std::optional<float> value near def_val cause release of optional
    bool drag_optional_float(const char* label, std::optional<float> &v, float v_speed, float v_min, float v_max, const char* format, float power, float def_val = .0f);

    /// <summary>
    /// Change position of imgui window
    /// </summary>
    /// <param name="window_name">ImGui identifier of window</param>
    /// <param name="output_window_offset">[output] optional </param>
    /// <param name="try_to_fix">When True Only move to be full visible otherwise reset position</param>
    /// <returns>New offset of window for function ImGui::SetNextWindowPos</returns>
    std::optional<ImVec2> change_window_position(const char *window_name, bool try_to_fix);

    /// <summary>
    /// Use ImGui internals to unactivate (lose focus) in input.
    /// When input is activ it can't change value by application.
    /// </summary>
    void left_inputs();

    /// <summary>
    /// Truncate text by ImGui draw function to specific width
    /// NOTE 1: ImGui must be initialized
    /// NOTE 2: Calculation for actual acive imgui font
    /// </summary>
    /// <param name="text">Text to be truncated</param>
    /// <param name="width">Maximal width before truncate</param>
    /// <param name="tail">String puted on end of text to be visible truncation</param>
    /// <returns>Truncated text</returns>
    std::string trunc(const std::string &text,
                      float              width,
                      const char        *tail = " ..");

    /// <summary>
    /// Escape ## in data by add space between hashes
    /// Needed when user written text is visualized by ImGui.
    /// </summary>
    /// <param name="text">In/Out text to be escaped</param>
    void escape_double_hash(std::string &text);
    
    /// <summary>
    /// Draw symbol of cross hair
    /// </summary>
    /// <param name="position">Center of cross hair</param>
    /// <param name="radius">Circle radius</param>
    /// <param name="color">Color of symbol</param>
    /// <param name="num_segments">Precission of circle</param>
    /// <param name="thickness">Thickness of Line in symbol</param>
    void draw_cross_hair(const ImVec2 &position,
                         float         radius       = 16.f,
                         ImU32         color        = ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, .75f)),
                         int           num_segments = 0,
                         float         thickness    = 4.f);

    /// <summary>
    /// Check that font ranges contain all chars in string
    /// (rendered Unicodes are stored in GlyphRanges)
    /// </summary>
    /// <param name="font">Contain glyph ranges</param>
    /// <param name="text">Vector of character to check</param>
    /// <returns>True when all glyphs from text are in font ranges</returns>
    bool contain_all_glyphs(const ImFont *font, const std::string &text);
    bool is_chars_in_ranges(const ImWchar *ranges, const char *chars_ptr);
    bool is_char_in_ranges(const ImWchar *ranges, unsigned int letter);

    bool begin_menu(const char* label, bool enabled = true);
    void end_menu();
    bool menu_item_with_icon(const char* label, const char* shortcut, ImVec2 icon_size = ImVec2(0, 0), ImU32 icon_color = 0, bool selected = false, bool enabled = true);

    const ImVec4 COL_GREY_DARK         = { 0.33f, 0.33f, 0.33f, 1.0f };
    const ImVec4 COL_GREY_LIGHT        = { 0.4f, 0.4f, 0.4f, 1.0f };
    const ImVec4 COL_ORANGE_DARK       = { 0.67f, 0.36f, 0.19f, 1.0f };
    const ImVec4 COL_ORANGE_LIGHT      = { 0.923f, 0.504f, 0.264f, 1.0f };
    const ImVec4 COL_WINDOW_BACKGROUND = { 0.13f, 0.13f, 0.13f, 0.8f };
    const ImVec4 COL_BUTTON_BACKGROUND = COL_ORANGE_DARK;
    const ImVec4 COL_BUTTON_HOVERED    = COL_ORANGE_LIGHT;
    const ImVec4 COL_BUTTON_ACTIVE     = COL_BUTTON_HOVERED;
}

#endif // slic3r_ImGuiPureWrap_hpp_

