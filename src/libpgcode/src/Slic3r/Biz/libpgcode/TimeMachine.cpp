
#include "TimeMachine.hpp"
#include "Slic3r/Biz/libpgcode/ProcessorResult.hpp"
#include "Slic3r/Domain/Constants.hpp"

#include <assert.h>

namespace Slic3r::Biz::libpgcode {
using namespace Domain;

template <typename T, typename Number>
constexpr inline T lerp(const T& a, const T& b, Number t)
{
    assert((t >= Number(-EPSILON)) && (t <= Number(1) + Number(EPSILON)));
    return (Number(1) - t) * a + t * b;
}

float max_allowable_speed(float acceleration, float target_velocity, float distance)
{
    // to avoid invalid negative numbers due to numerical errors 
    float value = std::max(0.0f, target_velocity * target_velocity - 2.0f * acceleration * distance);
    return std::sqrt(value);
}

void CustomGCodeTime::reset()
{
    needed = false;
    cache  = 0.0f;
    times.clear();
}

void TimeMachineState::reset()
{
    feedrate          = 0.0f;
    safe_feedrate     = 0.0f;
    axis_feedrate     = Vec4f::Zero();
    abs_axis_feedrate = Vec4f::Zero();
}

static void planner_forward_pass_kernel(const TimeBlock& prev, TimeBlock& curr)
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

static void planner_reverse_pass_kernel(TimeBlock& curr, const TimeBlock& next)
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

static void recalculate_trapezoids(TimeBlocks& blocks)
{
    TimeBlock* curr = nullptr;
    TimeBlock* next = nullptr;

    for (size_t i = 0; i < blocks.size(); ++i) {
        TimeBlock& b = blocks[i];

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

void TimeMachine::calculate_time(ProcessorResult& result, TimeMode mode, size_t keep_last_n_blocks, float additional_time)
{
    if (!enabled || blocks.size() < 2)
        return;

    assert(keep_last_n_blocks <= blocks.size());

    // reverse_pass
    for (size_t i = blocks.size() - 1; i >= 1; --i) {
        planner_reverse_pass_kernel(blocks[i - 1], blocks[i]);
    }

    // forward_pass
    for (size_t i = 0; i + 1 < blocks.size(); ++i) {
        planner_forward_pass_kernel(blocks[i], blocks[i + 1]);
    }

    recalculate_trapezoids(blocks);

    size_t n_blocks_process = blocks.size() - keep_last_n_blocks;
    for (size_t i = 0; i < n_blocks_process; ++i) {
        const TimeBlock& block = blocks[i];
        float block_time = block.time();
        if (i + 1 == n_blocks_process) {
            block_time += additional_time;
        }

        time += double(block_time);
        result.moves()[block.move_id].time[size_t(mode)] = block_time;
        gcode_time.cache += block_time;
        if (block.layer_id == 1)
            first_layer_time += block_time;

        // detect actual speed moves required to render toolpaths using actual speed
        if (mode == TimeMode::Normal) {
            MoveVertex& curr_move = result.moves()[block.move_id];
            if (curr_move.type == MoveType::Extrude ||
                curr_move.type == MoveType::Travel ||
                curr_move.type == MoveType::Wipe) {
                MoveVertex& prev_move = result.moves()[block.move_id - 1];
                const bool interpolate = (prev_move.type == curr_move.type);
                if (!interpolate &&
                    prev_move.type != MoveType::Extrude &&
                    prev_move.type != MoveType::Travel &&
                    prev_move.type != MoveType::Wipe)
                    prev_move.actual_feedrate = block.feedrate_profile.entry;

                if (float(EPSILON) < block.trapezoid.accelerate_until &&
                    block.trapezoid.accelerate_until < block.distance - float(EPSILON)) {
                    float t = block.trapezoid.accelerate_until / block.distance;
                    Vec3f position = lerp(prev_move.position, curr_move.position, t);
                    if ((position - prev_move.position).norm() > float(EPSILON) &&
                        (position - curr_move.position).norm() > float(EPSILON)) {
                        ActualSpeedMove move;
                        move.move_id         = block.move_id;
                        move.actual_feedrate = block.trapezoid.cruise_feedrate;
                        move.position        = position;
                        move.delta_extruder  = interpolate ? lerp(prev_move.delta_extruder, curr_move.delta_extruder, t) : curr_move.delta_extruder;
                        move.feedrate        = interpolate ? lerp(prev_move.feedrate, curr_move.feedrate, t) : curr_move.feedrate;
                        move.width           = interpolate ? lerp(prev_move.width, curr_move.width, t) : curr_move.width;
                        move.height          = interpolate ? lerp(prev_move.height, curr_move.height, t) : curr_move.height;
                        move.mm3_per_mm      = interpolate ? lerp(prev_move.mm3_per_mm, curr_move.mm3_per_mm, t) : curr_move.mm3_per_mm;
                        move.fan_speed       = interpolate ? lerp(prev_move.fan_speed, curr_move.fan_speed, t) : curr_move.fan_speed;
                        move.temperature     = interpolate ? lerp(prev_move.temperature, curr_move.temperature, t) : curr_move.temperature;

                        actual_speed_moves.emplace_back(move);
                    }
                }

                bool has_deceleration = block.trapezoid.deceleration_distance(block.distance) > float(EPSILON);
                if (has_deceleration && block.trapezoid.decelerate_after > block.trapezoid.accelerate_until + float(EPSILON)) {
                    float t = block.trapezoid.decelerate_after / block.distance;
                    Vec3f position = lerp(prev_move.position, curr_move.position, t);
                    if ((position - prev_move.position).norm() > float(EPSILON) &&
                        (position - curr_move.position).norm() > float(EPSILON)) {
                        ActualSpeedMove move;
                        move.move_id         = block.move_id;
                        move.actual_feedrate = block.trapezoid.cruise_feedrate;
                        move.position        = position;
                        move.delta_extruder  = interpolate ? lerp(prev_move.delta_extruder, curr_move.delta_extruder, t) : curr_move.delta_extruder;
                        move.feedrate        = interpolate ? lerp(prev_move.feedrate, curr_move.feedrate, t) : curr_move.feedrate;
                        move.width           = interpolate ? lerp(prev_move.width, curr_move.width, t) : curr_move.width;
                        move.height          = interpolate ? lerp(prev_move.height, curr_move.height, t) : curr_move.height;
                        move.mm3_per_mm      = interpolate ? lerp(prev_move.mm3_per_mm, curr_move.mm3_per_mm, t) : curr_move.mm3_per_mm;
                        move.fan_speed       = interpolate ? lerp(prev_move.fan_speed, curr_move.fan_speed, t) : curr_move.fan_speed;
                        move.temperature     = interpolate ? lerp(prev_move.temperature, curr_move.temperature, t) : curr_move.temperature;

                        actual_speed_moves.emplace_back(move);
                    }
                }

                bool is_cruise_only = block.trapezoid.is_cruise_only(block.distance);
                ActualSpeedMove move;
                move.move_id         = block.move_id;
                move.actual_feedrate = (is_cruise_only || !has_deceleration) ? block.trapezoid.cruise_feedrate : block.feedrate_profile.exit;
                actual_speed_moves.emplace_back(move);
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
    }
    else
        blocks.clear();
}

void TimeMachine::set_acceleration(float value)
{
    // Clamp the acceleration with the maximum.
    acceleration = (max_acceleration == 0.0f) ? value : std::min(value, max_acceleration);
}

void TimeMachine::set_travel_acceleration(float value)
{
    // Clamp the acceleration with the maximum.
    travel_acceleration = (max_travel_acceleration == 0.0f) ? value : std::min(value, max_travel_acceleration);
}

void TimeMachine::set_retract_acceleration(float value)
{
    // Clamp the acceleration with the maximum.
    retract_acceleration = (max_retract_acceleration == 0.0f) ? value : std::min(value, max_retract_acceleration);
}

void TimeMachine::reset()
{
    enabled = false;
    extrude_factor_override_percentage = 1.0f;
    acceleration = 0.0f;
    max_acceleration = 0.0f;
    retract_acceleration = 0.0f;
    max_retract_acceleration = 0.0f;
    travel_acceleration = 0.0f;
    max_travel_acceleration = 0.0f;
    first_layer_time = 0.0f;
    time = 0.0;
    line_m73_main_mask.clear();
    line_m73_stop_mask.clear();
    blocks.clear();
    actual_speed_moves.clear();
    g1_times_cache.clear();
    stop_times.clear();
    gcode_time.reset();
    curr.reset();
    prev.reset();
}

} // namespace Slic3r::Biz::libpgcode
