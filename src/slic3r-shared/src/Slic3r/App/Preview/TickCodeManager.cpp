#include "Slic3r/App/Preview/TickCodeManager.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/Algorithms/Color.hpp"
#include <Slic3r/Biz/libpgcode/Utils.hpp>
#include "Slic3r/Biz/GCodeReader/Utils.hpp"

#include "Slic3r/Domain/Color.hpp"

#include <random>

using namespace Slic3r;
using namespace Slic3r::Biz;

using Slic3r::Domain::ColorRGB;

using Slic3r::Biz::Algorithms::Color::decode_color;
using Slic3r::Biz::Algorithms::Color::encode_color;
using Slic3r::Biz::Algorithms::Color::opposite;
using Slic3r::Biz::GCodeReader::contains_reserved_tags;

namespace Slic3r::App::Preview {

namespace CustomGCode = Domain::CustomGCode;

static constexpr float EPSILON = 0.0011f;
static constexpr int YES = 0x00000002; // an analogue of wxYES   
static constexpr int NO = 0x00000008; // an analogue of wxNO    
static constexpr int CANCEL = 0x00000010; // an analogue of wxCANCEL

void TickCodeManager::set_ticks(const CustomGCode::Info& custom_gcode_per_print_z)
{
    ticks.clear();

    const std::vector<CustomGCode::Item>& heights = custom_gcode_per_print_z.gcodes;
    for (auto h : heights) {
        int tick = tick_from_value(h.print_z);
        if (tick >=0)
            ticks.emplace(TickCode{ tick, h.type, h.extruder, h.color, h.extra });
    }

    if (custom_gcode_per_print_z.mode != CustomGCode::Mode::Undef && !custom_gcode_per_print_z.gcodes.empty())
        mode = custom_gcode_per_print_z.mode;
}

bool TickCodeManager::has_tick(int tick) const
{
    return ticks.find(TickCode{ tick }) != ticks.end();
}

bool TickCodeManager::add_tick(const int tick, CustomGCode::Type type, const int extruder, float print_z)
{
    std::string extra;
    std::string color = color_for_tick(TickCode{ tick }, type, extruder);
    if (color.empty())
        return false;

    ticks.emplace(TickCode{ tick, type, extruder, color, extra });
    return true;
}

bool TickCodeManager::add_pause_print_tick(const int tick, const std::string& msg, int extruder, float print_z)
{
    assert(!msg.empty());
    ticks.emplace(TickCode{ tick, CustomGCode::Type::PausePrint, extruder, "", msg });
    return true;
}

bool TickCodeManager::add_color_change_tick(const int tick, const std::string& color, int extruder, float print_z)
{
    assert(Biz::libpgcode::is_valid_color(color));
    ticks.emplace(TickCode{ tick, CustomGCode::Type::ColorChange, extruder, color, "" });
    return true;
}

bool TickCodeManager::add_custom_gcode_tick(const int tick, const std::string& gcode, int extruder, float print_z)
{
    std::vector<std::string> reserved_tags;
    assert(!contains_reserved_tags(gcode, Biz::libpgcode::RESERVED_TAGS, 1, reserved_tags));
    ticks.emplace(TickCode{ tick, CustomGCode::Type::Custom, extruder, "", gcode });
    return true;
}

bool TickCodeManager::add_template_tick(const int tick, const std::string& color, int extruder, float print_z)
{
    assert(Biz::libpgcode::is_valid_color(color));
    std::string extra = gcode(CustomGCode::Type::Template);
    assert(!extra.empty());
    ticks.emplace(TickCode{ tick, CustomGCode::Type::Template, extruder, color, extra });
    return true;
}

bool TickCodeManager::edit_tick(std::set<TickCode>::iterator it, float print_z, const std::string& new_value)
{
    // Save previously value of the tick before the call a Dialog from get_... functions,
    // otherwise a background process can change ticks values and current iterator wouldn't be valid for the moment of a Dialog close
    // and PS will crash (see https://github.com/prusa3d/PrusaSlicer/issues/10941)
    TickCode changed_tick = *it;

    std::string edited_value;
    if (it->type == CustomGCode::Type::ColorChange)
        edited_value = new_value;
    else if (it->type == CustomGCode::Type::PausePrint)
        edited_value = new_value;
    else if (it->type == CustomGCode::Type::Custom)
        edited_value = new_value;
    else if (it->type == CustomGCode::Type::Template)
        edited_value = new_value;

    if (edited_value.empty())
        return false;

    // Update iterator. For this moment its value can be invalid
    if (it = ticks.find(changed_tick); it == ticks.end())
        return false;

    if (it->type == CustomGCode::Type::ColorChange) {
        if (it->color == edited_value)
            return false;
        changed_tick.color = edited_value;
    }
    else if (it->type == CustomGCode::Type::Template) {
        if (gcode(CustomGCode::Type::Template) == edited_value)
            return false;
        changed_tick.extra = edited_value;
        changed_tick.type = CustomGCode::Type::Custom;
    }
    else if (it->type == CustomGCode::Type::Custom || it->type == CustomGCode::Type::PausePrint) {
        if (it->extra == edited_value)
            return false;
        changed_tick.extra = edited_value;
    }

    ticks.erase(it);
    ticks.emplace(changed_tick);

    return true;
}

void TickCodeManager::add_auto_color_change(CustomGCode::Mode main_mode, const int extruders_cnt, float print_z)
{
    int tick = tick_from_value(print_z);
    if (tick >= 0 && !has_tick(tick)) {
        if (main_mode == CustomGCode::Mode::SingleExtruder) {
            set_use_default_colors(true);
            std::string color = color_for_tick(TickCode{ tick }, CustomGCode::Type::ColorChange, -1);
            add_color_change_tick(tick, color, -1, print_z);
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
            add_tick(tick, CustomGCode::Type::ToolChange, extruder, print_z);
        }
    }
}

void TickCodeManager::switch_code(CustomGCode::Type type_from, CustomGCode::Type type_to)
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

bool TickCodeManager::switch_code_for_tick(std::set<TickCode>::iterator it, CustomGCode::Type type_to, const int extruder)
{
    std::string color = color_for_tick(*it, type_to, extruder);
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

bool TickCodeManager::erase_all_ticks_with_code(CustomGCode::Type type)
{
    const size_t ticks_cnt = ticks.size();
    for (auto it{ ticks.begin() }, end{ ticks.end() }; it != end; ) {
        if (it->type == type)
            it = ticks.erase(it);
        else
            ++it;
    }
    return ticks_cnt != ticks.size();
}

ConflictType TickCodeManager::is_conflict_tick(const TickCode& tick, CustomGCode::Mode main_mode, float print_z) const
{
    if ((tick.type == CustomGCode::Type::ColorChange && (
            (mode == CustomGCode::Mode::SingleExtruder && main_mode == CustomGCode::Mode::MultiExtruder ) ||
            (mode == CustomGCode::Mode::MultiExtruder  && main_mode == CustomGCode::Mode::SingleExtruder)    )) ||
        (tick.type == CustomGCode::Type::ToolChange &&
            (mode == CustomGCode::Mode::MultiAsSingle && main_mode != CustomGCode::Mode::MultiAsSingle)) )
        return ConflictType::ModeConflict;

    // check ColorChange tick
    if (tick.type == CustomGCode::Type::ColorChange) {
        // We should mark a tick as a "MeaninglessColorChange", 
        // if it has a ColorChange for unused extruder from current print to end of the print
        std::set<int> extruders_for_tick = used_extruders_for_tick(tick.tick, print_z, main_mode);

        if (extruders_for_tick.find(tick.extruder) == extruders_for_tick.end())
            return ConflictType::MeaninglessColorChange;

        // We should mark a tick as a "Redundant", 
        // if it has a ColorChange for extruder that has not been used before
        if (mode == CustomGCode::Mode::MultiAsSingle && tick.extruder != std::max<int>(only_extruder_id, 1)) {
            auto it = ticks.lower_bound( tick );
            if (it == ticks.begin() && it->type == CustomGCode::Type::ToolChange && tick.extruder == it->extruder)
                return ConflictType::None;

            while (it != ticks.begin()) {
                --it;
                if (it->type == CustomGCode::Type::ToolChange && tick.extruder == it->extruder)
                    return ConflictType::None;
            }

            return ConflictType::Redundant;
        }
    }

    // check ToolChange tick
    if (mode == CustomGCode::Mode::MultiAsSingle && tick.type == CustomGCode::Type::ToolChange) {
        // We should mark a tick as a "MeaninglessToolChange", 
        // if it has a ToolChange to the same extruder
        auto it = ticks.find(tick);
        if (it->extruder > int(colors.size()))
            return ConflictType::NotPossibleToolChange;

        if (it == ticks.begin())
            return tick.extruder == std::max<int>(only_extruder_id, 1) ? ConflictType::MeaninglessToolChange : ConflictType::None;

        while (it != ticks.begin()) {
            --it;
            if (it->type == CustomGCode::Type::ToolChange)
                return tick.extruder == it->extruder ? ConflictType::MeaninglessToolChange : ConflictType::None;
        }
    }

    return ConflictType::None;
}

int TickCodeManager::tick_from_value(float value, bool force_lower_bound/* = false*/) const
{
    if (!m_values)
        return -1;
    std::vector<float>::const_iterator it;
    if (is_wipe_tower && !force_lower_bound)
        it = std::find_if(m_values->begin(), m_values->end(),
                          [value](const float& val) { return fabs(value - val) <= EPSILON; });
    else
        it = std::lower_bound(m_values->begin(), m_values->end(), value - EPSILON);

    if (it == m_values->end())
        return -1;
    return int(it - m_values->begin());
}

std::string TickCodeManager::gcode(CustomGCode::Type type) const
{
    return (m_cb_get_gcode != nullptr) ? m_cb_get_gcode(type) : std::string();
}

// Get used extruders for tick. 
// Means all extruders(tools) which will be used during printing from current tick to the end
std::set<int> TickCodeManager::used_extruders_for_tick(int tick, float print_z, CustomGCode::Mode force_mode/* = Undef*/) const
{
    CustomGCode::Mode e_mode = (force_mode == CustomGCode::Mode::Undef) ? mode : force_mode;

    if (e_mode == CustomGCode::Mode::MultiExtruder) {
        if (m_cb_get_used_extruders_in_print)
            return m_cb_get_used_extruders_in_print(print_z);
        return {};
    }

    int default_initial_extruder = e_mode == CustomGCode::Mode::MultiAsSingle ? std::max(only_extruder_id, 1) : 1;
    if (ticks.empty() || e_mode == CustomGCode::Mode::SingleExtruder)
        return { default_initial_extruder };

    std::set<int> used_extruders;

    auto it_start = ticks.lower_bound(TickCode{ tick });
    auto it = it_start;
    if (it == ticks.begin() && it->type == CustomGCode::Type::ToolChange &&
        tick != it->tick)  // In case of switch of ToolChange to ColorChange, when tick exists,
        // we shouldn't change color for extruder, which will be deleted
    {
        used_extruders.emplace(it->extruder);
        if (tick < it->tick)
            used_extruders.emplace(default_initial_extruder);
    }

    while (it != ticks.begin()) {
        --it;
        if (it->type == CustomGCode::Type::ToolChange && tick != it->tick) {
            used_extruders.emplace(it->extruder);
            break;
        }
    }

    if (it == ticks.begin() && used_extruders.empty())
        used_extruders.emplace(default_initial_extruder);

    for (it = it_start; it != ticks.end(); ++it)
        if (it->type == CustomGCode::Type::ToolChange && tick != it->tick)
            used_extruders.emplace(it->extruder);

    return used_extruders;
}

// Get active extruders for tick. 
// Means one current extruder for not existing tick OR 
// 2 extruders - for existing tick (extruder before ToolChange and extruder of current existing tick)
// Use those values to disable selection of active extruders
std::array<int, 2> TickCodeManager::active_extruders_for_tick(int tick, CustomGCode::Mode main_mode) const
{
    int default_initial_extruder = main_mode == CustomGCode::Mode::MultiAsSingle ? std::max<int>(1, only_extruder_id) : 1;
    std::array<int, 2> extruders = { default_initial_extruder, -1 };
    if (empty())
        return extruders;

    auto it = ticks.lower_bound(TickCode{tick});

    if (it != ticks.end() && it->tick == tick) // current tick exists
        extruders[1] = it->extruder;

    while (it != ticks.begin()) {
        --it;
        if (it->type == CustomGCode::Type::ToolChange) {
            extruders[0] = it->extruder;
            break;
        }
    }

    return extruders;
}

std::string TickCodeManager::color_for_tool_change_tick(std::set<TickCode>::const_iterator it) const
{
    int current_extruder = it->extruder == 0 ? std::max<int>(only_extruder_id, 1) : it->extruder;

    if (current_extruder > int(colors.size()))
        return it->color;

    auto it_n = it;
    while (it_n != ticks.begin()) {
        --it_n;
        if (it_n->type == CustomGCode::Type::ColorChange && it_n->extruder == current_extruder)
            return it_n->color;
    }

    return colors[current_extruder-1]; // return a color for a specific extruder from the colors list 
}

std::string TickCodeManager::color_for_color_change_tick(std::set<TickCode>::const_iterator it) const
{
    int def_extruder = std::max<int>(1, only_extruder_id);
    auto it_n = it;
    bool is_tool_change = false;
    while (it_n != ticks.begin()) {
        --it_n;
        if (it_n->type == CustomGCode::Type::ToolChange) {
            is_tool_change = true;
            if (it_n->extruder == it->extruder)
                return it->color;
            break;
        }
        if (it_n->type == CustomGCode::Type::ColorChange && it_n->extruder == it->extruder)
            return it->color;
    }
    if (!is_tool_change && it->extruder == def_extruder)
        return it->color;

    return "";
}

bool TickCodeManager::check_ticks_changed_event(CustomGCode::Type type, CustomGCode::Mode main_mode)
{
    if (mode == main_mode ||
        (type != CustomGCode::Type::ColorChange && type != CustomGCode::Type::ToolChange) ||
        (mode == CustomGCode::Mode::SingleExtruder && main_mode == CustomGCode::Mode::MultiAsSingle) || // All ColorChanges will be applied for 1st extruder
        (mode == CustomGCode::Mode::MultiExtruder  && main_mode == CustomGCode::Mode::MultiAsSingle))  // Just mark ColorChanges for all unused extruders
        return true;

    if ((mode == CustomGCode::Mode::SingleExtruder && main_mode == CustomGCode::Mode::MultiExtruder) ||
        (mode == CustomGCode::Mode::MultiExtruder && main_mode == CustomGCode::Mode::SingleExtruder))
    {
        if (!has_tick_with_code(CustomGCode::Type::ColorChange))
            return true;

        if (m_cb_show_info_msg) {
            std::string message = (mode == CustomGCode::Mode::SingleExtruder) ?
                            _u8L("The last color change data was saved for a single extruder printing.") :
                            (
                                _u8L("The last color change data was saved for a multi extruder printing.") + "\n" +
                                _u8L("Your current changes will delete all saved color changes.") + "\n\n\t" +
                                _u8L("Are you sure you want to continue?")
                            );

            if ( m_cb_show_info_msg(message, YES | NO) == YES)
                erase_all_ticks_with_code(CustomGCode::Type::ColorChange);
        }
        return false;
    }
    //          m_ticks_mode == MultiAsSingle
    if (has_tick_with_code(CustomGCode::Type::ToolChange)) {
        if (m_cb_show_info_msg) {
            std::string message = (main_mode == CustomGCode::Mode::SingleExtruder) ?
                            (
                                _u8L("The last color change data was saved for a multi extruder printing.") + "\n\n" +
                                _u8L("Select YES if you want to delete all saved tool changes, \n"
                                   "NO if you want all tool changes switch to color changes, \n"
                                   "or CANCEL to leave it unchanged.") + "\n\n\t" +
                                _u8L("Do you want to delete all saved tool changes?")
                            ) :
                            (   // MultiExtruder
                                _u8L("The last color change data was saved for a multi extruder printing with tool changes for whole print.") + "\n\n" +
                                _u8L("Your current changes will delete all saved extruder (tool) changes.") + "\n\n\t" +
                                _u8L("Are you sure you want to continue?")
                            );

            int answer = m_cb_show_info_msg(message, YES | NO | (main_mode == CustomGCode::Mode::SingleExtruder ? CANCEL : 0));
            if (answer == YES)
                erase_all_ticks_with_code(CustomGCode::Type::ToolChange);
            else if (main_mode == CustomGCode::Mode::SingleExtruder && answer == NO)
                switch_code(CustomGCode::Type::ToolChange, CustomGCode::Type::ColorChange);
        }
        return false;
    }

    if (type == CustomGCode::Type::ColorChange && gcode(CustomGCode::Type::ColorChange).empty()) {
        if (m_cb_notify_empty_color_change_gcode)
            m_cb_notify_empty_color_change_gcode();
    }

    return true;
}

bool TickCodeManager::edit_extruder_sequence(const int max_tick, CustomGCode::Mode main_mode)
{
    if (!check_ticks_changed_event(CustomGCode::Type::ToolChange, main_mode) || !m_cb_get_extruders_sequence)
        return false;

    // init extruder sequence in respect to the extruders count 
    if (empty())
        m_extruders_sequence.init(colors.size());

    if(!m_cb_get_extruders_sequence(m_extruders_sequence))
        return false;

    erase_all_ticks_with_code(CustomGCode::Type::ToolChange);

    int extr_cnt = int(m_extruders_sequence.extruders.size());
    if (extr_cnt == 1)
        return true;

    int tick = 0;
    float value = 0.0f;
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

        int cur_extruder = int(m_extruders_sequence.extruders[extruder]);

        bool meaningless_tick = tick == 0.0 && cur_extruder == extruder;
        if (!meaningless_tick && !color_repetition)
            ticks.emplace(TickCode{ tick, CustomGCode::Type::ToolChange, cur_extruder + 1, colors[cur_extruder] });

        if (m_extruders_sequence.is_mm_intervals) {
            value += m_extruders_sequence.interval_by_mm;
            tick = tick_from_value(value, true);
            if (tick < 0)
                break;
        }
        else
            tick += m_extruders_sequence.interval_by_layers;
    }

    return true;
}

bool TickCodeManager::has_tick_with_code(CustomGCode::Type type)
{
    for (const TickCode& tick : ticks)
        if (tick.type == type)
            return true;

    return false;
}

std::string TickCodeManager::color_for_tick(TickCode tick, CustomGCode::Type type, const int extruder)
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

