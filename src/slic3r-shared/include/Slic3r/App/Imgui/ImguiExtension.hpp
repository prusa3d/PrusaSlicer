// TODO: cleanup this file and create ImguiUtils from it
#pragma once

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <Slic3r/App/Render/ImguiTypes.hpp>

#include <string>
#include <sstream>
#include <cstdint>
#include <vector>

namespace Slic3r::Domain {
class ColorRGB;
class ColorRGBA;
} // namespace Slic3r::Domain

namespace Slic3r::App::Imgui {

static constexpr float DEFAULT_WINDOW_BG_ALPHA = 0.8f;

class ScopedStyleColors {
public:
    ScopedStyleColors(std::initializer_list<std::pair<ImGuiCol, ImColor>> initializer_list);
    ~ScopedStyleColors();
    ScopedStyleColors(const ScopedStyleColors& rhs) = delete;
    ScopedStyleColors& operator=(const ScopedStyleColors& rhs) = delete;

private:
    int m_count = 0;
};

class UnifiedWindowStyle
{
public:
    void push();
    void pop();
};

struct ScopedGroup
{
    ScopedGroup(const char* id) {
        ImGui::BeginGroup();
        ImGui::PushID(id);
    }

    ~ScopedGroup() {
        ImGui::PopID();
        ImGui::EndGroup();
    }
};

inline void disable_background_fadeout_animation() { ImGui::GetCurrentContext()->DimBgRatio = 1.0f; }

void tooltip(const char* label, float wrap_width = 0.0f);
void tooltip(const std::string& label, float wrap_width = 0.0f);

bool menu_item_with_icon(const char* label, const char* shortcut = nullptr, ImU32 icon_color = 0,
    bool selected = false, bool enabled = true);

void icon_image(Render::Icon icon, const ImVec2& size = { 0.0f, 0.0f }, bool disabled = false);

/* Use id, when the window has more than one icon_button to avoid using of the same ID generated from default label ("##btn") */
bool icon_button(Render::Icon icon, const ImVec2& size = { 0.0f, 0.0f }, const std::string& id = std::string());

void toggle_button(const std::string& label, bool* on, bool right_align = false);

ImU32 to_ImU32(const Domain::ColorRGBA& color);
ImU32 to_ImU32(const Domain::ColorRGB& color, uint8_t alpha = 255);

ImVec4 to_ImVec4(const Domain::ColorRGBA& color);
ImVec4 to_ImVec4(const Domain::ColorRGB& color, uint8_t alpha = 255);

// Adjusts the brightness of an ImGui ImColor by the given factor (0 to 2)
// factor < 1.0f darkens the color, factor > 1.0f brightens it
ImColor adjust_brightness(ImColor color, float factor);

// this code is borrowed from https://stackoverflow.com/questions/16605967/set-precision-of-stdto-string-when-converting-floating-point-values
template <typename T>
std::string to_string_with_precision(const T a_value, const uint8_t n = 2)
{
    std::ostringstream out;
    out.precision(n);
    out << std::fixed << a_value;
    return std::move(out).str();
}

// Aligned widgets
// align_x: 0.0f = left, 0.5f = center, 1.0f = right.

void text_with_bg_aligned(float align_x, const std::string& label, ImVec4 color = {});
bool button_aligned(float align_x, const std::string& label, const ImVec2& size_arg = ImVec2(0, 0), ImGuiButtonFlags flags= ImGuiButtonFlags_None);

void colored_circle_marker_aligned(float align_x, const std::string& label_str, const std::vector<ImVec4>& colors, const ImVec2& size_arg = ImVec2(0, 0));

void move_window_to_bounds(const ImVec2& available_size, ImRect& window);

bool is_dark(ImColor color);

ImColor contrast_color(ImColor color);

/**
 * @brief Draw a dashed border around a rounded rectangle.
 *
 * The dashes use the same path as a solid AddRect(), so a dashed and a solid border of the
 * same rectangle line up exactly.
 */
void draw_dashed_rounded_rect(
    ImDrawList* draw_list,
    const ImVec2& rect_min,
    const ImVec2& rect_max,
    ImU32 color,
    float thickness,
    float rounding,
    float dash_length,
    float gap_length
);

} // namespace Slic3r::App::Imgui
