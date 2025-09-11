///|/ Copyright (c) Prusa Research 2020 - 2022 Vojtěch Bubník @bubnikv, Oleksandra Iushchenko @YuSanka, Enrico Turri @enricoturri1966, Lukáš Matěna @lukasmatena
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_GUI_DoubleSliderForLayers_hpp_
#define slic3r_GUI_DoubleSliderForLayers_hpp_

#include <vector>
#include <set>
#include <functional>
#include <string>

#include "ImGuiDoubleSlider.hpp"
#include "RulerForDoubleSlider.hpp"
#include "TickCodesManager.hpp"
#include "libslic3r/CustomGCode.hpp"

struct ImVec2;

namespace Slic3r {
class Print;

namespace GUI
{
class ImGuiWrapper;
}
}

using namespace Slic3r::CustomGCode;

namespace DoubleSlider {

enum FocusedItem {
    fiNone,
    fiRevertIcon,
    fiOneLayerIcon,
    fiCogIcon,
    fiColorBand,
    fiActionIcon,
    fiSmartWipeTower,
    fiTick
};

enum DrawMode
{
    dmRegular,
    dmSlaPrint,
    dmSequentialFffPrint,
};

enum LabelType
{
    ltHeightWithLayer,
    ltHeight,
    ltEstimatedTime,
};

class DSForLayers : public Manager<double>
{
public:
    DSForLayers() : Manager<double>() {}
    DSForLayers(int lowerValue,
                int higherValue,
                int minValue,
                int maxValue,
                bool allow_editing);
    ~DSForLayers() {}

    void    ChangeOneLayerLock();

    Info    GetTicksValues() const;
    void    SetTicksValues(const Info& custom_gcode_per_print_z);
    void    SetLayersTimes(const std::vector<float>& layers_times, float total_time);
    void    SetLayersTimes(const std::vector<double>& layers_times);

    void    SetDrawMode(bool is_sla_print, bool is_sequential_print);

    void    SetModeAndOnlyExtruder(const bool is_one_extruder_printed_model, const int only_extruder);

    void    Render(const int canvas_width, const int canvas_height, float extra_scale = 1.f, float offset = 0.f) override;
    void    force_ruler_update();

    // jump to selected layer
    void    jump_to_value();

    // just for editor

    void    SetExtruderColors(const std::vector<std::string>& extruder_colors);
    void    UseDefaultColors(bool def_colors_on);
    bool    is_new_print(const std::string& print_obj_idxs);
    void    set_imgui_wrapper(Slic3r::GUI::ImGuiWrapper* imgui) { m_imgui = imgui; }
    void    show_estimated_times(bool show)                     { m_show_estimated_times = show; }
    void    show_ruler(bool show, bool show_bg)                 { m_show_ruler = show; m_show_ruler_bg = show_bg; }
    void    seq_top_layer_only(bool show)                       { m_seq_top_layer_only = show; }

    // manipulation with slider from keyboard

    // add default action for tick, when press "+"
    void    add_current_tick();
    // delete current tick, when press "-"
    void    delete_current_tick();
    // process adding of auto color change
    void    auto_color_change();

    void    set_callback_on_ticks_changed(std::function<void()> cb) 
            { m_cb_ticks_changed = cb; };

    void    set_callback_on_check_gcode(std::function<void(Type)> cb ) 
            { m_ticks.set_callback_on_check_gcode(cb); }

    void    set_callback_on_get_extruder_colors(std::function<std::vector<std::string>()> cb)
            { m_cb_get_extruder_colors = cb; }

    void    set_callback_on_get_print (std::function<const Slic3r::Print& ()> cb)
            { m_cb_get_print = cb; }

    void    set_callback_on_change_app_config (std::function<void(const std::string&, const std::string&)> cb)
            { m_cb_change_app_config = cb; }

    void    set_callback_on_empty_auto_color_change(std::function<void()> cb)
            { m_ticks.set_callback_on_empty_auto_color_change(cb); }

    void    set_callback_on_get_custom_code(std::function<std::string(const std::string&, double)> cb)
            { m_ticks.set_callback_on_get_custom_code(cb); }

