///|/ Copyright (c) Prusa Research 2020 - 2023 Enrico Turri @enricoturri1966, Vojtěch Bubník @bubnikv, Pavel Mikuš @Godrak, Lukáš Matěna @lukasmatena, Filip Sykala @Jony01, Oleksandra Iushchenko @YuSanka
///|/ Copyright (c) SuperSlicer 2023 Remi Durand @supermerill
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/#include "libslic3r/libslic3r.h"
#include "libslic3r/Utils.hpp"
#include "libslic3r/Print.hpp"
#include <LocalesUtils.hpp>
#include "libslic3r/format.hpp"
#include "libslic3r/I18N.hpp"
#include "libslic3r/GCode/GCodeWriter.hpp"
#include "libslic3r/I18N.hpp"
#include "libslic3r/Geometry/ArcWelder.hpp"
#include "GCodeProcessor.hpp"

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/log/trivial.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/nowide/cstdio.hpp>
#include <boost/filesystem/path.hpp>

#include <float.h>
#include <assert.h>

#if __has_include(<charconv>)
    #include <charconv>
    #include <utility>
#endif

#include <chrono>

static const float DEFAULT_TOOLPATH_WIDTH = 0.4f;
static const float DEFAULT_TOOLPATH_HEIGHT = 0.2f;

static const float INCHES_TO_MM = 25.4f;
static const float MMMIN_TO_MMSEC = 1.0f / 60.0f;
static const float DEFAULT_ACCELERATION = 1500.0f; // Prusa Firmware 1_75mm_MK2
static const float DEFAULT_RETRACT_ACCELERATION = 1500.0f; // Prusa Firmware 1_75mm_MK2
static const float DEFAULT_TRAVEL_ACCELERATION = 1250.0f;

static const size_t MIN_EXTRUDERS_COUNT = 5;
static const float DEFAULT_FILAMENT_DIAMETER = 1.75f;
static const float DEFAULT_FILAMENT_DENSITY = 1.245f;
static const float DEFAULT_FILAMENT_COST = 0.0f;
static const Slic3r::Vec3f DEFAULT_EXTRUDER_OFFSET = Slic3r::Vec3f::Zero();
// taken from PrusaResearch.ini - [printer:Original Prusa i3 MK2.5 MMU2]
static const std::vector<std::string> DEFAULT_EXTRUDER_COLORS = { "#FF8000", "#DB5182", "#3EC0FF", "#FF4F4F", "#FBEB7D" };

namespace Slic3r {

const std::vector<std::string> GCodeProcessor::Reserved_Tags = {
    "TYPE:",
    "WIPE_START",
    "WIPE_END",
    "HEIGHT:",
    "WIDTH:",
    "LAYER_CHANGE",
    "COLOR_CHANGE",
    "PAUSE_PRINT",
    "CUSTOM_GCODE",
    "_GP_FIRST_LINE_M73_PLACEHOLDER",
    "_GP_LAST_LINE_M73_PLACEHOLDER",
    "_GP_ESTIMATED_PRINTING_TIME_PLACEHOLDER"
};

const float GCodeProcessor::Wipe_Width = 0.05f;
const float GCodeProcessor::Wipe_Height = 0.05f;

bgcode::binarize::BinarizerConfig GCodeProcessor::s_binarizer_config{
    {
        bgcode::core::ECompressionType::None,            // file metadata
        bgcode::core::ECompressionType::None,            // printer metadata
        bgcode::core::ECompressionType::Deflate,         // print metadata
        bgcode::core::ECompressionType::Deflate,         // slicer metadata
        bgcode::core::ECompressionType::Heatshrink_12_4, // gcode
    },
    bgcode::core::EGCodeEncodingType::MeatPackComments,
    bgcode::core::EMetadataEncodingType::INI,
    bgcode::core::EChecksumType::CRC32
};

static void set_option_value(ConfigOptionFloats& option, size_t id, float value)
{
    if (id < option.values.size())
        option.values[id] = static_cast<double>(value);
};

static float get_option_value(const ConfigOptionFloats& option, size_t id)
{
    return option.values.empty() ? 0.0f :
        ((id < option.values.size()) ? static_cast<float>(option.values[id]) : static_cast<float>(option.values.back()));
}

static float estimated_acceleration_distance(float initial_rate, float target_rate, float acceleration)
{
    return (acceleration == 0.0f) ? 0.0f : (sqr(target_rate) - sqr(initial_rate)) / (2.0f * acceleration);
}

static float intersection_distance(float initial_rate, float final_rate, float acceleration, float distance)
{
    return (acceleration == 0.0f) ? 0.0f : (2.0f * acceleration * distance - sqr(initial_rate) + sqr(final_rate)) / (4.0f * acceleration);
}

static float speed_from_distance(float initial_feedrate, float distance, float acceleration)
{
    // to avoid invalid negative numbers due to numerical errors 
    const float value = std::max(0.0f, sqr(initial_feedrate) + 2.0f * acceleration * distance);
    return ::sqrt(value);
}

// Calculates the maximum allowable speed at this point when you must be able to reach target_velocity using the 
// acceleration within the allotted distance.
static float max_allowable_speed(float acceleration, float target_velocity, float distance)
{
    // to avoid invalid negative numbers due to numerical errors 
    const float value = std::max(0.0f, sqr(target_velocity) - 2.0f * acceleration * distance);
    return std::sqrt(value);
}

static float acceleration_time_from_distance(float initial_feedrate, float distance, float acceleration)
{
    return (acceleration != 0.0f) ? (speed_from_distance(initial_feedrate, distance, acceleration) - initial_feedrate) / acceleration : 0.0f;
}

void GCodeProcessor::CachedPosition::reset()
{
    std::fill(position.begin(), position.end(), FLT_MAX);
    feedrate = FLT_MAX;
}

void GCodeProcessor::CpColor::reset()
{
    counter = 0;
    current = 0;
}

float GCodeProcessor::Trapezoid::acceleration_time(float entry_feedrate, float acceleration) const
{
    return acceleration_time_from_distance(entry_feedrate, acceleration_distance(), acceleration);
}

float GCodeProcessor::Trapezoid::deceleration_time(float distance, float acceleration) const
{
    return acceleration_time_from_distance(cruise_feedrate, deceleration_distance(distance), -acceleration);
}

void GCodeProcessor::TimeBlock::calculate_trapezoid()
{
    float accelerate_distance = std::max(0.0f, estimated_acceleration_distance(feedrate_profile.entry, feedrate_profile.cruise, acceleration));
    const float decelerate_distance = std::max(0.0f, estimated_acceleration_distance(feedrate_profile.cruise, feedrate_profile.exit, -acceleration));
    float cruise_distance = distance - accelerate_distance - decelerate_distance;

    // Not enough space to reach the nominal feedrate.
    // This means no cruising, and we'll have to use intersection_distance() to calculate when to abort acceleration 
    // and start braking in order to reach the exit_feedrate exactly at the end of this block.
    if (cruise_distance < 0.0f) {
        accelerate_distance = std::clamp(intersection_distance(feedrate_profile.entry, feedrate_profile.exit, acceleration, distance), 0.0f, distance);
        cruise_distance = 0.0f;
        trapezoid.cruise_feedrate = speed_from_distance(feedrate_profile.entry, accelerate_distance, acceleration);
    }
    else
        trapezoid.cruise_feedrate = feedrate_profile.cruise;

    trapezoid.accelerate_until = accelerate_distance;
    trapezoid.decelerate_after = accelerate_distance + cruise_distance;
}

void GCodeProcessor::TimeMachine::State::reset()
{
    feedrate = 0.0f;
    safe_feedrate = 0.0f;
    axis_feedrate = { 0.0f, 0.0f, 0.0f, 0.0f };
    abs_axis_feedrate = { 0.0f, 0.0f, 0.0f, 0.0f };
}

void GCodeProcessor::TimeMachine::CustomGCodeTime::reset()
{
    needed = false;
    cache = 0.0f;
    times = std::vector<std::pair<CustomGCode::Type, float>>();
}

void GCodeProcessor::TimeMachine::reset()
{
    enabled = false;
    acceleration = 0.0f;
    max_acceleration = 0.0f;
    retract_acceleration = 0.0f;
    max_retract_acceleration = 0.0f;
    travel_acceleration = 0.0f;
    max_travel_acceleration = 0.0f;
    extrude_factor_override_percentage = 1.0f;
    time = 0.0f;
    stop_times = std::vector<StopTime>();
    curr.reset();
    prev.reset();
    gcode_time.reset();
    blocks = std::vector<TimeBlock>();
    g1_times_cache = std::vector<G1LinesCacheItem>();
    first_layer_time = 0.0f;
}

static void planner_forward_pass_kernel(const GCodeProcessor::TimeBlock& prev, GCodeProcessor::TimeBlock& curr)
{
    //
    // C:\prusa\firmware\Prusa-Firmware-Buddy\lib\Marlin\Marlin\src\module\planner.cpp
    // Line 954
    // 
    // If the previous block is an acceleration block, too short to complete the full speed
    // change, adjust the entry speed accordingly. Entry speeds have already been reset,
    // maximized, and reverse-planned. If nominal length is set, max junction speed is
    // guaranteed to be reached. No need to recheck.
    if (!prev.flags.nominal_length && prev.feedrate_profile.entry < curr.feedrate_profile.entry) {
        // Compute the maximum allowable speed
        const float new_entry_speed = max_allowable_speed(-prev.acceleration, prev.feedrate_profile.entry, prev.distance);
        // If true, current block is full-acceleration and we can move the planned pointer forward.
        if (new_entry_speed < curr.feedrate_profile.entry) {
            // Always <= max_entry_speed_sqr. Backward pass sets this.
            curr.feedrate_profile.entry = new_entry_speed;
            curr.flags.recalculate = true;
        }
    }
}

static void planner_reverse_pass_kernel(GCodeProcessor::TimeBlock& curr, const GCodeProcessor::TimeBlock& next)
{
    //
    // C:\prusa\firmware\Prusa-Firmware-Buddy\lib\Marlin\Marlin\src\module\planner.cpp
    // Line 857
    // 
    // If entry speed is already at the maximum entry speed, and there was no change of speed
    // in the next block, there is no need to recheck. Block is cruising and there is no need to
    // compute anything for this block,
    // If not, block entry speed needs to be recalculated to ensure maximum possible planned speed.
    const float max_entry_speed = curr.max_entry_speed;
    // Compute maximum entry speed decelerating over the current block from its exit speed.
    // If not at the maximum entry speed, or the previous block entry speed changed
    if (curr.feedrate_profile.entry != max_entry_speed || next.flags.recalculate) {
        // If nominal length true, max junction speed is guaranteed to be reached.
        // If a block can de/ac-celerate from nominal speed to zero within the length of the block, then
        // the current block and next block junction speeds are guaranteed to always be at their maximum
        // junction speeds in deceleration and acceleration, respectively. This is due to how the current
        // block nominal speed limits both the current and next maximum junction speeds. Hence, in both
        // the reverse and forward planners, the corresponding block junction speed will always be at the
        // the maximum junction speed and may always be ignored for any speed reduction checks.
        const float new_entry_speed = curr.flags.nominal_length ? max_entry_speed :
            std::min(max_entry_speed, max_allowable_speed(-curr.acceleration, next.feedrate_profile.entry, curr.distance));
        if (curr.feedrate_profile.entry != new_entry_speed) {
            // Just Set the new entry speed.
            curr.feedrate_profile.entry = new_entry_speed;
            curr.flags.recalculate = true;
        }
    }
}

static void recalculate_trapezoids(std::vector<GCodeProcessor::TimeBlock>& blocks)
{
    GCodeProcessor::TimeBlock* curr = nullptr;
    GCodeProcessor::TimeBlock* next = nullptr;

    for (size_t i = 0; i < blocks.size(); ++i) {
      GCodeProcessor::TimeBlock& b = blocks[i];

        curr = next;
        next = &b;

        if (curr != nullptr) {
            // Recalculate if current block entry or exit junction speed has changed.
            if (curr->flags.recalculate || next->flags.recalculate) {
                // NOTE: Entry and exit factors always > 0 by all previous logic operations.
                curr->feedrate_profile.exit = next->feedrate_profile.entry;
                curr->calculate_trapezoid();
                curr->flags.recalculate = false; // Reset current only to ensure next trapezoid is computed
            }
        }
    }

    // Last/newest block in buffer. Always recalculated.
    if (next != nullptr) {
        next->feedrate_profile.exit = next->safe_feedrate;
        next->calculate_trapezoid();
        next->flags.recalculate = false;
    }
}

void GCodeProcessor::TimeMachine::calculate_time(GCodeProcessorResult& result, PrintEstimatedStatistics::ETimeMode mode, size_t keep_last_n_blocks, float additional_time)
{
    if (!enabled || blocks.size() < 2)
        return;

    assert(keep_last_n_blocks <= blocks.size());

    // reverse_pass
    for (int i = static_cast<int>(blocks.size()) - 1; i > 0; --i) {
        planner_reverse_pass_kernel(blocks[i - 1], blocks[i]);
    }

    // forward_pass
    for (size_t i = 0; i + 1 < blocks.size(); ++i) {
        planner_forward_pass_kernel(blocks[i], blocks[i + 1]);
    }

    recalculate_trapezoids(blocks);

    const size_t n_blocks_process = blocks.size() - keep_last_n_blocks;
    for (size_t i = 0; i < n_blocks_process; ++i) {
        const TimeBlock& block = blocks[i];
        float block_time = block.time();
        if (i == 0)
            block_time += additional_time;

        time += double(block_time);
        result.moves[block.move_id].time[static_cast<size_t>(mode)] = block_time;
        gcode_time.cache += block_time;
        if (block.layer_id == 1)
            first_layer_time += block_time;

        // detect actual speed moves required to render toolpaths using actual speed
        if (mode == PrintEstimatedStatistics::ETimeMode::Normal) {
            GCodeProcessorResult::MoveVertex& curr_move = result.moves[block.move_id];
            if (curr_move.type == EMoveType::Extrude ||
                curr_move.type == EMoveType::Travel ||
                curr_move.type == EMoveType::Wipe) {
                assert(curr_move.actual_feedrate == 0.0f);

                GCodeProcessorResult::MoveVertex& prev_move = result.moves[block.move_id - 1];
                const bool interpolate = (prev_move.type == curr_move.type);
                if (!interpolate &&
                    prev_move.type != EMoveType::Extrude &&
                    prev_move.type != EMoveType::Travel &&
                    prev_move.type != EMoveType::Wipe)
                    prev_move.actual_feedrate = block.feedrate_profile.entry;

                if (EPSILON < block.trapezoid.accelerate_until && block.trapezoid.accelerate_until < block.distance - EPSILON) {
                    const float t = block.trapezoid.accelerate_until / block.distance;
                    const Vec3f position = lerp(prev_move.position, curr_move.position, t);
                    if ((position - prev_move.position).norm() > EPSILON &&
                        (position - curr_move.position).norm() > EPSILON) {
                        const float delta_extruder = interpolate ? lerp(prev_move.delta_extruder, curr_move.delta_extruder, t) : curr_move.delta_extruder;
                        const float feedrate = interpolate ? lerp(prev_move.feedrate, curr_move.feedrate, t) : curr_move.feedrate;
                        const float width = interpolate ? lerp(prev_move.width, curr_move.width, t) : curr_move.width;
                        const float height = interpolate ? lerp(prev_move.height, curr_move.height, t) : curr_move.height;
                        const float mm3_per_mm = interpolate ? lerp(prev_move.mm3_per_mm, curr_move.mm3_per_mm, t) : curr_move.mm3_per_mm;
                        const float fan_speed = interpolate ? lerp(prev_move.fan_speed, curr_move.fan_speed, t) : curr_move.fan_speed;
                        const float temperature = interpolate ? lerp(prev_move.temperature, curr_move.temperature, t) : curr_move.temperature;
                        actual_speed_moves.push_back({
                            block.move_id,
                            position,
                            block.trapezoid.cruise_feedrate,
                            delta_extruder,
                            feedrate,
                            width,
                            height,
                            mm3_per_mm,
                            fan_speed,
                            temperature
                        });
                    }
                }

                const bool has_deceleration = block.trapezoid.deceleration_distance(block.distance) > EPSILON;
                if (has_deceleration && block.trapezoid.decelerate_after > block.trapezoid.accelerate_until + EPSILON) {
                    const float t = block.trapezoid.decelerate_after / block.distance;
                    const Vec3f position = lerp(prev_move.position, curr_move.position, t);
                    if ((position - prev_move.position).norm() > EPSILON &&
                        (position - curr_move.position).norm() > EPSILON) {
                        const float delta_extruder = interpolate ? lerp(prev_move.delta_extruder, curr_move.delta_extruder, t) : curr_move.delta_extruder;
                        const float feedrate = interpolate ? lerp(prev_move.feedrate, curr_move.feedrate, t) : curr_move.feedrate;
                        const float width = interpolate ? lerp(prev_move.width, curr_move.width, t) : curr_move.width;
                        const float height = interpolate ? lerp(prev_move.height, curr_move.height, t) : curr_move.height;
                        const float mm3_per_mm = interpolate ? lerp(prev_move.mm3_per_mm, curr_move.mm3_per_mm, t) : curr_move.mm3_per_mm;
                        const float fan_speed = interpolate ? lerp(prev_move.fan_speed, curr_move.fan_speed, t) : curr_move.fan_speed;
                        const float temperature = interpolate ? lerp(prev_move.temperature, curr_move.temperature, t) : curr_move.temperature;
                        actual_speed_moves.push_back({
                            block.move_id,
                            position,
                            block.trapezoid.cruise_feedrate,
                            delta_extruder,
                            feedrate,
                            width,
                            height,
                            mm3_per_mm,
                            fan_speed,
                            temperature
                        });
                    }
                }

                const bool is_cruise_only = block.trapezoid.is_cruise_only(block.distance);
                actual_speed_moves.push_back({
                    block.move_id,
                    std::nullopt,
                    (is_cruise_only || !has_deceleration) ? block.trapezoid.cruise_feedrate : block.feedrate_profile.exit,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt
                });
            }
        }
        g1_times_cache.push_back({ block.g1_line_id, block.remaining_internal_g1_lines, float(time) });
        // update times for remaining time to printer stop placeholders
        auto it_stop_time = std::lower_bound(stop_times.begin(), stop_times.end(), block.g1_line_id,
            [](const StopTime& t, unsigned int value) { return t.g1_line_id < value; });
        if (it_stop_time != stop_times.end() && it_stop_time->g1_line_id >= block.g1_line_id)
            it_stop_time->elapsed_time = float(time);
    }

    if (keep_last_n_blocks) {
        blocks.erase(blocks.begin(), blocks.begin() + n_blocks_process);

        // Ensure that the new first block's entry speed will be preserved to prevent discontinuity
        // between the erased blocks' exit speed and the new first block's entry speed.
        // Otherwise, the first block's entry speed could be recalculated on the next pass without
        // considering that there are no more blocks before this first block. This could lead
        // to discontinuity between the exit speed (of already processed blocks) and the entry
        // speed of the first block.
        TimeBlock &first_block = blocks.front();
        first_block.max_entry_speed = first_block.feedrate_profile.entry;
    } else {
        blocks.clear();
    }
}

void GCodeProcessor::TimeProcessor::reset()
{
    extruder_unloaded = true;
    export_remaining_time_enabled = false;
    machine_envelope_processing_enabled = false;
    machine_limits = MachineEnvelopeConfig();
    filament_load_times = std::vector<float>();
    filament_unload_times = std::vector<float>();
    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        machines[i].reset();
    }
    machines[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)].enabled = true;
}

void GCodeProcessor::UsedFilaments::reset()
{
    color_change_cache = 0.0;
    volumes_per_color_change = std::vector<double>();

    tool_change_cache = 0.0;
    volumes_per_extruder.clear();

    role_cache = 0.0;
    filaments_per_role.clear();

    extruder_retracted_volume.clear();
}

void GCodeProcessor::UsedFilaments::increase_caches(double extruded_volume, unsigned char extruder_id, double parking_volume, double extra_loading_volume)
{
    if (extruder_id >= extruder_retracted_volume.size())
        extruder_retracted_volume.resize(extruder_id + 1, parking_volume);
    
    if (recent_toolchange) {
        extruded_volume -= extra_loading_volume;
        recent_toolchange = false;
    }
    
    extruder_retracted_volume[extruder_id] -= extruded_volume;

    if (extruder_retracted_volume[extruder_id] < 0.) {
        extruded_volume = - extruder_retracted_volume[extruder_id];
        extruder_retracted_volume[extruder_id] = 0.;

        color_change_cache += extruded_volume;
        tool_change_cache += extruded_volume;
        role_cache += extruded_volume;
    }
}

void GCodeProcessor::UsedFilaments::process_color_change_cache()
{
    if (color_change_cache != 0.0f) {
        volumes_per_color_change.push_back(color_change_cache);
        color_change_cache = 0.0f;
    }
}

void GCodeProcessor::UsedFilaments::process_extruder_cache(unsigned char extruder_id)
 {
    if (tool_change_cache != 0.0) {
        volumes_per_extruder[extruder_id] += tool_change_cache;
         tool_change_cache = 0.0;
     }
    recent_toolchange = true;
}

void GCodeProcessor::UsedFilaments::process_role_cache(const GCodeProcessor* processor)
{
    if (role_cache != 0.0) {
        std::pair<double, double> filament = { 0.0f, 0.0f };

        const double s = PI * sqr(0.5 * processor->m_result.filament_diameters[processor->m_extruder_id]);
        filament.first = role_cache / s * 0.001;
        filament.second = role_cache * processor->m_result.filament_densities[processor->m_extruder_id] * 0.001;

        GCodeExtrusionRole active_role = processor->m_extrusion_role;
        if (filaments_per_role.find(active_role) != filaments_per_role.end()) {
            filaments_per_role[active_role].first += filament.first;
            filaments_per_role[active_role].second += filament.second;
        }
        else
            filaments_per_role[active_role] = filament;
        role_cache = 0.0;
    }
}

void GCodeProcessor::UsedFilaments::process_caches(const GCodeProcessor* processor)
{
    process_color_change_cache();
    process_extruder_cache(processor->m_extruder_id);
    process_role_cache(processor);
}

void GCodeProcessorResult::reset() {
    is_binary_file = false;
    moves.clear();
    lines_ends.clear();
    bed_shape = Pointfs();
    max_print_height = 0.0f;
    z_offset = 0.0f;
    settings_ids.reset();
    extruders_count = 0;
    backtrace_enabled = false;
    extruder_colors = std::vector<std::string>();
    filament_diameters = std::vector<float>(MIN_EXTRUDERS_COUNT, DEFAULT_FILAMENT_DIAMETER);
    filament_densities = std::vector<float>(MIN_EXTRUDERS_COUNT, DEFAULT_FILAMENT_DENSITY);
    filament_cost = std::vector<float>(MIN_EXTRUDERS_COUNT, DEFAULT_FILAMENT_COST);
    custom_gcode_per_print_z = std::vector<CustomGCode::Item>();
    spiral_vase_mode = false;
    conflict_result = std::nullopt;
    sequential_collision_detected = std::nullopt;
}

const std::vector<std::pair<GCodeProcessor::EProducer, std::string>> GCodeProcessor::Producers = {
    { EProducer::PrusaSlicer, "generated by PrusaSlicer" },
    { EProducer::Slic3rPE,    "generated by Slic3r Prusa Edition" },
    { EProducer::Slic3r,      "generated by Slic3r" },
    { EProducer::SuperSlicer, "generated by SuperSlicer" },
    { EProducer::Cura,        "Cura_SteamEngine" },
    { EProducer::Simplify3D,  "generated by Simplify3D(R)" },
    { EProducer::CraftWare,   "CraftWare" },
    { EProducer::ideaMaker,   "ideaMaker" },
    { EProducer::KissSlicer,  "KISSlicer" },
    { EProducer::BambuStudio, "BambuStudio" }
};

unsigned int GCodeProcessor::s_result_id = 0;

bool GCodeProcessor::contains_reserved_tag(const std::string& gcode, std::string& found_tag)
{
    bool ret = false;

    GCodeReader parser;
    parser.parse_buffer(gcode, [&ret, &found_tag](GCodeReader& parser, const GCodeReader::GCodeLine& line) {
        std::string comment = line.raw();
        if (comment.length() > 2 && comment.front() == ';') {
            comment = comment.substr(1);
            for (const std::string& s : Reserved_Tags) {
                if (boost::starts_with(comment, s)) {
                    ret = true;
                    found_tag = comment;
                    parser.quit_parsing();
                    return;
                }
            }
        }
        });

    return ret;
}

