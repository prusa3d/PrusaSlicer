///|/ Copyright (c) Prusa Research 2020 - 2022 Vojtěch Bubník @bubnikv, Oleksandra Iushchenko @YuSanka, Enrico Turri @enricoturri1966, Lukáš Matěna @lukasmatena
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_ImGUI_DoubleSlider_hpp_
#define slic3r_ImGUI_DoubleSlider_hpp_

#include "ImGuiPureWrap.hpp"
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <sstream>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "imgui/imgui_internal.h"

// this code is borrowed from https://stackoverflow.com/questions/16605967/set-precision-of-stdto-string-when-converting-floating-point-values
template <typename T>
std::string to_string_with_precision(const T a_value, const int n = 2)
{
    std::ostringstream out;
    out.precision(n);
    out << std::fixed << a_value;
    return std::move(out).str();
}

namespace DoubleSlider {

enum SelectedSlider {
    ssUndef,
    ssLower,
    ssHigher
};

class ImGuiControl
{
public:
    ImGuiControl(int lowerValue,
                 int higherValue,
                 int minValue,
                 int maxValue,
                 ImGuiSliderFlags flags = ImGuiSliderFlags_None,
                 std::string name = "d_slider",
                 bool use_lower_thumb = true);
    ImGuiControl() {}
    ~ImGuiControl() {}

    int     GetMinPos() const     { return m_min_pos; }
    int     GetMaxPos() const     { return m_max_pos; }
    int     GetLowerPos()  const  { return m_lower_pos; }
    int     GetHigherPos() const  { return m_higher_pos; }
    int     GetActivePos() const;

    // Set low and high slider position. If the span is non-empty, disable the "one layer" mode.
    void    SetLowerPos (const int lower_pos);
    void    SetHigherPos(const int higher_pos);
    void    SetSelectionSpan(const int lower_pos, const int higher_pos);

    void    SetMaxPos(const int max_pos);
    void    CombineThumbs(bool combine);
    void    ResetPositions();

    void    SetCtrlPos(ImVec2 pos)          { m_pos = pos; }
    void    SetCtrlSize(ImVec2 size)        { m_size = size; }
    void    SetCtrlScale(float scale)       { m_draw_opts.scale = scale; }
    void    Init(const ImVec2& pos, const ImVec2& size, float scale, bool has_ruler = false) {
                                          m_pos = pos; 
                                          m_size = size;
                                          m_draw_opts.scale = scale;
                                          m_draw_opts.has_ruler = has_ruler;
    }
    ImVec2  GetCtrlSize()               { return m_size; }
    ImVec2  GetCtrlPos()                { return m_pos; }
    
    void    Show(bool show)             { m_is_shown = show; }
    void    Hide()                      { m_is_shown = false; }
    bool    IsShown() const             { return m_is_shown; }
    bool    IsCombineThumbs() const     { return m_combine_thumbs; }
    bool    IsActiveHigherThumb() const { return m_selection == ssHigher; }
    void    MoveActiveThumb(int delta);
    void    ShowLowerThumb(bool show)   { m_draw_lower_thumb = show; }

    void    ShowLabelOnMouseMove(bool show = true) { m_show_move_label = show; }
    ImRect  GetGrooveRect() const       { return m_draw_opts.groove(m_pos, m_size, is_horizontal()); }
    float   GetPositionInRect(int pos, const ImRect& rect) const;
    ImRect  GetActiveThumbRect() const;

    bool    IsRClickOnThumb() const     { return m_rclick_on_selected_thumb; }
    bool    IsLClickOnThumb();
    bool    IsLClickOnHoveredPos();

    bool    is_horizontal() const       { return !(m_flags & ImGuiSliderFlags_Vertical); }
    bool    render();

    std::string get_label(int pos) const;
    float   rounding() const            { return m_draw_opts.rounding(); }
    ImVec2  left_dummy_sz() const       { return m_draw_opts.text_dummy_sz() + m_draw_opts.text_padding(); }

    void    SetHoveredRegion(ImRect region) { m_hovered_region = region; }
    void    InvalidateHoveredRegion()       { m_hovered_region = ImRect(0.f, 0.f, 0.f, 0.f); }

