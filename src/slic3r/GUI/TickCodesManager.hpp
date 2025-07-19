///|/ Copyright (c) Prusa Research 2020 - 2022 Vojtěch Bubník @bubnikv, Oleksandra Iushchenko @YuSanka, Enrico Turri @enricoturri1966, Lukáš Matěna @lukasmatena
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_GUI_TickCodesManager_hpp_
#define slic3r_GUI_TickCodesManager_hpp_

#include <stddef.h>
#include <array>
#include <functional>
#include <vector>
#include <set>
#include <string>
#include <cstddef>

#include "libslic3r/CustomGCode.hpp"

using namespace Slic3r::CustomGCode;
namespace Slic3r {
class PrintObject;
class Print;
class Layer;
}

namespace DoubleSlider {

// return true when areas are mostly equivalent
bool equivalent_areas(const double& bottom_area, const double& top_area);

// return true if color change was detected
bool check_color_change(const Slic3r::PrintObject* object, size_t frst_layer_id, size_t layers_cnt, bool check_overhangs,
                        // what to do with detected color change
                        // and return true when detection have to be desturbed
                        std::function<bool(const Slic3r::Layer*)> break_condition);
enum ConflictType
{
    ctNone,
    ctModeConflict,
    ctMeaninglessColorChange,
    ctMeaninglessToolChange,
    ctNotPossibleToolChange,
    ctRedundant
};

struct ExtrudersSequence
{
    bool            is_mm_intervals     = true;
    double          interval_by_mm      = 3.0;
    int             interval_by_layers  = 10;
    bool            random_sequence     { false };
    bool            color_repetition    { false };
    std::vector<size_t>  extruders      = { 0 };

    bool operator==(const ExtrudersSequence& other) const
    {
        return  (other.is_mm_intervals      == this->is_mm_intervals    ) &&
                (other.interval_by_mm       == this->interval_by_mm     ) &&
                (other.interval_by_layers   == this->interval_by_layers ) &&
                (other.random_sequence      == this->random_sequence    ) &&
                (other.color_repetition     == this->color_repetition   ) &&
                (other.extruders            == this->extruders          ) ;
    }
    bool operator!=(const ExtrudersSequence& other) const
    {
        return  (other.is_mm_intervals      != this->is_mm_intervals    ) ||
                (other.interval_by_mm       != this->interval_by_mm     ) ||
                (other.interval_by_layers   != this->interval_by_layers ) ||
                (other.random_sequence      != this->random_sequence    ) ||
                (other.color_repetition     != this->color_repetition   ) ||
                (other.extruders            != this->extruders          ) ;
    }

    void add_extruder(size_t pos, size_t extruder_id = size_t(0))
    {
        extruders.insert(extruders.begin() + pos+1, extruder_id);
    }

    void delete_extruder(size_t pos)
    {            
        if (extruders.size() == 1)
            return;// last item can't be deleted
        extruders.erase(extruders.begin() + pos);
    }

    void init(size_t extruders_count) 
    {
        extruders.clear();
        for (size_t extruder = 0; extruder < extruders_count; extruder++)
            extruders.push_back(extruder);
    }
};


struct TickCode
{
    bool operator<(const TickCode& other) const { return other.tick > this->tick; }
    bool operator>(const TickCode& other) const { return other.tick < this->tick; }

    int         tick = 0;
    Type        type = ColorChange;
    int         extruder = 0;
    std::string color;
    std::string extra;
};


class TickCodeManager
{
    std::string                 m_custom_gcode;
    std::string                 m_pause_print_msg;
    bool                        m_use_default_colors    { true };

    const Slic3r::Print*        m_print{ nullptr };
    // pointer to the m_values from DSForLayers
    const std::vector<double>*  m_values{ nullptr };

    ExtrudersSequence           m_extruders_sequence;

    bool            has_tick_with_code(Type type);

    std::string     get_color_for_tick(TickCode tick, Type type, const int extruder);

    std::string     get_custom_code(const std::string& code_in, double height);
    std::string     get_pause_print_msg(const std::string& msg_in, double height);
    std::string     get_new_color(const std::string& color);

    std::function<void()>                                   m_cb_notify_empty_color_change  { nullptr };
    std::function<void(Type type)>                          m_cb_check_gcode_and_notify     { nullptr };

    std::function<std::string(const std::string&, double)>  m_cb_get_custom_code            { nullptr };
    std::function<std::string(const std::string&, double)>  m_cb_get_pause_print_msg        { nullptr };
    std::function<std::string(const std::string&)>          m_cb_get_new_color              { nullptr };

