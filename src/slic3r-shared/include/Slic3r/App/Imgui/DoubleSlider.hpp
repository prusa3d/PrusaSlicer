#pragma once

#include "Slic3r/App/Yoga/Window.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Imgui/ImguiExtension.hpp"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <vector>
#include <functional>

namespace Slic3r::App {
class MenuManager;
class CommandBindingManager;
} // namespace Slic3r::App

namespace Slic3r::App::Imgui::DoubleSlider {

typedef std::function<void(void)> OnThumbMoveCallback;
typedef std::function<void(unsigned int)> RequestExtraFramesCallback;
typedef std::function<std::string(int)> GetLabelOnMoveCallback;
typedef std::function<std::string(int)> GetLabelCallback;
typedef std::function<void(const ImRect&, const ImRect&)> DrawScrollLineCallback;
typedef std::function<void(const ImRect&)> ExtraDrawCallback;

enum class SelectedSlider
{
    Undefined,
    Lower,
    Higher
};

class Control : public Yoga::Item
{
public:
    // parameters for action functions is a bounding box of item
    struct Callbacks
    {
        std::function<void()> value_changed{ nullptr };
        std::function<void()> request_extra_frame{ nullptr };
        std::function<void()> extra_render{ nullptr };
    };

    Control(ImGuiSliderFlags flags = ImGuiSliderFlags_None,
            bool use_lower_thumb = true);
    ~Control() = default;

    void render(const Domain::Vec2f& pos, const Domain::Vec2f& size) override final;

    Callbacks& callbacks() { return m_callbacks; }

    int min_pos() const { return m_min_pos; }
    int max_pos() const { return m_max_pos; }
    int lower_pos() const { return m_lower_pos; }
    int higher_pos() const { return m_higher_pos; }
    int active_pos() const;

    // Set low and high slider position. If the span is non-empty, disable the "one layer" mode.
    void set_lower_pos(const int lower_pos);
    void set_higher_pos(const int higher_pos);
    void set_selection_span(const int lower_pos, const int higher_pos);

    void set_max_pos(const int max_pos);
    void combine_thumbs(bool combine);
    void reset_positions();

    void set_ctrl_scale(float scale) { m_draw_opts.scale = scale; }
    ImVec2 ctrl_size() const { return m_size; }
    ImVec2 ctrl_pos() const { return m_pos; }

    void set_border_color(ImU32 color) { m_border_color = color; }
    ImU32 border_color() { return m_border_color; }

    void update_draw_options(float scale, bool has_ruler = false);

    bool is_combine_thumbs() const { return m_combine_thumbs; }
    bool is_active_higher_thumb() const { return m_selection == SelectedSlider::Higher; }
    void move_active_thumb(int delta);

    void show_lower_thumb(bool show) { m_draw_lower_thumb = show; }
    void show_label_on_mouse_move(bool show = true) { m_show_move_label = show; }
   
    ImRect groove_rect() const { return m_draw_opts.groove(m_pos, m_size, is_horizontal()); }
    float position_in_rect(int pos, const ImRect& rect) const;
    ImRect active_thumb_rect() const;

    bool is_rclick_on_thumb();
    bool is_lclick_on_thumb();
    bool is_lclick_on_hovered_pos();

    bool is_horizontal() const { return !(m_flags & ImGuiSliderFlags_Vertical); }

    std::string label(int pos) const;
    float rounding() const { return m_draw_opts.rounding(); }
    ImVec2 left_dummy_sz() const { return m_draw_opts.text_dummy_sz() + m_draw_opts.text_padding(); }

    void set_hovered_region(ImRect region) { m_hovered_region = region; }
    void invalidate_hovered_region() { m_hovered_region = ImRect(0.f, 0.f, 0.f, 0.f); }

    void set_get_label_on_move_cb(GetLabelOnMoveCallback cb) { m_cb_get_label_on_move = cb; }
    void set_get_label_cb(GetLabelCallback cb) { m_cb_get_label = cb; }
    void set_draw_scroll_line_cb(DrawScrollLineCallback cb) { m_cb_draw_scroll_line = cb; }
    void set_extra_draw_cb(ExtraDrawCallback cb) { m_cb_extra_draw = cb; }

private:
    void correct_lower_pos();
    void correct_higher_pos();
    std::string label_on_move(int pos) const { return m_cb_get_label_on_move ? m_cb_get_label_on_move(pos) : label(pos); }