bool GCodeProcessor::contains_reserved_tags(const std::string& gcode, unsigned int max_count, std::vector<std::string>& found_tag)
{
    max_count = std::max(max_count, 1U);

    bool ret = false;

    CNumericLocalesSetter locales_setter;

    GCodeReader parser;
    parser.parse_buffer(gcode, [&ret, &found_tag, max_count](GCodeReader& parser, const GCodeReader::GCodeLine& line) {
        std::string comment = line.raw();
        if (comment.length() > 2 && comment.front() == ';') {
            comment = comment.substr(1);
            for (const std::string& s : Reserved_Tags) {
                if (boost::starts_with(comment, s)) {
                    ret = true;
                    found_tag.push_back(comment);
                    if (found_tag.size() == max_count) {
                        parser.quit_parsing();
                        return;
                    }
                }
            }
        }
        });

    return ret;
}

GCodeProcessor::GCodeProcessor()
: m_options_z_corrector(m_result)
{
    reset();
    m_time_processor.machines[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)].line_m73_main_mask = "M73 P%s R%s\n";
    m_time_processor.machines[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)].line_m73_stop_mask = "M73 C%s\n";
    m_time_processor.machines[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Stealth)].line_m73_main_mask = "M73 Q%s S%s\n";
    m_time_processor.machines[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Stealth)].line_m73_stop_mask = "M73 D%s\n";
}

void GCodeProcessor::apply_config(const PrintConfig& config)
{
    m_parser.apply_config(config);

    m_binarizer.set_enabled(config.binary_gcode);
    m_result.is_binary_file = config.binary_gcode;

    m_producer = EProducer::PrusaSlicer;
    m_flavor = config.gcode_flavor;

    m_result.backtrace_enabled = is_XL_printer(config);

    size_t extruders_count = config.nozzle_diameter.values.size();
    m_result.extruders_count = extruders_count;

    m_extruder_offsets.resize(extruders_count);
    m_extruder_colors.resize(extruders_count);
    m_result.filament_diameters.resize(extruders_count);
    m_result.filament_densities.resize(extruders_count);
    m_result.filament_cost.resize(extruders_count);
    m_extruder_temps.resize(extruders_count);
    m_extruder_temps_config.resize(extruders_count);
    m_extruder_temps_first_layer_config.resize(extruders_count);
    m_is_XL_printer = is_XL_printer(config);

    for (size_t i = 0; i < extruders_count; ++ i) {
        m_extruder_offsets[i]           = to_3d(config.extruder_offset.get_at(i).cast<float>().eval(), 0.f);
        m_extruder_colors[i]            = static_cast<unsigned char>(i);
        m_extruder_temps_first_layer_config[i] = static_cast<int>(config.first_layer_temperature.get_at(i));
        m_extruder_temps_config[i]      = static_cast<int>(config.temperature.get_at(i));
        if (m_extruder_temps_config[i] == 0) {
            // This means the value should be ignored and first layer temp should be used.
            m_extruder_temps_config[i] = m_extruder_temps_first_layer_config[i];
        }
        m_result.filament_diameters[i]  = static_cast<float>(config.filament_diameter.get_at(i));
        m_result.filament_densities[i]  = static_cast<float>(config.filament_density.get_at(i));
        m_result.filament_cost[i]       = static_cast<float>(config.filament_cost.get_at(i));
    }

    if ((m_flavor == gcfMarlinLegacy || m_flavor == gcfMarlinFirmware || m_flavor == gcfRepRapFirmware || m_flavor == gcfKlipper)
         && config.machine_limits_usage.value != MachineLimitsUsage::Ignore) {
        m_time_processor.machine_limits = reinterpret_cast<const MachineEnvelopeConfig&>(config);
        if (m_flavor == gcfMarlinLegacy || m_flavor == gcfKlipper) {
            // Legacy Marlin and Klipper don't have separate travel acceleration, they use the 'extruding' value instead.
            m_time_processor.machine_limits.machine_max_acceleration_travel = m_time_processor.machine_limits.machine_max_acceleration_extruding;
        }
        if (m_flavor == gcfRepRapFirmware) {
            // RRF does not support setting min feedrates. Set them to zero.
            m_time_processor.machine_limits.machine_min_travel_rate.values.assign(m_time_processor.machine_limits.machine_min_travel_rate.size(), 0.);
            m_time_processor.machine_limits.machine_min_extruding_rate.values.assign(m_time_processor.machine_limits.machine_min_extruding_rate.size(), 0.);
        }
    }

    // Filament load / unload times are not specific to a firmware flavor. Let anybody use it if they find it useful.
    // As of now the fields are shown at the UI dialog in the same combo box as the ramming values, so they
    // are considered to be active for the single extruder multi-material printers only.
    m_time_processor.filament_load_times.resize(config.filament_load_time.values.size());
    for (size_t i = 0; i < config.filament_load_time.values.size(); ++i) {
        m_time_processor.filament_load_times[i] = static_cast<float>(config.filament_load_time.values[i]);
    }
    m_time_processor.filament_unload_times.resize(config.filament_unload_time.values.size());
    for (size_t i = 0; i < config.filament_unload_time.values.size(); ++i) {
        m_time_processor.filament_unload_times[i] = static_cast<float>(config.filament_unload_time.values[i]);
    }

    m_single_extruder_multi_material = config.single_extruder_multi_material;

    // With MM setups like Prusa MMU2, the filaments may be expected to be parked at the beginning.
    // Remember the parking position so the initial load is not included in filament estimate.
    if (m_single_extruder_multi_material && extruders_count > 1 && config.wipe_tower) {
        m_parking_position = float(config.parking_pos_retraction.value);
        m_extra_loading_move = float(config.extra_loading_move);
    }

    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        float max_acceleration = get_option_value(m_time_processor.machine_limits.machine_max_acceleration_extruding, i);
        m_time_processor.machines[i].max_acceleration = max_acceleration;
        m_time_processor.machines[i].acceleration = (max_acceleration > 0.0f) ? max_acceleration : DEFAULT_ACCELERATION;
        float max_retract_acceleration = get_option_value(m_time_processor.machine_limits.machine_max_acceleration_retracting, i);
        m_time_processor.machines[i].max_retract_acceleration = max_retract_acceleration;
        m_time_processor.machines[i].retract_acceleration = (max_retract_acceleration > 0.0f) ? max_retract_acceleration : DEFAULT_RETRACT_ACCELERATION;

        float max_travel_acceleration = get_option_value(m_time_processor.machine_limits.machine_max_acceleration_travel, i);
        if ( ! GCodeWriter::supports_separate_travel_acceleration(config.gcode_flavor.value) || config.machine_limits_usage.value != MachineLimitsUsage::EmitToGCode) {
            // Only clamp travel acceleration when it is accessible in machine limits.
            max_travel_acceleration = 0;
        }
        m_time_processor.machines[i].max_travel_acceleration = max_travel_acceleration;
        m_time_processor.machines[i].travel_acceleration = (max_travel_acceleration > 0.0f) ? max_travel_acceleration : DEFAULT_TRAVEL_ACCELERATION;
    }

    m_time_processor.export_remaining_time_enabled = config.remaining_times.value;
    m_use_volumetric_e = config.use_volumetric_e;

    const ConfigOptionFloatOrPercent* first_layer_height = config.option<ConfigOptionFloatOrPercent>("first_layer_height");
    if (first_layer_height != nullptr)
        m_first_layer_height = std::abs(first_layer_height->value);

    m_result.max_print_height = config.max_print_height;

    const ConfigOptionBool* spiral_vase = config.option<ConfigOptionBool>("spiral_vase");
    if (spiral_vase != nullptr)
        m_result.spiral_vase_mode = spiral_vase->value;

    const ConfigOptionFloat* z_offset = config.option<ConfigOptionFloat>("z_offset");
    if (z_offset != nullptr)
        m_z_offset = z_offset->value;
}

void GCodeProcessor::apply_config(const DynamicPrintConfig& config)
{
    m_parser.apply_config(config);

    const ConfigOptionEnum<GCodeFlavor>* gcode_flavor = config.option<ConfigOptionEnum<GCodeFlavor>>("gcode_flavor");
    if (gcode_flavor != nullptr)
        m_flavor = gcode_flavor->value;

    const ConfigOptionPoints* bed_shape = config.option<ConfigOptionPoints>("bed_shape");
    if (bed_shape != nullptr)
        m_result.bed_shape = bed_shape->values;

    const ConfigOptionString* print_settings_id = config.option<ConfigOptionString>("print_settings_id");
    if (print_settings_id != nullptr)
        m_result.settings_ids.print = print_settings_id->value;

    const ConfigOptionStrings* filament_settings_id = config.option<ConfigOptionStrings>("filament_settings_id");
    if (filament_settings_id != nullptr)
        m_result.settings_ids.filament = filament_settings_id->values;

    const ConfigOptionString* printer_settings_id = config.option<ConfigOptionString>("printer_settings_id");
    if (printer_settings_id != nullptr)
        m_result.settings_ids.printer = printer_settings_id->value;

    m_result.extruders_count = config.option<ConfigOptionFloats>("nozzle_diameter")->values.size();

    const ConfigOptionFloats* filament_diameters = config.option<ConfigOptionFloats>("filament_diameter");
    if (filament_diameters != nullptr) {
        m_result.filament_diameters.clear();
        m_result.filament_diameters.resize(filament_diameters->values.size());
        for (size_t i = 0; i < filament_diameters->values.size(); ++i) {
            m_result.filament_diameters[i] = static_cast<float>(filament_diameters->values[i]);
        }
    }

    if (m_result.filament_diameters.size() < m_result.extruders_count) {
        for (size_t i = m_result.filament_diameters.size(); i < m_result.extruders_count; ++i) {
            m_result.filament_diameters.emplace_back(DEFAULT_FILAMENT_DIAMETER);
        }
    }

    const ConfigOptionFloats* filament_densities = config.option<ConfigOptionFloats>("filament_density");
    if (filament_densities != nullptr) {
        m_result.filament_densities.clear();
        m_result.filament_densities.resize(filament_densities->values.size());
        for (size_t i = 0; i < filament_densities->values.size(); ++i) {
            m_result.filament_densities[i] = static_cast<float>(filament_densities->values[i]);
        }
    }

    if (m_result.filament_densities.size() < m_result.extruders_count) {
        for (size_t i = m_result.filament_densities.size(); i < m_result.extruders_count; ++i) {
            m_result.filament_densities.emplace_back(DEFAULT_FILAMENT_DENSITY);
        }
    }

    const ConfigOptionFloats* filament_cost = config.option<ConfigOptionFloats>("filament_cost");
    if (filament_cost != nullptr) {
        m_result.filament_cost.clear();
        m_result.filament_cost.resize(filament_cost->values.size());
        for (size_t i = 0; i < filament_cost->values.size(); ++i) {
            m_result.filament_cost[i] = static_cast<float>(filament_cost->values[i]);
        }
    }

    if (m_result.filament_cost.size() < m_result.extruders_count) {
        for (size_t i = m_result.filament_cost.size(); i < m_result.extruders_count; ++i) {
            m_result.filament_cost.emplace_back(DEFAULT_FILAMENT_COST);
        }
    }

    const ConfigOptionPoints* extruder_offset = config.option<ConfigOptionPoints>("extruder_offset");
    if (extruder_offset != nullptr) {
        m_extruder_offsets.resize(extruder_offset->values.size());
        for (size_t i = 0; i < extruder_offset->values.size(); ++i) {
            Vec2f offset = extruder_offset->values[i].cast<float>();
            m_extruder_offsets[i] = { offset(0), offset(1), 0.0f };
        }
    }
    
    if (m_extruder_offsets.size() < m_result.extruders_count) {
        for (size_t i = m_extruder_offsets.size(); i < m_result.extruders_count; ++i) {
            m_extruder_offsets.emplace_back(DEFAULT_EXTRUDER_OFFSET);
        }
    }

    const ConfigOptionStrings* extruder_colour = config.option<ConfigOptionStrings>("extruder_colour");
    if (extruder_colour != nullptr) {
        // takes colors from config
        m_result.extruder_colors = extruder_colour->values;
        // try to replace missing values with filament colors
        const ConfigOptionStrings* filament_colour = config.option<ConfigOptionStrings>("filament_colour");
        if (filament_colour != nullptr && filament_colour->values.size() == m_result.extruder_colors.size()) {
            for (size_t i = 0; i < m_result.extruder_colors.size(); ++i) {
                if (m_result.extruder_colors[i].empty())
                    m_result.extruder_colors[i] = filament_colour->values[i];
            }
        }
    }

    if (m_result.extruder_colors.size() < m_result.extruders_count) {
        for (size_t i = m_result.extruder_colors.size(); i < m_result.extruders_count; ++i) {
            m_result.extruder_colors.emplace_back(std::string());
        }
    }

    // replace missing values with default
    for (size_t i = 0; i < m_result.extruder_colors.size(); ++i) {
        if (m_result.extruder_colors[i].empty())
            m_result.extruder_colors[i] = "#FF8000";
    }

    m_extruder_colors.resize(m_result.extruder_colors.size());
    for (size_t i = 0; i < m_result.extruder_colors.size(); ++i) {
        m_extruder_colors[i] = static_cast<unsigned char>(i);
    }

    m_extruder_temps.resize(m_result.extruders_count);

    const ConfigOptionFloats* filament_load_time = config.option<ConfigOptionFloats>("filament_load_time");
    if (filament_load_time != nullptr) {
        m_time_processor.filament_load_times.resize(filament_load_time->values.size());
        for (size_t i = 0; i < filament_load_time->values.size(); ++i) {
            m_time_processor.filament_load_times[i] = static_cast<float>(filament_load_time->values[i]);
        }
    }

    const ConfigOptionFloats* filament_unload_time = config.option<ConfigOptionFloats>("filament_unload_time");
    if (filament_unload_time != nullptr) {
        m_time_processor.filament_unload_times.resize(filament_unload_time->values.size());
        for (size_t i = 0; i < filament_unload_time->values.size(); ++i) {
            m_time_processor.filament_unload_times[i] = static_cast<float>(filament_unload_time->values[i]);
        }
    }

    // With MM setups like Prusa MMU2, the filaments may be expected to be parked at the beginning.
    // Remember the parking position so the initial load is not included in filament estimate.
    const ConfigOptionBool* single_extruder_multi_material = config.option<ConfigOptionBool>("single_extruder_multi_material");
    const ConfigOptionBool* wipe_tower = config.option<ConfigOptionBool>("wipe_tower");
    const ConfigOptionFloat* parking_pos_retraction = config.option<ConfigOptionFloat>("parking_pos_retraction");
    const ConfigOptionFloat* extra_loading_move = config.option<ConfigOptionFloat>("extra_loading_move");

    m_single_extruder_multi_material = single_extruder_multi_material != nullptr && single_extruder_multi_material->value;

    if (m_single_extruder_multi_material && wipe_tower != nullptr && parking_pos_retraction != nullptr && extra_loading_move != nullptr) {
        if (m_single_extruder_multi_material && m_result.extruders_count > 1 && wipe_tower->value) {
            m_parking_position = float(parking_pos_retraction->value);
            m_extra_loading_move = float(extra_loading_move->value);
        }
    }

    bool use_machine_limits = false;
    const ConfigOptionEnum<MachineLimitsUsage>* machine_limits_usage = config.option<ConfigOptionEnum<MachineLimitsUsage>>("machine_limits_usage");
    if (machine_limits_usage != nullptr)
        use_machine_limits = machine_limits_usage->value != MachineLimitsUsage::Ignore;

    if (use_machine_limits && (m_flavor == gcfMarlinLegacy || m_flavor == gcfMarlinFirmware || m_flavor == gcfRepRapFirmware || m_flavor == gcfKlipper)) {
        const ConfigOptionFloats* machine_max_acceleration_x = config.option<ConfigOptionFloats>("machine_max_acceleration_x");
        if (machine_max_acceleration_x != nullptr)
            m_time_processor.machine_limits.machine_max_acceleration_x.values = machine_max_acceleration_x->values;

        const ConfigOptionFloats* machine_max_acceleration_y = config.option<ConfigOptionFloats>("machine_max_acceleration_y");
        if (machine_max_acceleration_y != nullptr)
            m_time_processor.machine_limits.machine_max_acceleration_y.values = machine_max_acceleration_y->values;

        const ConfigOptionFloats* machine_max_acceleration_z = config.option<ConfigOptionFloats>("machine_max_acceleration_z");
        if (machine_max_acceleration_z != nullptr)
            m_time_processor.machine_limits.machine_max_acceleration_z.values = machine_max_acceleration_z->values;

        const ConfigOptionFloats* machine_max_acceleration_e = config.option<ConfigOptionFloats>("machine_max_acceleration_e");
        if (machine_max_acceleration_e != nullptr)
            m_time_processor.machine_limits.machine_max_acceleration_e.values = machine_max_acceleration_e->values;

        const ConfigOptionFloats* machine_max_feedrate_x = config.option<ConfigOptionFloats>("machine_max_feedrate_x");
        if (machine_max_feedrate_x != nullptr)
            m_time_processor.machine_limits.machine_max_feedrate_x.values = machine_max_feedrate_x->values;

        const ConfigOptionFloats* machine_max_feedrate_y = config.option<ConfigOptionFloats>("machine_max_feedrate_y");
        if (machine_max_feedrate_y != nullptr)
            m_time_processor.machine_limits.machine_max_feedrate_y.values = machine_max_feedrate_y->values;

        const ConfigOptionFloats* machine_max_feedrate_z = config.option<ConfigOptionFloats>("machine_max_feedrate_z");
        if (machine_max_feedrate_z != nullptr)
            m_time_processor.machine_limits.machine_max_feedrate_z.values = machine_max_feedrate_z->values;

        const ConfigOptionFloats* machine_max_feedrate_e = config.option<ConfigOptionFloats>("machine_max_feedrate_e");
        if (machine_max_feedrate_e != nullptr)
            m_time_processor.machine_limits.machine_max_feedrate_e.values = machine_max_feedrate_e->values;

        const ConfigOptionFloats* machine_max_jerk_x = config.option<ConfigOptionFloats>("machine_max_jerk_x");
        if (machine_max_jerk_x != nullptr)
            m_time_processor.machine_limits.machine_max_jerk_x.values = machine_max_jerk_x->values;

        const ConfigOptionFloats* machine_max_jerk_y = config.option<ConfigOptionFloats>("machine_max_jerk_y");
        if (machine_max_jerk_y != nullptr)
            m_time_processor.machine_limits.machine_max_jerk_y.values = machine_max_jerk_y->values;

        const ConfigOptionFloats* machine_max_jerk_z = config.option<ConfigOptionFloats>("machine_max_jerkz");
        if (machine_max_jerk_z != nullptr)
            m_time_processor.machine_limits.machine_max_jerk_z.values = machine_max_jerk_z->values;

        const ConfigOptionFloats* machine_max_jerk_e = config.option<ConfigOptionFloats>("machine_max_jerk_e");
        if (machine_max_jerk_e != nullptr)
            m_time_processor.machine_limits.machine_max_jerk_e.values = machine_max_jerk_e->values;

        const ConfigOptionFloats* machine_max_acceleration_extruding = config.option<ConfigOptionFloats>("machine_max_acceleration_extruding");
        if (machine_max_acceleration_extruding != nullptr)
            m_time_processor.machine_limits.machine_max_acceleration_extruding.values = machine_max_acceleration_extruding->values;

        const ConfigOptionFloats* machine_max_acceleration_retracting = config.option<ConfigOptionFloats>("machine_max_acceleration_retracting");
        if (machine_max_acceleration_retracting != nullptr)
            m_time_processor.machine_limits.machine_max_acceleration_retracting.values = machine_max_acceleration_retracting->values;


        // Legacy Marlin and Klipper don't have separate travel acceleration, they use the 'extruding' value instead.
        const ConfigOptionFloats* machine_max_acceleration_travel = config.option<ConfigOptionFloats>((m_flavor == gcfMarlinLegacy || m_flavor == gcfKlipper)
                                                                                                    ? "machine_max_acceleration_extruding"
                                                                                                    : "machine_max_acceleration_travel");
        if (machine_max_acceleration_travel != nullptr)
            m_time_processor.machine_limits.machine_max_acceleration_travel.values = machine_max_acceleration_travel->values;


        const ConfigOptionFloats* machine_min_extruding_rate = config.option<ConfigOptionFloats>("machine_min_extruding_rate");
        if (machine_min_extruding_rate != nullptr) {
            m_time_processor.machine_limits.machine_min_extruding_rate.values = machine_min_extruding_rate->values;
            if (m_flavor == gcfRepRapFirmware) {
                // RRF does not support setting min feedrates. Set zero.
                m_time_processor.machine_limits.machine_min_extruding_rate.values.assign(m_time_processor.machine_limits.machine_min_extruding_rate.size(), 0.);
            }
        }

        const ConfigOptionFloats* machine_min_travel_rate = config.option<ConfigOptionFloats>("machine_min_travel_rate");
        if (machine_min_travel_rate != nullptr) {
            m_time_processor.machine_limits.machine_min_travel_rate.values = machine_min_travel_rate->values;
            if (m_flavor == gcfRepRapFirmware) {
                // RRF does not support setting min feedrates. Set zero.
                m_time_processor.machine_limits.machine_min_travel_rate.values.assign(m_time_processor.machine_limits.machine_min_travel_rate.size(), 0.);
            }
        }
    }

    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        float max_acceleration = get_option_value(m_time_processor.machine_limits.machine_max_acceleration_extruding, i);
        m_time_processor.machines[i].max_acceleration = max_acceleration;
        m_time_processor.machines[i].acceleration = (max_acceleration > 0.0f) ? max_acceleration : DEFAULT_ACCELERATION;
        float max_retract_acceleration = get_option_value(m_time_processor.machine_limits.machine_max_acceleration_retracting, i);
        m_time_processor.machines[i].max_retract_acceleration = max_retract_acceleration;
        m_time_processor.machines[i].retract_acceleration = (max_retract_acceleration > 0.0f) ? max_retract_acceleration : DEFAULT_RETRACT_ACCELERATION;
        float max_travel_acceleration = get_option_value(m_time_processor.machine_limits.machine_max_acceleration_travel, i);
        m_time_processor.machines[i].max_travel_acceleration = max_travel_acceleration;
        m_time_processor.machines[i].travel_acceleration = (max_travel_acceleration > 0.0f) ? max_travel_acceleration : DEFAULT_TRAVEL_ACCELERATION;
    }

    if (m_flavor == gcfMarlinLegacy || m_flavor == gcfMarlinFirmware) { // No Klipper here, it does not support silent mode.
        const ConfigOptionBool* silent_mode = config.option<ConfigOptionBool>("silent_mode");
        if (silent_mode != nullptr) {
            if (silent_mode->value && m_time_processor.machine_limits.machine_max_acceleration_x.values.size() > 1)
                enable_stealth_time_estimator(true);
        }
    }

    const ConfigOptionBool* use_volumetric_e = config.option<ConfigOptionBool>("use_volumetric_e");
    if (use_volumetric_e != nullptr)
        m_use_volumetric_e = use_volumetric_e->value;

    const ConfigOptionFloatOrPercent* first_layer_height = config.option<ConfigOptionFloatOrPercent>("first_layer_height");
    if (first_layer_height != nullptr)
        m_first_layer_height = std::abs(first_layer_height->value);

    const ConfigOptionFloat* max_print_height = config.option<ConfigOptionFloat>("max_print_height");
    if (max_print_height != nullptr)
        m_result.max_print_height = max_print_height->value;

    const ConfigOptionBool* spiral_vase = config.option<ConfigOptionBool>("spiral_vase");
    if (spiral_vase != nullptr)
        m_result.spiral_vase_mode = spiral_vase->value;

    const ConfigOptionFloat* z_offset = config.option<ConfigOptionFloat>("z_offset");
    if (z_offset != nullptr)
        m_z_offset = z_offset->value;
}

void GCodeProcessor::enable_stealth_time_estimator(bool enabled)
{
    m_time_processor.machines[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Stealth)].enabled = enabled;
}

void GCodeProcessor::reset()
{
    m_units = EUnits::Millimeters;
    m_global_positioning_type = EPositioningType::Absolute;
    m_e_local_positioning_type = EPositioningType::Absolute;
    m_extruder_offsets = std::vector<Vec3f>(MIN_EXTRUDERS_COUNT, Vec3f::Zero());
    m_flavor = gcfRepRapSprinter;

    m_start_position = { 0.0f, 0.0f, 0.0f, 0.0f };
    m_end_position = { 0.0f, 0.0f, 0.0f, 0.0f };
    m_saved_position = { 0.0f, 0.0f, 0.0f, 0.0f };
    m_origin = { 0.0f, 0.0f, 0.0f, 0.0f };
    m_cached_position.reset();
    m_wiping = false;

    m_line_id = 0;
    m_last_line_id = 0;
    m_feedrate = 0.0f;
    m_feed_multiply.reset();
    m_width = 0.0f;
    m_height = 0.0f;
    m_forced_width = 0.0f;
    m_forced_height = 0.0f;
    m_mm3_per_mm = 0.0f;
    m_fan_speed = 0.0f;
    m_z_offset = 0.0f;

    m_extrusion_role = GCodeExtrusionRole::None;
    m_extruder_id = 0;
    m_extruder_colors.resize(MIN_EXTRUDERS_COUNT);
    for (size_t i = 0; i < MIN_EXTRUDERS_COUNT; ++i) {
        m_extruder_colors[i] = static_cast<unsigned char>(i);
    }
    m_extruder_temps.resize(MIN_EXTRUDERS_COUNT);
    for (size_t i = 0; i < MIN_EXTRUDERS_COUNT; ++i) {
        m_extruder_temps[i] = 0.0f;
    }

    m_parking_position = 0.f;
    m_extra_loading_move = 0.f;
    m_extruded_last_z = 0.0f;
    m_first_layer_height = 0.0f;
    m_g1_line_id = 0;
    m_layer_id = 0;
    m_cp_color.reset();

    m_producer = EProducer::Unknown;

    m_time_processor.reset();
    m_used_filaments.reset();

    m_result.reset();
    m_result.id = ++s_result_id;

    m_use_volumetric_e = false;
    m_last_default_color_id = 0;

    m_options_z_corrector.reset();

    m_kissslicer_toolchange_time_correction = 0.0f;

    m_single_extruder_multi_material = false;
}

