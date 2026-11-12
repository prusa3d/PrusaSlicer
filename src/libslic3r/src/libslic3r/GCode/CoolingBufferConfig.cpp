#include "libslic3r/GCode/CoolingBufferConfig.hpp"

namespace Slic3r::Biz::Slicing {
CoolingBufferConfig::CoolingBufferConfig(const PrintConfigView& print_config) :
    bridge_fan_speed{print_config.get<std::vector<int>>("bridge_fan_speed")},
    cooling{print_config.get<std::vector<bool>>("cooling")},
    cooling_perimeter_transition_distance{
        print_config.get<std::vector<double>>("cooling_perimeter_transition_distance")
    },
    cooling_slowdown_logic{
        print_config.get<std::vector<Domain::CoolingSlowdownLogicType>>("cooling_slowdown_logic")
    },
    disable_fan_first_layers{print_config.get<std::vector<int>>("disable_fan_first_layers")},
    fan_always_on{print_config.get<std::vector<bool>>("fan_always_on")},
    fan_below_layer_time{print_config.get<std::vector<int>>("fan_below_layer_time")},
    full_fan_speed_layer{print_config.get<std::vector<int>>("full_fan_speed_layer")},
    gcode_comments{print_config.get<bool>("gcode_comments")},
    gcode_flavor{print_config.get<Domain::GCodeFlavor>("gcode_flavor")},
    max_fan_speed{print_config.get<std::vector<int>>("max_fan_speed")},
    min_fan_speed{print_config.get<std::vector<int>>("min_fan_speed")},
    min_print_speed{print_config.get<std::vector<double>>("min_print_speed")},
    slowdown_below_layer_time{print_config.get<std::vector<int>>("slowdown_below_layer_time")},
    travel_speed{print_config.get<double>("travel_speed")},
    use_relative_e_distances{print_config.get<bool>("use_relative_e_distances")}
{}
} // namespace Slic3r::Biz::Slicing
