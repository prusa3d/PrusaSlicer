#pragma once

#include "Slic3r/Domain/GCodeFlavor.hpp"
#include "libslic3r/ConfigViews.hpp"

namespace Slic3r::Biz::Slicing {
struct GCodeWriterConfig
{
    GCodeWriterConfig(const PrintConfigView& print_config);

    Domain::GCodeFlavor gcode_flavor{};

    std::string extrusion_axis{};
    bool single_extruder_multi_material{};

    bool supports_separate_travel_acceleration{};
    unsigned int max_acceleration{};
    unsigned int max_travel_acceleration{};
    double max_junction_deviation{};

    bool gcode_comments{};
    bool use_volumetric_e{};
    bool use_relative_e_distances{};
    bool use_firmware_retraction{};
    double travel_speed{};
    double travel_speed_z{};
    std::vector<double> filament_diameter{};
    std::vector<double> filament_density{};
    std::vector<double> filament_cost{};
    std::vector<double> extrusion_multiplier{};
    std::vector<Domain::Percentage> retract_before_wipe{};
    std::vector<double> retract_length{};
    std::vector<double> retract_speed{};
    std::vector<double> deretract_speed{};
    std::vector<double> retract_restart_extra{};
    std::vector<double> retract_length_toolchange{};
    std::vector<double> retract_restart_extra_toolchange{};
};
} // namespace Slic3r::Biz::Slicing
