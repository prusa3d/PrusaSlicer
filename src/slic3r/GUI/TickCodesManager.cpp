///|/ Copyright (c) Prusa Research 2020 - 2023 Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv, Tomáš Mészáros @tamasmeszaros, Lukáš Matěna @lukasmatena, Enrico Turri @enricoturri1966
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "TickCodesManager.hpp"

#include <algorithm>
#include <random>
#include <cmath>

#include "I18N.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/Color.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/GCode/ToolOrdering.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/libslic3r.h"

using namespace Slic3r;
using namespace CustomGCode;

namespace DoubleSlider {

constexpr double min_delta_area = scale_(scale_(25));  // equal to 25 mm2
constexpr double miscalculation = scale_(scale_(1));   // equal to 1 mm2

static const int YES    = 0x00000002; // an analogue of wxYES   
static const int NO     = 0x00000008; // an analogue of wxNO    
static const int CANCEL = 0x00000010; // an analogue of wxCANCEL

bool equivalent_areas(const double& bottom_area, const double& top_area)
{
    return fabs(bottom_area - top_area) <= miscalculation;
}

TickCodeManager::TickCodeManager()
{
    m_pause_print_msg = _u8L("Place bearings in slots and resume printing");
}

std::string TickCodeManager::gcode(CustomGCode::Type type)
{
    if (m_print) {
        const Slic3r::PrintConfig& config = m_print->config();
        switch (type) {
        case CustomGCode::ColorChange: return config.color_change_gcode;
        case CustomGCode::PausePrint:  return config.pause_print_gcode;
        case CustomGCode::Template:    return config.template_custom_gcode;
        default:          return std::string();
        }
    }
    return std::string();
}

int TickCodeManager::get_tick_from_value(double value, bool force_lower_bound/* = false*/)
{
    if (!m_values)
        return -1;
    std::vector<double>::const_iterator it;
    if (is_wipe_tower && !force_lower_bound)
        it = std::find_if(m_values->begin(), m_values->end(),
                          [value](const double & val) { return fabs(value - val) <= epsilon(); });
    else
        it = std::lower_bound(m_values->begin(), m_values->end(), value - epsilon());

    if (it == m_values->end())
        return -1;
    return int(it - m_values->begin());
}

void TickCodeManager::set_ticks(const Info& custom_gcode_per_print_z)
{
    ticks.clear();

    const std::vector<CustomGCode::Item>& heights = custom_gcode_per_print_z.gcodes;
    for (auto h : heights) {
        int tick = get_tick_from_value(h.print_z);
        if (tick >=0)
            ticks.emplace(TickCode{ tick, h.type, h.extruder, h.color, h.extra });
    }

    if (custom_gcode_per_print_z.mode && !custom_gcode_per_print_z.gcodes.empty())
        mode = custom_gcode_per_print_z.mode;
}

// Get active extruders for tick. 
// Means one current extruder for not existing tick OR 
// 2 extruders - for existing tick (extruder before ToolChange and extruder of current existing tick)
// Use those values to disable selection of active extruders
std::array<int, 2> TickCodeManager::get_active_extruders_for_tick(int tick, Mode main_mode) const
{
    int default_initial_extruder = main_mode == MultiAsSingle ? std::max<int>(1, only_extruder_id) : 1;
    std::array<int, 2> extruders = { default_initial_extruder, -1 };
    if (empty())
        return extruders;

    auto it = ticks.lower_bound(TickCode{tick});

    if (it != ticks.end() && it->tick == tick) // current tick exists
        extruders[1] = it->extruder;

    while (it != ticks.begin()) {
        --it;
        if(it->type == ToolChange) {
            extruders[0] = it->extruder;
            break;
        }
    }

    return extruders;
}

bool check_color_change(const PrintObject* object, size_t frst_layer_id, size_t layers_cnt, bool check_overhangs, std::function<bool(const Layer*)> break_condition)
{
    double prev_area = area(object->get_layer(frst_layer_id)->lslices);

    bool detected = false;
    for (size_t i = frst_layer_id+1; i < layers_cnt; i++) {
        const Layer* layer = object->get_layer(i);
        double cur_area = area(layer->lslices);

        // check for overhangs
        if (check_overhangs && cur_area > prev_area && !equivalent_areas(prev_area, cur_area))
            break;

        // Check percent of the area decrease.
        // This value have to be more than min_delta_area and more then 10%
        if ((prev_area - cur_area > min_delta_area) && (cur_area / prev_area < 0.9)) {
            detected = true;
            if (break_condition(layer))
                break;
        }

        prev_area = cur_area;
    }
    return detected;
}

bool TickCodeManager::auto_color_change(Mode main_mode)
{
    if (!m_print)
        return false;

    if (!empty()) {
        if (m_cb_show_warning_msg) {
            std::string msg_text = _u8L("This action will cause deletion of all ticks on vertical slider.") + "\n\n" +
                                   _u8L("This action is not revertible.\nDo you want to proceed?");
            if (m_cb_show_warning_msg(msg_text, YES | NO) == NO)
                return false;
        }
        ticks.clear();
    }

    int extruders_cnt = m_cb_get_extruders_cnt ? m_cb_get_extruders_cnt() : 0;

    for (auto object : m_print->objects()) {
        // An object should to have at least 2 layers to apply an auto color change
        if (object->layer_count() < 2)
            continue;

        check_color_change(object, 1, object->layers().size(), false, [this, extruders_cnt, main_mode](const Layer* layer)
        {
            int tick = get_tick_from_value(layer->print_z);
            if (tick >= 0 && !has_tick(tick)) {
                if (main_mode == SingleExtruder) {
                    set_default_colors(true);
                    add_tick(tick, ColorChange, 1, layer->print_z);
                }
                else {
                    int extruder = 2;
                    if (!empty()) {
                        auto it = ticks.end();
                        it--;
                        extruder = it->extruder + 1;
                        if (extruder > extruders_cnt)
                            extruder = 1;
                    }
                    add_tick(tick, ToolChange, extruder, layer->print_z);
                }
            }
            // allow max 3 auto color changes
            return ticks.size() > 2;
        });
    }

    if (empty() && m_cb_notify_empty_color_change)
        m_cb_notify_empty_color_change();

    return true;
}

std::string TickCodeManager::get_new_color(const std::string& color)
{
    if (m_cb_get_new_color)
        return m_cb_get_new_color(color);
    return std::string();
}

std::string TickCodeManager::get_custom_code(const std::string& code_in, double height)
{
    if (m_cb_get_custom_code) 
        return m_cb_get_custom_code(code_in, height);
    return std::string();
}

std::string TickCodeManager::get_pause_print_msg(const std::string& msg_in, double height)
{
    if (m_cb_get_pause_print_msg)
        return m_cb_get_pause_print_msg(msg_in, height);
    return std::string();
}

bool TickCodeManager::edit_extruder_sequence(const int max_tick, Mode main_mode)
{
    if (!check_ticks_changed_event(ToolChange, main_mode) || !m_cb_get_extruders_sequence)
        return false;

    // init extruder sequence in respect to the extruders count 
    if (empty())
        m_extruders_sequence.init(colors.size());

    if(!m_cb_get_extruders_sequence(m_extruders_sequence))
        return false;

    erase_all_ticks_with_code(ToolChange);

    const int extr_cnt = m_extruders_sequence.extruders.size();
    if (extr_cnt == 1)
        return true;

    int tick = 0;
    double value = 0.0;
    int extruder = -1;

    std::random_device rd;  //Will be used to obtain a seed for the random number engine
    std::mt19937 gen(rd()); //Standard mersenne_twister_engine seeded with rd()
    std::uniform_int_distribution<> distrib(0, extr_cnt-1);

    while (tick <= max_tick)
    {
        bool color_repetition = false;
        if (m_extruders_sequence.random_sequence) {
            int rand_extr = distrib(gen);
            if (m_extruders_sequence.color_repetition)
                color_repetition = rand_extr == extruder;
            else
                while (rand_extr == extruder)
                    rand_extr = distrib(gen);
            extruder = rand_extr;
        }
        else {
            extruder++;
            if (extruder == extr_cnt)
                extruder = 0;
        }

        const int cur_extruder = m_extruders_sequence.extruders[extruder];

        bool meaningless_tick = tick == 0.0 && cur_extruder == extruder;
        if (!meaningless_tick && !color_repetition)
            ticks.emplace(TickCode{tick, ToolChange,cur_extruder + 1, colors[cur_extruder]});

        if (m_extruders_sequence.is_mm_intervals) {
            value += m_extruders_sequence.interval_by_mm;
            tick = get_tick_from_value(value, true);
            if (tick < 0)
                break;
        }
        else
            tick += m_extruders_sequence.interval_by_layers;
    }

    return true;
}

bool TickCodeManager::check_ticks_changed_event(Type type, Mode main_mode)
{
    if ( mode == main_mode                                                     ||
        (type != ColorChange && type != ToolChange)                       ||
        (mode == SingleExtruder && main_mode == MultiAsSingle) || // All ColorChanges will be applied for 1st extruder
        (mode == MultiExtruder  && main_mode == MultiAsSingle) )  // Just mark ColorChanges for all unused extruders
        return true;

    if ((mode == SingleExtruder && main_mode == MultiExtruder ) ||
        (mode == MultiExtruder  && main_mode == SingleExtruder)    )
    {
        if (!has_tick_with_code(ColorChange))
            return true;

        if (m_cb_show_info_msg) {
            std::string message = (mode == SingleExtruder ?
                            _u8L("The last color change data was saved for a single extruder printing.") :
                            _u8L("The last color change data was saved for a multi extruder printing.") 
                            ) + "\n" +
                            _u8L("Your current changes will delete all saved color changes.") + "\n\n\t" +
                            _u8L("Are you sure you want to continue?");

            if ( m_cb_show_info_msg(message, YES | NO) == YES)
                erase_all_ticks_with_code(ColorChange);
        }
        return false;
    }
    //          m_ticks_mode == MultiAsSingle
    if( has_tick_with_code(ToolChange) ) {
        if (m_cb_show_info_msg) {
            std::string message =  main_mode == SingleExtruder ?                          (
                            _u8L("The last color change data was saved for a multi extruder printing.") + "\n\n" +
                            _u8L("Select YES if you want to delete all saved tool changes, \n"
                               "NO if you want all tool changes switch to color changes, \n"
                               "or CANCEL to leave it unchanged.") + "\n\n\t" +
                            _u8L("Do you want to delete all saved tool changes?")  
                            ): ( // MultiExtruder
                            _u8L("The last color change data was saved for a multi extruder printing with tool changes for whole print.") + "\n\n" +
                            _u8L("Your current changes will delete all saved extruder (tool) changes.") + "\n\n\t" +
                            _u8L("Are you sure you want to continue?")                  ) ;

            const int answer = m_cb_show_info_msg(message, YES | NO | (main_mode == SingleExtruder ? CANCEL : 0));
            if (answer == YES) {
                erase_all_ticks_with_code(ToolChange);
            }
            else if (main_mode == SingleExtruder && answer == NO) {
                switch_code(ToolChange, ColorChange);
            }
        }
        return false;
    }

    if (m_cb_check_gcode_and_notify)
        m_cb_check_gcode_and_notify(type);

    return true;
}




// Get used extruders for tick. 
// Means all extruders(tools) which will be used during printing from current tick to the end
std::set<int> TickCodeManager::get_used_extruders_for_tick(int tick, double print_z, Mode force_mode/* = Undef*/) const
{
    if (!m_print)
        return {};

    Mode e_mode = !force_mode ? mode : force_mode;

    if (e_mode == MultiExtruder) {
        const ToolOrdering& tool_ordering = m_print->get_tool_ordering();

        if (tool_ordering.empty())
            return {};

        std::set<int> used_extruders;

        auto it_layer_tools = std::lower_bound(tool_ordering.begin(), tool_ordering.end(), print_z, [](const LayerTools& lhs, double rhs) { return lhs.print_z < rhs; });
        for (; it_layer_tools != tool_ordering.end(); ++it_layer_tools) {
            const std::vector<unsigned>& extruders = it_layer_tools->extruders;
            for (const auto& extruder : extruders)
                used_extruders.emplace(extruder + 1);
        }

        return used_extruders;
    }

    const int default_initial_extruder = e_mode == MultiAsSingle ? std::max(only_extruder_id, 1) : 1;
    if (ticks.empty() || e_mode == SingleExtruder)
        return { default_initial_extruder };

    std::set<int> used_extruders;

    auto it_start = ticks.lower_bound(TickCode{ tick });
    auto it = it_start;
    if (it == ticks.begin() && it->type == ToolChange &&
        tick != it->tick)  // In case of switch of ToolChange to ColorChange, when tick exists,
        // we shouldn't change color for extruder, which will be deleted
    {
        used_extruders.emplace(it->extruder);
        if (tick < it->tick)
            used_extruders.emplace(default_initial_extruder);
    }

    while (it != ticks.begin()) {
        --it;
        if (it->type == ToolChange && tick != it->tick) {
            used_extruders.emplace(it->extruder);
            break;
        }
    }

    if (it == ticks.begin() && used_extruders.empty())
        used_extruders.emplace(default_initial_extruder);

    for (it = it_start; it != ticks.end(); ++it)
        if (it->type == ToolChange && tick != it->tick)
            used_extruders.emplace(it->extruder);

    return used_extruders;
}

std::string TickCodeManager::get_color_for_tick(TickCode tick, Type type, const int extruder)
{
    auto opposite_one_color = [](const std::string& color) {
        ColorRGB rgb;
        decode_color(color, rgb);
        return encode_color(opposite(rgb));
    };
    auto opposite_two_colors = [](const std::string& a, const std::string& b) {
        ColorRGB rgb1; decode_color(a, rgb1);
        ColorRGB rgb2; decode_color(b, rgb2);
        return encode_color(opposite(rgb1, rgb2));
    };

    if (mode == SingleExtruder && type == ColorChange && m_use_default_colors) {

        if (ticks.empty())
            return opposite_one_color(colors[0]);

        auto before_tick_it = std::lower_bound(ticks.begin(), ticks.end(), tick);
        if (before_tick_it == ticks.end()) {
            while (before_tick_it != ticks.begin())
                if (--before_tick_it; before_tick_it->type == ColorChange)
                    break;
            if (before_tick_it->type == ColorChange)
                return opposite_one_color(before_tick_it->color);

            return opposite_one_color(colors[0]);
        }

        if (before_tick_it == ticks.begin()) {
            const std::string& frst_color = colors[0];
            if (before_tick_it->type == ColorChange)
                return opposite_two_colors(frst_color, before_tick_it->color);

            auto next_tick_it = before_tick_it;
            while (next_tick_it != ticks.end())
                if (++next_tick_it; next_tick_it != ticks.end() && next_tick_it->type == ColorChange)
                    break;
            if (next_tick_it != ticks.end() && next_tick_it->type == ColorChange)
                return opposite_two_colors(frst_color, next_tick_it->color);

            return opposite_one_color(frst_color);
        }

        std::string frst_color = "";
        if (before_tick_it->type == ColorChange)
            frst_color = before_tick_it->color;
        else {
            auto next_tick_it = before_tick_it;
            while (next_tick_it != ticks.end())
                if (++next_tick_it; next_tick_it != ticks.end() && next_tick_it->type == ColorChange) {
                    frst_color = next_tick_it->color;
                    break;
                }
        }

        while (before_tick_it != ticks.begin())
            if (--before_tick_it; before_tick_it->type == ColorChange)
                break;

        if (before_tick_it->type == ColorChange) {
            if (frst_color.empty())
                return opposite_one_color(before_tick_it->color);

            return opposite_two_colors(before_tick_it->color, frst_color);
        }

        if (frst_color.empty())
            return opposite_one_color(colors[0]);

        return opposite_two_colors(colors[0], frst_color);
    }

    std::string color = colors[extruder - 1];

    if (type == ColorChange) {
        if (!ticks.empty()) {
            auto before_tick_it = std::lower_bound(ticks.begin(), ticks.end(), tick );
            while (before_tick_it != ticks.begin()) {
                --before_tick_it;
                if (before_tick_it->type == ColorChange && before_tick_it->extruder == extruder) {
                    color = before_tick_it->color;
                    break;
                }
            }
        }

        color = get_new_color(color);
    }
    return color;
}

bool TickCodeManager::add_tick(const int tick, Type type, const int extruder, double print_z)
{
    std::string color;
    std::string extra;
    if (type == Custom)           // custom Gcode
    {
        extra = get_custom_code(m_custom_gcode, print_z);
        if (extra.empty())
            return false;
        m_custom_gcode = extra;
    }
    else if (type == PausePrint) {
        extra = get_pause_print_msg(m_pause_print_msg, print_z);
        if (extra.empty())
            return false;
        m_pause_print_msg = extra;
    }
    else {
        color = get_color_for_tick(TickCode{ tick }, type, extruder);
        if (color.empty())
            return false;
    }

    ticks.emplace(TickCode{ tick, type, extruder, color, extra });
    return true;
}

bool TickCodeManager::edit_tick(std::set<TickCode>::iterator it, double print_z)
{
    // Save previously value of the tick before the call a Dialog from get_... functions,
    // otherwise a background process can change ticks values and current iterator wouldn't be valid for the moment of a Dialog close
    // and PS will crash (see https://github.com/prusa3d/PrusaSlicer/issues/10941)
    TickCode changed_tick = *it;

    std::string edited_value;
    if (it->type == ColorChange)
        edited_value = get_new_color(it->color);
    else if (it->type == PausePrint)
        edited_value = get_pause_print_msg(it->extra, print_z);
    else
        edited_value = get_custom_code(it->type == Template ? gcode(Template) : it->extra, print_z);

    if (edited_value.empty())
        return false;

    // Update iterator. For this moment its value can be invalid
    if (it = ticks.find(changed_tick); it == ticks.end())
        return false;

    if (it->type == ColorChange) {
        if (it->color == edited_value)
            return false;
        changed_tick.color = edited_value;
    }
    else if (it->type == Template) {
        if (gcode(Template) == edited_value)
            return false;
        changed_tick.extra = edited_value;
        changed_tick.type  = Custom;
    }
    else if (it->type == Custom || it->type == PausePrint) {
        if (it->extra == edited_value)
            return false;
        changed_tick.extra = edited_value;
        if (it->type == Template)
            changed_tick.type = Custom;
    }

    ticks.erase(it);
    ticks.emplace(changed_tick);

    return true;
}

void TickCodeManager::switch_code(Type type_from, Type type_to)
{
    for (auto it{ ticks.begin() }, end{ ticks.end() }; it != end; )
        if (it->type == type_from) {
            TickCode tick = *it;
            tick.type = type_to;
            tick.extruder = 1;
            ticks.erase(it);
            it = ticks.emplace(tick).first;
        }
        else
            ++it;
}

bool TickCodeManager::switch_code_for_tick(std::set<TickCode>::iterator it, Type type_to, const int extruder)
{
    const std::string color = get_color_for_tick(*it, type_to, extruder);
    if (color.empty())
        return false;

    TickCode changed_tick   = *it;
    changed_tick.type       = type_to;
    changed_tick.extruder   = extruder;
    changed_tick.color      = color;

    ticks.erase(it);
    ticks.emplace(changed_tick);

    return true;
}

void TickCodeManager::erase_all_ticks_with_code(Type type)
{
    for (auto it{ ticks.begin() }, end{ ticks.end() }; it != end; ) {
        if (it->type == type)
            it = ticks.erase(it);
        else
            ++it;
    }
}

bool TickCodeManager::has_tick_with_code(Type type)
{
    for (const TickCode& tick : ticks)
        if (tick.type == type)
            return true;

    return false;
}

bool TickCodeManager::has_tick(int tick)
{
    return ticks.find(TickCode{ tick }) != ticks.end();
}

ConflictType TickCodeManager::is_conflict_tick(const TickCode& tick, Mode main_mode, double print_z)
{
    if ((tick.type == ColorChange && (
            (mode == SingleExtruder && main_mode == MultiExtruder ) ||
            (mode == MultiExtruder  && main_mode == SingleExtruder)    )) ||
        (tick.type == ToolChange &&
            (mode == MultiAsSingle && main_mode != MultiAsSingle)) )
        return ctModeConflict;

    // check ColorChange tick
    if (tick.type == ColorChange) {
        // We should mark a tick as a "MeaninglessColorChange", 
        // if it has a ColorChange for unused extruder from current print to end of the print
        std::set<int> used_extruders_for_tick = get_used_extruders_for_tick(tick.tick, print_z, main_mode);

        if (used_extruders_for_tick.find(tick.extruder) == used_extruders_for_tick.end())
            return ctMeaninglessColorChange;

        // We should mark a tick as a "Redundant", 
        // if it has a ColorChange for extruder that has not been used before
        if (mode == MultiAsSingle && tick.extruder != std::max<int>(only_extruder_id, 1) )
        {
            auto it = ticks.lower_bound( tick );
            if (it == ticks.begin() && it->type == ToolChange && tick.extruder == it->extruder)
                return ctNone;

            while (it != ticks.begin()) {
                --it;
                if (it->type == ToolChange && tick.extruder == it->extruder)
                    return ctNone;
            }

            return ctRedundant;
        }
    }

    // check ToolChange tick
    if (mode == MultiAsSingle && tick.type == ToolChange) {
        // We should mark a tick as a "MeaninglessToolChange", 
        // if it has a ToolChange to the same extruder
        auto it = ticks.find(tick);
        if (it->extruder > colors.size())
            return ctNotPossibleToolChange;

        if (it == ticks.begin())
            return tick.extruder == std::max<int>(only_extruder_id, 1) ? ctMeaninglessToolChange : ctNone;

        while (it != ticks.begin()) {
            --it;
            if (it->type == ToolChange)
                return tick.extruder == it->extruder ? ctMeaninglessToolChange : ctNone;
        }
    }

    return ctNone;
}

std::string TickCodeManager::get_color_for_tool_change_tick(std::set<TickCode>::const_iterator it) const
{
    const int current_extruder = it->extruder == 0 ? std::max<int>(only_extruder_id, 1) : it->extruder;

    if (current_extruder > colors.size())
        return it->color;

    auto it_n = it;
    while (it_n != ticks.begin()) {
        --it_n;
        if (it_n->type == ColorChange && it_n->extruder == current_extruder)
            return it_n->color;
    }

    return colors[current_extruder-1]; // return a color for a specific extruder from the colors list 
}

std::string TickCodeManager::get_color_for_color_change_tick(std::set<TickCode>::const_iterator it) const
{
    const int def_extruder = std::max<int>(1, only_extruder_id);
    auto it_n = it;
    bool is_tool_change = false;
    while (it_n != ticks.begin()) {
        --it_n;
        if (it_n->type == ToolChange) {
            is_tool_change = true;
            if (it_n->extruder == it->extruder)
                return it->color;
            break;
        }
        if (it_n->type == ColorChange && it_n->extruder == it->extruder)
            return it->color;
    }
    if (!is_tool_change && it->extruder == def_extruder)
        return it->color;

    return "";
}

} // DoubleSlider