static inline const char* skip_whitespaces(const char *begin, const char *end) {
    for (; begin != end && (*begin == ' ' || *begin == '\t'); ++ begin);
    return begin;
}

static inline const char* remove_eols(const char *begin, const char *end) {
    for (; begin != end && (*(end - 1) == '\r' || *(end - 1) == '\n'); -- end);
    return end;
}

// Load a G-code into a stand-alone G-code viewer.
// throws CanceledException through print->throw_if_canceled() (sent by the caller as callback).
void GCodeProcessor::process_file(const std::string& filename, GCodeReader::ProgressCallback progress_callback,
    std::function<void(void)> cancel_callback)
{
    FILE* file = boost::nowide::fopen(filename.c_str(), "rb");
    if (file == nullptr)
        throw Slic3r::RuntimeError(format("Error opening file %1%", filename));

    using namespace bgcode::core;
    std::vector<std::byte> cs_buffer(65536);
    const bool is_binary = is_valid_binary_gcode(*file, true, cs_buffer.data(), cs_buffer.size()) == EResult::Success;
    fclose(file);

    if (is_binary)
        process_binary_file(filename, progress_callback, cancel_callback);
    else
        process_ascii_file(filename, progress_callback, cancel_callback);
}

void GCodeProcessor::process_ascii_file(const std::string& filename, GCodeReader::ProgressCallback progress_callback,
    std::function<void(void)> cancel_callback)
{
    CNumericLocalesSetter locales_setter;

    // pre-processing
    // parse the gcode file to detect its producer
    {
        m_parser.parse_file_raw(filename, [this](GCodeReader& reader, const char *begin, const char *end) {
            begin = skip_whitespaces(begin, end);
            if (begin != end && *begin == ';') {
                // Comment.
                begin = skip_whitespaces(++ begin, end);
                end   = remove_eols(begin, end);
                if (begin != end && detect_producer(std::string_view(begin, end - begin)))
                    m_parser.quit_parsing();
            }
        });
        m_parser.reset();

        // if the gcode was produced by PrusaSlicer,
        // extract the config from it
        if (m_producer == EProducer::PrusaSlicer || m_producer == EProducer::Slic3rPE || m_producer == EProducer::Slic3r) {
            DynamicPrintConfig config;
            config.apply(FullPrintConfig::defaults());
            // Silently substitute unknown values by new ones for loading configurations from PrusaSlicer's own G-code.
            // Showing substitution log or errors may make sense, but we are not really reading many values from the G-code config,
            // thus a probability of incorrect substitution is low and the G-code viewer is a consumer-only anyways.
            config.load_from_gcode_file(filename, ForwardCompatibilitySubstitutionRule::EnableSilent);
            apply_config(config);
        }
        else {
            m_result.extruder_colors = DEFAULT_EXTRUDER_COLORS;

            if (m_producer == EProducer::Simplify3D)
                apply_config_simplify3d(filename);
            else if (m_producer == EProducer::SuperSlicer)
                apply_config_superslicer(filename);
            else if (m_producer == EProducer::KissSlicer)
                apply_config_kissslicer(filename);

            if (m_result.extruders_count == 0)
                m_result.extruders_count = MIN_EXTRUDERS_COUNT;
        }
    }

    // process gcode
    m_result.filename = filename;
    m_result.is_binary_file = false;
    m_result.id = ++s_result_id;
    initialize_result_moves();
    size_t parse_line_callback_cntr = 10000;
    m_parser.set_progress_callback(progress_callback);
    m_parser.parse_file(filename, [this, cancel_callback, &parse_line_callback_cntr](GCodeReader& reader, const GCodeReader::GCodeLine& line) {
        if (-- parse_line_callback_cntr == 0) {
            // Don't call the cancel_callback() too often, do it every at every 10000'th line.
            parse_line_callback_cntr = 10000;
            if (cancel_callback)
                cancel_callback();
        }
        this->process_gcode_line(line, true);
    }, m_result.lines_ends);

    // Don't post-process the G-code to update time stamps.
    this->finalize(false);
}

static void update_lines_ends_and_out_file_pos(const std::string& out_string, std::vector<size_t>& lines_ends, size_t* out_file_pos)
{
    for (size_t i = 0; i < out_string.size(); ++i) {
        if (out_string[i] == '\n')
            lines_ends.emplace_back((out_file_pos != nullptr) ? *out_file_pos + i + 1 : i + 1);
    }
    if (out_file_pos != nullptr)
        *out_file_pos += out_string.size();
}

void GCodeProcessor::process_binary_file(const std::string& filename, GCodeReader::ProgressCallback progress_callback,
    std::function<void()> cancel_callback)
{
    FilePtr file{ boost::nowide::fopen(filename.c_str(), "rb") };
    if (file.f == nullptr)
        throw Slic3r::RuntimeError(format("Error opening file %1%", filename));

    fseek(file.f, 0, SEEK_END);
    const long file_size = ftell(file.f);
    rewind(file.f);

    auto update_progress = [progress_callback, file_size, &file]() {
        const long pos = ftell(file.f);
        if (progress_callback != nullptr)
            progress_callback(float(pos) / float(file_size));
    };

    auto throw_error = [progress_callback](const std::string& msg) {
        if (progress_callback != nullptr)
            progress_callback(1.0f);
        throw Slic3r::RuntimeError(msg.c_str());
    };

    // read file header
    using namespace bgcode::core;
    using namespace bgcode::binarize;
    FileHeader file_header;
    EResult res = read_header(*file.f, file_header, nullptr);
    update_progress();
    if (res != EResult::Success)
        throw_error(format("File %1% does not contain a valid binary gcode\nError: %2%", filename, 
            std::string(translate_result(res))));

    // read file metadata block, if present
    BlockHeader block_header;
    std::vector<std::byte> cs_buffer(65536);
    res = read_next_block_header(*file.f, file_header, block_header, cs_buffer.data(), cs_buffer.size());
    update_progress();
    if (res != EResult::Success)
        throw_error(format("Error reading file %1%: %2%", filename, std::string(translate_result(res))));
    if ((EBlockType)block_header.type != EBlockType::FileMetadata &&
        (EBlockType)block_header.type != EBlockType::PrinterMetadata)
        throw_error(format("Unable to find file metadata block in file %1%", filename));
    if ((EBlockType)block_header.type == EBlockType::FileMetadata) {
        FileMetadataBlock file_metadata_block;
        res = file_metadata_block.read_data(*file.f, file_header, block_header);
        update_progress();
        if (res != EResult::Success)
            throw_error(format("Error reading file %1%: %2%", filename, std::string(translate_result(res))));
        auto producer_it = std::find_if(file_metadata_block.raw_data.begin(), file_metadata_block.raw_data.end(),
            [](const std::pair<std::string, std::string>& item) { return item.first == "Producer"; });
        if (producer_it != file_metadata_block.raw_data.end() && boost::starts_with(producer_it->second, std::string(SLIC3R_APP_NAME)))
            m_producer = EProducer::PrusaSlicer;
        else
            m_producer = EProducer::Unknown;
        res = read_next_block_header(*file.f, file_header, block_header, cs_buffer.data(), cs_buffer.size());
        update_progress();
        if (res != EResult::Success)
            throw_error(format("Error reading file %1%: %2%", filename, std::string(translate_result(res))));
    }
    else {
        m_producer = EProducer::Unknown;
    }

    // read printer metadata block
    if ((EBlockType)block_header.type != EBlockType::PrinterMetadata)
        throw_error(format("Unable to find printer metadata block in file %1%", filename));
    PrinterMetadataBlock printer_metadata_block;
    res = printer_metadata_block.read_data(*file.f, file_header, block_header);
    update_progress();
    if (res != EResult::Success)
        throw_error(format("Error reading file %1%: %2%", filename, std::string(translate_result(res))));

    // read thumbnail blocks
    res = read_next_block_header(*file.f, file_header, block_header, cs_buffer.data(), cs_buffer.size());
    update_progress();
    if (res != EResult::Success)
        throw_error(format("Error reading file %1%: %2%", filename, std::string(translate_result(res))));

    while ((EBlockType)block_header.type == EBlockType::Thumbnail) {
        ThumbnailBlock thumbnail_block;
        res = thumbnail_block.read_data(*file.f, file_header, block_header);
        update_progress();
        if (res != EResult::Success)
            throw_error(format("Error reading file %1%: %2%", filename, std::string(translate_result(res))));
        res = read_next_block_header(*file.f, file_header, block_header, cs_buffer.data(), cs_buffer.size());
        update_progress();
        if (res != EResult::Success)
            throw_error(format("Error reading file %1%: %2%", filename, std::string(translate_result(res))));
    }

    // read print metadata block
    if ((EBlockType)block_header.type != EBlockType::PrintMetadata)
        throw_error(format("Unable to find print metadata block in file %1%", filename));
    PrintMetadataBlock print_metadata_block;
    res = print_metadata_block.read_data(*file.f, file_header, block_header);
    update_progress();
    if (res != EResult::Success)
        throw_error(format("Error reading file %1%: %2%", filename, std::string(translate_result(res))));

    // read slicer metadata block
    res = read_next_block_header(*file.f, file_header, block_header, cs_buffer.data(), cs_buffer.size());
    update_progress();
    if (res != EResult::Success)
        throw_error(format("Error reading file %1%: %2%", filename, std::string(translate_result(res))));
    if ((EBlockType)block_header.type != EBlockType::SlicerMetadata)
        throw_error(format("Unable to find slicer metadata block in file %1%", filename));
    SlicerMetadataBlock slicer_metadata_block;
    res = slicer_metadata_block.read_data(*file.f, file_header, block_header);
    update_progress();
    if (res != EResult::Success)
        throw_error(format("Error reading file %1%: %2%", filename, std::string(translate_result(res))));
    DynamicPrintConfig config;
    config.apply(FullPrintConfig::defaults());
    std::string str;
    for (const auto& [key, value] : slicer_metadata_block.raw_data) {
        str += key + " = " + value + "\n";
    }
    // Silently substitute unknown values by new ones for loading configurations from PrusaSlicer's own G-code.
    // Showing substitution log or errors may make sense, but we are not really reading many values from the G-code config,
    // thus a probability of incorrect substitution is low and the G-code viewer is a consumer-only anyways.
    config.load_from_ini_string(str, ForwardCompatibilitySubstitutionRule::EnableSilent);
    apply_config(config);

    m_result.filename = filename;
    m_result.is_binary_file = true;
    m_result.id = ++s_result_id;
    initialize_result_moves();

    // read gcodes block
    res = read_next_block_header(*file.f, file_header, block_header, cs_buffer.data(), cs_buffer.size());
    update_progress();
    if (res != EResult::Success)
        throw_error(format("Error reading file %1%: %2%", filename, std::string(translate_result(res))));
    if ((EBlockType)block_header.type != EBlockType::GCode)
        throw_error(format("Unable to find gcode block in file %1%", filename));
    while ((EBlockType)block_header.type == EBlockType::GCode) {
        GCodeBlock block;
        res = block.read_data(*file.f, file_header, block_header);
        update_progress();
        if (res != EResult::Success)
            throw_error(format("Error reading file %1%: %2%", filename, std::string(translate_result(res))));

        std::vector<size_t>& lines_ends = m_result.lines_ends.emplace_back(std::vector<size_t>());
        update_lines_ends_and_out_file_pos(block.raw_data, lines_ends, nullptr);

        m_parser.parse_buffer(block.raw_data, [this](GCodeReader& reader, const GCodeReader::GCodeLine& line) {
            this->process_gcode_line(line, true);
        });

        if (ftell(file.f) == file_size)
            break;

        res = read_next_block_header(*file.f, file_header, block_header, cs_buffer.data(), cs_buffer.size());
        update_progress();
        if (res != EResult::Success)
            throw_error(format("Error reading file %1%: %2%", filename, std::string(translate_result(res))));
    }

    // Don't post-process the G-code to update time stamps.
    this->finalize(false);
}

void GCodeProcessor::initialize(const std::string& filename)
{
    assert(is_decimal_separator_point());

    // process gcode
    m_result.filename = filename;
    m_result.id = ++s_result_id;
}

void GCodeProcessor::process_buffer(const std::string &buffer)
{
    //FIXME maybe cache GCodeLine gline to be over multiple parse_buffer() invocations.
    m_parser.parse_buffer(buffer, [this](GCodeReader&, const GCodeReader::GCodeLine& line) { 
        this->process_gcode_line(line, false);
    });
}

void GCodeProcessor::finalize(bool perform_post_process)
{
    m_result.z_offset = m_z_offset;

    // update width/height of wipe moves
    for (GCodeProcessorResult::MoveVertex& move : m_result.moves) {
        if (move.type == EMoveType::Wipe) {
            move.width = Wipe_Width;
            move.height = Wipe_Height;
        }
    }

    calculate_time(m_result);

    // process the time blocks
    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        TimeMachine& machine = m_time_processor.machines[i];
        TimeMachine::CustomGCodeTime& gcode_time = machine.gcode_time;
        if (gcode_time.needed && gcode_time.cache != 0.0f)
            gcode_time.times.push_back({ CustomGCode::ColorChange, gcode_time.cache });
    }

    m_used_filaments.process_caches(this);

    update_estimated_statistics();

    if (perform_post_process)
        post_process();
}

float GCodeProcessor::get_time(PrintEstimatedStatistics::ETimeMode mode) const
{
    return (mode < PrintEstimatedStatistics::ETimeMode::Count) ? float(m_time_processor.machines[static_cast<size_t>(mode)].time) : 0.0f;
}

std::string GCodeProcessor::get_time_dhm(PrintEstimatedStatistics::ETimeMode mode) const
{
    return (mode < PrintEstimatedStatistics::ETimeMode::Count) ? short_time(get_time_dhms(float(m_time_processor.machines[static_cast<size_t>(mode)].time))) : std::string("N/A");
}

std::vector<std::pair<CustomGCode::Type, std::pair<float, float>>> GCodeProcessor::get_custom_gcode_times(PrintEstimatedStatistics::ETimeMode mode, bool include_remaining) const
{
    std::vector<std::pair<CustomGCode::Type, std::pair<float, float>>> ret;
    if (mode < PrintEstimatedStatistics::ETimeMode::Count) {
        const TimeMachine& machine = m_time_processor.machines[static_cast<size_t>(mode)];
        float total_time = 0.0f;
        for (const auto& [type, time] : machine.gcode_time.times) {
            float remaining = include_remaining ? machine.time - total_time : 0.0f;
            ret.push_back({ type, { time, remaining } });
            total_time += time;
        }
    }
    return ret;
}

ConfigSubstitutions load_from_superslicer_gcode_file(const std::string& filename, DynamicPrintConfig& config, ForwardCompatibilitySubstitutionRule compatibility_rule)
{
    // for reference, see: ConfigBase::load_from_gcode_file()

    boost::nowide::ifstream ifs(filename);

    auto                      header_end_pos = ifs.tellg();
    ConfigSubstitutionContext substitutions_ctxt(compatibility_rule);
    size_t                    key_value_pairs = 0;

    ifs.seekg(0, ifs.end);
    auto file_length = ifs.tellg();
    auto data_length = std::min<std::fstream::pos_type>(65535, file_length - header_end_pos);
    ifs.seekg(file_length - data_length, ifs.beg);
    std::vector<char> data(size_t(data_length) + 1, 0);
    ifs.read(data.data(), data_length);
    ifs.close();
    key_value_pairs = ConfigBase::load_from_gcode_string_legacy(config, data.data(), substitutions_ctxt);

    if (key_value_pairs < 80)
        throw Slic3r::RuntimeError(format("Suspiciously low number of configuration values extracted from %1%: %2%", filename, key_value_pairs));

    return std::move(substitutions_ctxt.substitutions);
}

void GCodeProcessor::apply_config_superslicer(const std::string& filename)
{
    DynamicPrintConfig config;
    config.apply(FullPrintConfig::defaults());
    load_from_superslicer_gcode_file(filename, config, ForwardCompatibilitySubstitutionRule::EnableSilent);
    apply_config(config);
}

void GCodeProcessor::apply_config_kissslicer(const std::string& filename)
{
    size_t found_counter = 0;
    m_parser.parse_file_raw(filename, [this, &found_counter](GCodeReader& reader, const char* begin, const char* end) {
        auto detect_flavor = [this](const std::string_view comment) {
            static const std::string search_str = "firmware_type";
            const size_t pos = comment.find(search_str);
            if (pos != comment.npos) {
                std::vector<std::string> elements;
                boost::split(elements, comment, boost::is_any_of("="));
                if (elements.size() == 2) {
                    try
                    {
                        switch (std::stoi(elements[1]))
                        {
                        default: { break; }
                        case 1:
                        case 2:
                        case 3: { m_flavor = gcfMarlinLegacy; break; }
                        }
                        return true;
                    }
                    catch (...)
                    {
                        // invalid data, do nothing
                    }
                }
            }
            return false;
        };

        auto detect_printer = [this](const std::string_view comment) {
            static const std::string search_str = "printer_name";
            const size_t pos = comment.find(search_str);
            if (pos != comment.npos) {
                std::vector<std::string> elements;
                boost::split(elements, comment, boost::is_any_of("="));
                if (elements.size() == 2) {
                    elements[1] = boost::to_upper_copy(elements[1]);
                    if (boost::contains(elements[1], "MK2.5") || boost::contains(elements[1], "MK3"))
                        m_kissslicer_toolchange_time_correction = 18.0f; // MMU2
                    else if (boost::contains(elements[1], "MK2"))
                        m_kissslicer_toolchange_time_correction = 5.0f; // MMU
                }
                return true;
            }

            return false;
        };

        begin = skip_whitespaces(begin, end);
        if (begin != end) {
            if (*begin == ';') {
                // Comment.
                begin = skip_whitespaces(++begin, end);
                end = remove_eols(begin, end);
                if (begin != end) {
                    const std::string_view comment(begin, end - begin);
                    if (detect_flavor(comment) || detect_printer(comment))
                        ++found_counter;
                }

                // we got the data,
                // force early exit to avoid parsing the entire file
                if (found_counter == 2)
                    m_parser.quit_parsing();
            }
            else if (*begin == 'M' || *begin == 'G')
                // the header has been fully parsed, quit search
                m_parser.quit_parsing();
        }
        }
    );
    m_parser.reset();
}

float GCodeProcessor::get_first_layer_time(PrintEstimatedStatistics::ETimeMode mode) const
{
    return (mode < PrintEstimatedStatistics::ETimeMode::Count) ? m_time_processor.machines[static_cast<size_t>(mode)].first_layer_time : 0.0f;
}

void GCodeProcessor::apply_config_simplify3d(const std::string& filename)
{
    struct BedSize
    {
        double x{ 0.0 };
        double y{ 0.0 };

        bool is_defined() const { return x > 0.0 && y > 0.0; }
    };

    BedSize bed_size;
    bool    producer_detected = false;

    m_parser.parse_file_raw(filename, [this, &bed_size, &producer_detected](GCodeReader& reader, const char* begin, const char* end) {

        auto extract_double = [](const std::string_view cmt, const std::string& key, double& out) {
            size_t pos = cmt.find(key);
            if (pos != cmt.npos) {
                pos = cmt.find(',', pos);
                if (pos != cmt.npos) {
                    out = string_to_double_decimal_point(cmt.substr(pos+1));
                    return true;
                }
            }
            return false;
        };

        auto extract_floats = [](const std::string_view cmt, const std::string& key, std::vector<float>& out) {
            size_t pos = cmt.find(key);
            if (pos != cmt.npos) {
                pos = cmt.find(',', pos);
                if (pos != cmt.npos) {
                    const std::string_view data_str = cmt.substr(pos + 1);
                    std::vector<std::string> values_str;
                    boost::split(values_str, data_str, boost::is_any_of("|,"), boost::token_compress_on);
                    for (const std::string& s : values_str) {
                        out.emplace_back(static_cast<float>(string_to_double_decimal_point(s)));
                    }
                    return true;
                }
            }
            return false;
        };
        
        begin = skip_whitespaces(begin, end);
        end   = remove_eols(begin, end);
        if (begin != end) {
            if (*begin == ';') {
                // Comment.
                begin = skip_whitespaces(++ begin, end);
                if (begin != end) {
                    std::string_view comment(begin, end - begin);
                    if (producer_detected) {
                        if (bed_size.x == 0.0 && comment.find("strokeXoverride") != comment.npos)
                            extract_double(comment, "strokeXoverride", bed_size.x);
                        else if (bed_size.y == 0.0 && comment.find("strokeYoverride") != comment.npos)
                            extract_double(comment, "strokeYoverride", bed_size.y);
                        else if (comment.find("filamentDiameters") != comment.npos) {
                            m_result.filament_diameters.clear();
                            extract_floats(comment, "filamentDiameters", m_result.filament_diameters);
                        } else if (comment.find("filamentDensities") != comment.npos) {
                            m_result.filament_densities.clear();
                            extract_floats(comment, "filamentDensities", m_result.filament_densities);
                        }
                        else if (comment.find("filamentPricesPerKg") != comment.npos) {
                            m_result.filament_cost.clear();
                            extract_floats(comment, "filamentPricesPerKg", m_result.filament_cost);
                        } else if (comment.find("extruderDiameter") != comment.npos) {
                            std::vector<float> extruder_diameters;
                            extract_floats(comment, "extruderDiameter", extruder_diameters);
                            m_result.extruders_count = extruder_diameters.size();
                        }
                    } else if (boost::starts_with(comment, "G-Code generated by Simplify3D(R)"))
                        producer_detected = true;
                }
            } else {
                // Some non-empty G-code line detected, stop parsing config comments.
                reader.quit_parsing();
            }
        }
    });

    if (m_result.extruders_count == 0)
        m_result.extruders_count = std::max<size_t>(1, std::min(m_result.filament_diameters.size(), 
            std::min(m_result.filament_densities.size(), m_result.filament_cost.size())));

    if (bed_size.is_defined()) {
        m_result.bed_shape = {
            { 0.0, 0.0 },
            { bed_size.x, 0.0 },
            { bed_size.x, bed_size.y },
            { 0.0, bed_size.y }
        };
    }
}

