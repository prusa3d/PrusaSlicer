#pragma once

#include "Slic3r/Biz/libpgcode/PostProcessorConfig.hpp"
#include "TimeBlock.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Domain/CustomGCode.hpp"

namespace Slic3r::Biz::libpgcode {

struct ProcessorResult;

struct ActualSpeedMove
{
    uint32_t move_id{ 0 };
    float actual_feedrate{ 0.0f };
    std::optional<Domain::Vec3f> position;
    std::optional<float> delta_extruder;
    std::optional<float> feedrate;
    std::optional<float> width;
    std::optional<float> height;
    std::optional<float> mm3_per_mm;
    std::optional<float> fan_speed;
    std::optional<float> temperature;
};

using ActualSpeedMoves = std::vector<ActualSpeedMove>;

struct CustomGCodeTime
{
    bool needed{ false };
    float cache{ 0.0f };
    std::vector<std::pair<Domain::CustomGCode::Type, float>> times;

    void reset();
};

struct TimeMachineState
{
    float feedrate{ 0.0f }; // mm/s
    float safe_feedrate{ 0.0f }; // mm/s
    Domain::Vec4f axis_feedrate{ Domain::Vec4f::Zero() }; // mm/s
    Domain::Vec4f abs_axis_feedrate{ Domain::Vec4f::Zero() }; // mm/s
    Domain::Vec4f jd_unit_vec;

    void reset();
};

struct TimeMachine
{
    bool enabled{ false };
    float extrude_factor_override_percentage{ 1.0f };
    float acceleration{ 0.0f }; // mm/s^2
    // hard limit for the acceleration, to which the firmware will clamp.
    float max_acceleration{ 0.0f }; // mm/s^2
    float retract_acceleration{ 0.0f }; // mm/s^2
    // hard limit for the acceleration, to which the firmware will clamp.
    float max_retract_acceleration{ 0.0f }; // mm/s^2
    float travel_acceleration{ 0.0f }; // mm/s^2
    // hard limit for the travel acceleration, to which the firmware will clamp.
    float max_travel_acceleration{ 0.0f }; // mm/s^2
    float first_layer_time{ 0.0f };
    // We accumulate total print time in doubles to reduce the loss of precision
    // while adding big floating numbers with small float numbers.
    double time{ 0.0 }; // s
    std::string line_m73_main_mask;
    std::string line_m73_stop_mask;
    TimeBlocks blocks;
    ActualSpeedMoves actual_speed_moves;
    G1LinesCacheItems g1_times_cache;
    StopTimes stop_times;
    CustomGCodeTime gcode_time;
    TimeMachineState curr;
    TimeMachineState prev;

    void calculate_time(ProcessorResult& result, TimeMode mode, size_t keep_last_n_blocks = 0, float additional_time = 0.0f);
    void set_acceleration(float value);
    void set_travel_acceleration(float value);
    void set_retract_acceleration(float value);
    void reset();
};

using TimeMachines = std::array<TimeMachine, TIME_MODES_COUNT>;

// Calculates the maximum allowable speed at this point when you must be able to reach target_velocity using the 
// acceleration within the allotted distance.
float max_allowable_speed(float acceleration, float target_velocity, float distance);

} // namespace Slic3r::Biz::libpgcode