    void    set_get_label_on_move_cb(std::function<std::string(int)> cb)                    { m_cb_get_label_on_move = cb; }
    void    set_get_label_cb(std::function<std::string(int)> cb)                            { m_cb_get_label = cb; }
    void    set_draw_scroll_line_cb(std::function<void(const ImRect&, const ImRect&)> cb)   { m_cb_draw_scroll_line = cb; }
    void    set_extra_draw_cb(std::function<void(const ImRect&)> cb)                        { m_cb_extra_draw = cb; }

private:

    struct DrawOptions {
        float       scale                   { 1.f }; // used for Retina on osx
        bool        has_ruler               { false };

        ImVec2      dummy_sz()           const { return ImVec2(has_ruler ? 48.0f : 24.0f, 16.0f) * scale; }
        ImVec2      thumb_dummy_sz()     const { return ImVec2(17.0f, 17.0f) * scale; }
        ImVec2      groove_sz()          const { return ImVec2(4.0f,   4.0f) * scale; }
        ImVec2      draggable_region_sz()const { return ImVec2(20.0f, 19.0f) * scale; }
        ImVec2      text_dummy_sz()      const { return ImVec2(60.0f, 34.0f) * scale; }
        ImVec2      text_padding()       const { return ImVec2( 5.0f,  2.0f) * scale; }

        float       thumb_radius()       const { return 10.0f * scale; }
        float       thumb_border()       const { return 2.0f * scale; }
        float       rounding()           const { return 2.0f * scale; }

        ImRect      groove(const ImVec2& pos, const ImVec2& size, bool is_horizontal) const;
        ImRect      draggable_region(const ImRect& groove, bool is_horizontal) const;
        ImRect      slider_line(const ImRect& draggable_region, const ImVec2& h_thumb_center, const ImVec2& l_thumb_center, bool is_horizontal) const;
    };

    struct Regions {
        ImRect higher_slideable_region;
        ImRect lower_slideable_region;
        ImRect higher_thumb;
        ImRect lower_thumb;
    };

    SelectedSlider  m_selection;
    ImVec2          m_pos;
    ImVec2          m_size;
    std::string     m_name;
    ImGuiSliderFlags m_flags{ ImGuiSliderFlags_None };
    bool            m_is_shown{ true };

    int             m_min_pos;
    int             m_max_pos;
    int             m_lower_pos;
    int             m_higher_pos;
    // slider's position of the mouse cursor
    int             m_mouse_pos       { 0 };

    bool            m_rclick_on_selected_thumb{ false };
    bool            m_lclick_on_selected_thumb{ false };
    bool            m_lclick_on_hovered_pos   { false };
    bool            m_suppress_process_behavior{ false };
    ImRect          m_active_thumb;
    ImRect          m_hovered_region;

    bool            m_draw_lower_thumb{ true };
    bool            m_combine_thumbs  { false };
    bool            m_show_move_label { false };

    DrawOptions     m_draw_opts;
    Regions         m_regions;

    std::function<std::string(int)>                     m_cb_get_label          { nullptr };
    std::function<std::string(int)>                     m_cb_get_label_on_move  { nullptr };
    std::function<void(const ImRect&, const ImRect&)>   m_cb_draw_scroll_line   { nullptr };
    std::function<void(const ImRect&)>                  m_cb_extra_draw         { nullptr };

    void        correct_lower_pos();
    void        correct_higher_pos();
    std::string get_label_on_move(int pos) const { return m_cb_get_label_on_move ? m_cb_get_label_on_move(pos) : get_label(pos); }

    void        apply_regions(int higher_pos, int lower_pos, const ImRect& draggable_region);
    void        check_and_correct_thumbs(int* higher_pos, int* lower_pos);

    void        draw_scroll_line(const ImRect& scroll_line, const ImRect& slideable_region);
    void        draw_background(const ImRect& slideable_region);
    void        draw_label(std::string label, const ImRect& thumb, bool is_mirrored = false, bool with_border = false);
    void        draw_thumb(const ImVec2& center, bool mark = false);
    bool        draw_slider(int* higher_pos, int* lower_pos,
                            std::string& higher_label, std::string& lower_label,
                            const ImVec2& pos, const ImVec2& size, float scale = 1.0f);
};

// VatType = a typ of values, related to the each position in slider
template<typename ValType>
class Manager
{
public:

