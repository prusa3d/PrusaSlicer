///|/ Copyright (c) Prusa Research 2020 - 2023 Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv, Tomáš Mészáros @tamasmeszaros, Lukáš Matěna @lukasmatena, Enrico Turri @enricoturri1966
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "DoubleSliderForLayers.hpp"

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/constants.hpp>
#include <boost/format.hpp>
#include <cmath>
#include <algorithm>
#include <array>
#include <set>
#include <cstdio>

#include "libslic3r/Utils.hpp"  // -> get_time_dhms()
#include "libslic3r/format.hpp" // -> format()
#include "I18N.hpp"
#include "ImGuiWrapper.hpp"
#include "libslic3r/libslic3r.h"
#include "slic3r/GUI/ImGuiDoubleSlider.hpp"
#include "slic3r/GUI/ImGuiPureWrap.hpp"
#include "slic3r/GUI/RulerForDoubleSlider.hpp"
#include "slic3r/GUI/TickCodesManager.hpp"

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui/imgui_internal.h>

using namespace Slic3r;
using namespace CustomGCode;
using Slic3r::format;

namespace DoubleSlider {

//static const float VERTICAL_SLIDER_WIDTH  = 105.0f;

DSForLayers::DSForLayers(   int lowerValue,
                            int higherValue,
                            int minValue,
                            int maxValue,
                            bool allow_editing) :
    m_allow_editing(allow_editing)
{
#ifdef __WXOSX__ 
    is_osx = true;
#endif //__WXOSX__
    Init(lowerValue, higherValue, minValue, maxValue, "layers_slider", false);
    m_ctrl.ShowLabelOnMouseMove(true);

    m_ctrl.set_get_label_on_move_cb([this](int pos) {
        m_pos_on_move = pos; 
        return m_show_estimated_times ? get_label(pos, ltEstimatedTime) : "";
    });
    m_ctrl.set_extra_draw_cb([this](const ImRect& draw_rc) {return draw_ticks(draw_rc); });

    m_ticks.set_values(&m_values);
}

Info DSForLayers::GetTicksValues() const
{
    Info custom_gcode_per_print_z;
    std::vector<CustomGCode::Item>& values = custom_gcode_per_print_z.gcodes;

    const int val_size = m_values.size();
    if (!m_values.empty())
        for (const TickCode& tick : m_ticks.ticks) {
            if (tick.tick > val_size)
                break;
            values.emplace_back(CustomGCode::Item{ m_values[tick.tick], tick.type, tick.extruder, tick.color, tick.extra });
        }

    custom_gcode_per_print_z.mode = m_mode;

    return custom_gcode_per_print_z;
}

void DSForLayers::SetTicksValues(const Info& custom_gcode_per_print_z)
{
    if (m_values.empty()) {
        m_ticks.mode = m_mode;
        return;
    }

    if (m_cb_get_print)
        m_ticks.set_print(m_cb_get_print());

    const bool was_empty = m_ticks.empty();

    m_ticks.set_ticks(custom_gcode_per_print_z);
    
    if (!was_empty && m_ticks.empty())
        // Switch to the "Feature type"/"Tool" from the very beginning of a new object slicing after deleting of the old one
        process_ticks_changed();

    update_draw_scroll_line_cb();
}

void DSForLayers::SetLayersTimes(const std::vector<float>& layers_times, float total_time)
{ 
    m_layers_times.clear();
    if (layers_times.empty())
        return;
    m_layers_times.resize(layers_times.size(), 0.0);
    m_layers_times[0] = layers_times[0];
    for (size_t i = 1; i < layers_times.size(); i++)
        m_layers_times[i] = m_layers_times[i - 1] + layers_times[i];

    // Erase duplicates values from m_values and save it to the m_layers_values
    // They will be used for show the correct estimated time for MM print, when "No sparce layer" is enabled
    // See https://github.com/prusa3d/PrusaSlicer/issues/6232
    if (m_ticks.is_wipe_tower && m_values.size() != m_layers_times.size()) {
        m_layers_values = m_values;
        sort(m_layers_values.begin(), m_layers_values.end());
        m_layers_values.erase(unique(m_layers_values.begin(), m_layers_values.end()), m_layers_values.end());

        // When whipe tower is used to the end of print, there is one layer which is not marked in layers_times
        // So, add this value from the total print time value
        if (m_layers_values.size() != m_layers_times.size())
            for (size_t i = m_layers_times.size(); i < m_layers_values.size(); i++)
                m_layers_times.push_back(total_time);
    }
}

void DSForLayers::SetLayersTimes(const std::vector<double>& layers_times)
{ 
    m_ticks.is_wipe_tower = false;
    m_layers_times = layers_times;
    std::copy(layers_times.begin(), layers_times.end(), m_layers_times.begin());
}

void DSForLayers::SetDrawMode(bool is_sla_print, bool is_sequential_print)
{ 
    m_draw_mode = is_sla_print          ? dmSlaPrint            : 
                  is_sequential_print   ? dmSequentialFffPrint  : 
                                          dmRegular;

    update_draw_scroll_line_cb();
}

void DSForLayers::SetModeAndOnlyExtruder(const bool is_one_extruder_printed_model, const int only_extruder)
{
    m_mode = !is_one_extruder_printed_model ? MultiExtruder :
             only_extruder < 0              ? SingleExtruder :
                                              MultiAsSingle;
    if (!m_ticks.mode || (m_ticks.empty() && m_ticks.mode != m_mode))
        m_ticks.mode = m_mode;

    m_ticks.only_extruder_id = only_extruder;
    m_ticks.is_wipe_tower = m_mode != SingleExtruder;

    if (m_mode != SingleExtruder)
        UseDefaultColors(false);
}

void DSForLayers::SetExtruderColors( const std::vector<std::string>& extruder_colors)
{
    m_ticks.colors = extruder_colors;
}

bool DSForLayers::is_new_print(const std::string& idxs)
{
    if (idxs == "sla" || idxs == m_print_obj_idxs)
        return false;

    m_print_obj_idxs = idxs;
    return true;
}

void DSForLayers::update_draw_scroll_line_cb()
{
    if (m_ticks.empty() || m_draw_mode == dmSequentialFffPrint || m_draw_mode == dmSlaPrint)
        m_ctrl.set_draw_scroll_line_cb(nullptr);
    else
        m_ctrl.set_draw_scroll_line_cb([this](const ImRect& scroll_line, const ImRect& slideable_region) { draw_colored_band(scroll_line, slideable_region); });
}

using namespace ImGui;

void DSForLayers::draw_ticks(const ImRect& slideable_region)
{
    if (m_show_ruler)
        draw_ruler(slideable_region);

    if (m_ticks.empty() || m_draw_mode == dmSlaPrint)
        return;

    // distance form center           begin  end 
    const ImVec2 tick_border = ImVec2(23.0f, 2.0f) * m_scale;

    const float inner_x     = 11.f * m_scale;
    const float outer_x     = 19.f * m_scale;
    const float x_center    = slideable_region.GetCenter().x;

    const float tick_width  = float(int(1.0f * m_scale + 0.5f));
    const float icon_side   = m_imgui->GetTextureCustomRect(ImGui::PausePrint)->Height;
    const float icon_offset = 0.5f * icon_side;;

    const ImU32 tick_clr         = ImGui::ColorConvertFloat4ToU32(m_show_ruler ? ImGuiPureWrap::COL_ORANGE_LIGHT : ImGuiPureWrap::COL_ORANGE_DARK);
    const ImU32 tick_hovered_clr = ImGui::ColorConvertFloat4ToU32(m_show_ruler ? ImGuiPureWrap::COL_ORANGE_DARK : ImGuiPureWrap::COL_WINDOW_BACKGROUND);

    auto get_tick_pos = [this, slideable_region](int tick) {
        return m_ctrl.GetPositionInRect(tick, slideable_region);
    };

    std::set<TickCode>::const_iterator tick_it = m_ticks.ticks.begin();
    bool is_hovered_tick = false;
    while (tick_it != m_ticks.ticks.end())
    {
        float tick_pos = get_tick_pos(tick_it->tick);

        //draw tick hover box when hovered
        ImRect tick_hover_box = ImRect(x_center - tick_border.x, tick_pos - tick_border.y, 
                                       x_center + tick_border.x, tick_pos + tick_border.y - tick_width);

        if (ImGui::IsMouseHoveringRect(tick_hover_box.Min, tick_hover_box.Max)) {
            ImGui::RenderFrame(tick_hover_box.Min, tick_hover_box.Max, tick_hovered_clr, false);
            if (tick_it->type == ColorChange || tick_it->type == ToolChange) {
                m_focus = fiTick;
                ImGuiPureWrap::tooltip(get_tooltip(tick_it->tick), ImGui::GetFontSize() * 20.f);
            }
            is_hovered_tick = true;
            m_ctrl.SetHoveredRegion(tick_hover_box);
            if (m_ctrl.IsLClickOnHoveredPos())
                m_ctrl.IsActiveHigherThumb() ? SetHigherPos(tick_it->tick) : SetLowerPos(tick_it->tick);
            break;
        }
        ++tick_it;
    }
    if (!is_hovered_tick)
        m_ctrl.InvalidateHoveredRegion();

    auto active_tick_it = m_ticks.ticks.find(TickCode{ m_ctrl.GetActivePos() });

    tick_it = m_ticks.ticks.begin();
    while (tick_it != m_ticks.ticks.end())
    {
        float tick_pos = get_tick_pos(tick_it->tick);

        //draw ticks
        ImRect tick_left    = ImRect(x_center - outer_x, tick_pos - tick_width, x_center - inner_x, tick_pos);
        ImRect tick_right   = ImRect(x_center + inner_x, tick_pos - tick_width, x_center + outer_x, tick_pos);
        ImGui::RenderFrame(tick_left.Min, tick_left.Max, tick_clr, false);
        ImGui::RenderFrame(tick_right.Min, tick_right.Max, tick_clr, false);

        ImVec2      icon_pos = ImVec2(m_ctrl.GetCtrlPos().x + GetWidth(), tick_pos - icon_offset);
        std::string btn_label   = "tick " + std::to_string(tick_it->tick);

        //draw tick icon-buttons
        bool activate_this_tick = false;
        if (tick_it == active_tick_it && m_allow_editing) {
            // delete tick
            if (render_button(ImGui::RemoveTick, ImGui::RemoveTickHovered, btn_label, icon_pos, fiActionIcon, tick_it->tick)) {
                m_ticks.ticks.erase(tick_it);
                process_ticks_changed();
                break;
            }
        }        
        else if (m_draw_mode != dmRegular)// if we have non-regular draw mode, all ticks should be marked with error icon
            activate_this_tick = render_button(ImGui::ErrorTick, ImGui::ErrorTickHovered, btn_label, icon_pos, fiTick, tick_it->tick);
        else if (tick_it->type == ColorChange || tick_it->type == ToolChange) {
            if (m_ticks.is_conflict_tick(*tick_it, m_mode, m_values[tick_it->tick]))
                activate_this_tick = render_button(ImGui::ErrorTick, ImGui::ErrorTickHovered, btn_label, icon_pos, fiTick, tick_it->tick);
        }
        else if (tick_it->type == CustomGCode::PausePrint)
            activate_this_tick = render_button(ImGui::PausePrint, ImGui::PausePrintHovered, btn_label, icon_pos, fiTick, tick_it->tick);
        else
            activate_this_tick = render_button(ImGui::EditGCode, ImGui::EditGCodeHovered, btn_label, icon_pos, fiTick, tick_it->tick);

        if (activate_this_tick) {
            m_ctrl.IsActiveHigherThumb() ? SetHigherPos(tick_it->tick) : SetLowerPos(tick_it->tick);
            break;
        }

        ++tick_it;
    }
}

void DSForLayers::draw_ruler(const ImRect& slideable_region)
{
    if (m_values.empty())
        return;

    const double step = double(slideable_region.GetHeight()) / (m_ctrl.GetMaxPos() - m_ctrl.GetMinPos());

    if (!m_ruler.valid())
        m_ruler.init(m_values, step);

    const float inner_x         = 11.f * m_scale;
    const float long_outer_x    = 17.f * m_scale;
    const float short_outer_x   = 14.f * m_scale;
    const float tick_width      = float(int(1.0f * m_scale +0.5f));
    const float label_height    = m_imgui->GetTextureCustomRect(ImGui::PausePrint)->Height;

    constexpr ImU32 tick_clr = IM_COL32(255, 255, 255, 255);

    const float x_center = slideable_region.GetCenter().x;

    double max_val = 0.;
    for (const auto& val : m_ruler.max_values)
        if (max_val < val)
            max_val = val;

    if (m_show_ruler_bg) {
        // draw ruler BG
        ImRect bg_rect = slideable_region;
        bg_rect.Expand(ImVec2(0.f, long_outer_x));
        bg_rect.Min.x -= tick_width;
        bg_rect.Max.x = m_ctrl.GetCtrlPos().x + GetWidth();
        bg_rect.Min.y = m_ctrl.GetCtrlPos().y + label_height;
        bg_rect.Max.y = m_ctrl.GetCtrlPos().y + GetHeight() - label_height;
        const ImU32 bg_color = ImGui::ColorConvertFloat4ToU32(ImVec4(0.13f, 0.13f, 0.13f, 0.5f));

        ImGui::RenderFrame(bg_rect.Min, bg_rect.Max, bg_color, false, 2.f * m_ctrl.rounding());
    }

    auto get_tick_pos = [this, slideable_region](int tick) -> float {
        return m_ctrl.GetPositionInRect(tick, slideable_region);
    };

    auto draw_text = [max_val, x_center, label_height,  long_outer_x, this](const int tick, const float tick_pos)
    {
        ImVec2 start = ImVec2(x_center + long_outer_x + 1, tick_pos - (0.5f * label_height));
        std::string label = get_label(tick, ltHeight, max_val > 100.0 ? "%1$.1f" : "%1$.2f");
        ImGui::RenderText(start, label.c_str());
    };

    auto draw_tick = [x_center, tick_width, inner_x](const float tick_pos, const float outer_x)
    {
        ImRect tick_right = ImRect(x_center + inner_x, tick_pos - tick_width, x_center + outer_x, tick_pos);
        ImGui::RenderFrame(tick_right.Min, tick_right.Max, tick_clr, false);
    };

    auto draw_short_ticks = [this, short_outer_x, draw_tick, get_tick_pos](double& current_tick, int max_tick) 
    {
        if (m_ruler.short_step <= 0.0)
            return;
        while (current_tick < max_tick) {
            float pos = get_tick_pos(lround(current_tick));
            draw_tick(pos, short_outer_x);
            current_tick += m_ruler.short_step;
            if (current_tick > m_ctrl.GetMaxPos())
                break;
        }
    };

    double short_tick = NaNd;
    int tick = 0;
    double value = 0.0;
    size_t sequence = 0;
    float prev_y_pos = -1.f;
    int values_size = (int)m_values.size();

    const float label_shift = 0.5f * label_height;

    if (m_ruler.long_step < 0) {
        // sequential print when long_step wasn't detected because of a lot of printed objects 
        if (m_ruler.max_values.size() > 1) {
            float last_pos = get_tick_pos(m_ctrl.GetMaxPos());
            while (tick <= m_ctrl.GetMaxPos() && sequence < m_ruler.count()) {
                // draw just ticks with max value
                value = m_ruler.max_values[sequence];
                short_tick = tick;

                for (; tick < values_size; tick++) {
                    if (m_values[tick] == value)
                        break;
                    if (m_values[tick] > value) {
                        if (tick > 0)
                            tick--;
                        break;
                    }
                }
                if (tick > m_ctrl.GetMaxPos())
                    break;

                float pos = get_tick_pos(tick);
                draw_tick(pos, long_outer_x);
                if (prev_y_pos < 0 || pos == last_pos || (prev_y_pos - pos >= label_shift && pos - last_pos >= label_shift)) {
                    draw_text(tick, pos);
                    prev_y_pos = pos;
                }
                draw_short_ticks(short_tick, tick);

                sequence++;
                tick++;
            }
        }
        // very short object or some non-trivial ruler with non-regular step (see https://github.com/prusa3d/PrusaSlicer/issues/7263)
        else {
            if (step < 1) // step less then 1 px indicates very tall object with non-regular laayer step (probably in vase mode)
                return;
            for (size_t tick = 1; tick < m_values.size(); tick++) {
                float pos = get_tick_pos(tick);
                draw_tick(pos, long_outer_x);
                draw_text(tick, pos);
            }
        }
    }
    else {
        std::vector<int> last_positions; 
        if (m_ruler.count() == 1)
            last_positions.emplace_back(m_ctrl.GetMaxPos());
        else {
            // fill last positions for each object in sequential print
            last_positions.reserve(m_ruler.count());

            int tick = 0;
            double value = 0.0;
            size_t sequence = 0;

            while (tick <= m_ctrl.GetMaxPos()) {
                value += m_ruler.long_step;

                if (sequence < m_ruler.count() && value > m_ruler.max_values[sequence])
                    value = m_ruler.max_values[sequence];

                for (; tick < values_size; tick++) {
                    if (m_values[tick] == value)
                        break;
                    if (m_values[tick] > value) {
                        if (tick > 0)
                            tick--;
                        break;
                    }
                }
                if (tick > m_ctrl.GetMaxPos())
                    break;

                if (sequence < m_ruler.count() && value == m_ruler.max_values[sequence]) {
                    last_positions.emplace_back(tick);
                    value = 0.0;
                    sequence++;
                    tick++;
                }
            }
        }

        float last_pos = get_tick_pos(last_positions[sequence]);

        while (tick <= m_ctrl.GetMaxPos()) {
            value += m_ruler.long_step;

            if (sequence < m_ruler.count() && value > m_ruler.max_values[sequence])
                value = m_ruler.max_values[sequence];

            short_tick = tick;

            for (; tick < values_size; tick++) {
                if (m_values[tick] == value)
                    break;
                if (m_values[tick] > value) {
                    if (tick > 0)
                        tick--;
                    break;
                }
            }
            if (tick > m_ctrl.GetMaxPos())
                break;

            float pos = get_tick_pos(tick);
            draw_tick(pos, long_outer_x);
            if (prev_y_pos < 0 || pos == last_pos || (prev_y_pos - pos >= label_shift && pos - last_pos >= label_shift) ) {
                draw_text(tick, pos);
                prev_y_pos = pos;
            }

            draw_short_ticks(short_tick, tick);

            if (sequence < m_ruler.count() && value == m_ruler.max_values[sequence]) {
                value = 0.0;
                sequence++;
                tick++;

                if (sequence < m_ruler.count())
                    last_pos = get_tick_pos(last_positions[sequence]);
            }
        }
        // short ticks from the last tick to the end 
        draw_short_ticks(short_tick, m_ctrl.GetMaxPos());
    }

    // draw mose move line
    if (m_pos_on_move > 0) {
        float line_pos = get_tick_pos(m_pos_on_move);

        ImRect move_line = ImRect(x_center + 0.75f * inner_x, line_pos - tick_width, x_center + 1.5f * long_outer_x, line_pos);
        ImGui::RenderFrame(move_line.Min, move_line.Max, ImGui::ColorConvertFloat4ToU32(ImGuiPureWrap::COL_ORANGE_LIGHT), false);
        m_pos_on_move = -1;
    }
}

static std::array<float, 4> decode_color_to_float_array(const std::string color)
{
    auto hex_digit_to_int = [](const char c) {
        return
            (c >= '0' && c <= '9') ? int(c - '0') :
            (c >= 'A' && c <= 'F') ? int(c - 'A') + 10 :
            (c >= 'a' && c <= 'f') ? int(c - 'a') + 10 : -1;
        };

    // set alpha to 1.0f by default
    std::array<float, 4> ret = { 0, 0, 0, 1.0f };
    const char* c = color.data() + 1;
    if (color.size() == 7 && color.front() == '#') {
        for (size_t j = 0; j < 3; ++j) {
            int digit1 = hex_digit_to_int(*c++);
            int digit2 = hex_digit_to_int(*c++);
            if (digit1 == -1 || digit2 == -1) break;
            ret[j] = float(digit1 * 16 + digit2) / 255.0f;
        }
    }
    return ret;
}

std::string encode_color_from_float_array(const std::array<float, 4>& color)
{
    char buffer[64];
    ::sprintf(buffer, "#%02X%02X%02X", int(color[0] * 255.0f), int(color[1] * 255.0f), int(color[2] * 255.0f));
    return std::string(buffer);
}

void DSForLayers::draw_colored_band(const ImRect& groove, const ImRect& slideable_region)
{
    if (m_ticks.empty() || m_draw_mode == dmSequentialFffPrint)
        return;

    ImVec2 blank_padding = ImVec2(0.5f * m_ctrl.GetGrooveRect().GetWidth(), 2.0f * m_scale);
    float  blank_width = 1.0f * m_scale;

    ImRect blank_rect = ImRect(groove.GetCenter().x - blank_width, groove.Min.y, groove.GetCenter().x + blank_width, groove.Max.y);

    ImRect main_band = ImRect(blank_rect);
    main_band.Expand(blank_padding);

    auto draw_band = [this](const ImU32& clr, const ImRect& band_rc) {
        ImGui::RenderFrame(band_rc.Min, band_rc.Max, clr, false, band_rc.GetWidth() * 0.5);
        //cover round corner
        ImGui::RenderFrame(ImVec2(band_rc.Min.x, band_rc.Max.y - band_rc.GetWidth() * 0.5), band_rc.Max, clr, false);

        // add tooltip
        if (ImGui::IsMouseHoveringRect(band_rc.Min, band_rc.Max))
            m_focus = fiColorBand;
    };

    auto draw_main_band = [&main_band](const ImU32& clr) {
        ImGui::RenderFrame(main_band.Min, main_band.Max, clr, false, main_band.GetWidth() * 0.5);
    };

    //draw main colored band
    const int default_color_idx = m_mode == MultiAsSingle ? std::max<int>(m_ticks.only_extruder_id - 1, 0) : 0;
    std::array<float, 4>rgba = decode_color_to_float_array(m_ticks.colors[default_color_idx]);
    ImU32 band_clr = IM_COL32(rgba[0] * 255.0f, rgba[1] * 255.0f, rgba[2] * 255.0f, rgba[3] * 255.0f);
    draw_main_band(band_clr);

    static float tick_pos;
    std::set<TickCode>::const_iterator tick_it = m_ticks.ticks.begin();

    int rclicked_tick = -1;
    while (tick_it != m_ticks.ticks.end())
    {
        //get position from tick
        tick_pos = m_ctrl.GetPositionInRect(tick_it->tick, slideable_region);

        ImRect band_rect = ImRect(ImVec2(main_band.Min.x, std::min(tick_pos, main_band.Min.y)), 
                                  ImVec2(main_band.Max.x, std::min(tick_pos, main_band.Max.y)));

        if (main_band.Contains(band_rect)) {
            if ((m_mode == SingleExtruder && tick_it->type == ColorChange) ||
                (m_mode == MultiAsSingle && (tick_it->type == ToolChange || tick_it->type == ColorChange)))
            {
                const std::string clr_str = m_mode == SingleExtruder ? tick_it->color :
                    tick_it->type == ToolChange ?
                    m_ticks.get_color_for_tool_change_tick(tick_it) :
                    m_ticks.get_color_for_color_change_tick(tick_it);

                if (!clr_str.empty()) {
                    std::array<float, 4>rgba = decode_color_to_float_array(clr_str);
                    ImU32 band_clr = IM_COL32(rgba[0] * 255.0f, rgba[1] * 255.0f, rgba[2] * 255.0f, rgba[3] * 255.0f);
                    if (tick_it->tick == 0)
                        draw_main_band(band_clr);
                    else {
                        draw_band(band_clr, band_rect);

                        ImGuiContext& g = *GImGui;
                        if (ImGui::IsMouseHoveringRect(band_rect.Min, band_rect.Max) && 
                            g.IO.MouseClicked[1] && !m_ctrl.IsRClickOnThumb()) {
                            rclicked_tick = tick_it->tick;
                        }
                    }
                }
            }
        }
        tick_it++;
    }

    if (m_focus == fiColorBand) {
        if (rclicked_tick > 0)
            edit_tick(rclicked_tick);
        else if (auto tip = get_tooltip(); !tip.empty())
            ImGuiPureWrap::tooltip(tip, ImGui::GetFontSize() * 20.f);
    }
}

void DSForLayers::render_menu()
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f) * m_scale);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 4.0f * m_scale);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 1, ImGui::GetStyle().ItemSpacing.y });
    ImGui::PushStyleVar(ImGuiStyleVar_::ImGuiStyleVar_ChildRounding, 4.0f * m_scale);

    ImGui::PushStyleColor(ImGuiCol_::ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

    if (m_ctrl.IsRClickOnThumb())
        ImGui::OpenPopup("slider_full_menu_popup");
    else if (m_show_just_color_change_menu)
        ImGui::OpenPopup("slider_add_tick_menu_popup");
    else if (m_show_cog_menu)
        ImGui::OpenPopup("cog_menu_popup");
    else if (m_show_edit_menu)
        ImGui::OpenPopup("edit_menu_popup");

    if (can_edit())
        render_add_tick_menu();
    render_cog_menu();
    render_edit_menu();

    ImGui::PopStyleColor(1);
    ImGui::PopStyleVar(4);

    ImGuiContext& context = *GImGui;
    if (context.IO.MouseReleased[0]) {
        m_show_just_color_change_menu = false;
        m_show_cog_menu = false;
        m_show_edit_menu = false;
    }
}