    if (mode == CustomGCode::Mode::SingleExtruder && type == CustomGCode::Type::ColorChange && m_use_default_colors) {
        if (ticks.empty())
            return opposite_one_color(colors[0]);

        auto before_tick_it = std::lower_bound(ticks.begin(), ticks.end(), tick);
        if (before_tick_it == ticks.end()) {
            while (before_tick_it != ticks.begin())
                if (--before_tick_it; before_tick_it->type == CustomGCode::Type::ColorChange)
                    break;
            if (before_tick_it->type == CustomGCode::Type::ColorChange)
                return opposite_one_color(before_tick_it->color);

            return opposite_one_color(colors[0]);
        }

        if (before_tick_it == ticks.begin()) {
            const std::string& frst_color = colors[0];
            if (before_tick_it->type == CustomGCode::Type::ColorChange)
                return opposite_two_colors(frst_color, before_tick_it->color);

            auto next_tick_it = before_tick_it;
            while (next_tick_it != ticks.end())
                if (++next_tick_it; next_tick_it != ticks.end() && next_tick_it->type == CustomGCode::Type::ColorChange)
                    break;
            if (next_tick_it != ticks.end() && next_tick_it->type == CustomGCode::Type::ColorChange)
                return opposite_two_colors(frst_color, next_tick_it->color);

            return opposite_one_color(frst_color);
        }

        std::string frst_color;
        if (before_tick_it->type == CustomGCode::Type::ColorChange)
            frst_color = before_tick_it->color;
        else {
            auto next_tick_it = before_tick_it;
            while (next_tick_it != ticks.end())
                if (++next_tick_it; next_tick_it != ticks.end() && next_tick_it->type == CustomGCode::Type::ColorChange) {
                    frst_color = next_tick_it->color;
                    break;
                }
        }

        while (before_tick_it != ticks.begin())
            if (--before_tick_it; before_tick_it->type == CustomGCode::Type::ColorChange)
                break;

        if (before_tick_it->type == CustomGCode::Type::ColorChange) {
            if (frst_color.empty())
                return opposite_one_color(before_tick_it->color);

            return opposite_two_colors(before_tick_it->color, frst_color);
        }

        if (frst_color.empty())
            return opposite_one_color(colors[0]);

        return opposite_two_colors(colors[0], frst_color);
    }

    std::string color = colors[extruder - 1];

    if (type == CustomGCode::Type::ColorChange) {
        if (!ticks.empty()) {
            auto before_tick_it = std::lower_bound(ticks.begin(), ticks.end(), tick );
            while (before_tick_it != ticks.begin()) {
                --before_tick_it;
                if (before_tick_it->type == CustomGCode::Type::ColorChange && before_tick_it->extruder == extruder) {
                    color = before_tick_it->color;
                    break;
                }
            }
        }

        color = new_color(color);
    }
    return color;
}

std::string TickCodeManager::new_color(const std::string& color)
{
    return std::string();
}

} // namespace Slic3r::App::Preview