void GCodeProcessor::process_gcode_line(const GCodeReader::GCodeLine& line, bool producers_enabled)
{
/* std::cout << line.raw() << std::endl; */

    ++m_line_id;

    // update start position
    m_start_position = m_end_position;

    const std::string_view cmd = line.cmd();
    if (cmd.length() > 1) {
        // process command lines
        switch (cmd[0])
        {
        case 'g':
        case 'G':
            switch (cmd.size()) {
            case 2:
                switch (cmd[1]) {
                case '0': { process_G0(line); break; }  // Move
                case '1': { process_G1(line); break; }  // Move
                case '2': { process_G2_G3(line, true); break; }   // CW Arc Move
                case '3': { process_G2_G3(line, false); break; }  // CCW Arc Move
                default: break;
                }
                break;
            case 3:
                switch (cmd[1]) {
                case '1':
                    switch (cmd[2]) {
                    case '0': { process_G10(line); break; } // Retract or Set tool temperature
                    case '1': { process_G11(line); break; } // Unretract
                    default: break;
                    }
                    break;
                case '2':
                    switch (cmd[2]) {
                    case '0': { process_G20(line); break; } // Set Units to Inches
                    case '1': { process_G21(line); break; } // Set Units to Millimeters
                    case '2': { process_G22(line); break; } // Firmware controlled retract
                    case '3': { process_G23(line); break; } // Firmware controlled unretract
                    case '8': { process_G28(line); break; } // Move to origin
                    default: break;
                    }
                    break;
                case '6':
                    switch (cmd[2]) {
                    case '0': { process_G60(line); break; } // Save Current Position
                    case '1': { process_G61(line); break; } // Return to Saved Position
                    default: break;
                    }
                    break;
                case '9':
                    switch (cmd[2]) {
                    case '0': { process_G90(line); break; } // Set to Absolute Positioning
                    case '1': { process_G91(line); break; } // Set to Relative Positioning
                    case '2': { process_G92(line); break; } // Set Position
                    default: break;
                    }
                    break;
                }
                break;
            default:
                break;
            }
            break;
        case 'm':
        case 'M':
            switch (cmd.size()) {
            case 2:
                switch (cmd[1]) {
                case '1': { process_M1(line); break; }   // Sleep or Conditional stop
                default: break;
                }
                break;
            case 3:
                switch (cmd[1]) {
                case '8':
                    switch (cmd[2]) {
                    case '2': { process_M82(line); break; }  // Set extruder to absolute mode
                    case '3': { process_M83(line); break; }  // Set extruder to relative mode
                    default: break;
                    }
                    break;
                default:
                    break;
                }
                break;
            case 4:
                switch (cmd[1]) {
                case '1':
                    switch (cmd[2]) {
                    case '0':
                        switch (cmd[3]) {
                        case '4': { process_M104(line); break; } // Set extruder temperature
                        case '6': { process_M106(line); break; } // Set fan speed
                        case '7': { process_M107(line); break; } // Disable fan
                        case '8': { process_M108(line); break; } // Set tool (Sailfish)
                        case '9': { process_M109(line); break; } // Set extruder temperature and wait
                        default: break;
                        }
                        break;
                    case '3':
                        switch (cmd[3]) {
                        case '2': { process_M132(line); break; } // Recall stored home offsets
                        case '5': { process_M135(line); break; } // Set tool (MakerWare)
                        default: break;
                        }
                        break;
                    default:
                        break;
                    }
                    break;
                case '2':
                    switch (cmd[2]) {
                    case '0':
                        switch (cmd[3]) {
                        case '1': { process_M201(line); break; } // Set max printing acceleration
                        case '3': { process_M203(line); break; } // Set maximum feedrate
                        case '4': { process_M204(line); break; } // Set default acceleration
                        case '5': { process_M205(line); break; } // Advanced settings
                        default: break;
                        }
                        break;
                    case '2':
                        switch (cmd[3]) {
                        case '0': { process_M220(line); break; } // Set Feedrate Percentage
                        case '1': { process_M221(line); break; } // Set extrude factor override percentage
                        default: break;
                        }
                        break;
                    default:
                        break;
                    }
                    break;
                case '4':
                    switch (cmd[2]) {
                    case '0':
                        switch (cmd[3]) {
                        case '1': { process_M401(line); break; } // Repetier: Store x, y and z position
                        case '2': { process_M402(line); break; } // Repetier: Go to stored position
                        default: break;
                        }
                        break;
                    default:
                        break;
                    }
                    break;
                case '5':
                    switch (cmd[2]) {
                    case '6':
                        switch (cmd[3]) {
                        case '6': { process_M566(line); break; } // Set allowable instantaneous speed change
                        default: break;
                        }
                        break;
                    default:
                        break;
                    }
                    break;
                case '7':
                    switch (cmd[2]) {
                    case '0':
                        switch (cmd[3]) {
                        case '2': { process_M702(line); break; } // Unload the current filament into the MK3 MMU2 unit at the end of print.
                        default: break;
                        }
                        break;
                    default:
                        break;
                    }
                    break;
                default:
                    break;
                }
                break;
            default:
                break;
            }
            break;
        case 't':
        case 'T':
            process_T(line); // Select Tool
            break;
        default:
            break;
        }
    }
    else {
        const std::string &comment = line.raw();
        if (comment.length() > 2 && comment.front() == ';')
            // Process tags embedded into comments. Tag comments always start at the start of a line
            // with a comment and continue with a tag without any whitespace separator.
            process_tags(comment.substr(1), producers_enabled);
    }
}

#if __has_include(<charconv>)
    template <typename T, typename = void>
    struct is_from_chars_convertible : std::false_type {};
    template <typename T>
    struct is_from_chars_convertible<T, std::void_t<decltype(std::from_chars(std::declval<const char*>(), std::declval<const char*>(), std::declval<T&>()))>> : std::true_type {};
#endif

// Returns true if the number was parsed correctly into out and the number spanned the whole input string.
template<typename T>
[[nodiscard]] static inline bool parse_number(const std::string_view sv, T &out)
{
    // https://www.bfilipek.com/2019/07/detect-overload-from-chars.html#example-stdfromchars
#if __has_include(<charconv>)
    // Visual Studio 19 supports from_chars all right.
    // OSX compiler that we use only implements std::from_chars just for ints.
    // GCC that we compile on does not provide <charconv> at all.
    if constexpr (is_from_chars_convertible<T>::value) {
        auto str_end = sv.data() + sv.size();
        auto [end_ptr, error_code] = std::from_chars(sv.data(), str_end, out);
        return error_code == std::errc() && end_ptr == str_end;
    } 
    else
#endif
    {
        // Legacy conversion, which is costly due to having to make a copy of the string before conversion.
        try {
            assert(sv.size() < 1024);
	    assert(sv.data() != nullptr);
            std::string str { sv };
            size_t read = 0;
            if constexpr (std::is_same_v<T, int>)
                out = std::stoi(str, &read);
            else if constexpr (std::is_same_v<T, long>)
                out = std::stol(str, &read);
            else if constexpr (std::is_same_v<T, float>)
                out = string_to_double_decimal_point(str, &read);
            else if constexpr (std::is_same_v<T, double>)
                out = string_to_double_decimal_point(str, &read);
            return str.size() == read;
        } catch (...) {
            return false;
        }
    }
}

void GCodeProcessor::process_tags(const std::string_view comment, bool producers_enabled)
{
    // producers tags
    if (producers_enabled && process_producers_tags(comment))
        return;

    // extrusion role tag
    if (boost::starts_with(comment, reserved_tag(ETags::Role))) {
        set_extrusion_role(string_to_gcode_extrusion_role(comment.substr(reserved_tag(ETags::Role).length())));
        if (m_extrusion_role == GCodeExtrusionRole::ExternalPerimeter)
            m_seams_detector.activate(true);
        return;
    }

    // wipe start tag
    if (boost::starts_with(comment, reserved_tag(ETags::Wipe_Start))) {
        m_wiping = true;
        return;
    }

    // wipe end tag
    if (boost::starts_with(comment, reserved_tag(ETags::Wipe_End))) {
        m_wiping = false;
        return;
    }

    if (!producers_enabled || m_producer == EProducer::PrusaSlicer) {
        // height tag
        if (boost::starts_with(comment, reserved_tag(ETags::Height))) {
            if (!parse_number(comment.substr(reserved_tag(ETags::Height).size()), m_forced_height))
                BOOST_LOG_TRIVIAL(error) << "GCodeProcessor encountered an invalid value for Height (" << comment << ").";
            return;
        }
        // width tag
        if (boost::starts_with(comment, reserved_tag(ETags::Width))) {
            if (!parse_number(comment.substr(reserved_tag(ETags::Width).size()), m_forced_width))
                BOOST_LOG_TRIVIAL(error) << "GCodeProcessor encountered an invalid value for Width (" << comment << ").";
            return;
        }
    }

    // color change tag
    if (boost::starts_with(comment, reserved_tag(ETags::Color_Change))) {
        unsigned char extruder_id = 0;
        static std::vector<std::string> Default_Colors = {
            "#0B2C7A", // { 0.043f, 0.173f, 0.478f }, // bluish
            "#1C8891", // { 0.110f, 0.533f, 0.569f },
            "#AAF200", // { 0.667f, 0.949f, 0.000f },
            "#F5CE0A", // { 0.961f, 0.808f, 0.039f },
            "#D16830", // { 0.820f, 0.408f, 0.188f },
            "#942616", // { 0.581f, 0.149f, 0.087f }  // reddish
        };

        std::string color = Default_Colors[0];
        auto is_valid_color = [](const std::string& color) {
            auto is_hex_digit = [](char c) {
                return ((c >= '0' && c <= '9') ||
                        (c >= 'A' && c <= 'F') ||
                        (c >= 'a' && c <= 'f'));
            };

            if (color[0] != '#' || color.length() != 7)
                return false;
            for (int i = 1; i <= 6; ++i) {
                if (!is_hex_digit(color[i]))
                    return false;
            }
            return true;
        };

        std::vector<std::string> tokens;
        boost::split(tokens, comment, boost::is_any_of(","), boost::token_compress_on);
        if (tokens.size() > 1) {
            if (tokens[1][0] == 'T') {
                int eid;
                if (!parse_number(tokens[1].substr(1), eid) || eid < 0 || eid > 255) {
                    BOOST_LOG_TRIVIAL(error) << "GCodeProcessor encountered an invalid value for Color_Change (" << comment << ").";
                    return;
                }
                extruder_id = static_cast<unsigned char>(eid);
            }
        }
        if (tokens.size() > 2) {
            if (is_valid_color(tokens[2]))
                color = tokens[2];
        }
        else {
            color = Default_Colors[m_last_default_color_id];
            ++m_last_default_color_id;
            if (m_last_default_color_id == Default_Colors.size())
                m_last_default_color_id = 0;
        }

        if (extruder_id < m_extruder_colors.size())
            m_extruder_colors[extruder_id] = static_cast<unsigned char>(m_extruder_offsets.size()) + m_cp_color.counter; // color_change position in list of color for preview
        ++m_cp_color.counter;
        if (m_cp_color.counter == UCHAR_MAX)
            m_cp_color.counter = 0;

        if (m_extruder_id == extruder_id) {
            m_cp_color.current = m_extruder_colors[extruder_id];
            store_move_vertex(EMoveType::Color_change);
            CustomGCode::Item item = { static_cast<double>(m_end_position[2]), CustomGCode::ColorChange, extruder_id + 1, color, "" };
            m_result.custom_gcode_per_print_z.emplace_back(item);
            m_options_z_corrector.set();
            process_custom_gcode_time(CustomGCode::ColorChange);
            process_filaments(CustomGCode::ColorChange);
        }

        return;
    }

    // pause print tag
    if (comment == reserved_tag(ETags::Pause_Print)) {
        store_move_vertex(EMoveType::Pause_Print);
        CustomGCode::Item item = { static_cast<double>(m_end_position[2]), CustomGCode::PausePrint, m_extruder_id + 1, "", "" };
        m_result.custom_gcode_per_print_z.emplace_back(item);
        m_options_z_corrector.set();
        process_custom_gcode_time(CustomGCode::PausePrint);
        return;
    }

    // custom code tag
    if (comment == reserved_tag(ETags::Custom_Code)) {
        store_move_vertex(EMoveType::Custom_GCode);
        CustomGCode::Item item = { static_cast<double>(m_end_position[2]), CustomGCode::Custom, m_extruder_id + 1, "", "" };
        m_result.custom_gcode_per_print_z.emplace_back(item);
        m_options_z_corrector.set();
        return;
    }

    // layer change tag
    if (comment == reserved_tag(ETags::Layer_Change)) {
        ++m_layer_id;
        return;
    }
}

bool GCodeProcessor::process_producers_tags(const std::string_view comment)
{
    switch (m_producer)
    {
    case EProducer::Slic3rPE:
    case EProducer::Slic3r: 
    case EProducer::SuperSlicer:
    case EProducer::PrusaSlicer: { return process_prusaslicer_tags(comment); }
    case EProducer::Cura:        { return process_cura_tags(comment); }
    case EProducer::Simplify3D:  { return process_simplify3d_tags(comment); }
    case EProducer::CraftWare:   { return process_craftware_tags(comment); }
    case EProducer::ideaMaker:   { return process_ideamaker_tags(comment); }
    case EProducer::KissSlicer:  { return process_kissslicer_tags(comment); }
    case EProducer::BambuStudio: { return process_bambustudio_tags(comment); }
    default:                     { return false; }
    }
}

bool GCodeProcessor::process_prusaslicer_tags(const std::string_view comment)
{
    return false;
}

bool GCodeProcessor::process_cura_tags(const std::string_view comment)
{
    // TYPE -> extrusion role
    std::string tag = "TYPE:";
    size_t pos = comment.find(tag);
    if (pos != comment.npos) {
        const std::string_view type = comment.substr(pos + tag.length());
        if (type == "SKIRT")
            set_extrusion_role(GCodeExtrusionRole::Skirt);
        else if (type == "WALL-OUTER")
            set_extrusion_role(GCodeExtrusionRole::ExternalPerimeter);
        else if (type == "WALL-INNER")
            set_extrusion_role(GCodeExtrusionRole::Perimeter);
        else if (type == "SKIN")
            set_extrusion_role(GCodeExtrusionRole::SolidInfill);
        else if (type == "FILL")
            set_extrusion_role(GCodeExtrusionRole::InternalInfill);
        else if (type == "SUPPORT")
            set_extrusion_role(GCodeExtrusionRole::SupportMaterial);
        else if (type == "SUPPORT-INTERFACE")
            set_extrusion_role(GCodeExtrusionRole::SupportMaterialInterface);
        else if (type == "PRIME-TOWER")
            set_extrusion_role(GCodeExtrusionRole::WipeTower);
        else {
            set_extrusion_role(GCodeExtrusionRole::None);
            BOOST_LOG_TRIVIAL(warning) << "GCodeProcessor found unknown extrusion role: " << type;
        }

        if (m_extrusion_role == GCodeExtrusionRole::ExternalPerimeter)
            m_seams_detector.activate(true);

        return true;
    }

    // flavor
    tag = "FLAVOR:";
    pos = comment.find(tag);
    if (pos != comment.npos) {
        const std::string_view flavor = comment.substr(pos + tag.length());
        if (flavor == "BFB")
            m_flavor = gcfMarlinLegacy; // is this correct ?
        else if (flavor == "Mach3")
            m_flavor = gcfMach3;
        else if (flavor == "Makerbot")
            m_flavor = gcfMakerWare;
        else if (flavor == "UltiGCode")
            m_flavor = gcfMarlinLegacy; // is this correct ?
        else if (flavor == "Marlin(Volumetric)")
            m_flavor = gcfMarlinLegacy; // is this correct ?
        else if (flavor == "Griffin")
            m_flavor = gcfMarlinLegacy; // is this correct ?
        else if (flavor == "Repetier")
            m_flavor = gcfRepetier;
        else if (flavor == "RepRap")
            m_flavor = gcfRepRapFirmware;
        else if (flavor == "Marlin")
            m_flavor = gcfMarlinLegacy;
        else
            BOOST_LOG_TRIVIAL(warning) << "GCodeProcessor found unknown flavor: " << flavor;

        return true;
    }

    // layer
    tag = "LAYER:";
    pos = comment.find(tag);
    if (pos != comment.npos) {
        ++m_layer_id;
        return true;
    }

    return false;
}

bool GCodeProcessor::process_simplify3d_tags(const std::string_view comment)
{
    // extrusion roles

    // in older versions the comments did not contain the key 'feature'
    std::string_view cmt = comment;
    size_t pos = cmt.find(" feature");
    if (pos == 0)
        cmt.remove_prefix(8);

    // ; skirt
    pos = cmt.find(" skirt");
    if (pos == 0) {
        set_extrusion_role(GCodeExtrusionRole::Skirt);
        return true;
    }
    
    // ; outer perimeter
    pos = cmt.find(" outer perimeter");
    if (pos == 0) {
        set_extrusion_role(GCodeExtrusionRole::ExternalPerimeter);
        m_seams_detector.activate(true);
        return true;
    }

    // ; inner perimeter
    pos = cmt.find(" inner perimeter");
    if (pos == 0) {
        set_extrusion_role(GCodeExtrusionRole::Perimeter);
        return true;
    }

    // ; gap fill
    pos = cmt.find(" gap fill");
    if (pos == 0) {
        set_extrusion_role(GCodeExtrusionRole::GapFill);
        return true;
    }

    // ; infill
    pos = cmt.find(" infill");
    if (pos == 0) {
        set_extrusion_role(GCodeExtrusionRole::InternalInfill);
        return true;
    }

    // ; solid layer
    pos = cmt.find(" solid layer");
    if (pos == 0) {
        set_extrusion_role(GCodeExtrusionRole::SolidInfill);
        return true;
    }

    // ; bridge
    pos = cmt.find(" bridge");
    if (pos == 0) {
        set_extrusion_role(GCodeExtrusionRole::BridgeInfill);
        return true;
    }

    // ; support
    pos = cmt.find(" support");
    if (pos == 0) {
        set_extrusion_role(GCodeExtrusionRole::SupportMaterial);
        return true;
    }

    // ; dense support
    pos = cmt.find(" dense support");
    if (pos == 0) {
        set_extrusion_role(GCodeExtrusionRole::SupportMaterialInterface);
        return true;
    }

    // ; prime pillar
    pos = cmt.find(" prime pillar");
    if (pos == 0) {
        set_extrusion_role(GCodeExtrusionRole::WipeTower);
        return true;
    }

    // ; ooze shield
    pos = cmt.find(" ooze shield");
    if (pos == 0) {
        set_extrusion_role(GCodeExtrusionRole::None); // Missing mapping
        return true;
    }

    // ; raft
    pos = cmt.find(" raft");
    if (pos == 0) {
        set_extrusion_role(GCodeExtrusionRole::SupportMaterial);
        return true;
    }

    // ; internal single extrusion
    pos = cmt.find(" internal single extrusion");
    if (pos == 0) {
        set_extrusion_role(GCodeExtrusionRole::None); // Missing mapping
        return true;
    }

    // geometry
    // ; tool
    std::string tag = " tool";
    pos = cmt.find(tag);
    if (pos == 0) {
        const std::string_view data = cmt.substr(pos + tag.length());
        std::string h_tag = "H";
        size_t h_start = data.find(h_tag);
        size_t h_end = data.find_first_of(' ', h_start);
        std::string w_tag = "W";
        size_t w_start = data.find(w_tag);
        size_t w_end = data.find_first_of(' ', w_start);
        if (h_start != data.npos) {
            if (!parse_number(data.substr(h_start + 1, (h_end != data.npos) ? h_end - h_start - 1 : h_end), m_forced_height))
                BOOST_LOG_TRIVIAL(error) << "GCodeProcessor encountered an invalid value for Height (" << comment << ").";
        }
        if (w_start != data.npos) {
            if (!parse_number(data.substr(w_start + 1, (w_end != data.npos) ? w_end - w_start - 1 : w_end), m_forced_width))
                BOOST_LOG_TRIVIAL(error) << "GCodeProcessor encountered an invalid value for Width (" << comment << ").";
        }

        return true;
    }

    // ; layer | ;layer
    tag = "layer";
    pos = cmt.find(tag);
    if (pos == 0 || pos == 1) {
        // skip lines "; layer end"
        const std::string_view data = cmt.substr(pos + tag.length());
        size_t end_start = data.find("end");
        if (end_start == data.npos)
            ++m_layer_id;

        return true;
    }

    return false;
}

bool GCodeProcessor::process_craftware_tags(const std::string_view comment)
{
    // segType -> extrusion role
    std::string tag = "segType:";
    size_t pos = comment.find(tag);
    if (pos != comment.npos) {
        const std::string_view type = comment.substr(pos + tag.length());
        if (type == "Skirt")
            set_extrusion_role(GCodeExtrusionRole::Skirt);
        else if (type == "Perimeter")
            set_extrusion_role(GCodeExtrusionRole::ExternalPerimeter);
        else if (type == "HShell")
            set_extrusion_role(GCodeExtrusionRole::None); // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
        else if (type == "InnerHair")
            set_extrusion_role(GCodeExtrusionRole::None); // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
        else if (type == "Loop")
            set_extrusion_role(GCodeExtrusionRole::None); // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
        else if (type == "Infill")
            set_extrusion_role(GCodeExtrusionRole::InternalInfill);
        else if (type == "Raft")
            set_extrusion_role(GCodeExtrusionRole::Skirt);
        else if (type == "Support")
            set_extrusion_role(GCodeExtrusionRole::SupportMaterial);
        else if (type == "SupportTouch")
            set_extrusion_role(GCodeExtrusionRole::SupportMaterial);
        else if (type == "SoftSupport")
            set_extrusion_role(GCodeExtrusionRole::SupportMaterialInterface);
        else if (type == "Pillar")
            set_extrusion_role(GCodeExtrusionRole::WipeTower);
        else {
            set_extrusion_role(GCodeExtrusionRole::None);
            BOOST_LOG_TRIVIAL(warning) << "GCodeProcessor found unknown extrusion role: " << type;
        }

        if (m_extrusion_role == GCodeExtrusionRole::ExternalPerimeter)
            m_seams_detector.activate(true);

        return true;
    }

    // layer
    pos = comment.find(" Layer #");
    if (pos == 0) {
        ++m_layer_id;
        return true;
    }

    return false;
}

bool GCodeProcessor::process_ideamaker_tags(const std::string_view comment)
{
    // TYPE -> extrusion role
    std::string tag = "TYPE:";
    size_t pos = comment.find(tag);
    if (pos != comment.npos) {
        const std::string_view type = comment.substr(pos + tag.length());
        if (type == "RAFT")
            set_extrusion_role(GCodeExtrusionRole::Skirt);
        else if (type == "WALL-OUTER")
            set_extrusion_role(GCodeExtrusionRole::ExternalPerimeter);
        else if (type == "WALL-INNER")
            set_extrusion_role(GCodeExtrusionRole::Perimeter);
        else if (type == "SOLID-FILL")
            set_extrusion_role(GCodeExtrusionRole::SolidInfill);
        else if (type == "FILL")
            set_extrusion_role(GCodeExtrusionRole::InternalInfill);
        else if (type == "BRIDGE")
            set_extrusion_role(GCodeExtrusionRole::BridgeInfill);
        else if (type == "SUPPORT")
            set_extrusion_role(GCodeExtrusionRole::SupportMaterial);
        else {
            set_extrusion_role(GCodeExtrusionRole::None);
            BOOST_LOG_TRIVIAL(warning) << "GCodeProcessor found unknown extrusion role: " << type;
        }

        if (m_extrusion_role == GCodeExtrusionRole::ExternalPerimeter)
            m_seams_detector.activate(true);

        return true;
    }

    // geometry
    // width
    tag = "WIDTH:";
    pos = comment.find(tag);
    if (pos != comment.npos) {
        if (!parse_number(comment.substr(pos + tag.length()), m_forced_width))
            BOOST_LOG_TRIVIAL(error) << "GCodeProcessor encountered an invalid value for Width (" << comment << ").";
        return true;
    }

    // height
    tag = "HEIGHT:";
    pos = comment.find(tag);
    if (pos != comment.npos) {
        if (!parse_number(comment.substr(pos + tag.length()), m_forced_height))
            BOOST_LOG_TRIVIAL(error) << "GCodeProcessor encountered an invalid value for Height (" << comment << ").";
        return true;
    }

    // layer
    pos = comment.find("LAYER:");
    if (pos == 0) {
        ++m_layer_id;
        return true;
    }

    return false;
}

bool GCodeProcessor::process_kissslicer_tags(const std::string_view comment)
{
    // extrusion roles

    // ; 'Raft Path'
    size_t pos = comment.find(" 'Raft Path'");
    if (pos == 0) {
        set_extrusion_role(GCodeExtrusionRole::Skirt);
        return true;
    }

    // ; 'Support Interface Path'
    pos = comment.find(" 'Support Interface Path'");
    if (pos == 0) {
        set_extrusion_role(GCodeExtrusionRole::SupportMaterialInterface);
        return true;
    }

    // ; 'Travel/Ironing Path'
    pos = comment.find(" 'Travel/Ironing Path'");
    if (pos == 0) {
        set_extrusion_role(GCodeExtrusionRole::Ironing);
        return true;
    }

    // ; 'Support (may Stack) Path'
    pos = comment.find(" 'Support (may Stack) Path'");
    if (pos == 0) {
        set_extrusion_role(GCodeExtrusionRole::SupportMaterial);
        return true;
    }

    // ; 'Perimeter Path'
    pos = comment.find(" 'Perimeter Path'");
    if (pos == 0) {
        set_extrusion_role(GCodeExtrusionRole::ExternalPerimeter);
        m_seams_detector.activate(true);
        return true;
    }

    // ; 'Pillar Path'
    pos = comment.find(" 'Pillar Path'");
    if (pos == 0) {
        set_extrusion_role(GCodeExtrusionRole::None); // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
        return true;
    }

    // ; 'Destring/Wipe/Jump Path'
    pos = comment.find(" 'Destring/Wipe/Jump Path'");
    if (pos == 0) {
        set_extrusion_role(GCodeExtrusionRole::None); // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
        return true;
    }

    // ; 'Prime Pillar Path'
    pos = comment.find(" 'Prime Pillar Path'");
    if (pos == 0) {
        set_extrusion_role(GCodeExtrusionRole::None); // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
        return true;
    }

    // ; 'Loop Path'
    pos = comment.find(" 'Loop Path'");
    if (pos == 0) {
        set_extrusion_role(GCodeExtrusionRole::None); // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
        return true;
    }

    // ; 'Crown Path'
    pos = comment.find(" 'Crown Path'");
    if (pos == 0) {
        set_extrusion_role(GCodeExtrusionRole::None); // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
        return true;
    }

    // ; 'Solid Path'
    pos = comment.find(" 'Solid Path'");
    if (pos == 0) {
        set_extrusion_role(GCodeExtrusionRole::None);
        return true;
    }

    // ; 'Stacked Sparse Infill Path'
    pos = comment.find(" 'Stacked Sparse Infill Path'");
    if (pos == 0) {
        set_extrusion_role(GCodeExtrusionRole::InternalInfill);
        return true;
    }

    // ; 'Sparse Infill Path'
    pos = comment.find(" 'Sparse Infill Path'");
    if (pos == 0) {
        set_extrusion_role(GCodeExtrusionRole::SolidInfill);
        return true;
    }

    // geometry

    // layer
    pos = comment.find(" BEGIN_LAYER_");
    if (pos == 0) {
        ++m_layer_id;
        return true;
    }

    return false;
}