void DSForLayers::render_add_tick_menu()
{
    if (ImGui::BeginPopup("slider_full_menu_popup")) {
        if (m_mode == SingleExtruder) {
            if (ImGuiPureWrap::menu_item_with_icon(_u8L("Add Color Change").c_str(), "")) {
                add_code_as_tick(ColorChange);
            }
        }
        else
            render_multi_extruders_menu();

        if (ImGuiPureWrap::menu_item_with_icon(_u8L("Add Pause").c_str(), "")) {
            add_code_as_tick(CustomGCode::PausePrint);
        }
        if (ImGuiPureWrap::menu_item_with_icon(_u8L("Add Custom G-code").c_str(), "")) {
            add_code_as_tick(Custom);
        }
        if (!gcode(Template).empty() &&
            ImGuiPureWrap::menu_item_with_icon(_u8L("Add Custom Template").c_str(), "")) {
            add_code_as_tick(Template);
        }

        ImGui::EndPopup();
        return;
    }

    const std::string longest_menu_name = format(_u8L("Add color change (%1%) for:"), gcode(ColorChange));

    const ImVec2 label_size         = ImGui::CalcTextSize(longest_menu_name.c_str(), NULL, true);
    const ImRect active_thumb_rect  = m_ctrl.GetActiveThumbRect();
    const ImVec2 pos                = active_thumb_rect.GetCenter();

    ImGui::SetNextWindowPos(ImVec2(pos.x - label_size.x - active_thumb_rect.GetWidth(), pos.y));

    if (ImGui::BeginPopup("slider_add_tick_menu_popup")) {
        render_multi_extruders_menu();
        ImGui::EndPopup();
    }
}

