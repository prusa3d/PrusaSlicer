#pragma once

#include "ExtrudersSequence.hpp"
#include "Types.hpp"

#include <set>
#include <functional>
#include <array>

namespace Slic3r::App::Preview {

struct TickCode
{
    bool operator<(const TickCode& other) const { return other.tick > this->tick; }
    bool operator>(const TickCode& other) const { return other.tick < this->tick; }

    int tick{ 0 };
    Domain::CustomGCode::Type type{ Domain::CustomGCode::Type::ColorChange };
    int extruder = 0;
    std::string color;
    std::string extra;
};

enum class ConflictType
{
    None,
    ModeConflict,
    MeaninglessColorChange,
    MeaninglessToolChange,
    NotPossibleToolChange,
    Redundant
};

class TickCodeManager
{
public:
    std::set<TickCode> ticks;
    Domain::CustomGCode::Mode mode{ Domain::CustomGCode::Mode::Undef };
    bool is_wipe_tower{ false }; //This flag indicates that there is multiple extruder print with wipe tower
    int only_extruder_id{ -1 };

    // colors per extruder
    std::vector<std::string> colors;

    bool empty() const { return ticks.empty(); }

    void set_ticks(const Domain::CustomGCode::Info& custom_gcode_per_print_z);

    bool has_tick(int tick) const;
    bool add_tick(const int tick, Domain::CustomGCode::Type type, int extruder, float print_z);
    bool add_pause_print_tick(const int tick, const std::string& msg, int extruder, float print_z);
    bool add_color_change_tick(const int tick, const std::string& color, int extruder, float print_z);
    bool add_custom_gcode_tick(const int tick, const std::string& gcode, int extruder, float print_z);
    bool add_template_tick(const int tick, const std::string& gcode, int extruder, float print_z);
    bool edit_tick(std::set<TickCode>::iterator it, float print_z, const std::string& new_value);

    void add_auto_color_change(Domain::CustomGCode::Mode main_mode, const int extruders_cnt, float print_z);
    void switch_code(Domain::CustomGCode::Type type_from, Domain::CustomGCode::Type type_to);
    bool switch_code_for_tick(std::set<TickCode>::iterator it, Domain::CustomGCode::Type type_to, const int extruder);
    bool erase_all_ticks_with_code(Domain::CustomGCode::Type type);

    ConflictType is_conflict_tick(const TickCode& tick, Domain::CustomGCode::Mode main_mode, float print_z) const;

    int tick_from_value(float value, bool force_lower_bound = false) const;

    std::string gcode(Domain::CustomGCode::Type type) const;

    // Get used extruders for tick.
    // Means all extruders(tools) which will be used during printing from current tick to the end
    std::set<int> used_extruders_for_tick(int tick, float print_z,
        Domain::CustomGCode::Mode force_mode = Domain::CustomGCode::Mode::Undef) const;

    // Get active extruders for tick. 
    // Means one current extruder for not existing tick OR 
    // 2 extruders - for existing tick (extruder before ToolChangeCode and extruder of current existing tick)
    // Use those values to disable selection of active extruders
    std::array<int, 2> active_extruders_for_tick(int tick, Domain::CustomGCode::Mode main_mode) const;

    std::string color_for_tool_change_tick(std::set<TickCode>::const_iterator it) const;
    std::string color_for_color_change_tick(std::set<TickCode>::const_iterator it) const;

    // true  -> if manipulation with ticks with selected type and in respect to the main_mode (slider mode) is possible
    // false -> otherwise
    bool check_ticks_changed_event(Domain::CustomGCode::Type type, Domain::CustomGCode::Mode main_mode);

    // return true, if extruder sequence was changed
    bool edit_extruder_sequence(const int max_tick, Domain::CustomGCode::Mode main_mode);

    void set_use_default_colors(bool use) { m_use_default_colors = use; }
    bool use_default_colors() const { return m_use_default_colors; }

    void set_values(const std::vector<float>* values) { m_values = values; }

    void set_notify_empty_color_change_gcode_callback(NotifyEmptyColorChangeGCodeCallback cb) { m_cb_notify_empty_color_change_gcode = cb; }
    void set_show_info_msg_callback(ShowInfoMsgCallback cb) { m_cb_show_info_msg = cb; }
    void set_get_gcode_callback(GetGCodeCallback cb) { m_cb_get_gcode = cb; }
    void set_get_used_extruders_in_print_callback(GetUsedExtrudersInPrintCallback cb) { m_cb_get_used_extruders_in_print = cb; }
    void set_get_extruders_sequence_callback(GetExtrudersSequenceCallback cb) { m_cb_get_extruders_sequence = cb; }

    std::string color_for_tick(TickCode tick, Domain::CustomGCode::Type type, const int extruder);

private:
    bool has_tick_with_code(Domain::CustomGCode::Type type);

    std::string new_color(const std::string& color);

private:
    bool m_use_default_colors{ true };
    // pointer to the m_values from DSForLayers
    const std::vector<float>* m_values{ nullptr };
    ExtrudersSequence m_extruders_sequence;

    NotifyEmptyColorChangeGCodeCallback m_cb_notify_empty_color_change_gcode{ nullptr };
    ShowInfoMsgCallback m_cb_show_info_msg{ nullptr };
    GetGCodeCallback m_cb_get_gcode{ nullptr };
    GetUsedExtrudersInPrintCallback m_cb_get_used_extruders_in_print{ nullptr };
    GetExtrudersSequenceCallback m_cb_get_extruders_sequence{ nullptr };
};

} // namespace Slic3r::App::Preview