bool GCodeProcessor::process_bambustudio_tags(const std::string_view comment)
{
    // extrusion roles

    std::string tag = "FEATURE: ";
    size_t pos = comment.find(tag);
    if (pos != comment.npos) {
        const std::string_view type = comment.substr(pos + tag.length());
        if (type == "Custom")
            set_extrusion_role(GCodeExtrusionRole::Custom);
        else if (type == "Inner wall")
            set_extrusion_role(GCodeExtrusionRole::Perimeter);
        else if (type == "Outer wall")
            set_extrusion_role(GCodeExtrusionRole::ExternalPerimeter);
        else if (type == "Overhang wall")
            set_extrusion_role(GCodeExtrusionRole::OverhangPerimeter);
        else if (type == "Gap infill")
            set_extrusion_role(GCodeExtrusionRole::GapFill);
        else if (type == "Bridge")
            set_extrusion_role(GCodeExtrusionRole::BridgeInfill);
        else if (type == "Sparse infill")
            set_extrusion_role(GCodeExtrusionRole::InternalInfill);
        else if (type == "Internal solid infill")
            set_extrusion_role(GCodeExtrusionRole::SolidInfill);
        else if (type == "Top surface")
            set_extrusion_role(GCodeExtrusionRole::TopSolidInfill);
        else if (type == "Bottom surface")
            set_extrusion_role(GCodeExtrusionRole::None);
        else if (type == "Ironing")
            set_extrusion_role(GCodeExtrusionRole::Ironing);
        else if (type == "Skirt")
            set_extrusion_role(GCodeExtrusionRole::Skirt);
        else if (type == "Brim")
            set_extrusion_role(GCodeExtrusionRole::Skirt);
        else if (type == "Support")
            set_extrusion_role(GCodeExtrusionRole::SupportMaterial);
        else if (type == "Support interface")
            set_extrusion_role(GCodeExtrusionRole::SupportMaterialInterface);
        else if (type == "Support transition")
            set_extrusion_role(GCodeExtrusionRole::None);
        else if (type == "Prime tower")
            set_extrusion_role(GCodeExtrusionRole::WipeTower);
        else {
            set_extrusion_role(GCodeExtrusionRole::None);
            BOOST_LOG_TRIVIAL(warning) << "GCodeProcessor found unknown extrusion role: " << type;
        }

        if (m_extrusion_role == GCodeExtrusionRole::ExternalPerimeter)
            m_seams_detector.activate(true);

        return true;
    }

    return false;
}

bool GCodeProcessor::detect_producer(const std::string_view comment)
{
    for (const auto& [id, search_string] : Producers) {
        const size_t pos = comment.find(search_string);
        if (pos != comment.npos) {
            m_producer = id;
            BOOST_LOG_TRIVIAL(info) << "Detected gcode producer: " << search_string;
            return true;
        }
    }
    return false;
}

void GCodeProcessor::process_G0(const GCodeReader::GCodeLine& line)
{
    process_G1(line);
}

void GCodeProcessor::process_G1(const GCodeReader::GCodeLine& line)
{
    std::array<std::optional<double>, 4> g1_axes = { std::nullopt, std::nullopt, std::nullopt, std::nullopt };
    if (line.has_x()) g1_axes[X] = (double)line.x();
    if (line.has_y()) g1_axes[Y] = (double)line.y();
    if (line.has_z()) g1_axes[Z] = (double)line.z();
    if (line.has_e()) g1_axes[E] = (double)line.e();
    std::optional<double> g1_feedrate = std::nullopt;
    if (line.has_f()) g1_feedrate = (double)line.f();
    process_G1(g1_axes, g1_feedrate);
}

void GCodeProcessor::process_G1(const std::array<std::optional<double>, 4>& axes, const std::optional<double>& feedrate,
    G1DiscretizationOrigin origin, const std::optional<unsigned int>& remaining_internal_g1_lines)
{
    const float filament_diameter = (static_cast<size_t>(m_extruder_id) < m_result.filament_diameters.size()) ? m_result.filament_diameters[m_extruder_id] : m_result.filament_diameters.back();
    const float filament_radius = 0.5f * filament_diameter;
    const float area_filament_cross_section = static_cast<float>(M_PI) * sqr(filament_radius);

    auto move_type = [this](const AxisCoords& delta_pos) {
        if (m_wiping)
            return EMoveType::Wipe;
        else if (delta_pos[E] < 0.0f)
            return (delta_pos[X] != 0.0f || delta_pos[Y] != 0.0f || delta_pos[Z] != 0.0f) ? EMoveType::Travel : EMoveType::Retract;
        else if (delta_pos[E] > 0.0f) {
            if (delta_pos[X] == 0.0f && delta_pos[Y] == 0.0f)
                return (delta_pos[Z] == 0.0f) ? EMoveType::Unretract : EMoveType::Travel;
            else if (delta_pos[X] != 0.0f || delta_pos[Y] != 0.0f)
                return EMoveType::Extrude;
        }
        else if (delta_pos[X] != 0.0f || delta_pos[Y] != 0.0f || delta_pos[Z] != 0.0f)
            return EMoveType::Travel;

        return EMoveType::Noop;
    };

    auto extract_absolute_position_on_axis = [&](Axis axis, std::optional<double> value, double area_filament_cross_section)
    {
        if (value.has_value()) {
            bool is_relative = (m_global_positioning_type == EPositioningType::Relative);
            if (axis == E)
                is_relative |= (m_e_local_positioning_type == EPositioningType::Relative);

            const double lengthsScaleFactor = (m_units == EUnits::Inches) ? double(INCHES_TO_MM) : 1.0;
            double ret = *value * lengthsScaleFactor;
            if (axis == E && m_use_volumetric_e)
                ret /= area_filament_cross_section;
            return is_relative ? m_start_position[axis] + ret : m_origin[axis] + ret;
        }
        else
            return m_start_position[axis];
    };

    ++m_g1_line_id;

    // enable processing of lines M201/M203/M204/M205
    m_time_processor.machine_envelope_processing_enabled = true;

    // updates axes positions from line
    for (unsigned char a = X; a <= E; ++a) {
        m_end_position[a] = extract_absolute_position_on_axis((Axis)a, axes[a], double(area_filament_cross_section));
    }

    // updates feedrate from line, if present
    if (feedrate.has_value())
        m_feedrate = m_feed_multiply.current * (*feedrate) * MMMIN_TO_MMSEC;

    // calculates movement deltas
    AxisCoords delta_pos;
    for (unsigned char a = X; a <= E; ++a)
        delta_pos[a] = m_end_position[a] - m_start_position[a];

    if (std::all_of(delta_pos.begin(), delta_pos.end(), [](double d) { return d == 0.; }))
        return;

    const float volume_extruded_filament = area_filament_cross_section * delta_pos[E];

    if (volume_extruded_filament != 0.)
        m_used_filaments.increase_caches(volume_extruded_filament, m_extruder_id, area_filament_cross_section * m_parking_position,
                                         area_filament_cross_section * m_extra_loading_move);

    const EMoveType type = move_type(delta_pos);
    if (type == EMoveType::Extrude) {
        const float delta_xyz = std::sqrt(sqr(delta_pos[X]) + sqr(delta_pos[Y]) + sqr(delta_pos[Z]));
        const float area_toolpath_cross_section = volume_extruded_filament / delta_xyz;

        // volume extruded filament / tool displacement = area toolpath cross section
        m_mm3_per_mm = area_toolpath_cross_section;

        if (m_forced_height > 0.0f)
            // use height coming from the gcode tags
            m_height = m_forced_height;
        else if (m_layer_id == 0) { // first layer
            if (m_end_position[Z] > 0.0f)
                // use the current (clamped) z, if greater than zero  
                m_height = std::min<float>(m_end_position[Z], 2.0f);
            else
                // use the first layer height  
                m_height = m_first_layer_height + m_z_offset;
        }
        else if (origin == G1DiscretizationOrigin::G1) {
            if (m_end_position[Z] > m_extruded_last_z + EPSILON && delta_pos[Z] == 0.0)
                m_height = m_end_position[Z] - m_extruded_last_z;
        }

        if (m_height == 0.0f)
            m_height = DEFAULT_TOOLPATH_HEIGHT;

        if (origin == G1DiscretizationOrigin::G1)
            m_extruded_last_z = m_end_position[Z];
        m_options_z_corrector.update(m_height);

        if (m_forced_width > 0.0f)
            // use width coming from the gcode tags
            m_width = m_forced_width;
        else if (m_extrusion_role == GCodeExtrusionRole::ExternalPerimeter)
            // cross section: rectangle
            m_width = delta_pos[E] * static_cast<float>(M_PI * sqr(1.05f * filament_radius)) / (delta_xyz * m_height);
        else if (m_extrusion_role == GCodeExtrusionRole::BridgeInfill || m_extrusion_role == GCodeExtrusionRole::None)
            // cross section: circle
            m_width = static_cast<float>(m_result.filament_diameters[m_extruder_id]) * std::sqrt(delta_pos[E] / delta_xyz);
        else
            // cross section: rectangle + 2 semicircles
            m_width = delta_pos[E] * static_cast<float>(M_PI * sqr(filament_radius)) / (delta_xyz * m_height) + static_cast<float>(1.0 - 0.25 * M_PI) * m_height;

        if (m_width == 0.0f)
            m_width = DEFAULT_TOOLPATH_WIDTH;

        // clamp width to avoid artifacts which may arise from wrong values of m_height
        m_width = std::min(m_width, std::max(2.0f, 4.0f * m_height));
    }

    // time estimate section
    auto move_length = [](const AxisCoords& delta_pos) {
        const float sq_xyz_length = sqr(delta_pos[X]) + sqr(delta_pos[Y]) + sqr(delta_pos[Z]);
        return (sq_xyz_length > 0.0f) ? std::sqrt(sq_xyz_length) : std::abs(delta_pos[E]);
    };

    auto is_extrusion_only_move = [](const AxisCoords& delta_pos) {
        return delta_pos[X] == 0.0f && delta_pos[Y] == 0.0f && delta_pos[Z] == 0.0f && delta_pos[E] != 0.0f;
    };

    const float distance = move_length(delta_pos);
    assert(distance != 0.0f);
    const float inv_distance = 1.0f / distance;

    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        TimeMachine& machine = m_time_processor.machines[i];
        if (!machine.enabled)
            continue;

        TimeMachine::State& curr = machine.curr;
        TimeMachine::State& prev = machine.prev;
        std::vector<TimeBlock>& blocks = machine.blocks;

        curr.feedrate = (delta_pos[E] == 0.0f) ? minimum_travel_feedrate(static_cast<PrintEstimatedStatistics::ETimeMode>(i), m_feedrate) :
            minimum_feedrate(static_cast<PrintEstimatedStatistics::ETimeMode>(i), m_feedrate);

        TimeBlock block;
        block.move_type = type;
        block.role = m_extrusion_role;
        block.distance = distance;
        block.g1_line_id = m_g1_line_id;
        block.move_id = static_cast<unsigned int>(m_result.moves.size());
        block.remaining_internal_g1_lines = remaining_internal_g1_lines.has_value() ? *remaining_internal_g1_lines : 0;
        block.layer_id = std::max<unsigned int>(1, m_layer_id);

        // calculates block cruise feedrate
        float min_feedrate_factor = 1.0f;
        for (unsigned char a = X; a <= E; ++a) {
            curr.axis_feedrate[a] = curr.feedrate * delta_pos[a] * inv_distance;
            if (a == E)
                curr.axis_feedrate[a] *= machine.extrude_factor_override_percentage;

            curr.abs_axis_feedrate[a] = std::abs(curr.axis_feedrate[a]);
            if (curr.abs_axis_feedrate[a] != 0.0f) {
                const float axis_max_feedrate = get_axis_max_feedrate(static_cast<PrintEstimatedStatistics::ETimeMode>(i), static_cast<Axis>(a));
                if (axis_max_feedrate != 0.0f)
                    min_feedrate_factor = std::min<float>(min_feedrate_factor, axis_max_feedrate / curr.abs_axis_feedrate[a]);
            }
        }

        block.feedrate_profile.cruise = min_feedrate_factor * curr.feedrate;

        if (min_feedrate_factor < 1.0f) {
            for (unsigned char a = X; a <= E; ++a) {
                curr.axis_feedrate[a] *= min_feedrate_factor;
                curr.abs_axis_feedrate[a] *= min_feedrate_factor;
            }
        }

        // calculates block acceleration
        float acceleration = (type == EMoveType::Travel) ? get_travel_acceleration(static_cast<PrintEstimatedStatistics::ETimeMode>(i)) :
            (is_extrusion_only_move(delta_pos) ? get_retract_acceleration(static_cast<PrintEstimatedStatistics::ETimeMode>(i)) :
            get_acceleration(static_cast<PrintEstimatedStatistics::ETimeMode>(i)));

        for (unsigned char a = X; a <= E; ++a) {
            const float axis_max_acceleration = get_axis_max_acceleration(static_cast<PrintEstimatedStatistics::ETimeMode>(i), static_cast<Axis>(a));
            const float scale = std::abs(delta_pos[a]) * inv_distance;
            if (acceleration * scale > axis_max_acceleration)
                acceleration = axis_max_acceleration / scale;
        }

        block.acceleration = acceleration;

        // calculates block exit feedrate
        curr.safe_feedrate = block.feedrate_profile.cruise;

        for (unsigned char a = X; a <= E; ++a) {
            const float axis_max_jerk = get_axis_max_jerk(static_cast<PrintEstimatedStatistics::ETimeMode>(i), static_cast<Axis>(a));
            if (curr.abs_axis_feedrate[a] > axis_max_jerk)
                curr.safe_feedrate = std::min(curr.safe_feedrate, axis_max_jerk);
        }

        block.feedrate_profile.exit = curr.safe_feedrate;

        static const float PREVIOUS_FEEDRATE_THRESHOLD = 0.0001f;

        // calculates block entry feedrate
        float vmax_junction = curr.safe_feedrate;
        if (!blocks.empty() && prev.feedrate > PREVIOUS_FEEDRATE_THRESHOLD) {
            const bool prev_speed_larger = prev.feedrate > block.feedrate_profile.cruise;
            const float smaller_speed_factor = prev_speed_larger ? (block.feedrate_profile.cruise / prev.feedrate) : (prev.feedrate / block.feedrate_profile.cruise);
            // Pick the smaller of the nominal speeds. Higher speed shall not be achieved at the junction during coasting.
            vmax_junction = prev_speed_larger ? block.feedrate_profile.cruise : prev.feedrate;

            float v_factor = 1.0f;
            bool limited = false;

            for (unsigned char a = X; a <= E; ++a) {
                // Limit an axis. We have to differentiate coasting from the reversal of an axis movement, or a full stop.
                float v_exit = prev.axis_feedrate[a];
                float v_entry = curr.axis_feedrate[a];

                if (prev_speed_larger)
                    v_exit *= smaller_speed_factor;

                if (limited) {
                    v_exit *= v_factor;
                    v_entry *= v_factor;
                }

                // Calculate the jerk depending on whether the axis is coasting in the same direction or reversing a direction.
                const float jerk =
                  (v_exit > v_entry) ?
                  ((v_entry > 0.0f || v_exit < 0.0f) ?
                    // coasting
                    (v_exit - v_entry) :
                    // axis reversal
                    std::max(v_exit, -v_entry)) :
                  // v_exit <= v_entry
                  ((v_entry < 0.0f || v_exit > 0.0f) ?
                    // coasting
                    (v_entry - v_exit) :
                    // axis reversal
                    std::max(-v_exit, v_entry));

                const float axis_max_jerk = get_axis_max_jerk(static_cast<PrintEstimatedStatistics::ETimeMode>(i), static_cast<Axis>(a));
                if (jerk > axis_max_jerk) {
                    v_factor *= axis_max_jerk / jerk;
                    limited = true;
                }
            }

            if (limited)
                vmax_junction *= v_factor;

            // Now the transition velocity is known, which maximizes the shared exit / entry velocity while
            // respecting the jerk factors, it may be possible, that applying separate safe exit / entry velocities will achieve faster prints.
            const float vmax_junction_threshold = vmax_junction * 0.99f;

            // Not coasting. The machine will stop and start the movements anyway, better to start the segment from start.
            if (prev.safe_feedrate > vmax_junction_threshold && curr.safe_feedrate > vmax_junction_threshold)
                vmax_junction = curr.safe_feedrate;
        }

        const float v_allowable = max_allowable_speed(-acceleration, curr.safe_feedrate, block.distance);
        block.feedrate_profile.entry = std::min(vmax_junction, v_allowable);

        block.max_entry_speed = vmax_junction;
        block.flags.nominal_length = (block.feedrate_profile.cruise <= v_allowable);
        block.flags.recalculate = true;
        block.safe_feedrate = curr.safe_feedrate;

        // calculates block trapezoid
        block.calculate_trapezoid();

        // updates previous
        prev = curr;

        blocks.push_back(block);
    }

    if (m_time_processor.machines[0].blocks.size() > TimeProcessor::Planner::refresh_threshold)
        calculate_time(m_result, TimeProcessor::Planner::queue_size);

    if (m_seams_detector.is_active() && (
        type != EMoveType::Extrude
        || (
            m_extrusion_role != GCodeExtrusionRole::ExternalPerimeter
            && m_extrusion_role != GCodeExtrusionRole::OverhangPerimeter
        )
    )) {
        const AxisCoords curr_pos = m_end_position;
        const Vec3f new_pos = m_result.moves.back().position - m_extruder_offsets[m_extruder_id];
        for (unsigned char a = X; a < E; ++a) {
            m_end_position[a] = double(new_pos[a]);
        }
        m_end_position[Z] += m_z_offset;
        store_move_vertex(EMoveType::Seam);
        m_end_position = curr_pos;

        m_seams_detector.activate(false);
    } else if (type == EMoveType::Extrude && m_extrusion_role == GCodeExtrusionRole::ExternalPerimeter) {
        m_seams_detector.activate(true);
    }

    // store move
    store_move_vertex(type, origin == G1DiscretizationOrigin::G2G3);
}