bool DSForLayers::render_multi_extruders_menu(bool switch_current_code/* = false*/)
{
    bool ret = false;

    std::vector<std::string> colors;
    if (m_cb_get_extruder_colors)
        colors = m_cb_get_extruder_colors();

    int extruders_cnt = colors.size();

    if (extruders_cnt > 1) {
        const int tick = m_ctrl.GetActivePos();

        if (m_mode == MultiAsSingle) {
            const std::string menu_name = switch_current_code ? _u8L("Switch code to Change extruder") : _u8L("Change extruder");
            if (ImGuiPureWrap::begin_menu(menu_name.c_str())) {
                std::array<int, 2> active_extruders = m_ticks.get_active_extruders_for_tick(tick, m_mode);
                for (int i = 1; i <= extruders_cnt; i++) {
                    const bool is_active_extruder = i == active_extruders[0] || i == active_extruders[1];
                    std::string item_name = format(_u8L("Extruder %d"), i);
                    if (is_active_extruder)
                        item_name += " (" + _u8L("active") + ")";

                    std::array<float, 4> rgba = decode_color_to_float_array(colors[i - 1]);
                    ImU32                icon_clr = IM_COL32(rgba[0] * 255.0f, rgba[1] * 255.0f, rgba[2] * 255.0f, rgba[3] * 255.0f);
                    if (ImGuiPureWrap::menu_item_with_icon(item_name.c_str(), "", ImVec2(14, 14) * m_scale, icon_clr, false, !is_active_extruder)) {
                        add_code_as_tick(ToolChange, i);
                        ret = true;
                    }
                }
                ImGuiPureWrap::end_menu();
            }
        }
 
        const std::string menu_name =   switch_current_code ?
                                        format(_u8L("Switch code to Color change (%1%) for:"), gcode(ColorChange)) :
                                        format(_u8L("Add color change (%1%) for:"), gcode(ColorChange));
        if (ImGuiPureWrap::begin_menu(menu_name.c_str())) {
            std::set<int> used_extruders_for_tick = m_ticks.get_used_extruders_for_tick(tick, m_values[tick]);

            for (int i = 1; i <= extruders_cnt; i++) {
                const bool is_used_extruder = used_extruders_for_tick.empty() ? true : // #ys_FIXME till used_extruders_for_tick doesn't filled correct for mmMultiExtruder
                    used_extruders_for_tick.find(i) != used_extruders_for_tick.end();
                std::string item_name = format(_u8L("Extruder %d"), i);
                if (is_used_extruder)
                    item_name += " (" + _u8L("used") + ")";

                if (ImGuiPureWrap::menu_item_with_icon(item_name.c_str(), "")) {
                    add_code_as_tick(ColorChange, i);
                    ret = true;
                }
            }
            ImGuiPureWrap::end_menu();
        }
    }
    return ret;
}

