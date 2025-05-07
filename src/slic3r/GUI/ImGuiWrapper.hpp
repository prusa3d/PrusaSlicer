///|/ Copyright (c) Prusa Research 2018 - 2023 Oleksandra Iushchenko @YuSanka, Enrico Turri @enricoturri1966, Filip Sykala @Jony01, Lukáš Matěna @lukasmatena, Vojtěch Bubník @bubnikv, Lukáš Hejl @hejllukas, David Kocík @kocikdav, Vojtěch Král @vojtechkral
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_ImGuiWrapper_hpp_
#define slic3r_ImGuiWrapper_hpp_

#include "ImGuiPureWrap.hpp"

#include <wx/string.h>

#include "libslic3r/Point.hpp"
#include "libslic3r/Color.hpp"
#include "libslic3r/Polygon.hpp"

namespace Slic3r {
namespace Search {
struct OptionViewParameters;
} // namespace Search
} // namespace Slic3r

class wxString;
class wxMouseEvent;
class wxKeyEvent;
struct ImRect;

struct IMGUI_API ImGuiWindow;

namespace Slic3r {
namespace GUI {

class ImGuiWrapper
{
    std::vector<std::tuple<std::string, const ImWchar*, bool>> m_lang_glyphs_info; // language prefix, ranges, whether it needs CLK font
    const ImWchar* m_glyph_ranges{ nullptr };
    // Chinese, Japanese, Korean
    float m_font_size{ 18.0 };
    unsigned m_font_texture{ 0 };
    float m_style_scaling{ 1.0 };
    unsigned m_mouse_buttons{ 0 };
    bool m_disabled{ false };
    bool m_new_frame_open{ false };
    bool m_requires_extra_frame{ false };
    std::map<wchar_t, int> m_custom_glyph_rects_ids;
    std::string m_clipboard_text;

public:
    struct LastSliderStatus {
        bool hovered { false };
        bool edited  { false };
        bool clicked { false };
        bool deactivated_after_edit { false };
        // flag to indicate possibility to take snapshot from the slider value
        // It's used from Gizmos to take snapshots just from the very beginning of the editing
        bool can_take_snapshot { false };
        // When Undo/Redo snapshot is taken, then call this function
        void invalidate_snapshot() { can_take_snapshot = false; }
    };

    ImGuiWrapper();
    ~ImGuiWrapper();

    void set_language(const std::string &language);
    void set_scaling(float font_size, float scale_style, float scale_both);
    bool update_mouse_data(wxMouseEvent &evt);
    bool update_key_data(wxKeyEvent &evt);

    float get_font_size() const { return m_font_size; }
    float get_style_scaling() const { return m_style_scaling; }
    const ImWchar *get_glyph_ranges() const { return m_glyph_ranges; } // language specific

    void new_frame();
    void render();

    float scaled(float x) const { return x * m_font_size; }
    ImVec2 scaled(float x, float y) const { return ImVec2(x * m_font_size, y * m_font_size); }

    const LastSliderStatus& get_last_slider_status() const { return m_last_slider_status; }
    LastSliderStatus& get_last_slider_status() { return m_last_slider_status; }

    bool button(const std::string& label, const ImVec2 &size, bool enable); // default size = ImVec2(0.f, 0.f)
    void draw_icon(ImGuiWindow& window, const ImVec2& pos, float size, wchar_t icon_id);

    // Float sliders: Manually inserted values aren't clamped by ImGui.Using this wrapper function does (when clamp==true).
    bool slider_float(const char* label, float* v, float v_min, float v_max, const char* format = "%.3f", float power = 1.0f, bool clamp = true, const wxString& tooltip = {}, bool show_edit_btn = true);
    bool slider_float(const std::string& label, float* v, float v_min, float v_max, const char* format = "%.3f", float power = 1.0f, bool clamp = true, const wxString& tooltip = {}, bool show_edit_btn = true);
    bool slider_float(const wxString& label, float* v, float v_min, float v_max, const char* format = "%.3f", float power = 1.0f, bool clamp = true, const wxString& tooltip = {}, bool show_edit_btn = true);

    bool image_button(const wchar_t icon, const std::string& tooltip = {}, bool highlight_on_hover = true);
    void search_list(const ImVec2& size, bool (*items_getter)(int, const char** label, const char** tooltip), char* search_str,
                     Search::OptionViewParameters& view_params, int& selected, bool& edited, int& mouse_wheel, bool is_localized);

    void disabled_begin(bool disabled);
    void disabled_end();

    // Extended function ImGuiWrapper::slider_float to work with std::optional<float> value near def_val cause release of optional
    bool slider_optional_float(const char* label, std::optional<float> &v, float v_min, float v_max, const char* format = "%.3f", float power = 1.0f, bool clamp = true, const wxString& tooltip = {}, bool show_edit_btn = true, float def_val = .0f);
    // Extended function ImGuiWrapper::slider_float to work with std::optional<int>, when value == def_val than optional release its value
    bool slider_optional_int(const char* label, std::optional<int> &v, int v_min, int v_max, const char* format = "%.3f", float power = 1.0f, bool clamp = true, const wxString& tooltip = {}, bool show_edit_btn = true, int def_val = 0);

    /// <summary>
    /// Suggest loacation of dialog window,
    /// dependent on actual visible thing on platter
    /// like Gizmo menu size, notifications, ...
    /// To be near of polygon interest and not over it.
    /// And also not out of visible area.
    /// </summary>
    /// <param name="dialog_size">Define width and height of diaog window</param>
    /// <param name="interest">Area of interest. Result should be close to it</param>
    /// <param name="canvas_size">Available space a.k.a GLCanvas3D::get_current_canvas3D()</param>
    /// <returns>Suggestion for dialog offest</returns>
    static ImVec2 suggest_location(const ImVec2          &dialog_size,
                                   const Slic3r::Polygon &interest,
                                   const ImVec2          &canvas_size);

    /// <summary>
    /// Visualization of polygon
    /// </summary>
    /// <param name="polygon">Define what to draw</param>
    /// <param name="draw_list">Define where to draw it</param>
    /// <param name="color">Color of polygon</param>
    /// <param name="thickness">Width of polygon line</param>
    static void draw(const Polygon &polygon,
                     ImDrawList *   draw_list = ImGui::GetOverlayDrawList(),
                     ImU32 color     = ImGui::GetColorU32(ImGuiPureWrap::COL_ORANGE_LIGHT),
                     float thickness = 3.f);

    bool requires_extra_frame() const { return m_requires_extra_frame; }
    void set_requires_extra_frame() { m_requires_extra_frame = true; }
    void reset_requires_extra_frame() { m_requires_extra_frame = false; }

    ImFontAtlasCustomRect* GetTextureCustomRect(const wchar_t& tex_id);

private:
    void init_font(bool compress);
    void init_input();
    void init_style();
    void render_draw_data(ImDrawData *draw_data);
    bool display_initialized() const;
    void destroy_font();
    std::vector<unsigned char> load_svg(const std::string& bitmap_name, unsigned target_width, unsigned target_height);

    static const char* clipboard_get(void* user_data);
    static void clipboard_set(void* user_data, const char* text);

    LastSliderStatus m_last_slider_status;
};

namespace ImGuiPSWrap
{
    ImU32       to_ImU32(const ColorRGBA& color);
    ImVec4      to_ImVec4(const ColorRGBA& color);
    ColorRGBA   from_ImU32(const ImU32& color);
    ColorRGBA   from_ImVec4(const ImVec4& color);
}

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_ImGuiWrapper_hpp_