void GCodeProcessor::process_G2_G3(const GCodeReader::GCodeLine& line, bool clockwise)
{
    enum class EFitting { None, IJ, R };
    std::string_view axis_pos_I;
    std::string_view axis_pos_J;
    EFitting fitting = EFitting::None;
    if (line.has('R')) {
        fitting = EFitting::R;
    } else {
        axis_pos_I = line.axis_pos('I');
        axis_pos_J = line.axis_pos('J');
        if (! axis_pos_I.empty() || ! axis_pos_J.empty())
            fitting = EFitting::IJ;
    }

    if (fitting == EFitting::None)
        return;

    const float filament_diameter = (static_cast<size_t>(m_extruder_id) < m_result.filament_diameters.size()) ? m_result.filament_diameters[m_extruder_id] : m_result.filament_diameters.back();
    const float filament_radius = 0.5f * filament_diameter;
    const float area_filament_cross_section = static_cast<float>(M_PI) * sqr(filament_radius);

    AxisCoords end_position = m_start_position;
    for (unsigned char a = X; a <= E; ++a) {
        end_position[a] = extract_absolute_position_on_axis((Axis)a, line, double(area_filament_cross_section));
    }

    // relative center
    Vec3f rel_center = Vec3f::Zero();
#ifndef NDEBUG
    double radius = 0.0;
#endif // NDEBUG
    if (fitting == EFitting::R) {
        float r;
        if (!line.has_value('R', r) || r == 0.0f)
            return;
#ifndef NDEBUG
        radius = (double)std::abs(r);
#endif // NDEBUG
        const Vec2f start_pos((float)m_start_position[X], (float)m_start_position[Y]);
        const Vec2f end_pos((float)end_position[X], (float)end_position[Y]);
        const Vec2f c = Geometry::ArcWelder::arc_center(start_pos, end_pos, r, !clockwise);
        rel_center.x() = c.x() - m_start_position[X];
        rel_center.y() = c.y() - m_start_position[Y];
    }
    else {
        assert(fitting == EFitting::IJ);
        if (! axis_pos_I.empty() && ! line.has_value(axis_pos_I, rel_center.x()))
            return;
        if (! axis_pos_J.empty() && ! line.has_value(axis_pos_J, rel_center.y()))
            return;
    }

    // scale center, if needed
    if (m_units == EUnits::Inches)
        rel_center *= INCHES_TO_MM;

    struct Arc
    {
        Vec3d start{ Vec3d::Zero() };
        Vec3d end{ Vec3d::Zero() };
        Vec3d center{ Vec3d::Zero() };

        double angle{ 0.0 };
        double delta_x() const { return end.x() - start.x(); }
        double delta_y() const { return end.y() - start.y(); }
        double delta_z() const { return end.z() - start.z(); }

        double length() const { return angle * start_radius(); }
        double travel_length() const { return std::sqrt(sqr(length()) + sqr(delta_z())); }
        double start_radius() const { return (start - center).norm(); }
        double end_radius() const { return (end - center).norm(); }

        Vec3d relative_start() const { return start - center; }
        Vec3d relative_end() const { return end - center; }

        bool is_full_circle() const { return std::abs(delta_x()) < EPSILON && std::abs(delta_y()) < EPSILON; }
    };

    Arc arc;

    // arc start endpoint
    arc.start = Vec3d(m_start_position[X], m_start_position[Y], m_start_position[Z]);

    // arc center
    arc.center = arc.start + rel_center.cast<double>();

    // arc end endpoint
    arc.end = Vec3d(end_position[X], end_position[Y], end_position[Z]);

    // radii
    if (std::abs(arc.end_radius() - arc.start_radius()) > 0.001) {
        // what to do ???
    }

    assert(fitting != EFitting::R || std::abs(radius - arc.start_radius()) < EPSILON);

    // updates feedrate from line
    std::optional<float> feedrate;
    if (line.has_f())
        feedrate = m_feed_multiply.current * line.f() * MMMIN_TO_MMSEC;

    // updates extrusion from line
    std::optional<float> extrusion;
    if (line.has_e())
        extrusion = end_position[E] - m_start_position[E];

    // relative arc endpoints
    const Vec3d rel_arc_start = arc.relative_start();
    const Vec3d rel_arc_end   = arc.relative_end();

    // arc angle
    if (arc.is_full_circle())
        arc.angle = 2.0 * PI;
    else {
        arc.angle = std::atan2(rel_arc_start.x() * rel_arc_end.y() - rel_arc_start.y() * rel_arc_end.x(),
            rel_arc_start.x() * rel_arc_end.x() + rel_arc_start.y() * rel_arc_end.y());
        if (arc.angle < 0.0)
            arc.angle += 2.0 * PI;
        if (clockwise)
            arc.angle -= 2.0 * PI;
    }

    const double travel_length = arc.travel_length();
    if (travel_length < 0.001)
        return;

    auto adjust_target = [this, area_filament_cross_section](const AxisCoords& target, const AxisCoords& prev_position) {
        AxisCoords ret = target;
        if (m_global_positioning_type == EPositioningType::Relative) {
            for (unsigned char a = X; a <= E; ++a) {
                ret[a] -= prev_position[a];
            }
        }
        else if (m_e_local_positioning_type == EPositioningType::Relative)
            ret[E] -= prev_position[E];

        if (m_use_volumetric_e)
            ret[E] *= area_filament_cross_section;

        const double lengthsScaleFactor = (m_units == EUnits::Inches) ? double(INCHES_TO_MM) : 1.0;
        for (unsigned char a = X; a <= E; ++a) {
            ret[a] /= lengthsScaleFactor;
        }
        return ret;
    };

    auto internal_only_g1_line = [this](const AxisCoords& target, bool has_z, const std::optional<float>& feedrate,
        const std::optional<float>& extrusion, const std::optional<unsigned int>& remaining_internal_g1_lines = std::nullopt) {
          std::array<std::optional<double>, 4> g1_axes = { target[X], target[Y], std::nullopt, std::nullopt };
          std::optional<double> g1_feedrate = std::nullopt;
          if (has_z)
              g1_axes[Z] = target[Z];
          if (extrusion.has_value())
              g1_axes[E] = target[E];
          if (feedrate.has_value())
              g1_feedrate = (double)*feedrate;
          process_G1(g1_axes, g1_feedrate, G1DiscretizationOrigin::G2G3, remaining_internal_g1_lines);
    };

    if (m_flavor == gcfMarlinFirmware) {
        // calculate arc segments
        // reference:
        // Prusa-Firmware-Buddy\lib\Marlin\Marlin\src\gcode\motion\G2_G3.cpp - plan_arc()
        // https://github.com/prusa3d/Prusa-Firmware-Buddy-Private/blob/private/lib/Marlin/Marlin/src/gcode/motion/G2_G3.cpp

        static const float MAX_ARC_DEVIATION = 0.02f;
        static const float MIN_ARC_SEGMENTS_PER_SEC = 50;
        static const float MIN_ARC_SEGMENT_MM = 0.1f;
        static const float MAX_ARC_SEGMENT_MM = 2.0f;
        const float feedrate_mm_s = feedrate.has_value() ? *feedrate : m_feedrate;
        const float radius_mm = rel_center.norm();
        const float segment_mm = std::clamp(std::min(std::sqrt(8.0f * radius_mm * MAX_ARC_DEVIATION), feedrate_mm_s * (1.0f / MIN_ARC_SEGMENTS_PER_SEC)), MIN_ARC_SEGMENT_MM, MAX_ARC_SEGMENT_MM);
        const float flat_mm = radius_mm * std::abs(arc.angle);
        const size_t segments = std::max<size_t>(flat_mm / segment_mm + 0.8f, 1);

        AxisCoords prev_target = m_start_position;

        if (segments > 1) {
            const float inv_segments = 1.0f / static_cast<float>(segments);
            const float theta_per_segment = static_cast<float>(arc.angle) * inv_segments;
            const float cos_T = cos(theta_per_segment);
            const float sin_T = sin(theta_per_segment);
            const float z_per_segment = arc.delta_z() * inv_segments;
            const float extruder_per_segment = (extrusion.has_value()) ? *extrusion * inv_segments : 0.0f;

            static const size_t N_ARC_CORRECTION = 25;
            size_t arc_recalc_count = N_ARC_CORRECTION;

            Vec2f rvec(-rel_center.x(), -rel_center.y());
            AxisCoords arc_target = { 0.0f, 0.0f, m_start_position[Z], m_start_position[E] };
            for (size_t i = 1; i < segments; ++i) {
                if (--arc_recalc_count) {
                    // Apply vector rotation matrix to previous rvec.a / 1
                    const float r_new_Y = rvec.x() * sin_T + rvec.y() * cos_T;
                    rvec.x() = rvec.x() * cos_T - rvec.y() * sin_T;
                    rvec.y() = r_new_Y;
                }
                else {
                    arc_recalc_count = N_ARC_CORRECTION;
                    // Arc correction to radius vector. Computed only every N_ARC_CORRECTION increments.
                    // Compute exact location by applying transformation matrix from initial radius vector(=-offset).
                    // To reduce stuttering, the sin and cos could be computed at different times.
                    // For now, compute both at the same time.
                    const float Ti = i * theta_per_segment;
                    const float cos_Ti = cos(Ti);
                    const float sin_Ti = sin(Ti);
                    rvec.x() = -rel_center.x() * cos_Ti + rel_center.y() * sin_Ti;
                    rvec.y() = -rel_center.x() * sin_Ti - rel_center.y() * cos_Ti;
                }

                // Update arc_target location
                arc_target[X] = arc.center.x() + rvec.x();
                arc_target[Y] = arc.center.y() + rvec.y();
                arc_target[Z] += z_per_segment;
                arc_target[E] += extruder_per_segment;

                m_start_position = m_end_position; // this is required because we are skipping the call to process_gcode_line()
                internal_only_g1_line(adjust_target(arc_target, prev_target), z_per_segment != 0.0, (i == 1) ? feedrate : std::nullopt,
                    extrusion, segments - i);
                prev_target = arc_target;
            }
        }

        // Ensure last segment arrives at target location.
        m_start_position = m_end_position; // this is required because we are skipping the call to process_gcode_line()
        internal_only_g1_line(adjust_target(end_position, prev_target), arc.delta_z() != 0.0, (segments == 1) ? feedrate : std::nullopt, extrusion);
    }
    else {
        // calculate arc segments
        // reference:
        // Prusa-Firmware\Firmware\motion_control.cpp - mc_arc()
        // https://github.com/prusa3d/Prusa-Firmware/blob/MK3/Firmware/motion_control.cpp

        // segments count
#if 0
        static const double MM_PER_ARC_SEGMENT = 1.0;
        const size_t segments = std::max<size_t>(std::floor(travel_length / MM_PER_ARC_SEGMENT), 1);
#else
        static const double gcode_arc_tolerance = 0.0125;
        const size_t segments = Geometry::ArcWelder::arc_discretization_steps(arc.start_radius(), std::abs(arc.angle), gcode_arc_tolerance);
#endif

        const double inv_segment = 1.0 / double(segments);
        const double theta_per_segment = arc.angle * inv_segment;
        const double z_per_segment = arc.delta_z() * inv_segment;
        const double extruder_per_segment = (extrusion.has_value()) ? *extrusion * inv_segment : 0.0;
        const double sq_theta_per_segment = sqr(theta_per_segment);
        const double cos_T = 1.0 - 0.5 * sq_theta_per_segment;
        const double sin_T = theta_per_segment - sq_theta_per_segment * theta_per_segment / 6.0f;

        AxisCoords prev_target = m_start_position;
        AxisCoords arc_target;

        // Initialize the linear axis
        arc_target[Z] = m_start_position[Z];

        // Initialize the extruder axis
        arc_target[E] = m_start_position[E];

        static const size_t N_ARC_CORRECTION = 25;
        Vec3d curr_rel_arc_start = arc.relative_start();
        size_t count = N_ARC_CORRECTION;

        for (size_t i = 1; i < segments; ++i) {
            if (count-- == 0) {
                const double cos_Ti = ::cos(i * theta_per_segment);
                const double sin_Ti = ::sin(i * theta_per_segment);
                curr_rel_arc_start.x() = -double(rel_center.x()) * cos_Ti + double(rel_center.y()) * sin_Ti;
                curr_rel_arc_start.y() = -double(rel_center.x()) * sin_Ti - double(rel_center.y()) * cos_Ti;
                count = N_ARC_CORRECTION;
            }
            else {
                const float r_axisi = curr_rel_arc_start.x() * sin_T + curr_rel_arc_start.y() * cos_T;
                curr_rel_arc_start.x() = curr_rel_arc_start.x() * cos_T - curr_rel_arc_start.y() * sin_T;
                curr_rel_arc_start.y() = r_axisi;
            }

            // Update arc_target location
            arc_target[X] = arc.center.x() + curr_rel_arc_start.x();
            arc_target[Y] = arc.center.y() + curr_rel_arc_start.y();
            arc_target[Z] += z_per_segment;
            arc_target[E] += extruder_per_segment;

            m_start_position = m_end_position; // this is required because we are skipping the call to process_gcode_line()
            internal_only_g1_line(adjust_target(arc_target, prev_target), z_per_segment != 0.0, (i == 1) ? feedrate : std::nullopt,
                extrusion, segments - i);
            prev_target = arc_target;
        }

        // Ensure last segment arrives at target location.
        m_start_position = m_end_position; // this is required because we are skipping the call to process_gcode_line()
        internal_only_g1_line(adjust_target(end_position, prev_target), arc.delta_z() != 0.0, (segments == 1) ? feedrate : std::nullopt, extrusion);
    }
}

void GCodeProcessor::process_G10(const GCodeReader::GCodeLine& line)
{
    if (m_flavor == gcfRepRapFirmware) {
        // similar to M104/M109
        float new_temp;
        if (line.has_value('S', new_temp)) {
            size_t id = m_extruder_id;
            float val;
            if (line.has_value('P', val)) {
                const size_t eid = static_cast<size_t>(val);
                if (eid < m_extruder_temps.size())
                    id = eid;
            }

            m_extruder_temps[id] = new_temp;
            return;
        }
    }

    // stores retract move
    store_move_vertex(EMoveType::Retract);
}

void GCodeProcessor::process_G11(const GCodeReader::GCodeLine& line)
{
    // stores unretract move
    store_move_vertex(EMoveType::Unretract);
}

void GCodeProcessor::process_G20(const GCodeReader::GCodeLine& line)
{
    m_units = EUnits::Inches;
}

void GCodeProcessor::process_G21(const GCodeReader::GCodeLine& line)
{
    m_units = EUnits::Millimeters;
}

void GCodeProcessor::process_G22(const GCodeReader::GCodeLine& line)
{
    // stores retract move
    store_move_vertex(EMoveType::Retract);
}

void GCodeProcessor::process_G23(const GCodeReader::GCodeLine& line)
{
    // stores unretract move
    store_move_vertex(EMoveType::Unretract);
}

void GCodeProcessor::process_G28(const GCodeReader::GCodeLine& line)
{
    std::string_view cmd = line.cmd();
    std::string new_line_raw = { cmd.data(), cmd.size() };
    bool found = false;
    if (line.has('X')) {
        new_line_raw += " X0";
        found = true;
    }
    if (line.has('Y')) {
        new_line_raw += " Y0";
        found = true;
    }
    if (line.has('Z')) {
        new_line_raw += " Z0";
        found = true;
    }
    if (!found)
        new_line_raw += " X0  Y0  Z0";

    GCodeReader::GCodeLine new_gline;
    GCodeReader reader;
    reader.parse_line(new_line_raw, [&](GCodeReader& reader, const GCodeReader::GCodeLine& gline) { new_gline = gline; });
    process_G1(new_gline);
}

void GCodeProcessor::process_G60(const GCodeReader::GCodeLine& line)
{
    if (m_flavor == gcfMarlinLegacy || m_flavor == gcfMarlinFirmware)
        m_saved_position = m_end_position;
}

void GCodeProcessor::process_G61(const GCodeReader::GCodeLine& line)
{
    if (m_flavor == gcfMarlinLegacy || m_flavor == gcfMarlinFirmware) {
        bool modified = false;
        if (line.has_x()) {
            m_end_position[X] = m_saved_position[X];
            modified = true;
        }
        if (line.has_y()) {
            m_end_position[Y] = m_saved_position[Y];
            modified = true;
        }
        if (line.has_z()) {
            m_end_position[Z] = m_saved_position[Z];
            modified = true;
        }
        if (line.has_e()) {
            m_end_position[E] = m_saved_position[E];
            modified = true;
        }
        if (line.has_f())
            m_feedrate = m_feed_multiply.current * line.f();

        if (!modified)
            m_end_position = m_saved_position;


        store_move_vertex(EMoveType::Travel);
    }
}

void GCodeProcessor::process_G90(const GCodeReader::GCodeLine& line)
{
    m_global_positioning_type = EPositioningType::Absolute;
}

void GCodeProcessor::process_G91(const GCodeReader::GCodeLine& line)
{
    m_global_positioning_type = EPositioningType::Relative;
}

void GCodeProcessor::process_G92(const GCodeReader::GCodeLine& line)
{
    float lengths_scale_factor = (m_units == EUnits::Inches) ? INCHES_TO_MM : 1.0f;
    bool any_found = false;

    if (line.has_x()) {
        m_origin[X] = m_end_position[X] - line.x() * lengths_scale_factor;
        any_found = true;
    }

    if (line.has_y()) {
        m_origin[Y] = m_end_position[Y] - line.y() * lengths_scale_factor;
        any_found = true;
    }

    if (line.has_z()) {
        m_origin[Z] = m_end_position[Z] - line.z() * lengths_scale_factor;
        any_found = true;
    }

    if (line.has_e()) {
        // extruder coordinate can grow to the point where its float representation does not allow for proper addition with small increments,
        // we set the value taken from the G92 line as the new current position for it
        m_end_position[E] = line.e() * lengths_scale_factor;
        any_found = true;
    }
    else
        simulate_st_synchronize();

    if (!any_found && !line.has_unknown_axis()) {
        // The G92 may be called for axes that PrusaSlicer does not recognize, for example see GH issue #3510, 
        // where G92 A0 B0 is called although the extruder axis is till E.
        for (unsigned char a = X; a <= E; ++a) {
            m_origin[a] = m_end_position[a];
        }
    }
}

void GCodeProcessor::process_M1(const GCodeReader::GCodeLine& line)
{
    simulate_st_synchronize();
}

void GCodeProcessor::process_M82(const GCodeReader::GCodeLine& line)
{
    m_e_local_positioning_type = EPositioningType::Absolute;
}

void GCodeProcessor::process_M83(const GCodeReader::GCodeLine& line)
{
    m_e_local_positioning_type = EPositioningType::Relative;
}

void GCodeProcessor::process_M104(const GCodeReader::GCodeLine& line)
{
    float new_temp;
    if (line.has_value('S', new_temp)) {
        size_t id = m_extruder_id;
        float val;
        if (line.has_value('T', val)) {
            const size_t eid = static_cast<size_t>(val);
            if (eid < m_extruder_temps.size())
                id = eid;
        }

        m_extruder_temps[id] = new_temp;
    }
}

void GCodeProcessor::process_M106(const GCodeReader::GCodeLine& line)
{
    if (!line.has('P')) {
        // The absence of P means the print cooling fan, so ignore anything else.
        float new_fan_speed;
        if (line.has_value('S', new_fan_speed))
            m_fan_speed = (100.0f / 255.0f) * new_fan_speed;
        else
            m_fan_speed = 100.0f;
    }
}

void GCodeProcessor::process_M107(const GCodeReader::GCodeLine& line)
{
    m_fan_speed = 0.0f;
}

void GCodeProcessor::process_M108(const GCodeReader::GCodeLine& line)
{
    // These M-codes are used by Sailfish to change active tool.
    // They have to be processed otherwise toolchanges will be unrecognised
    // by the analyzer - see https://github.com/prusa3d/PrusaSlicer/issues/2566

    if (m_flavor != gcfSailfish)
        return;

    std::string cmd = line.raw();
    size_t pos = cmd.find("T");
    if (pos != std::string::npos)
        process_T(cmd.substr(pos));
}

void GCodeProcessor::process_M109(const GCodeReader::GCodeLine& line)
{
    float new_temp;
    size_t id = (size_t)-1;
    if (line.has_value('R', new_temp)) {
        float val;
        if (line.has_value('T', val)) {
            const size_t eid = static_cast<size_t>(val);
            if (eid < m_extruder_temps.size())
                id = eid;
        }
        else
            id = m_extruder_id;
    }
    else if (line.has_value('S', new_temp))
        id = m_extruder_id;

    if (id != (size_t)-1)
        m_extruder_temps[id] = new_temp;
}

void GCodeProcessor::process_M132(const GCodeReader::GCodeLine& line)
{
    // This command is used by Makerbot to load the current home position from EEPROM
    // see: https://github.com/makerbot/s3g/blob/master/doc/GCodeProtocol.md
    // Using this command to reset the axis origin to zero helps in fixing: https://github.com/prusa3d/PrusaSlicer/issues/3082

    if (line.has('X'))
        m_origin[X] = 0.0f;

    if (line.has('Y'))
        m_origin[Y] = 0.0f;

    if (line.has('Z'))
        m_origin[Z] = 0.0f;

    if (line.has('E'))
        m_origin[E] = 0.0f;
}

void GCodeProcessor::process_M135(const GCodeReader::GCodeLine& line)
{
    // These M-codes are used by MakerWare to change active tool.
    // They have to be processed otherwise toolchanges will be unrecognised
    // by the analyzer - see https://github.com/prusa3d/PrusaSlicer/issues/2566

    if (m_flavor != gcfMakerWare)
        return;

    std::string cmd = line.raw();
    size_t pos = cmd.find("T");
    if (pos != std::string::npos)
        process_T(cmd.substr(pos));
}

void GCodeProcessor::process_M201(const GCodeReader::GCodeLine& line)
{
    // see http://reprap.org/wiki/G-code#M201:_Set_max_printing_acceleration
    float factor = ((m_flavor != gcfRepRapSprinter && m_flavor != gcfRepRapFirmware) && m_units == EUnits::Inches) ? INCHES_TO_MM : 1.0f;

    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        if (static_cast<PrintEstimatedStatistics::ETimeMode>(i) == PrintEstimatedStatistics::ETimeMode::Normal ||
            m_time_processor.machine_envelope_processing_enabled) {
            if (line.has_x())
                set_option_value(m_time_processor.machine_limits.machine_max_acceleration_x, i, line.x() * factor);

            if (line.has_y())
                set_option_value(m_time_processor.machine_limits.machine_max_acceleration_y, i, line.y() * factor);

            if (line.has_z())
                set_option_value(m_time_processor.machine_limits.machine_max_acceleration_z, i, line.z() * factor);

            if (line.has_e())
                set_option_value(m_time_processor.machine_limits.machine_max_acceleration_e, i, line.e() * factor);
        }
    }
}

void GCodeProcessor::process_M203(const GCodeReader::GCodeLine& line)
{
    // see http://reprap.org/wiki/G-code#M203:_Set_maximum_feedrate
    if (m_flavor == gcfRepetier)
        return;

    // see http://reprap.org/wiki/G-code#M203:_Set_maximum_feedrate
    // http://smoothieware.org/supported-g-codes
    float factor = (m_flavor == gcfMarlinLegacy || m_flavor == gcfMarlinFirmware || m_flavor == gcfSmoothie) ? 1.0f : MMMIN_TO_MMSEC;

    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        if (static_cast<PrintEstimatedStatistics::ETimeMode>(i) == PrintEstimatedStatistics::ETimeMode::Normal ||
            m_time_processor.machine_envelope_processing_enabled) {
            if (line.has_x())
                set_option_value(m_time_processor.machine_limits.machine_max_feedrate_x, i, line.x() * factor);

            if (line.has_y())
                set_option_value(m_time_processor.machine_limits.machine_max_feedrate_y, i, line.y() * factor);

            if (line.has_z())
                set_option_value(m_time_processor.machine_limits.machine_max_feedrate_z, i, line.z() * factor);

            if (line.has_e())
                set_option_value(m_time_processor.machine_limits.machine_max_feedrate_e, i, line.e() * factor);
        }
    }
}

void GCodeProcessor::process_M204(const GCodeReader::GCodeLine& line)
{
    float value;
    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        if (static_cast<PrintEstimatedStatistics::ETimeMode>(i) == PrintEstimatedStatistics::ETimeMode::Normal ||
            m_time_processor.machine_envelope_processing_enabled) {
            if (line.has_value('S', value)) {
                // Legacy acceleration format. This format is used by the legacy Marlin, MK2 or MK3 firmware
                // It is also generated by PrusaSlicer to control acceleration per extrusion type
                // (perimeters, first layer etc) when 'Marlin (legacy)' flavor is used.
                set_acceleration(static_cast<PrintEstimatedStatistics::ETimeMode>(i), value);
                set_travel_acceleration(static_cast<PrintEstimatedStatistics::ETimeMode>(i), value);
                if (line.has_value('T', value))
                    set_retract_acceleration(static_cast<PrintEstimatedStatistics::ETimeMode>(i), value);
            }
            else {
                // New acceleration format, compatible with the upstream Marlin.
                if (line.has_value('P', value))
                    set_acceleration(static_cast<PrintEstimatedStatistics::ETimeMode>(i), value);
                if (line.has_value('R', value))
                    set_retract_acceleration(static_cast<PrintEstimatedStatistics::ETimeMode>(i), value);
                if (line.has_value('T', value))
                    // Interpret the T value as the travel acceleration in the new Marlin format.
                    set_travel_acceleration(static_cast<PrintEstimatedStatistics::ETimeMode>(i), value);
            }
        }
    }
}

void GCodeProcessor::process_M205(const GCodeReader::GCodeLine& line)
{
    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        if (static_cast<PrintEstimatedStatistics::ETimeMode>(i) == PrintEstimatedStatistics::ETimeMode::Normal ||
            m_time_processor.machine_envelope_processing_enabled) {
            if (line.has_x()) {
                float max_jerk = line.x();
                set_option_value(m_time_processor.machine_limits.machine_max_jerk_x, i, max_jerk);
                set_option_value(m_time_processor.machine_limits.machine_max_jerk_y, i, max_jerk);
            }

            if (line.has_y())
                set_option_value(m_time_processor.machine_limits.machine_max_jerk_y, i, line.y());

            if (line.has_z())
                set_option_value(m_time_processor.machine_limits.machine_max_jerk_z, i, line.z());

            if (line.has_e())
                set_option_value(m_time_processor.machine_limits.machine_max_jerk_e, i, line.e());

            float value;
            if (line.has_value('S', value))
                set_option_value(m_time_processor.machine_limits.machine_min_extruding_rate, i, value);

            if (line.has_value('T', value))
                set_option_value(m_time_processor.machine_limits.machine_min_travel_rate, i, value);
        }
    }
}

void GCodeProcessor::process_M220(const GCodeReader::GCodeLine& line)
{
    if (m_flavor != gcfMarlinLegacy && m_flavor != gcfMarlinFirmware && m_flavor != gcfKlipper)
        return;

    if (line.has('B'))
        m_feed_multiply.saved = m_feed_multiply.current;
    float value;
    if (line.has_value('S', value))
        m_feed_multiply.current = value * 0.01f;
    if (line.has('R'))
        m_feed_multiply.current = m_feed_multiply.saved;
}

void GCodeProcessor::process_M221(const GCodeReader::GCodeLine& line)
{
    float value_s;
    float value_t;
    if (line.has_value('S', value_s) && !line.has_value('T', value_t)) {
        value_s *= 0.01f;
        for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
            m_time_processor.machines[i].extrude_factor_override_percentage = value_s;
        }
    }
}

void GCodeProcessor::process_M401(const GCodeReader::GCodeLine& line)
{
    if (m_flavor != gcfRepetier)
        return;

    for (unsigned char a = 0; a <= 3; ++a) {
        m_cached_position.position[a] = m_start_position[a];
    }
    m_cached_position.feedrate = m_feedrate;
}

void GCodeProcessor::process_M402(const GCodeReader::GCodeLine& line)
{
    if (m_flavor != gcfRepetier)
        return;

    // see for reference:
    // https://github.com/repetier/Repetier-Firmware/blob/master/src/ArduinoAVR/Repetier/Printer.cpp
    // void Printer::GoToMemoryPosition(bool x, bool y, bool z, bool e, float feed)

    bool has_xyz = !(line.has('X') || line.has('Y') || line.has('Z'));

    float p = FLT_MAX;
    for (unsigned char a = X; a <= Z; ++a) {
        if (has_xyz || line.has(a)) {
            p = m_cached_position.position[a];
            if (p != FLT_MAX)
                m_start_position[a] = p;
        }
    }

    p = m_cached_position.position[E];
    if (p != FLT_MAX)
        m_start_position[E] = p;

    p = FLT_MAX;
    if (!line.has_value(4, p))
        p = m_cached_position.feedrate;

    if (p != FLT_MAX)
        m_feedrate = p;
}

void GCodeProcessor::process_M566(const GCodeReader::GCodeLine& line)
{
    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        if (line.has_x())
            set_option_value(m_time_processor.machine_limits.machine_max_jerk_x, i, line.x() * MMMIN_TO_MMSEC);

        if (line.has_y())
            set_option_value(m_time_processor.machine_limits.machine_max_jerk_y, i, line.y() * MMMIN_TO_MMSEC);

        if (line.has_z())
            set_option_value(m_time_processor.machine_limits.machine_max_jerk_z, i, line.z() * MMMIN_TO_MMSEC);

        if (line.has_e())
            set_option_value(m_time_processor.machine_limits.machine_max_jerk_e, i, line.e() * MMMIN_TO_MMSEC);
    }
}

void GCodeProcessor::process_M702(const GCodeReader::GCodeLine& line)
{
    if (line.has('C')) {
        // MK3 MMU2 specific M code:
        // M702 C is expected to be sent by the custom end G-code when finalizing a print.
        // The MK3 unit shall unload and park the active filament into the MMU2 unit.
        m_time_processor.extruder_unloaded = true;
        simulate_st_synchronize(get_filament_unload_time(m_extruder_id));
    }
}

void GCodeProcessor::process_T(const GCodeReader::GCodeLine& line)
{
    process_T(line.cmd());
}