void DSForLayers::render_color_picker()
{
    ImGuiContext& context = *GImGui;
    const std::string title = ("Select color for Color Change");
    if (m_show_color_picker) {

        ImGuiPureWrap::set_next_window_pos(1200, 200, ImGuiCond_Always, 0.5f, 0.0f);
        ImGuiPureWrap::begin(title, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar
            | ImGuiWindowFlags_NoScrollWithMouse);

        ImGuiColorEditFlags misc_flags = ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_NoDragDrop;

        auto col = decode_color_to_float_array(m_selectable_color);
        if (ImGui::ColorPicker4("color_picker", (float*)&col, misc_flags)) {
            m_selectable_color = encode_color_from_float_array(col);
            m_show_color_picker = false;
        }
        ImGuiPureWrap::end();
    }

    if (auto clr_pcr_win = ImGui::FindWindowByName(title.c_str()); clr_pcr_win && context.CurrentWindow != clr_pcr_win)
        m_show_color_picker = false;
}

void DSForLayers::render_cog_menu()
{
    const ImVec2 icon_sz = ImVec2(14, 14);
    if (ImGui::BeginPopup("cog_menu_popup")) {
        if (ImGuiPureWrap::menu_item_with_icon(_u8L("Jump to height").c_str(), "Shift+G")) {
            jump_to_value();
        }
        if (ImGuiPureWrap::menu_item_with_icon(_u8L("Show estimated print time on hover").c_str(), "", icon_sz, 0, m_show_estimated_times)) {
            m_show_estimated_times = !m_show_estimated_times;
            if (m_cb_change_app_config)
                m_cb_change_app_config("show_estimated_times_in_dbl_slider", m_show_estimated_times ? "1" : "0");
        }
        if (ImGuiPureWrap::menu_item_with_icon(_u8L("Sequential slider applied only to top layer").c_str(), "", icon_sz, 0, m_seq_top_layer_only)) {
            m_seq_top_layer_only = !m_seq_top_layer_only;
            if (m_cb_change_app_config)
                m_cb_change_app_config("seq_top_layer_only", m_seq_top_layer_only ? "1" : "0");
        }
        if (m_mode == MultiAsSingle && m_draw_mode == dmRegular && 
            ImGuiPureWrap::menu_item_with_icon(_u8L("Set extruder sequence for the entire print").c_str(), "")) {
            if (m_ticks.edit_extruder_sequence(m_ctrl.GetMaxPos(), m_mode))
                process_ticks_changed();
        }
        if (ImGuiPureWrap::begin_menu(_u8L("Ruler").c_str())) {
            if (ImGuiPureWrap::menu_item_with_icon(_u8L("Show").c_str(), "", icon_sz, 0, m_show_ruler)) {
                m_show_ruler = !m_show_ruler;
                if (m_show_ruler)
                    m_imgui->set_requires_extra_frame();
                if (m_cb_change_app_config)
                    m_cb_change_app_config("show_ruler_in_dbl_slider", m_show_ruler ? "1" : "0");
            }

            if (ImGuiPureWrap::menu_item_with_icon(_u8L("Show background").c_str(), "", icon_sz, 0, m_show_ruler_bg)) {
                m_show_ruler_bg = !m_show_ruler_bg;
                if (m_cb_change_app_config)
                    m_cb_change_app_config("show_ruler_bg_in_dbl_slider", m_show_ruler_bg ? "1" : "0");
            }

            ImGuiPureWrap::end_menu();
        }
        if (can_edit()) {
            if (ImGuiPureWrap::menu_item_with_icon(_u8L("Use default colors").c_str(), "", icon_sz, 0, m_ticks.used_default_colors())) {
                UseDefaultColors(!m_ticks.used_default_colors());
            }

            if (m_mode != MultiExtruder && m_draw_mode == dmRegular && 
                ImGuiPureWrap::menu_item_with_icon(_u8L("Set auto color changes").c_str(), "")) {
                auto_color_change();
            }
        }

        ImGui::EndPopup();
    }
}