    void Init(  int lowerPos,
                int higherPos,
                int minPos,
                int maxPos,
                const std::string& name,
                bool is_horizontal)
    {
        m_ctrl = ImGuiControl(  lowerPos, higherPos,
                                minPos, maxPos,
                                is_horizontal ? 0 : ImGuiSliderFlags_Vertical,
                                name, !is_horizontal);

        m_ctrl.set_get_label_cb([this](int pos) {return get_label(pos); });
    };

    Manager() {}
    Manager(int lowerPos,
            int higherPos,
            int minPos,
            int maxPos,
            const std::string& name,
            bool is_horizontal)
    {
        Init (lowerPos, higherPos, minPos, maxPos, name, is_horizontal);
    }
    ~Manager() {}

    int     GetMinPos()   const { return m_ctrl.GetMinPos(); }
    int     GetMaxPos()   const { return m_ctrl.GetMaxPos(); }
    int     GetLowerPos() const { return m_ctrl.GetLowerPos(); }
    int     GetHigherPos()const { return m_ctrl.GetHigherPos(); }

    ValType  GetMinValue()    { return m_values.empty() ? static_cast<ValType>(0) : m_values[GetMinPos()]; }
    ValType  GetMaxValue()    { return m_values.empty() ? static_cast<ValType>(0) : m_values[GetMaxPos()]; }
    ValType  GetLowerValue()  { return m_values.empty() ? static_cast<ValType>(0) : m_values[GetLowerPos()];}
    ValType  GetHigherValue() { return m_values.empty() ? static_cast<ValType>(0) : m_values[GetHigherPos()]; }

    // Set low and high slider position. If the span is non-empty, disable the "one layer" mode.
    void    SetLowerPos(const int lower_pos) {
        m_ctrl.SetLowerPos(lower_pos);
        process_thumb_move();
    }
    void    SetHigherPos(const int higher_pos) {
        m_ctrl.SetHigherPos(higher_pos);
        process_thumb_move();
    }
    void    SetSelectionSpan(const int lower_pos, const int higher_pos) {
        m_ctrl.SetSelectionSpan(lower_pos, higher_pos);
        process_thumb_move();
    }
    void    SetMaxPos(const int max_pos) {
        m_ctrl.SetMaxPos(max_pos);
        process_thumb_move();
    }
    void Freeze() {
        m_allow_process_thumb_move = false;
    }
    void Thaw() {
        m_allow_process_thumb_move = true;
        process_thumb_move(); 
    }

    void    SetSliderValues(const std::vector<ValType>& values)          { m_values = values; }
    // values used to show thumb labels
    void    SetSliderAlternateValues(const std::vector<ValType>& values) { m_alternate_values = values; }

    bool IsLowerAtMin() const   { return m_ctrl.GetLowerPos() == m_ctrl.GetMinPos(); }
    bool IsHigherAtMax() const  { return m_ctrl.GetHigherPos() == m_ctrl.GetMaxPos(); }

    void Show(bool show = true) { m_ctrl.Show(show); }
    void Hide()                 { m_ctrl.Show(false); }
    bool IsShown()              { return m_ctrl.IsShown(); }
    void SetEmUnit(int em_unit) { m_em = em_unit; }
    void ShowLowerThumb(bool show) { m_ctrl.ShowLowerThumb(show); }

    float GetWidth()            { return m_ctrl.GetCtrlSize().x; }
    float GetHeight()           { return m_ctrl.GetCtrlSize().y; }
    virtual void Render(const int canvas_width, const int canvas_height, float extra_scale = 1.f, float offset = 0.f) = 0;

    void set_callback_on_thumb_move(std::function<void()> cb) { m_cb_thumb_move = cb; };

    void move_current_thumb(const int delta)
    {
        m_ctrl.MoveActiveThumb(delta);
        process_thumb_move();
    }

protected:

    std::vector<ValType>    m_values;
    std::vector<ValType>    m_alternate_values;

    ImGuiControl            m_ctrl;
    int                     m_em{ 10 };
    float                   m_scale{ 1.f };

    virtual std::string get_label(int pos) const {
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

private:

    std::function<void()> m_cb_thumb_move            { nullptr };
    bool                  m_allow_process_thumb_move { true };
};

} // DoubleSlider


#endif // slic3r_ImGUI_DoubleSlider_hpp_