void GCodeProcessor::process_T(const std::string_view command)
{
    if (command.length() > 1) {
        int eid = 0;
        if (! parse_number(command.substr(1), eid) || eid < 0 || eid > 255) {
            // Specific to the MMU2 V2 (see https://www.help.prusa3d.com/en/article/prusa-specific-g-codes_112173):
            if ((m_flavor == gcfMarlinLegacy || m_flavor == gcfMarlinFirmware) && (command == "Tx" || command == "Tc" || command == "T?"))
                return;

            // T-1 is a valid gcode line for RepRap Firmwares (used to deselects all tools) see https://github.com/prusa3d/PrusaSlicer/issues/5677
            if ((m_flavor != gcfRepRapFirmware && m_flavor != gcfRepRapSprinter) || eid != -1)
                BOOST_LOG_TRIVIAL(error) << "GCodeProcessor encountered an invalid toolchange (" << command << ").";
        }
        else {
            unsigned char id = static_cast<unsigned char>(eid);
            if (m_extruder_id != id) {
                if (((m_producer == EProducer::PrusaSlicer || m_producer == EProducer::Slic3rPE || m_producer == EProducer::Slic3r) && id >= m_result.extruders_count) ||
                    ((m_producer != EProducer::PrusaSlicer && m_producer != EProducer::Slic3rPE && m_producer != EProducer::Slic3r) && id >= m_result.extruder_colors.size()))
                    BOOST_LOG_TRIVIAL(error) << "GCodeProcessor encountered an invalid toolchange, maybe from a custom gcode (" << command << ").";
                else {
                    unsigned char old_extruder_id = m_extruder_id;
                    process_filaments(CustomGCode::ToolChange);
                    m_extruder_id = id;
                    m_cp_color.current = m_extruder_colors[id];
                    // Specific to the MK3 MMU2:
                    // The initial value of extruder_unloaded is set to true indicating
                    // that the filament is parked in the MMU2 unit and there is nothing to be unloaded yet.
                    float extra_time = get_filament_unload_time(static_cast<size_t>(old_extruder_id));
                    m_time_processor.extruder_unloaded = false;
                    extra_time += get_filament_load_time(static_cast<size_t>(m_extruder_id));
                    if (m_producer == EProducer::KissSlicer && m_flavor == gcfMarlinLegacy)
                        extra_time += m_kissslicer_toolchange_time_correction;
                    simulate_st_synchronize(extra_time);

                    // specific to single extruder multi material, set the new extruder temperature
                    // to match the old one
                    if (m_single_extruder_multi_material)
                        m_extruder_temps[m_extruder_id] = m_extruder_temps[old_extruder_id];

                    m_result.extruders_count = std::max<size_t>(m_result.extruders_count, m_extruder_id + 1);
                }

                // store tool change move
                store_move_vertex(EMoveType::Tool_change);
            }
        }
    }
}

void GCodeProcessor::post_process()
{
    FilePtr in{ boost::nowide::fopen(m_result.filename.c_str(), "rb") };
    if (in.f == nullptr)
        throw Slic3r::RuntimeError(std::string("GCode processor post process export failed.\nCannot open file for reading.\n"));

    // temporary file to contain modified gcode
    std::string out_path = m_result.filename + ".postprocess";
    FilePtr out{ boost::nowide::fopen(out_path.c_str(), "wb") };
    if (out.f == nullptr)
        throw Slic3r::RuntimeError(std::string("GCode processor post process export failed.\nCannot open file for writing.\n"));

    std::vector<double> filament_mm(m_result.extruders_count, 0.0);
    std::vector<double> filament_cm3(m_result.extruders_count, 0.0);
    std::vector<double> filament_g(m_result.extruders_count, 0.0);
    std::vector<double> filament_cost(m_result.extruders_count, 0.0);

    double filament_total_g = 0.0;
    double filament_total_cost = 0.0;

    for (const auto& [id, volume] : m_result.print_statistics.volumes_per_extruder) {
        filament_mm[id] = volume / (static_cast<double>(M_PI) * sqr(0.5 * m_result.filament_diameters[id]));
        filament_cm3[id] = volume * 0.001;
        filament_g[id] = filament_cm3[id] * double(m_result.filament_densities[id]);
        filament_cost[id] = filament_g[id] * double(m_result.filament_cost[id]) * 0.001;
        filament_total_g += filament_g[id];
        filament_total_cost += filament_cost[id];
    }

    double total_g_wipe_tower = m_print->print_statistics().total_wipe_tower_filament_weight;

    if (m_binarizer.is_enabled()) {
        // update print metadata
        auto stringify = [](const std::vector<double>& values) {
            std::string ret;
            char buf[1024];
            for (size_t i = 0; i < values.size(); ++i) {
                sprintf(buf, i < values.size() - 1 ? "%.2lf, " : "%.2lf", values[i]);
                ret += buf;
            }
            return ret;
        };

        // update binary data
        bgcode::binarize::BinaryData& binary_data = m_binarizer.get_binary_data();
        binary_data.print_metadata.raw_data.emplace_back(PrintStatistics::FilamentUsedMm, stringify(filament_mm));
        binary_data.print_metadata.raw_data.emplace_back(PrintStatistics::FilamentUsedCm3, stringify(filament_cm3));
        binary_data.print_metadata.raw_data.emplace_back(PrintStatistics::FilamentUsedG, stringify(filament_g));
        binary_data.print_metadata.raw_data.emplace_back(PrintStatistics::FilamentCost, stringify(filament_cost));
        binary_data.print_metadata.raw_data.emplace_back(PrintStatistics::TotalFilamentUsedG, stringify({ filament_total_g }));
        binary_data.print_metadata.raw_data.emplace_back(PrintStatistics::TotalFilamentCost, stringify({ filament_total_cost }));
        binary_data.print_metadata.raw_data.emplace_back(PrintStatistics::TotalFilamentUsedWipeTower, stringify({ total_g_wipe_tower }));

        binary_data.printer_metadata.raw_data.emplace_back(PrintStatistics::FilamentUsedMm, stringify(filament_mm)); // duplicated into print metadata
        binary_data.printer_metadata.raw_data.emplace_back(PrintStatistics::FilamentUsedG, stringify(filament_g));   // duplicated into print metadata
        binary_data.printer_metadata.raw_data.emplace_back(PrintStatistics::FilamentCost, stringify(filament_cost));   // duplicated into print metadata
        binary_data.printer_metadata.raw_data.emplace_back(PrintStatistics::FilamentUsedCm3, stringify(filament_cm3)); // duplicated into print metadata
        binary_data.printer_metadata.raw_data.emplace_back(PrintStatistics::TotalFilamentUsedWipeTower, stringify({ total_g_wipe_tower })); // duplicated into print metadata

        for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
            const TimeMachine& machine = m_time_processor.machines[i];
            PrintEstimatedStatistics::ETimeMode mode = static_cast<PrintEstimatedStatistics::ETimeMode>(i);
            if (mode == PrintEstimatedStatistics::ETimeMode::Normal || machine.enabled) {
                char buf[128];
                sprintf(buf, "(%s mode)", (mode == PrintEstimatedStatistics::ETimeMode::Normal) ? "normal" : "silent");
                binary_data.print_metadata.raw_data.emplace_back("estimated printing time " + std::string(buf), get_time_dhms(machine.time));
                binary_data.print_metadata.raw_data.emplace_back("estimated first layer printing time " + std::string(buf), get_time_dhms(machine.first_layer_time));
                binary_data.printer_metadata.raw_data.emplace_back("estimated printing time " + std::string(buf), get_time_dhms(machine.time));
            }
        }

        const bgcode::core::EResult res = m_binarizer.initialize(*out.f, s_binarizer_config);
        if (res != bgcode::core::EResult::Success)
            throw Slic3r::RuntimeError(format("Unable to initialize the gcode binarizer.\nError: %1%", bgcode::core::translate_result(res)));
    }

    auto time_in_minutes = [](float time_in_seconds) {
        assert(time_in_seconds >= 0.f);
        return int((time_in_seconds + 0.5f) / 60.0f);
    };

    auto time_in_last_minute = [](float time_in_seconds) {
        assert(time_in_seconds <= 60.0f);
        return time_in_seconds / 60.0f;
    };

    auto format_line_M73_main = [](const std::string& mask, int percent, int time) {
        char line_M73[64];
        sprintf(line_M73, mask.c_str(),
            std::to_string(percent).c_str(),
            std::to_string(time).c_str());
        return std::string(line_M73);
    };

    auto format_line_M73_stop_int = [](const std::string& mask, int time) {
        char line_M73[64];
        sprintf(line_M73, mask.c_str(), std::to_string(time).c_str());
        return std::string(line_M73);
    };

    auto format_time_float = [](float time) {
        return Slic3r::float_to_string_decimal_point(time, 2);
    };

    auto format_line_M73_stop_float = [format_time_float](const std::string& mask, float time) {
        char line_M73[64];
        sprintf(line_M73, mask.c_str(), format_time_float(time).c_str());
        return std::string(line_M73);
    };

    std::string gcode_line;
    size_t g1_lines_counter = 0;
    // keeps track of last exported pair <percent, remaining time>
    std::array<std::pair<int, int>, static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count)> last_exported_main;
    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        last_exported_main[i] = { 0, time_in_minutes(m_time_processor.machines[i].time) };
    }

    // keeps track of last exported remaining time to next printer stop
    std::array<int, static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count)> last_exported_stop;
    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        last_exported_stop[i] = time_in_minutes(m_time_processor.machines[i].time);
    }

    // Helper class to modify and export gcode to file
    class ExportLines
    {
    public:
        struct Backtrace
        {
            float time{ 60.0f };
            unsigned int steps{ 10 };
            float time_step() const { return time / float(steps); }
        };

        enum class EWriteType
        {
            BySize,
            ByTime
        };

    private:
        struct LineData
        {
            std::string line;
            std::array<float, static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count)> times{ 0.0f, 0.0f };
        };

        enum ETimeMode
        {
            Normal  = static_cast<int>(PrintEstimatedStatistics::ETimeMode::Normal),
            Stealth = static_cast<int>(PrintEstimatedStatistics::ETimeMode::Stealth)
        };

#ifndef NDEBUG
        class Statistics
        {
            ExportLines& m_parent;
            size_t m_max_size{ 0 };
            size_t m_lines_count{ 0 };
            size_t m_max_lines_count{ 0 };

        public:
            explicit Statistics(ExportLines& parent)
            : m_parent(parent)
            {}

            void add_line(size_t line_size) {
                ++m_lines_count;
                m_max_size = std::max(m_max_size, m_parent.get_size() + line_size);
                m_max_lines_count = std::max(m_max_lines_count, m_lines_count);
            }

            void remove_line() { --m_lines_count; }
            void remove_all_lines() { m_lines_count = 0; }
        };

        Statistics m_statistics;
#endif // NDEBUG

        EWriteType m_write_type{ EWriteType::BySize };
        // Time machines containing g1 times cache
        const std::array<TimeMachine, static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count)>& m_machines;
        // Current time
        std::array<float, static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count)> m_times{ 0.0f, 0.0f };
        // Current size in bytes
        size_t m_size{ 0 };

        // gcode lines cache
        std::deque<LineData> m_lines;
        size_t m_added_lines_counter{ 0 };
        // map of gcode line ids from original to final 
        // used to update m_result.moves[].gcode_id
        std::vector<std::pair<size_t, size_t>> m_gcode_lines_map;

        size_t m_times_cache_id{ 0 };
        size_t m_out_file_pos{ 0 };

        bgcode::binarize::Binarizer& m_binarizer;

    public:
        ExportLines(bgcode::binarize::Binarizer& binarizer, EWriteType type,
            const std::array<TimeMachine, static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count)>& machines)
#ifndef NDEBUG
        : m_statistics(*this), m_binarizer(binarizer), m_write_type(type), m_machines(machines) {}
#else
        : m_binarizer(binarizer), m_write_type(type), m_machines(machines) {}