void DSForLayers::render_edit_menu()
{
    if (!m_show_edit_menu)
        return;

    if (m_ticks.has_tick(m_ctrl.GetActivePos()) && ImGui::BeginPopup("edit_menu_popup")) {
        std::set<TickCode>::iterator it = m_ticks.ticks.find(TickCode{ m_ctrl.GetActivePos()});

        if (it->type == ToolChange) {
            if (render_multi_extruders_menu(true)) {
                ImGui::EndPopup();
                return;
            }
        }
        else {
            std::string edit_item_name = it->type == CustomGCode::ColorChange ? _u8L("Edit color") :
                                         it->type == CustomGCode::PausePrint  ? _u8L("Edit pause print message") :
                                                                                _u8L("Edit custom G-code");
            if (ImGuiPureWrap::menu_item_with_icon(edit_item_name.c_str(), "")) {
                edit_tick();
                ImGui::EndPopup();
                return;
            }
        }

        if (it->type == ColorChange && m_mode == MultiAsSingle) {
            if (render_multi_extruders_menu(true)) {
                ImGui::EndPopup();
                return;
            }
        }

        std::string delete_item_name =  it->type == CustomGCode::ColorChange ? _u8L("Delete color change") :
                                        it->type == CustomGCode::ToolChange  ? _u8L("Delete tool change") :
                                        it->type == CustomGCode::PausePrint  ? _u8L("Delete pause print") :
                                                                               _u8L("Delete custom G-code");
        if (ImGuiPureWrap::menu_item_with_icon(delete_item_name.c_str(), ""))
            delete_current_tick();

        ImGui::EndPopup();
    }
}

