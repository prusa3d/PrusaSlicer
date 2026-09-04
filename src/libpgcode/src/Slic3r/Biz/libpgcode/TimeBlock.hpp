#pragma once

#include "Slic3r/Biz/libpgcode/Types.hpp"

namespace Slic3r::Biz::libpgcode {

struct FeedrateProfile
{
    float entry{ 0.0f }; // mm/s
    float cruise{ 0.0f }; // mm/s
    float exit{ 0.0f }; // mm/s
};

struct Trapezoid
{
    float accelerate_until{ 0.0f }; // mm
    float decelerate_after{ 0.0f }; // mm
    float cruise_feedrate{ 0.0f }; // mm/sec

    float acceleration_time(float entry_feedrate, float acceleration) const;
    float cruise_time() const { return (cruise_feedrate != 0.0f) ? cruise_distance() / cruise_feedrate : 0.0f; }
    float deceleration_time(float distance, float acceleration) const;
    float acceleration_distance() const { return accelerate_until; }
    float cruise_distance() const { return decelerate_after - accelerate_until; }
    float deceleration_distance(float distance) const { return distance - decelerate_after; }
    bool is_cruise_only(float distance) const;
};

struct TimeBlock
{
    struct Flags
    {
        bool recalculate{ false };
        bool nominal_length{ false };
    };

    MoveType move_type{ MoveType::Noop };
    Domain::GCodeExtrusionRole role{ Domain::GCodeExtrusionRole::None };
    uint32_t move_id{ 0 };
    uint32_t g1_line_id{ 0 };
    uint32_t layer_id{ 0 };
    uint32_t remaining_internal_g1_lines{ 0 };
    float distance{ 0.0f }; // mm
    float acceleration{ 0.0f }; // mm/s^2
    float max_entry_speed{ 0.0f }; // mm/s
    float safe_feedrate{ 0.0f }; // mm/s
    Flags flags;
    FeedrateProfile feedrate_profile;
    Trapezoid trapezoid;

    void calculate_trapezoid();
    float time() const {
        return trapezoid.acceleration_time(feedrate_profile.entry, acceleration) +
            trapezoid.cruise_time() + trapezoid.deceleration_time(distance, acceleration);
    }
};

using TimeBlocks = std::vector<TimeBlock>;

} // namespace Slic3r::Biz::libpgcode