    void    set_callback_on_get_pause_print_msg(std::function<std::string(const std::string&, double)> cb)
            { m_ticks.set_callback_on_get_pause_print_msg(cb); }

    void    set_callback_on_get_new_color(std::function<std::string(const std::string&)> cb)
            { m_ticks.set_callback_on_get_new_color(cb); }

    void    set_callback_on_show_info_msg(std::function<int(const std::string&, int)> cb)
            { m_ticks.set_callback_on_show_info_msg(cb); }

    void    set_callback_on_show_warning_msg(std::function<int(const std::string&, int)> cb)
            { m_ticks.set_callback_on_show_warning_msg(cb); }

    void    set_callback_on_get_extruders_cnt(std::function<int()> cb)
            { m_ticks.set_callback_on_get_extruders_cnt(cb); }

    void    set_callback_on_get_extruders_sequence(std::function<bool(ExtrudersSequence&)> cb)
            { m_ticks.set_callback_on_get_extruders_sequence(cb); }

    std::string gcode(Type type) { return m_ticks.gcode(type); }

private:

    bool        is_osx                  { false };
    bool        m_allow_editing         { true };
    bool        m_show_estimated_times  { true };
    bool        m_show_ruler            { false };
    bool        m_show_ruler_bg         { true };
    bool        m_show_cog_menu         { false };
    bool        m_show_edit_menu        { false };
    bool        m_seq_top_layer_only    { false };
    int         m_pos_on_move           { -1 };

    DrawMode    m_draw_mode             { dmRegular };
    Mode        m_mode                  { SingleExtruder };
    FocusedItem m_focus                 { fiNone };

    Ruler                       m_ruler;
    TickCodeManager             m_ticks;
    Slic3r::GUI::ImGuiWrapper*  m_imgui { nullptr };

    std::vector<double>         m_layers_times;
    std::vector<double>         m_layers_values;

    bool        is_wipe_tower_layer(int tick) const;

    std::string get_label(int tick, LabelType label_type, const std::string& fmt = "%1$.2f") const;

    std::string get_tooltip(int tick = -1);

    void        update_draw_scroll_line_cb();

    // functions for extend rendering of m_ctrl

    void        draw_colored_band(const ImRect& groove, const ImRect& slideable_region);
    void        draw_ticks(const ImRect& slideable_region);
    void        draw_ruler(const ImRect& slideable_region);
    void        render_menu();
    void        render_cog_menu();
    void        render_edit_menu();
    bool        render_button(const wchar_t btn_icon, const wchar_t btn_icon_hovered, const std::string& label_id, const ImVec2& pos, FocusedItem focus, int tick = -1);

    void        add_code_as_tick(Type type, int selected_extruder = -1);
    void        edit_tick(int tick = -1);
    void        discard_all_thicks();
    void        process_jump_to_value();
    bool        can_edit() const;

    std::string get_label(int pos) const override { return get_label(pos, ltHeightWithLayer); }

    void process_ticks_changed() { 
        if (m_cb_ticks_changed)
            m_cb_ticks_changed();
    }

    bool        m_show_just_color_change_menu   { false };
    bool        m_show_get_jump_value           { false };
    bool        m_show_color_picker             { false };

    double      m_jump_to_value                 { 0.0 };

    std::string m_print_obj_idxs;
    std::string m_selectable_color;

    void        render_add_tick_menu();
    bool        render_multi_extruders_menu(bool switch_current_code = false);
    bool        render_jump_to_window(const ImVec2& pos, double* active_value, double min_z, double max_z);
    void        render_color_picker();

    std::function<void()>                       m_cb_ticks_changed              { nullptr };
    std::function<std::vector<std::string>()>   m_cb_get_extruder_colors        { nullptr };
    std::function<const Slic3r::Print&()>       m_cb_get_print                  { nullptr };
    std::function<void(const std::string&, const std::string&)> m_cb_change_app_config  { nullptr };
};

} // DoubleSlider;



#endif // slic3r_GUI_DoubleSliderForLayers_hpp_