bool DSForLayers::render_button(const wchar_t btn_icon, const wchar_t btn_icon_hovered, const std::string& label_id, const ImVec2& pos, FocusedItem focus, int tick /*= -1*/)
{
    ImGui::PushStyleVar(ImGuiStyleVar_::ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_::ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_::ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::PushStyleColor(ImGuiCol_::ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));

    int windows_flag =    ImGuiWindowFlags_NoTitleBar
                        | ImGuiWindowFlags_NoCollapse
                        | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_NoResize
                        | ImGuiWindowFlags_NoScrollbar
                        | ImGuiWindowFlags_NoScrollWithMouse
                        | ImGuiWindowFlags_NoFocusOnAppearing;

    ImGuiPureWrap::set_next_window_pos(pos.x, pos.y, ImGuiCond_Always);
    std::string win_name = label_id + "##btn_win";
    ImGuiPureWrap::begin(win_name, windows_flag);

    ImGuiContext& g = *GImGui;

    m_focus = focus;
    std::string tooltip = m_allow_editing ? get_tooltip(tick) : "";
    ImGui::SetCursorPos(ImVec2(0, 0));
    const bool ret = m_imgui->image_button(g.HoveredWindow == g.CurrentWindow ? btn_icon_hovered : btn_icon, tooltip, false);

    if (tick > 0 && tick == m_ctrl.GetActivePos() && g.HoveredWindow == g.CurrentWindow && g.IO.MouseClicked[1])
        m_show_edit_menu = true;

    ImGuiPureWrap::end();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);

    return ret;
}

bool DSForLayers::render_jump_to_window(const ImVec2& pos, double* active_value, double min_z, double max_z)
{
    if (!m_show_get_jump_value)
        return false;

    const std::string   msg_text    = _u8L("Enter the height you want to jump to") + ":";
    const std::string   win_name    = _u8L("Jump to height") + "##btn_win";
    const ImVec2        msg_size    = ImGui::CalcTextSize(msg_text.c_str(), NULL, true);

    const float         ctrl_pos_x  = msg_size.x + 15 * m_scale;
    const float         ctrl_width  = 50.f * m_scale;

    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 12.0f, 8.0f });

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.13f, 0.13f, 0.13f, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));

    ImGuiWindowFlags windows_flag =   ImGuiWindowFlags_NoCollapse
                                    | ImGuiWindowFlags_NoMove
                                    | ImGuiWindowFlags_NoResize
                                    | ImGuiWindowFlags_NoScrollbar
                                    | ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::Begin(win_name.c_str(), &m_show_get_jump_value, windows_flag);

    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", msg_text.c_str());
    ImGui::SameLine(ctrl_pos_x);
    ImGui::PushItemWidth(ctrl_width);

    ImGui::InputDouble("##jump_to", active_value, 0.0, 0.0, "%.2f", ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_AutoSelectAll);
    //check if Enter was pressed
    bool enter_pressed = ImGui::IsItemDeactivatedAfterEdit();

    //check out of range
    bool disable_ok = *active_value < min_z || *active_value > max_z;

    ImGui::Text("%s", "");
    ImGui::SameLine(ctrl_pos_x);

    if (disable_ok) {
        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
    }

    bool ok_pressed = ImGui::Button("OK##jump_to", ImVec2(ctrl_width, 0.f));

    if (disable_ok) {
        ImGui::PopItemFlag();
        ImGui::PopStyleVar();
    }

    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);

    return enter_pressed || ok_pressed;
}

void DSForLayers::Render(const int canvas_width, const int canvas_height, float extra_scale/* = 0.1f*/, float offset /*= 0.f*/)
{
    if (!m_ctrl.IsShown())
        return;
    m_scale = extra_scale * 0.1f * m_em;

    m_ruler.set_scale(m_scale);

    const float action_btn_sz   = m_imgui->GetTextureCustomRect(ImGui::DSRevert)->Height;
    const float tick_icon_side  = m_imgui->GetTextureCustomRect(ImGui::PausePrint)->Height;

    ImVec2 pos;

    const float VERTICAL_SLIDER_WIDTH = m_show_ruler ? 125.f : 105.0f;

    pos.x = canvas_width - VERTICAL_SLIDER_WIDTH * m_scale - tick_icon_side;
    pos.y = 1.5f * action_btn_sz + offset;
    if (m_allow_editing)
        pos.y += 2.f;

    ImVec2 size = ImVec2(VERTICAL_SLIDER_WIDTH * m_scale, canvas_height - 4.f * action_btn_sz - offset);

    m_ctrl.Init(pos, size, m_scale, m_show_ruler);
    if (m_ctrl.render()) {
        // request one more frame if value was changes with mouse wheel
        if (GImGui->IO.MouseWheel != 0.0f)
            m_imgui->set_requires_extra_frame();
        process_thumb_move();

        // discard all getters dialogs
        m_show_get_jump_value = false;
    }
    else if (m_ctrl.IsLClickOnThumb() && can_edit() &&
             !m_ticks.has_tick(m_ctrl.GetActivePos())) {
        add_code_as_tick(ColorChange);
    }

    // draw action buttons

    const float groove_center_x = m_ctrl.GetGrooveRect().GetCenter().x;

    ImVec2 btn_pos = ImVec2(groove_center_x - 0.5f * action_btn_sz, pos.y - 0.75f * action_btn_sz);

    if (!m_ticks.empty() && can_edit() &&
        render_button(ImGui::DSRevert, ImGui::DSRevertHovered, "revert", btn_pos, fiRevertIcon))
        discard_all_thicks();

    btn_pos.y += 0.5f * action_btn_sz + size.y;
    const bool is_one_layer = m_ctrl.IsCombineThumbs();
    if (render_button(is_one_layer ? ImGui::Lock : ImGui::Unlock, is_one_layer ? ImGui::LockHovered : ImGui::UnlockHovered, "one_layer", btn_pos, fiOneLayerIcon))
        ChangeOneLayerLock();

    btn_pos.y += 1.2f * action_btn_sz;
    if (render_button(ImGui::DSSettings, ImGui::DSSettingsHovered, "settings", btn_pos, fiCogIcon)) {
        m_show_cog_menu = true;
    }

    if (m_draw_mode == dmSequentialFffPrint && m_ctrl.IsRClickOnThumb()) {
        std::string tooltip = _u8L("The sequential print is on.\n"
                                   "It's impossible to apply any custom G-code for objects printing sequentually.");
        ImGuiPureWrap::tooltip(tooltip, ImGui::GetFontSize() * 20.0f);
    }
    else
        render_menu();

    if (render_jump_to_window(ImVec2(0.5f * canvas_width, 0.5f*canvas_height), &m_jump_to_value,
                              m_values[m_ctrl.GetMinPos()], m_values[m_ctrl.GetMaxPos()]))
        process_jump_to_value();

    if (can_edit())
        render_color_picker();
}

void DSForLayers::force_ruler_update()
{
    m_ruler.invalidate();
}

bool DSForLayers::is_wipe_tower_layer(int tick) const
{
    if (!m_ticks.is_wipe_tower || tick >= (int)m_values.size())
        return false;
    if (tick == 0 || (tick == (int)m_values.size() - 1 && m_values[tick] > m_values[tick - 1]))
        return false;
    if ((m_values[tick - 1] == m_values[tick + 1] && m_values[tick] < m_values[tick + 1]) ||
        (tick > 0 && m_values[tick] < m_values[tick - 1]) ) // if there is just one wiping on the layer 
        return true;

    return false;
}