    void apply_regions(int higher_pos, int lower_pos, const ImRect& draggable_region);
    void check_and_correct_thumbs(int* higher_pos, int* lower_pos);

    void draw_scroll_line(const ImRect& scroll_line, const ImRect& slideable_region);
    void draw_background(const ImRect& slideable_region);
    void draw_label(std::string label, const ImRect& thumb, bool is_mirrored = false, bool with_border = false);
    void draw_thumb(const ImVec2& center, bool mark = false);
    bool draw_slider(int* higher_pos, int* lower_pos, const std::string& higher_label, const std::string& lower_label,
                     const ImVec2& pos, const ImVec2& size);

    ImU32 fg_color() const;
    ImU32 bg_color() const;
    ImU32 tooltip_bg_color() const;

private:
    struct DrawOptions
    {
        float scale{ 1.0f }; // used for Retina on osx
        float font_size{ 16.0f };
        ImVec2 text_size{ 0.0f, 0.0f };
        bool has_ruler{ false };

        ImVec2 dummy_sz() const;
        ImVec2 groove_sz() const;
        ImVec2 draggable_region_sz() const;
        ImVec2 text_dummy_sz() const;
        ImVec2 text_padding() const;
        ImVec2 triangle_offset() const;

        float thumb_radius() const;
        float thumb_border() const;
        float rounding() const;

        ImRect groove(const ImVec2& pos, const ImVec2& size, bool is_horizontal) const;
        ImRect draggable_region(const ImRect& groove, bool is_horizontal) const;
        ImRect slider_line(const ImRect& draggable_region, const ImVec2& h_thumb_center, const ImVec2& l_thumb_center,
            bool is_horizontal) const;

        ImVec2 calc_text_size(const std::string& txt) const {
            return ImGui::CalcTextSize(txt.c_str()) * scale + text_padding() * 2.0f + ImVec2({9.f, 0});
        }
    };

    struct Regions
    {
        ImRect higher_slideable_region;
        ImRect lower_slideable_region;
        ImRect higher_thumb;
        ImRect lower_thumb;
    };

    SelectedSlider m_selection{ SelectedSlider::Undefined };
    ImVec2 m_pos{ 0.0f, 0.0f };
    ImVec2 m_size{ 0.0f, 0.0f };
    ImU32 m_border_color;
    std::string m_name;
    ImGuiSliderFlags m_flags{ ImGuiSliderFlags_None };
    bool m_is_shown{ true };
    bool m_is_dragging{ false };

    int m_min_pos{ 0 };
    int m_max_pos{ 0 };
    int m_lower_pos{ 0 };
    int m_higher_pos{ 0 };
    // slider's position of the mouse cursor
    int m_mouse_pos{ 0 };

    bool m_rclick_on_selected_thumb{ false };
    bool m_lclick_on_selected_thumb{ false };
    bool m_lclick_on_hovered_pos{ false };
    bool m_suppress_process_behavior{ false };
    ImRect m_active_thumb;
    ImRect m_hovered_region;

    bool m_draw_lower_thumb{ true };
    bool m_combine_thumbs{ false };
    bool m_show_move_label{ false };

    bool m_is_activated{ false };

    DrawOptions m_draw_opts;
    Regions m_regions;

    GetLabelOnMoveCallback m_cb_get_label{ nullptr };
    GetLabelCallback m_cb_get_label_on_move{ nullptr };
    DrawScrollLineCallback m_cb_draw_scroll_line { nullptr };
    ExtraDrawCallback m_cb_extra_draw{ nullptr };

    Callbacks m_callbacks;
};

// VatType = a typ of values, related to the each position in slider
template<typename ValType>
class Manager : public Slic3r::App::Yoga::Window
{
public:
    Manager(
        const std::string& name,
        const std::string& header_text,
        Yoga::Orientation orientation
    ) :
        Slic3r::App::Yoga::Window(name)
    {
        set_padding({10.f, 5.f});
        set_orientation(orientation);
        set_alpha(0.f);

        const bool is_horizontal = orientation == Yoga::Orientation::Horizontal;

        m_ctrl = emplace_back<Control>(is_horizontal ? 0 : ImGuiSliderFlags_Vertical, !is_horizontal);
        if (is_horizontal) {
            set_min_height(50);
        } else {
            set_min_width(125);
        }
        m_ctrl->set_get_label_cb([this](int pos) { return label(pos); });
        m_ctrl->set_flex_grow(1.);
    }

