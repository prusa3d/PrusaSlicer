#pragma once

#include "Slic3r/Biz/libpgcode/Types.hpp"

namespace Slic3r::Biz::libpgcode {

struct G1LinesCacheItem
{
    uint32_t id{ 0 };
    uint32_t remaining_internal_g1_lines{ 0 };
    float elapsed_time{ 0.0f };
};

using G1LinesCacheItems = std::vector<G1LinesCacheItem>;

struct StopTime
{
    uint32_t g1_line_id{ 0 };
    float elapsed_time{ 0.0f };
};

using StopTimes = std::vector<StopTime>;

struct TimeMachineData
{
    bool enabled{ false };
    float time{ 0.0f };
    float first_layer_time{ 0.0f };
    std::string line_m73_main_mask;
    std::string line_m73_stop_mask;
    G1LinesCacheItems g1_times_cache;
    StopTimes stop_times;

    void reset();
};

struct PostProcessorConfig
{
    bool export_remaining_time_enabled{ false };
    bool do_M104_backtrace{ false };
    std::array<TimeMachineData, TIME_MODES_COUNT> time_machines;
    std::vector<int> extruder_temps_config;
    std::vector<int> extruder_temps_first_layer_config;

    void reset();
};

} // namespace Slic3r::Biz::libpgcode