static std::string short_and_splitted_time(const std::string& time)
{
    // Parse the dhms time format.
    int days = 0;
    int hours = 0;
    int minutes = 0;
    int seconds = 0;
    if (time.find('d') != std::string::npos)
        ::sscanf(time.c_str(), "%dd %dh %dm %ds", &days, &hours, &minutes, &seconds);
    else if (time.find('h') != std::string::npos)
        ::sscanf(time.c_str(), "%dh %dm %ds", &hours, &minutes, &seconds);
    else if (time.find('m') != std::string::npos)
        ::sscanf(time.c_str(), "%dm %ds", &minutes, &seconds);
    else if (time.find('s') != std::string::npos)
        ::sscanf(time.c_str(), "%ds", &seconds);

    // Format the dhm time.
    auto get_d = [days]()   { return format(_u8L("%1%d"), days); };
    auto get_h = [hours]()  { return format(_u8L("%1%h"), hours); };
    auto get_m = [minutes](){ return format(_u8L("%1%m"), minutes); };
    auto get_s = [seconds](){ return format(_u8L("%1%s"), seconds); };

    if (days > 0)
        return format("%1%%2%\n%3%", get_d(), get_h(), get_m());
    if (hours > 0) {
        if (hours < 10 && minutes < 10 && seconds < 10)
            return format("%1%%2%%3%", get_h(), get_m(), get_s());
        if (hours > 10 && minutes > 10 && seconds > 10)
            return format("%1%\n%2%\n%3%", get_h(), get_m(), get_s());
        if ((minutes < 10 && seconds > 10) || (minutes > 10 && seconds < 10))
            return format("%1%\n%2%%3%", get_h(), get_m(), get_s());
        return format("%1%%2%\n%3%", get_h(), get_m(), get_s());
    }
    if (minutes > 0) {
        if (minutes > 10 && seconds > 10)
            return format("%1%\n%2%", get_m(), get_s());
        return format("%1%%2%", get_m(), get_s());
    }
    return get_s();
}

std::string DSForLayers::get_label(int pos, LabelType label_type, const std::string& fmt/* = "%1$.2f"*/) const
{
    const size_t value = pos;

    if (m_values.empty())
        return format("%1%", pos);
    if (value >= m_values.size())
        return "ErrVal";

    // When "Print Settings -> Multiple Extruders -> No sparse layer" is enabled, then "Smart" Wipe Tower is used for wiping.
    // As a result, each layer with tool changes is splited for min 3 parts: first tool, wiping, second tool ...
    // So, vertical slider have to respect to this case.
    // see https://github.com/prusa3d/PrusaSlicer/issues/6232.
    // m_values contains data for all layer's parts,
    // but m_layers_values contains just unique Z values.
    // Use this function for correct conversion slider position to number of printed layer
    auto get_layer_number = [this](int value, LabelType label_type) {
        if (label_type == ltEstimatedTime && m_layers_times.empty())
            return size_t(-1);
        double layer_print_z = m_values[is_wipe_tower_layer(value) ? std::max<int>(value - 1, 0) : value];
        auto it = std::lower_bound(m_layers_values.begin(), m_layers_values.end(), layer_print_z - epsilon());
        if (it == m_layers_values.end()) {
            it = std::lower_bound(m_values.begin(), m_values.end(), layer_print_z - epsilon());
            if (it == m_values.end())
                return size_t(-1);
            return size_t(value);
        }
        return size_t(it - m_layers_values.begin());
    };

    if (label_type == ltEstimatedTime) {
        if (m_ticks.is_wipe_tower) {
            size_t layer_number = get_layer_number(value, label_type);
            return (layer_number == size_t(-1) || layer_number == m_layers_times.size()) ? "" : short_and_splitted_time(get_time_dhms(m_layers_times[layer_number]));
        }
        return value < m_layers_times.size() ? short_and_splitted_time(get_time_dhms(m_layers_times[value])) : "";
    }
    std::string str = format(fmt, m_values[value]);
    if (label_type == ltHeight)
        return str;
    if (label_type == ltHeightWithLayer) {
        size_t layer_number = m_ticks.is_wipe_tower ? get_layer_number(value, label_type) + 1 : (m_values.empty() ? value : value + 1);
        return format("%1%\n(%2%)", str, layer_number);
    }    

    return "";
}

void DSForLayers::ChangeOneLayerLock()
{
    m_ctrl.CombineThumbs(!m_ctrl.IsCombineThumbs()); 
    process_thumb_move();
}

