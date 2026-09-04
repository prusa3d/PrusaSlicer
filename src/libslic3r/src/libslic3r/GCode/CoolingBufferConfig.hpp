#pragma once

#include "Slic3r/Domain/GCodeFlavor.hpp"
#include "libslic3r/ConfigViews.hpp"
#include "Slic3r/Domain/ConfigDefsFDM.hpp"

namespace Slic3r::Biz::Slicing {
struct CoolingBufferConfig
{
    explicit CoolingBufferConfig(const PrintConfigView& print_config);

    std::vector<int> bridge_fan_speed{};
    std::vector<bool> cooling{};
    std::vector<double> cooling_perimeter_transition_distance{};
    std::vector<Domain::CoolingSlowdownLogicType> cooling_slowdown_logic{};
    std::vector<int> disable_fan_first_layers{};
    std::vector<bool> fan_always_on{};
    std::vector<int> fan_below_layer_time{};
    std::vector<int> full_fan_speed_layer{};
    bool gcode_comments{};
    Domain::GCodeFlavor gcode_flavor{};
    std::vector<int> max_fan_speed{};
    std::vector<int> min_fan_speed{};
    std::vector<double> min_print_speed{};
    std::vector<int> slowdown_below_layer_time{};
    double travel_speed{};
    bool use_relative_e_distances{};
};
}