    std::function<int(const std::string&, int)>             m_cb_show_info_msg              { nullptr };
    std::function<int(const std::string&, int)>             m_cb_show_warning_msg           { nullptr };
    std::function<int()>                                    m_cb_get_extruders_cnt          { nullptr };
    std::function<bool(ExtrudersSequence&)>                 m_cb_get_extruders_sequence     { nullptr };

public:

    TickCodeManager();
    ~TickCodeManager() {}
    std::set<TickCode>          ticks           {};
    Mode                        mode            { Undef };
    bool                        is_wipe_tower   { false }; //This flag indicates that there is multiple extruder print with wipe tower
    int                         only_extruder_id{ -1 };

    // colors per extruder
    std::vector<std::string>    colors          {};

    bool empty() const { return ticks.empty(); }

    void set_ticks(const Info& custom_gcode_per_print_z);

    bool has_tick(int tick);
    bool add_tick(const int tick, Type type, int extruder, double print_z);
    bool edit_tick(std::set<TickCode>::iterator it, double print_z);
    void switch_code(Type type_from, Type type_to);
    bool switch_code_for_tick(std::set<TickCode>::iterator it, Type type_to, const int extruder);
    void erase_all_ticks_with_code(Type type);

    ConflictType    is_conflict_tick(const TickCode& tick, Mode main_mode, double print_z);

    int             get_tick_from_value(double value, bool force_lower_bound = false);

    std::string     gcode(Slic3r::CustomGCode::Type type);

    // Get used extruders for tick.
    // Means all extruders(tools) which will be used during printing from current tick to the end
    std::set<int>   get_used_extruders_for_tick(int tick, double print_z, Mode force_mode = Undef) const;

    // Get active extruders for tick. 
    // Means one current extruder for not existing tick OR 
    // 2 extruders - for existing tick (extruder before ToolChangeCode and extruder of current existing tick)
    // Use those values to disable selection of active extruders
    std::array<int, 2> get_active_extruders_for_tick(int tick, Mode main_mode) const;

    std::string     get_color_for_tool_change_tick(std::set<TickCode>::const_iterator it) const;
    std::string     get_color_for_color_change_tick(std::set<TickCode>::const_iterator it) const;

    // true  -> if manipulation with ticks with selected type and in respect to the main_mode (slider mode) is possible
    // false -> otherwise
    bool            check_ticks_changed_event(Type type, Mode main_mode);

    // return true, if extruder sequence was changed
    bool            edit_extruder_sequence(const int max_tick, Mode main_mode);

    // return true, if auto color change was successfully processed
    bool            auto_color_change(Mode main_mode);

    void set_default_colors(bool default_colors_on)     { m_use_default_colors = default_colors_on; }
    bool used_default_colors() const                    { return m_use_default_colors; }

    void set_print(const Slic3r::Print& print)          { if (!m_print) m_print = &print; }
    void set_values(const std::vector<double>* values)  { m_values = values; }

    void set_callback_on_empty_auto_color_change(std::function<void()> cb)
        { m_cb_notify_empty_color_change = cb; }

    void set_callback_on_check_gcode(std::function<void(Type)> cb ) 
         { m_cb_check_gcode_and_notify = cb; }

    void set_callback_on_get_custom_code(std::function<std::string(const std::string&, double)> cb)
        { m_cb_get_custom_code = cb; }

    void set_callback_on_get_pause_print_msg(std::function<std::string(const std::string&, double)> cb)
        { m_cb_get_pause_print_msg = cb; }

    void set_callback_on_get_new_color(std::function<std::string(const std::string&)> cb)
        { m_cb_get_new_color = cb; }

    void set_callback_on_show_info_msg(std::function<int(const std::string&, int)> cb)
        { m_cb_show_info_msg = cb; }

    void set_callback_on_show_warning_msg(std::function<int(const std::string&, int)> cb)
        { m_cb_show_warning_msg = cb; }

    void set_callback_on_get_extruders_cnt(std::function<int()> cb)
        { m_cb_get_extruders_cnt = cb; }

    void set_callback_on_get_extruders_sequence(std::function<bool(ExtrudersSequence&)> cb)
        { m_cb_get_extruders_sequence = cb; }
};

} // DoubleSlider;

#endif // slic3r_GUI_TickCodesManager_hpp_