std::string DSForLayers::get_tooltip(int tick/*=-1*/)
{
    if (m_focus == fiNone)
        return "";
    if (m_focus == fiOneLayerIcon)
        return _u8L("One layer mode");
    if (m_focus == fiRevertIcon)
        return _u8L("Discard all custom changes");
    if (m_focus == fiCogIcon)
    {
        return m_mode == MultiAsSingle ?
        (boost::format(_u8L("Jump to height %s\n"
                            "Set ruler mode\n"
                            "or Set extruder sequence for the entire print")) % "(Shift + G)").str() :
        (boost::format(_u8L("Jump to height %s\n"
                            "or Set ruler mode")) % "(Shift + G)").str();
    }
    if (m_focus == fiColorBand)
        return m_mode != SingleExtruder || !can_edit() ? "" :
               _u8L("Edit current color - Right click the colored slider segment");
    if (m_focus == fiSmartWipeTower)
        return _u8L("This is wipe tower layer");
    if (m_draw_mode == dmSlaPrint)
        return ""; // no drawn ticks and no tooltips for them in SlaPrinting mode

    std::string tooltip;
    const auto tick_code_it = m_ticks.ticks.find(TickCode{tick});

    if (tick_code_it == m_ticks.ticks.end() && m_focus == fiActionIcon)    // tick doesn't exist
    {
        if (m_draw_mode == dmSequentialFffPrint)
            return  (_u8L("The sequential print is on.\n"
                        "It's impossible to apply any custom G-code for objects printing sequentually.") + "\n");

        // Show mode as a first string of tooltop
        tooltip = "    " + _u8L("Print mode") + ": ";
        tooltip += (m_mode == SingleExtruder ? SingleExtruderMode :
                    m_mode == MultiAsSingle  ? MultiAsSingleMode  :
                                               MultiExtruderMode );
        tooltip += "\n\n";

        /* Note: just on OSX!!!
         * Right click event causes a little scrolling.
         * So, as a workaround we use Ctrl+LeftMouseClick instead of RightMouseClick
         * Show this information in tooltip
         * */

        // Show list of actions with new tick
        tooltip += ( m_mode == MultiAsSingle                                ?
                  _u8L("Add extruder change - Left click")                    :
                     m_mode == SingleExtruder                               ?
                  _u8L("Add color change - Left click for predefined color or "
                     "Shift + Left click for custom color selection")       :
                  _u8L("Add color change - Left click")  ) + " " +
                  _u8L("or press \"+\" key") + "\n" + (
                     is_osx ? 
                  _u8L("Add another code - Ctrl + Left click") :
                  _u8L("Add another code - Right click") );
    }

    if (tick_code_it != m_ticks.ticks.end())                                    // tick exists
    {
        if (m_draw_mode == dmSequentialFffPrint)
            return _u8L("The sequential print is on.\n"
                        "It's impossible to apply any custom G-code for objects printing sequentually.\n" 
                        "This code won't be processed during G-code generation.");
        
        // Show custom Gcode as a first string of tooltop
        std::string space = "   ";
        tooltip = space;
        auto format_gcode = [space](std::string gcode) -> std::string {
            // when the tooltip is too long, it starts to flicker, see: https://github.com/prusa3d/PrusaSlicer/issues/7368
            // so we limit the number of lines shown
            std::vector<std::string> lines;
            boost::split(lines, gcode, boost::is_any_of("\n"), boost::token_compress_off);
            static const size_t MAX_LINES = 10;
            if (lines.size() > MAX_LINES) {
                gcode = lines.front() + '\n';
                for (size_t i = 1; i < MAX_LINES; ++i) {
                    gcode += lines[i] + '\n';
                }
                gcode += "[" + _u8L("continue") + "]\n";
            }
            boost::replace_all(gcode, "\n", "\n" + space);
            return gcode;
        };
        tooltip +=  
        	tick_code_it->type == ColorChange ?
        		(m_mode == SingleExtruder && tick_code_it->extruder==1 ?
                	format(_u8L("Color change (\"%1%\")"), gcode(ColorChange)) :
                    format(_u8L("Color change (\"%1%\") for Extruder %2%"), gcode(ColorChange), tick_code_it->extruder)) :
	            tick_code_it->type == CustomGCode::PausePrint ?
	                format(_u8L("Pause print (\"%1%\")"), gcode(CustomGCode::PausePrint)) :
	            tick_code_it->type == Template ?
	                format(_u8L("Custom template (\"%1%\")"), gcode(Template)) :
		            tick_code_it->type == ToolChange ?
		                format(_u8L("Extruder (tool) is changed to Extruder \"%1%\""), tick_code_it->extruder) :                
		                format_gcode(tick_code_it->extra);// tick_code_it->type == Custom

        // If tick is marked as a conflict (exclamation icon),
        // we should to explain why
        ConflictType conflict = m_ticks.is_conflict_tick(*tick_code_it, m_mode, m_values[tick]);
        if (conflict != ctNone)
            tooltip += "\n\n" + _u8L("Note") + "! ";
        if (conflict == ctModeConflict)
            tooltip += _u8L("G-code associated to this tick mark is in a conflict with print mode.\n"
                           "Editing it will cause changes of Slider data.");
        else if (conflict == ctMeaninglessColorChange)
            tooltip += _u8L("There is a color change for extruder that won't be used till the end of print job.\n"
                           "This code won't be processed during G-code generation.");
        else if (conflict == ctMeaninglessToolChange)
            tooltip += _u8L("There is an extruder change set to the same extruder.\n"
                           "This code won't be processed during G-code generation.");
        else if (conflict == ctNotPossibleToolChange)
            tooltip += _u8L("There is an extruder change set to a non-existing extruder.\n"
                           "This code won't be processed during G-code generation.");
        else if (conflict == ctRedundant)
            tooltip += _u8L("There is a color change for extruder that has not been used before.\n"
                           "Check your settings to avoid redundant color changes.");

        // Show list of actions with existing tick
        if (m_focus == fiActionIcon)
        tooltip += "\n\n" + _u8L("Delete tick mark - Left click or press \"-\" key") + "\n" + (
                      is_osx ? 
                   _u8L("Edit tick mark - Ctrl + Left click") :
                   _u8L("Edit tick mark - Right click") );
    }

    return tooltip;
}

void DSForLayers::UseDefaultColors(bool def_colors_on)
{
    m_ticks.set_default_colors(def_colors_on);
}

// !ysFIXME draw with imgui
void DSForLayers::auto_color_change()
{
    if (m_ticks.auto_color_change(m_mode)) {
        update_draw_scroll_line_cb();
        process_ticks_changed();
    }
}

void DSForLayers::add_code_as_tick(Type type, int selected_extruder/* = -1*/)
{
    const int tick = m_ctrl.GetActivePos();

    if (!m_ticks.check_ticks_changed_event(type, m_mode)) {
        process_ticks_changed();
        return;
    }

    const int extruder = selected_extruder > 0 ? selected_extruder : std::max<int>(1, m_ticks.only_extruder_id);
    const auto it = m_ticks.ticks.find(TickCode{ tick });

    bool was_ticks = m_ticks.empty();
    
    if ( it == m_ticks.ticks.end() ) {
        // try to add tick
        if (!m_ticks.add_tick(tick, type, extruder, m_values[tick]))
            return;
    }
    else if (type == ToolChange || type == ColorChange) {
        // try to switch tick code to ToolChange or ColorChange accordingly
        if (!m_ticks.switch_code_for_tick(it, type, extruder))
            return;
    }
    else
        return;

    if (was_ticks != m_ticks.empty())
        update_draw_scroll_line_cb();

    m_show_just_color_change_menu = false;
    process_ticks_changed();
}

void DSForLayers::add_current_tick()
{
    if (!can_edit())
        return;

    const int tick = m_ctrl.GetActivePos();
    auto it = m_ticks.ticks.find(TickCode{ tick });

    if (it != m_ticks.ticks.end())    // this tick is already exist
        return;
    if (!m_ticks.check_ticks_changed_event(m_mode == MultiAsSingle ? ToolChange : ColorChange, m_mode)) {
        process_ticks_changed();
        return;
    }

    if (m_mode == SingleExtruder)
        add_code_as_tick(ColorChange);
    else {
        m_show_just_color_change_menu = true;
        m_imgui->set_requires_extra_frame();
    }
}

void DSForLayers::delete_current_tick()
{
    auto it = m_ticks.ticks.find(TickCode{ m_ctrl.GetActivePos()});
    if (it == m_ticks.ticks.end())    // this tick doesn't exist
        return;

    m_ticks.ticks.erase(it);
    process_ticks_changed();
}

void DSForLayers::edit_tick(int tick/* = -1*/)
{
    if (tick < 0)
        tick = m_ctrl.GetActivePos();
    const std::set<TickCode>::iterator it = m_ticks.ticks.find(TickCode{ tick });

    if (it == m_ticks.ticks.end())    // this tick doesn't exist
        return;

    if (!m_ticks.check_ticks_changed_event(it->type, m_mode) ||
        m_ticks.edit_tick(it, m_values[it->tick]))
        process_ticks_changed();
}

// discard all custom changes on DoubleSlider
void DSForLayers::discard_all_thicks()
{
    m_ticks.ticks.clear();
    m_ctrl.ResetPositions();
    update_draw_scroll_line_cb();
    process_ticks_changed();
}

void DSForLayers::jump_to_value()
{
    //Init "jump to value";
    m_show_get_jump_value = true;
    m_jump_to_value = m_values[m_ctrl.GetActivePos()];

    m_imgui->set_requires_extra_frame();
}

void DSForLayers::process_jump_to_value()
{
    if (int tick_value = m_ticks.get_tick_from_value(m_jump_to_value, true); tick_value > 0.0) {
        m_show_get_jump_value = false;

        if (m_ctrl.IsActiveHigherThumb())
            SetHigherPos(tick_value);
        else
            SetLowerPos(tick_value);
    }
}

bool DSForLayers::can_edit() const
{
    return m_allow_editing && m_draw_mode != dmSlaPrint;
}

} // DoubleSlider