#endif // NDEBUG

        // return: number of internal G1 lines (from G2/G3 splitting) processed
        unsigned int update(const std::string& line, size_t lines_counter, size_t g1_lines_counter) {
            unsigned int ret = 0;
            m_gcode_lines_map.push_back({ lines_counter, 0 });

            if (GCodeReader::GCodeLine::cmd_is(line, "G0") ||
                GCodeReader::GCodeLine::cmd_is(line, "G1") ||
                GCodeReader::GCodeLine::cmd_is(line, "G2") ||
                GCodeReader::GCodeLine::cmd_is(line, "G3") ||
                GCodeReader::GCodeLine::cmd_is(line, "G28"))
                ++g1_lines_counter;
            else
                return ret;

            auto init_it = m_machines[Normal].g1_times_cache.begin() + m_times_cache_id;
            auto it = init_it;
            while (it != m_machines[Normal].g1_times_cache.end() && it->id < g1_lines_counter) {
                ++it;
                ++m_times_cache_id;
            }

            if (it == m_machines[Normal].g1_times_cache.end() || it->id > g1_lines_counter)
                return ret;

            // search for internal G1 lines
            if (GCodeReader::GCodeLine::cmd_is(line, "G2") || GCodeReader::GCodeLine::cmd_is(line, "G3")) {
                while (it != m_machines[Normal].g1_times_cache.end() && it->remaining_internal_g1_lines > 0) {
                    ++it;
                    ++m_times_cache_id;
                    ++g1_lines_counter;
                    ++ret;
                }
            }

            if (it != m_machines[Normal].g1_times_cache.end() && it->id == g1_lines_counter) {
                m_times[Normal] = it->elapsed_time;
                if (!m_machines[Stealth].g1_times_cache.empty())
                    m_times[Stealth] = (m_machines[Stealth].g1_times_cache.begin() + std::distance(m_machines[Normal].g1_times_cache.begin(), it))->elapsed_time;
            }

            return ret;
        }

        // add the given gcode line to the cache
        void append_line(const std::string& line) {
            m_lines.push_back({ line, m_times });
#ifndef NDEBUG
            m_statistics.add_line(line.length());
#endif // NDEBUG
            m_size += line.length();
            ++m_added_lines_counter;
            assert(!m_gcode_lines_map.empty());
            m_gcode_lines_map.back().second = m_added_lines_counter;
        }

        // Insert the gcode lines required by the command cmd by backtracing into the cache
        void insert_lines(const Backtrace& backtrace, const std::string& cmd,
            std::function<std::string(unsigned int, const std::vector<float>&)> line_inserter,
            std::function<std::string(const std::string&)> line_replacer) {
            assert(!m_lines.empty());
            const float time_step = backtrace.time_step();
            size_t rev_it_dist = 0; // distance from the end of the cache of the starting point of the backtrace
            float last_time_insertion = 0.0f; // used to avoid inserting two lines at the same time
            for (unsigned int i = 0; i < backtrace.steps; ++i) {
                const float backtrace_time_i = (i + 1) * time_step;
                const float time_threshold_i = m_times[Normal] - backtrace_time_i;
                auto rev_it = m_lines.rbegin() + rev_it_dist;
                auto start_rev_it = rev_it;

                std::string curr_cmd = GCodeReader::GCodeLine::extract_cmd(rev_it->line);
                // backtrace into the cache to find the place where to insert the line
                while (rev_it != m_lines.rend() && rev_it->times[Normal] > time_threshold_i && curr_cmd != cmd && curr_cmd != "G28" && curr_cmd != "G29") {
                    rev_it->line = line_replacer(rev_it->line);
                    ++rev_it;
                    if (rev_it != m_lines.rend())
                        curr_cmd = GCodeReader::GCodeLine::extract_cmd(rev_it->line);
                }

                // we met the previous evenience of cmd, or a G28/G29 command. stop inserting lines
                if (rev_it != m_lines.rend() && (curr_cmd == cmd || curr_cmd == "G28" || curr_cmd == "G29"))
                    break;

                // insert the line for the current step
                if (rev_it != m_lines.rend() && rev_it != start_rev_it && rev_it->times[Normal] != last_time_insertion) {
                    last_time_insertion = rev_it->times[Normal];
                    std::vector<float> time_diffs;
                    time_diffs.push_back(m_times[Normal] - last_time_insertion);
                    if (!m_machines[Stealth].g1_times_cache.empty())
                        time_diffs.push_back(m_times[Stealth] - rev_it->times[Stealth]);
                    const std::string out_line = line_inserter(i + 1, time_diffs);
                    rev_it_dist = std::distance(m_lines.rbegin(), rev_it) + 1;
                    m_lines.insert(rev_it.base(), { out_line, rev_it->times });
#ifndef NDEBUG
                    m_statistics.add_line(out_line.length());
#endif // NDEBUG
                    m_size += out_line.length();
                    // synchronize gcode lines map
                    for (auto map_it = m_gcode_lines_map.rbegin(); map_it != m_gcode_lines_map.rbegin() + rev_it_dist - 1; ++map_it) {
                        ++map_it->second;
                    }

                    ++m_added_lines_counter;
                }
            }
        }

        // write to file:
        // m_write_type == EWriteType::ByTime - all lines older than m_time - backtrace_time
        // m_write_type == EWriteType::BySize - all lines if current size is greater than 65535 bytes
        void write(FilePtr& out, float backtrace_time, GCodeProcessorResult& result, const std::string& out_path) {
            if (m_lines.empty())
                return;

            // collect lines to write into a single string
            std::string out_string;
            if (!m_lines.empty()) {
                if (m_write_type == EWriteType::ByTime) {
                    while (m_lines.front().times[Normal] < m_times[Normal] - backtrace_time) {
                        const LineData& data = m_lines.front();
                        out_string += data.line;
                        m_size -= data.line.length();
                        m_lines.pop_front();
#ifndef NDEBUG
                        m_statistics.remove_line();
#endif // NDEBUG
                    }
                }
                else {
                    if (m_size > 65535) {
                        while (!m_lines.empty()) {
                            out_string += m_lines.front().line;
                            m_lines.pop_front();
                        }
                        m_size = 0;
#ifndef NDEBUG
                        m_statistics.remove_all_lines();
#endif // NDEBUG
                    }
                }
            }

            if (m_binarizer.is_enabled()) {
                if (m_binarizer.append_gcode(out_string) != bgcode::core::EResult::Success)
                    throw Slic3r::RuntimeError("Error while sending gcode to the binarizer.");
            }
            else {
                write_to_file(out, out_string, result, out_path);
                update_lines_ends_and_out_file_pos(out_string, result.lines_ends.front(), &m_out_file_pos);
            }
        }

        // flush the current content of the cache to file
        void flush(FilePtr& out, GCodeProcessorResult& result, const std::string& out_path) {
            // collect lines to flush into a single string
            std::string out_string;
            while (!m_lines.empty()) {
                out_string += m_lines.front().line;
                m_lines.pop_front();
            }
            m_size = 0;
#ifndef NDEBUG
            m_statistics.remove_all_lines();
#endif // NDEBUG

            if (m_binarizer.is_enabled()) {
                if (m_binarizer.append_gcode(out_string) != bgcode::core::EResult::Success)
                    throw Slic3r::RuntimeError("Error while sending gcode to the binarizer.");
            }
            else {
                write_to_file(out, out_string, result, out_path);
                update_lines_ends_and_out_file_pos(out_string, result.lines_ends.front(), &m_out_file_pos);
            }
        }

        void synchronize_moves(GCodeProcessorResult& result) const {
            auto it = m_gcode_lines_map.begin();
            for (GCodeProcessorResult::MoveVertex& move : result.moves) {
                while (it != m_gcode_lines_map.end() && it->first < move.gcode_id) {
                    ++it;
                }
                if (it != m_gcode_lines_map.end() && it->first == move.gcode_id)
                    move.gcode_id = it->second;
            }
        }

        size_t get_size() const { return m_size; }

    private:
        void write_to_file(FilePtr& out, const std::string& out_string, GCodeProcessorResult& result, const std::string& out_path) {
            if (!out_string.empty()) {
                if (!m_binarizer.is_enabled()) {
                    fwrite((const void*)out_string.c_str(), 1, out_string.length(), out.f);
                    if (ferror(out.f)) {
                        out.close();
                        boost::nowide::remove(out_path.c_str());
                        throw Slic3r::RuntimeError("GCode processor post process export failed.\nIs the disk full?");
                    }
                }
            }
        }
    };

    ExportLines export_lines(m_binarizer, m_result.backtrace_enabled ? ExportLines::EWriteType::ByTime : ExportLines::EWriteType::BySize,
        m_time_processor.machines);

    // replace placeholder lines with the proper final value
    // gcode_line is in/out parameter, to reduce expensive memory allocation
    auto process_placeholders = [&](std::string& gcode_line) {
        bool processed = false;

        // remove trailing '\n'
        auto line = std::string_view(gcode_line).substr(0, gcode_line.length() - 1);

        if (line.length() > 1) {
            line = line.substr(1);
            if (m_time_processor.export_remaining_time_enabled &&
                (line == reserved_tag(ETags::First_Line_M73_Placeholder) || line == reserved_tag(ETags::Last_Line_M73_Placeholder))) {
                for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
                    const TimeMachine& machine = m_time_processor.machines[i];
                    if (machine.enabled) {
                        // export pair <percent, remaining time>
                        export_lines.append_line(format_line_M73_main(machine.line_m73_main_mask.c_str(),
                            (line == reserved_tag(ETags::First_Line_M73_Placeholder)) ? 0 : 100,
                            (line == reserved_tag(ETags::First_Line_M73_Placeholder)) ? time_in_minutes(machine.time) : 0));
                        processed = true;

                        // export remaining time to next printer stop
                        if (line == reserved_tag(ETags::First_Line_M73_Placeholder) && !machine.stop_times.empty()) {
                            const int to_export_stop = time_in_minutes(machine.stop_times.front().elapsed_time);
                            export_lines.append_line(format_line_M73_stop_int(machine.line_m73_stop_mask.c_str(), to_export_stop));
                            last_exported_stop[i] = to_export_stop;
                        }
                    }
                }
            }
            else if (line == reserved_tag(ETags::Estimated_Printing_Time_Placeholder)) {
                for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
                    const TimeMachine& machine = m_time_processor.machines[i];
                    PrintEstimatedStatistics::ETimeMode mode = static_cast<PrintEstimatedStatistics::ETimeMode>(i);
                    if (mode == PrintEstimatedStatistics::ETimeMode::Normal || machine.enabled) {
                        char buf[128];
                        sprintf(buf, "; estimated printing time (%s mode) = %s\n",
                            (mode == PrintEstimatedStatistics::ETimeMode::Normal) ? "normal" : "silent",
                            get_time_dhms(machine.time).c_str());
                        export_lines.append_line(buf);
                        processed = true;
                    }
                }
                for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
                    const TimeMachine& machine = m_time_processor.machines[i];
                    PrintEstimatedStatistics::ETimeMode mode = static_cast<PrintEstimatedStatistics::ETimeMode>(i);
                    if (mode == PrintEstimatedStatistics::ETimeMode::Normal || machine.enabled) {
                        char buf[128];
                        sprintf(buf, "; estimated first layer printing time (%s mode) = %s\n",
                            (mode == PrintEstimatedStatistics::ETimeMode::Normal) ? "normal" : "silent",
                            get_time_dhms(machine.first_layer_time).c_str());
                        export_lines.append_line(buf);
                        processed = true;
                    }
                }
            }
        }

        return processed;
    };

    auto process_used_filament = [&](std::string& gcode_line) {
        // Prefilter for parsing speed.
        if (gcode_line.size() < 8 || gcode_line[0] != ';' || gcode_line[1] != ' ')
            return false;
        if (const char c = gcode_line[2]; c != 'f' && c != 't')
            return false;
        auto process_tag = [](std::string& gcode_line, const std::string_view tag, const std::vector<double>& values) {
            if (boost::algorithm::starts_with(gcode_line, tag)) {
                gcode_line = tag;
                char buf[1024];
                for (size_t i = 0; i < values.size(); ++i) {
                    sprintf(buf, i == values.size() - 1 ? " %.2lf\n" : " %.2lf,", values[i]);
                    gcode_line += buf;
                }
                return true;
            }
            return false;
        };

        bool ret = false;
        ret |= process_tag(gcode_line, PrintStatistics::FilamentUsedMmMask, filament_mm);
        ret |= process_tag(gcode_line, PrintStatistics::FilamentUsedGMask, filament_g);
        ret |= process_tag(gcode_line, PrintStatistics::TotalFilamentUsedGMask, { filament_total_g });
        ret |= process_tag(gcode_line, PrintStatistics::FilamentUsedCm3Mask, filament_cm3);
        ret |= process_tag(gcode_line, PrintStatistics::FilamentCostMask, filament_cost);
        ret |= process_tag(gcode_line, PrintStatistics::TotalFilamentCostMask, { filament_total_cost });
        return ret;
    };

    // check for temporary lines
    auto is_temporary_decoration = [](const std::string_view gcode_line) {
        // remove trailing '\n'
        assert(!gcode_line.empty());
        assert(gcode_line.back() == '\n');

        // return true for decorations which are used in processing the gcode but that should not be exported into the final gcode
        // i.e.:
        // bool ret = gcode_line.substr(0, gcode_line.length() - 1) == ";" + Layer_Change_Tag;
        // ...
        // return ret;
        return false;
    };

    // Iterators for the normal and silent cached time estimate entry recently processed, used by process_line_G1.
    auto g1_times_cache_it = Slic3r::reserve_vector<std::vector<TimeMachine::G1LinesCacheItem>::const_iterator>(m_time_processor.machines.size());
    for (const auto& machine : m_time_processor.machines)
        g1_times_cache_it.emplace_back(machine.g1_times_cache.begin());

    // add lines M73 to exported gcode
    auto process_line_G1 = [this,
        // Lambdas, mostly for string formatting, all with an empty capture block.
        time_in_minutes, format_time_float, format_line_M73_main, format_line_M73_stop_int, format_line_M73_stop_float, time_in_last_minute,
        // Caches, to be modified
        &g1_times_cache_it, &last_exported_main, &last_exported_stop,
        &export_lines]
        (const size_t g1_lines_counter) {
        if (m_time_processor.export_remaining_time_enabled) {
            for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
                const TimeMachine& machine = m_time_processor.machines[i];
                if (machine.enabled) {
                    // export pair <percent, remaining time>
                    // Skip all machine.g1_times_cache below g1_lines_counter.
                    auto& it = g1_times_cache_it[i];
                    while (it != machine.g1_times_cache.end() && it->id < g1_lines_counter)
                        ++it;
                    if (it != machine.g1_times_cache.end() && it->id == g1_lines_counter) {
                        std::pair<int, int> to_export_main = { int(100.0f * it->elapsed_time / machine.time),
                                                                time_in_minutes(machine.time - it->elapsed_time) };
                        if (last_exported_main[i] != to_export_main) {
                            export_lines.append_line(format_line_M73_main(machine.line_m73_main_mask.c_str(),
                                to_export_main.first, to_export_main.second));
                            last_exported_main[i] = to_export_main;
                        }
                        // export remaining time to next printer stop
                        auto it_stop = std::upper_bound(machine.stop_times.begin(), machine.stop_times.end(), it->elapsed_time,
                            [](float value, const TimeMachine::StopTime& t) { return value < t.elapsed_time; });
                        if (it_stop != machine.stop_times.end()) {
                            int to_export_stop = time_in_minutes(it_stop->elapsed_time - it->elapsed_time);
                            if (last_exported_stop[i] != to_export_stop) {
                                if (to_export_stop > 0) {
                                    if (last_exported_stop[i] != to_export_stop) {
                                        export_lines.append_line(format_line_M73_stop_int(machine.line_m73_stop_mask.c_str(), to_export_stop));
                                        last_exported_stop[i] = to_export_stop;
                                    }
                                }
                                else {
                                    bool is_last = false;
                                    auto next_it = it + 1;
                                    is_last |= (next_it == machine.g1_times_cache.end());

                                    if (next_it != machine.g1_times_cache.end()) {
                                        auto next_it_stop = std::upper_bound(machine.stop_times.begin(), machine.stop_times.end(), next_it->elapsed_time,
                                            [](float value, const TimeMachine::StopTime& t) { return value < t.elapsed_time; });
                                        is_last |= (next_it_stop != it_stop);

                                        std::string time_float_str = format_time_float(time_in_last_minute(it_stop->elapsed_time - it->elapsed_time));
                                        std::string next_time_float_str = format_time_float(time_in_last_minute(it_stop->elapsed_time - next_it->elapsed_time));
                                        is_last |= (string_to_double_decimal_point(time_float_str) > 0. && string_to_double_decimal_point(next_time_float_str) == 0.);
                                    }

                                    if (is_last) {
                                        if (std::distance(machine.stop_times.begin(), it_stop) == static_cast<ptrdiff_t>(machine.stop_times.size() - 1))
                                            export_lines.append_line(format_line_M73_stop_int(machine.line_m73_stop_mask.c_str(), to_export_stop));
                                        else
                                            export_lines.append_line(format_line_M73_stop_float(machine.line_m73_stop_mask.c_str(), time_in_last_minute(it_stop->elapsed_time - it->elapsed_time)));

                                        last_exported_stop[i] = to_export_stop;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    };

    // add lines M104 to exported gcode
    auto process_line_T = [this, &export_lines](const std::string& gcode_line, const size_t g1_lines_counter, const ExportLines::Backtrace& backtrace) {
        const std::string cmd = GCodeReader::GCodeLine::extract_cmd(gcode_line);
        if (cmd.size() >= 2) {
            std::stringstream ss(cmd.substr(1));
            int tool_number = -1;
            ss >> tool_number;
            if (tool_number != -1) {
                if (tool_number < 0 || (int)m_extruder_temps_config.size() <= tool_number) {
                    // found an invalid value, clamp it to a valid one
                    tool_number = std::clamp<int>(0, m_extruder_temps_config.size() - 1, tool_number);
                    // emit warning
                    std::string warning = _u8L("GCode Post-Processor encountered an invalid toolchange, maybe from a custom gcode:");
                    warning += "\n> ";
                    warning += gcode_line;
                    warning += _u8L("Generated M104 lines may be incorrect.");
                    BOOST_LOG_TRIVIAL(error) << warning;
                    if (m_print != nullptr)
                        m_print->active_step_add_warning(PrintStateBase::WarningLevel::CRITICAL, warning);
                }
            }
            export_lines.insert_lines(backtrace, cmd,
                // line inserter
                [tool_number, this](unsigned int id, const std::vector<float>& time_diffs) {
                    const int temperature = int(m_layer_id != 1 ? m_extruder_temps_config[tool_number] : m_extruder_temps_first_layer_config[tool_number]);
                    std::string out = "M104.1 T" + std::to_string(tool_number);
                    if (time_diffs.size() > 0)
                        out += " P" + std::to_string(int(std::round(time_diffs[0])));
                    if (time_diffs.size() > 1)
                        out += " Q" + std::to_string(int(std::round(time_diffs[1])));
                    out += " S" + std::to_string(temperature) + "\n";
                    return out;
                },
                // line replacer
                [this, tool_number](const std::string& line) {
                    if (GCodeReader::GCodeLine::cmd_is(line, "M104")) {
                        GCodeReader::GCodeLine gline;
                        GCodeReader reader;
                        reader.parse_line(line, [&gline](GCodeReader& reader, const GCodeReader::GCodeLine& l) { gline = l; });

                        float val;
                        if (gline.has_value('T', val) && gline.raw().find("cooldown") != std::string::npos && m_is_XL_printer) {
                            if (static_cast<int>(val) == tool_number)
                                return std::string("; removed M104\n");
                        }
                    }
                    return line;
                });
        }
    };

    m_result.lines_ends.clear();
    m_result.lines_ends.emplace_back(std::vector<size_t>());

    unsigned int line_id = 0;
    // Backtrace data for Tx gcode lines
    static const ExportLines::Backtrace backtrace_T = { 120.0f, 10 };
    // In case there are multiple sources of backtracing, keeps track of the longest backtrack time needed
    // to flush the backtrace cache accordingly
    float max_backtrace_time = 120.0f;

    {
        // Read the input stream 64kB at a time, extract lines and process them.
        std::vector<char> buffer(65536 * 10, 0);
        // Line buffer.
        assert(gcode_line.empty());
        for (;;) {
            size_t cnt_read = ::fread(buffer.data(), 1, buffer.size(), in.f);
            if (::ferror(in.f))
                throw Slic3r::RuntimeError(std::string("GCode processor post process export failed.\nError while reading from file.\n"));
            bool eof = cnt_read == 0;
            auto it = buffer.begin();
            auto it_bufend = buffer.begin() + cnt_read;
            while (it != it_bufend || (eof && !gcode_line.empty())) {
                // Find end of line.
                bool eol = false;
                auto it_end = it;
                for (; it_end != it_bufend && !(eol = *it_end == '\r' || *it_end == '\n'); ++it_end);
                // End of line is indicated also if end of file was reached.
                eol |= eof && it_end == it_bufend;
                gcode_line.insert(gcode_line.end(), it, it_end);
                if (eol) {
                    ++line_id;
                    gcode_line += "\n";
                    const unsigned int internal_g1_lines_counter = export_lines.update(gcode_line, line_id, g1_lines_counter);
                    // replace placeholder lines
                    bool processed = process_placeholders(gcode_line);
                    if (processed)
                        gcode_line.clear();
                    if (!processed)
                        processed = process_used_filament(gcode_line);
                    if (!processed && !is_temporary_decoration(gcode_line)) {
                        if (GCodeReader::GCodeLine::cmd_is(gcode_line, "G0") || GCodeReader::GCodeLine::cmd_is(gcode_line, "G1")) {
                            export_lines.append_line(gcode_line);
                            // add lines M73 where needed
                            process_line_G1(g1_lines_counter++);
                            gcode_line.clear();
                        }
                        else if (GCodeReader::GCodeLine::cmd_is(gcode_line, "G2") || GCodeReader::GCodeLine::cmd_is(gcode_line, "G3")) {
                            export_lines.append_line(gcode_line);
                            // add lines M73 where needed
                            process_line_G1(g1_lines_counter + internal_g1_lines_counter);
                            g1_lines_counter += (1 + internal_g1_lines_counter);
                            gcode_line.clear();
                        }
                        else if (GCodeReader::GCodeLine::cmd_is(gcode_line, "G28")) {
                            ++g1_lines_counter;
                        }
                        else if (m_result.backtrace_enabled && GCodeReader::GCodeLine::cmd_starts_with(gcode_line, "T")) {
                            // add lines M104 where needed
                            process_line_T(gcode_line, g1_lines_counter, backtrace_T);
                            max_backtrace_time = std::max(max_backtrace_time, backtrace_T.time);
                        }
                    }

                    if (!gcode_line.empty())
                        export_lines.append_line(gcode_line);
                    export_lines.write(out, 1.1f * max_backtrace_time, m_result, out_path);
                    gcode_line.clear();
                }
                // Skip EOL.
                it = it_end;
                if (it != it_bufend && *it == '\r')
                    ++it;
                if (it != it_bufend && *it == '\n')
                    ++it;
            }
            if (eof)
                break;
        }
    }

    export_lines.flush(out, m_result, out_path);

    if (m_binarizer.is_enabled()) {
        if (m_binarizer.finalize() != bgcode::core::EResult::Success)
            throw Slic3r::RuntimeError("Error while finalizing the gcode binarizer.");
    }

    out.close();
    in.close();

    const std::string result_filename = m_result.filename;
    if (m_binarizer.is_enabled()) {
        // The list of lines in the binary gcode is different from the original one.
        // This requires to re-process the binarized file to be able to synchronize with it all the data needed by the preview,
        // as gcode window, tool position and moves slider which relies on indexing the gcode lines.
        reset();
        // the following call modifies m_result.filename
        process_binary_file(out_path);
        // restore the proper filename
        m_result.filename = result_filename;
    }
    else
        export_lines.synchronize_moves(m_result);

    if (rename_file(out_path, result_filename))
        throw Slic3r::RuntimeError(std::string("Failed to rename the output G-code file from ") + out_path + " to " + result_filename + '\n' +
            "Is " + out_path + " locked?" + '\n');
}

void GCodeProcessor::store_move_vertex(EMoveType type, bool internal_only)
{
    m_last_line_id = (type == EMoveType::Color_change || type == EMoveType::Pause_Print || type == EMoveType::Custom_GCode) ?
        m_line_id + 1 :
        ((type == EMoveType::Seam) ? m_last_line_id : m_line_id);

    m_result.moves.push_back({
        m_last_line_id,
        type,
        m_extrusion_role,
        m_extruder_id,
        m_cp_color.current,
        Vec3f(m_end_position[X], m_end_position[Y], m_end_position[Z] - m_z_offset) + m_extruder_offsets[m_extruder_id],
        static_cast<float>(m_end_position[E] - m_start_position[E]),
        m_feedrate,
        0.0f, // actual feedrate
        m_width,
        m_height,
        m_mm3_per_mm,
        m_fan_speed,
        m_extruder_temps[m_extruder_id],
        { 0.0f, 0.0f }, // time
        std::max<unsigned int>(1, m_layer_id) - 1,
        internal_only
    });

    // stores stop time placeholders for later use
    if (type == EMoveType::Color_change || type == EMoveType::Pause_Print) {
        for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
            TimeMachine& machine = m_time_processor.machines[i];
            if (!machine.enabled)
                continue;

            machine.stop_times.push_back({ m_g1_line_id, 0.0f });
        }
    }
}

void GCodeProcessor::set_extrusion_role(GCodeExtrusionRole role)
{
    m_used_filaments.process_role_cache(this);
    m_extrusion_role = role;
}

float GCodeProcessor::minimum_feedrate(PrintEstimatedStatistics::ETimeMode mode, float feedrate) const
{
    if (m_time_processor.machine_limits.machine_min_extruding_rate.empty())
        return feedrate;

    return std::max(feedrate, get_option_value(m_time_processor.machine_limits.machine_min_extruding_rate, static_cast<size_t>(mode)));
}

float GCodeProcessor::minimum_travel_feedrate(PrintEstimatedStatistics::ETimeMode mode, float feedrate) const
{
    if (m_time_processor.machine_limits.machine_min_travel_rate.empty())
        return feedrate;

    return std::max(feedrate, get_option_value(m_time_processor.machine_limits.machine_min_travel_rate, static_cast<size_t>(mode)));
}

float GCodeProcessor::get_axis_max_feedrate(PrintEstimatedStatistics::ETimeMode mode, Axis axis) const
{
    switch (axis)
    {
    case X: { return get_option_value(m_time_processor.machine_limits.machine_max_feedrate_x, static_cast<size_t>(mode)); }
    case Y: { return get_option_value(m_time_processor.machine_limits.machine_max_feedrate_y, static_cast<size_t>(mode)); }
    case Z: { return get_option_value(m_time_processor.machine_limits.machine_max_feedrate_z, static_cast<size_t>(mode)); }
    case E: { return get_option_value(m_time_processor.machine_limits.machine_max_feedrate_e, static_cast<size_t>(mode)); }
    default: { return 0.0f; }
    }
}

float GCodeProcessor::get_axis_max_acceleration(PrintEstimatedStatistics::ETimeMode mode, Axis axis) const
{
    switch (axis)
    {
    case X: { return get_option_value(m_time_processor.machine_limits.machine_max_acceleration_x, static_cast<size_t>(mode)); }
    case Y: { return get_option_value(m_time_processor.machine_limits.machine_max_acceleration_y, static_cast<size_t>(mode)); }
    case Z: { return get_option_value(m_time_processor.machine_limits.machine_max_acceleration_z, static_cast<size_t>(mode)); }
    case E: { return get_option_value(m_time_processor.machine_limits.machine_max_acceleration_e, static_cast<size_t>(mode)); }
    default: { return 0.0f; }
    }
}

float GCodeProcessor::get_axis_max_jerk(PrintEstimatedStatistics::ETimeMode mode, Axis axis) const
{
    switch (axis)
    {
    case X: { return get_option_value(m_time_processor.machine_limits.machine_max_jerk_x, static_cast<size_t>(mode)); }
    case Y: { return get_option_value(m_time_processor.machine_limits.machine_max_jerk_y, static_cast<size_t>(mode)); }
    case Z: { return get_option_value(m_time_processor.machine_limits.machine_max_jerk_z, static_cast<size_t>(mode)); }
    case E: { return get_option_value(m_time_processor.machine_limits.machine_max_jerk_e, static_cast<size_t>(mode)); }
    default: { return 0.0f; }
    }
}

float GCodeProcessor::get_retract_acceleration(PrintEstimatedStatistics::ETimeMode mode) const
{
    size_t id = static_cast<size_t>(mode);
    return (id < m_time_processor.machines.size()) ? m_time_processor.machines[id].retract_acceleration : DEFAULT_RETRACT_ACCELERATION;
}

void GCodeProcessor::set_retract_acceleration(PrintEstimatedStatistics::ETimeMode mode, float value)
{
    size_t id = static_cast<size_t>(mode);
    if (id < m_time_processor.machines.size()) {
        m_time_processor.machines[id].retract_acceleration = (m_time_processor.machines[id].max_retract_acceleration == 0.0f) ? value :
            // Clamp the acceleration with the maximum.
            std::min(value, m_time_processor.machines[id].max_retract_acceleration);
    }
}

float GCodeProcessor::get_acceleration(PrintEstimatedStatistics::ETimeMode mode) const
{
    size_t id = static_cast<size_t>(mode);
    return (id < m_time_processor.machines.size()) ? m_time_processor.machines[id].acceleration : DEFAULT_ACCELERATION;
}

void GCodeProcessor::set_acceleration(PrintEstimatedStatistics::ETimeMode mode, float value)
{
    size_t id = static_cast<size_t>(mode);
    if (id < m_time_processor.machines.size()) {
        m_time_processor.machines[id].acceleration = (m_time_processor.machines[id].max_acceleration == 0.0f) ? value :
            // Clamp the acceleration with the maximum.
            std::min(value, m_time_processor.machines[id].max_acceleration);
    }
}

float GCodeProcessor::get_travel_acceleration(PrintEstimatedStatistics::ETimeMode mode) const
{
    size_t id = static_cast<size_t>(mode);
    return (id < m_time_processor.machines.size()) ? m_time_processor.machines[id].travel_acceleration : DEFAULT_TRAVEL_ACCELERATION;
}

void GCodeProcessor::set_travel_acceleration(PrintEstimatedStatistics::ETimeMode mode, float value)
{
    size_t id = static_cast<size_t>(mode);
    if (id < m_time_processor.machines.size()) {
        m_time_processor.machines[id].travel_acceleration = (m_time_processor.machines[id].max_travel_acceleration == 0.0f) ? value :
            // Clamp the acceleration with the maximum.
            std::min(value, m_time_processor.machines[id].max_travel_acceleration);
    }
}

float GCodeProcessor::get_filament_load_time(size_t extruder_id)
{
    if (m_is_XL_printer)
        return 4.5f; // FIXME
    return (m_time_processor.filament_load_times.empty() || m_time_processor.extruder_unloaded) ?
        0.0f :
        ((extruder_id < m_time_processor.filament_load_times.size()) ?
            m_time_processor.filament_load_times[extruder_id] : m_time_processor.filament_load_times.front());
}

float GCodeProcessor::get_filament_unload_time(size_t extruder_id)
{
    if (m_is_XL_printer)
        return 0.f; // FIXME
    return (m_time_processor.filament_unload_times.empty() || m_time_processor.extruder_unloaded) ?
        0.0f :
        ((extruder_id < m_time_processor.filament_unload_times.size()) ?
            m_time_processor.filament_unload_times[extruder_id] : m_time_processor.filament_unload_times.front());
}

void GCodeProcessor::process_custom_gcode_time(CustomGCode::Type code)
{
    //FIXME this simulates st_synchronize! is it correct?
    // The estimated time may be longer than the real print time.
    simulate_st_synchronize();
    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        TimeMachine& machine = m_time_processor.machines[i];
        if (!machine.enabled)
            continue;

        TimeMachine::CustomGCodeTime& gcode_time = machine.gcode_time;
        gcode_time.needed = true;
        if (gcode_time.cache != 0.0f) {
            gcode_time.times.push_back({ code, gcode_time.cache });
            gcode_time.cache = 0.0f;
        }
    }
}

void GCodeProcessor::process_filaments(CustomGCode::Type code)
{
    if (code == CustomGCode::ColorChange)
        m_used_filaments.process_color_change_cache();

    if (code == CustomGCode::ToolChange)
        m_used_filaments.process_extruder_cache(m_extruder_id);
}

void GCodeProcessor::calculate_time(GCodeProcessorResult& result, size_t keep_last_n_blocks, float additional_time)
{
    // calculate times
    std::vector<TimeMachine::ActualSpeedMove> actual_speed_moves;
    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        TimeMachine& machine = m_time_processor.machines[i];
        machine.calculate_time(m_result, static_cast<PrintEstimatedStatistics::ETimeMode>(i), keep_last_n_blocks, additional_time);
        if (static_cast<PrintEstimatedStatistics::ETimeMode>(i) == PrintEstimatedStatistics::ETimeMode::Normal)
            actual_speed_moves = std::move(machine.actual_speed_moves);
    }

    // insert actual speed moves into the move list. We will do this in two stages (to avoid inserting in the middle of
    // result.moves repeatedly). First, we create individual vectors of MoveVertices, and store them along with their
    // required index in the result.moves vector after they are all inserted. Then we go through the destination
    // vector once and move all the elements where we want them in one go.
    std::vector<std::pair<size_t, std::vector<GCodeProcessorResult::MoveVertex>>> moves_to_insert = {std::make_pair(0, std::vector<GCodeProcessorResult::MoveVertex>{})};
    size_t inserted_count = 0;
    std::map<unsigned int, unsigned int> id_map;
    for (auto it = actual_speed_moves.begin(); it != actual_speed_moves.end(); ++it) {
        const unsigned int base_id_old = it->move_id;
        if (it->position.has_value()) {
            // insert actual speed move into the move list
            // clone from existing move
            GCodeProcessorResult::MoveVertex new_move = result.moves[base_id_old];
            // override modified parameters
            new_move.time = { 0.0f, 0.0f };
            new_move.position = *it->position;
            new_move.actual_feedrate = it->actual_feedrate;
            new_move.delta_extruder = *it->delta_extruder;
            new_move.feedrate = *it->feedrate;
            new_move.width = *it->width;
            new_move.height = *it->height;
            new_move.mm3_per_mm = *it->mm3_per_mm;
            new_move.fan_speed = *it->fan_speed;
            new_move.temperature = *it->temperature;
            new_move.internal_only = true;
            moves_to_insert.back().second.emplace_back(new_move);
        }
        else {
            moves_to_insert.back().first = base_id_old + inserted_count; // Save required position of this range in the NEW vector.
            id_map[base_id_old]          = base_id_old + inserted_count; // Remember where the old element will end up.
            inserted_count += moves_to_insert.back().second.size();      // Increase the number of moves that are already planned to be added.

            result.moves[base_id_old].actual_feedrate = it->actual_feedrate; // update move actual speed
            
            // synchronize seams actual speed
            if (base_id_old + 1 < result.moves.size()) {
                GCodeProcessorResult::MoveVertex& move = result.moves[base_id_old + 1];
                if (move.type == EMoveType::Seam)
                    move.actual_feedrate = it->actual_feedrate;
            }
            moves_to_insert.emplace_back(std::make_pair(0, std::vector<GCodeProcessorResult::MoveVertex>{}));
        }
    }

    // Now actually do the insertion of the ranges into the destination vector.
    std::vector<GCodeProcessorResult::MoveVertex>& m = result.moves;
    size_t offset = inserted_count;    
    m.resize(m.size() + offset); // grow the vector to its final size   
    size_t last_pos = m.size() - 1;  // index of the last element that still needs to be moved
    for (auto it = moves_to_insert.rbegin(); it != moves_to_insert.rend(); ++it) {
        const auto& [new_pos, new_moves] = *it;
        if (new_moves.empty())
            continue;
        for (int i = last_pos; i >= new_pos + new_moves.size(); --i) // Move the elements to their final place.
            m[i] = m[i - offset];
        std::copy(new_moves.begin(), new_moves.end(), m.begin() + new_pos);
        last_pos = new_pos - 1;
        offset -= new_moves.size();
    }
    assert(offset == 0);

    // synchronize blocks' move_ids with after moves for actual speed insertion
    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        for (GCodeProcessor::TimeBlock& block : m_time_processor.machines[i].blocks) {
            auto it = id_map.find(block.move_id);
            block.move_id = (it != id_map.end()) ? it->second : block.move_id + inserted_count;
        }
    }
}

void GCodeProcessor::simulate_st_synchronize(float additional_time)
{
    calculate_time(m_result, 0, additional_time);
}

void GCodeProcessor::update_estimated_statistics()
{
    auto update_mode = [this](PrintEstimatedStatistics::ETimeMode mode) {
        PrintEstimatedStatistics::Mode& data = m_result.print_statistics.modes[static_cast<size_t>(mode)];
        data.time = get_time(mode);
        data.custom_gcode_times = get_custom_gcode_times(mode, true);
    };

    update_mode(PrintEstimatedStatistics::ETimeMode::Normal);
    if (m_time_processor.machines[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Stealth)].enabled)
        update_mode(PrintEstimatedStatistics::ETimeMode::Stealth);
    else
        m_result.print_statistics.modes[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Stealth)].reset();

    m_result.print_statistics.volumes_per_color_change  = m_used_filaments.volumes_per_color_change;
    m_result.print_statistics.volumes_per_extruder      = m_used_filaments.volumes_per_extruder;
    m_result.print_statistics.used_filaments_per_role   = m_used_filaments.filaments_per_role;
}

double GCodeProcessor::extract_absolute_position_on_axis(Axis axis, const GCodeReader::GCodeLine& line, double area_filament_cross_section)
{
    if (line.has(Slic3r::Axis(axis))) {
        bool is_relative = (m_global_positioning_type == EPositioningType::Relative);
        if (axis == E)
            is_relative |= (m_e_local_positioning_type == EPositioningType::Relative);

        const double lengthsScaleFactor = (m_units == EUnits::Inches) ? double(INCHES_TO_MM) : 1.0;
        double ret = line.value(Slic3r::Axis(axis)) * lengthsScaleFactor;
        if (axis == E && m_use_volumetric_e)
            ret /= area_filament_cross_section;
        return is_relative ? m_start_position[axis] + ret : m_origin[axis] + ret;
    }
    else
        return m_start_position[axis];
}

} /* namespace Slic3r */