    int min_pos() const { return m_ctrl->min_pos(); }
    int max_pos() const { return m_ctrl->max_pos(); }
    int lower_pos() const { return m_ctrl->lower_pos(); }
    int higher_pos() const { return m_ctrl->higher_pos(); }

    ValType min_value() const { return m_values.empty() ? static_cast<ValType>(0) : m_values[min_pos()]; }
    ValType max_value() const { return m_values.empty() ? static_cast<ValType>(0) : m_values[max_pos()]; }
    ValType lower_value() const { return m_values.empty() ? static_cast<ValType>(0) : m_values[lower_pos()];}
    ValType higher_value() const { return m_values.empty() ? static_cast<ValType>(0) : m_values[higher_pos()]; }

    // Set low and high slider position. If the span is non-empty, disable the "one layer" mode.
    void set_lower_pos(const int lower_pos) {
        m_ctrl->set_lower_pos(lower_pos);
        process_thumb_move();
    }

    void set_higher_pos(const int higher_pos) {
        m_ctrl->set_higher_pos(higher_pos);
        process_thumb_move();
    }

    void set_selection_span(const int lower_pos, const int higher_pos) {
        m_ctrl->set_selection_span(lower_pos, higher_pos);
        process_thumb_move();
    }

    void set_max_pos(const int max_pos) {
        m_ctrl->set_max_pos(max_pos);
        process_thumb_move();
    }

    void freeze() { m_allow_process_thumb_move = false; }
    void thaw() {
        m_allow_process_thumb_move = true;
        process_thumb_move(); 
    }

    void set_slider_values(std::vector<ValType>&& values) { m_values = std::move(values); }
    // values used to show thumb labels
    void set_slider_alternate_values(std::vector<ValType>&& values) { m_alternate_values = std::move(values); }

    bool is_lower_at_min() const { return m_ctrl->lower_pos() == m_ctrl->min_pos(); }
    bool is_higher_at_max() const { return m_ctrl->higher_pos() == m_ctrl->max_pos(); }

    void show_lower_thumb(bool show) { m_ctrl->show_lower_thumb(show); }

    void set_scale(float scale) {
        m_scale = scale;
    }

    void set_on_thumb_move_callback(OnThumbMoveCallback cb) { m_cb_thumb_move = cb; };
    void set_request_extra_frames_callback(RequestExtraFramesCallback cb) { m_cb_request_extra_frames = cb; };

    void move_current_thumb(const int delta) {
        m_ctrl->move_active_thumb(delta);
        process_thumb_move();
    }

    virtual void register_commands(
        MenuManager& menu_manager,
        CommandBindingManager& command_binding_manager
    )
    {}

protected:
    virtual std::string label(int pos) const {
        if (m_values.empty())
            return std::to_string(pos);
        if (pos >= int(m_values.size()))
            return "ErrVal";
        return to_string_with_precision(static_cast<ValType>(m_alternate_values.empty() ? m_values[pos] : m_alternate_values[pos]));
    }

    void process_thumb_move() { 
        if (m_cb_thumb_move && m_allow_process_thumb_move)
            m_cb_thumb_move(); 
    }

    void process_request_extra_frames(unsigned int count = 1) {
        if (m_cb_request_extra_frames)
            m_cb_request_extra_frames(count);
    }

protected:
    std::vector<ValType> m_values;
    std::vector<ValType> m_alternate_values;
    Control* m_ctrl;
    float m_scale{ 1.0f };

    MenuManager* m_menu_manager{nullptr};
    CommandBindingManager* m_command_binding_manager{nullptr};

private:
    bool m_allow_process_thumb_move{ true };

    OnThumbMoveCallback m_cb_thumb_move{ nullptr };
    RequestExtraFramesCallback m_cb_request_extra_frames{ nullptr };
};

} // namespace Slic3r::App::Imgui::DoubleSlider
