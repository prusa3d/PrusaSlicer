#pragma once

#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Domain/ConfigDefsFDM.hpp"
#include "Slic3r/Domain/GCodeFlavor.hpp"

namespace Slic3r::Biz::Slicing {
struct ExtrudeConfig
{
    explicit ExtrudeConfig(const Domain::ConfigView& config);

    std::vector<double> default_acceleration{};
    std::vector<double> first_layer_acceleration{};
    std::vector<double> first_layer_acceleration_over_raft{};
    std::vector<double> bridge_acceleration{};
    std::vector<double> top_solid_infill_acceleration{};
    std::vector<double> solid_infill_acceleration{};
    std::vector<double> infill_acceleration{};
    std::vector<double> external_perimeter_acceleration{};
    std::vector<double> perimeter_acceleration{};
    std::vector<double> perimeter_speed{};
    std::vector<double> infill_speed{};
    std::vector<Domain::FloatOrPercentage> solid_infill_speed{};
    std::vector<Domain::FloatOrPercentage> external_perimeter_speed{};
    std::vector<double> bridge_speed{};
    std::vector<double> gap_fill_speed{};
    std::vector<double> retract_speed{};
    std::vector<double> retract_lift{};
    std::vector<double> retract_lift_above{};
    std::vector<double> retract_lift_below{};
    std::vector<double> retract_length{};
    std::vector<double> retract_before_travel{};
    std::vector<double> travel_max_lift{};
    std::vector<double> travel_slope{};
    std::vector<bool> travel_ramping_lift{};
    std::vector<bool> retract_layer_change{};
    double travel_acceleration{};
    double travel_short_distance_acceleration{};

    std::vector<double> max_volumetric_speed{};
    std::vector<double> filament_max_volumetric_speed{};
    std::vector<double> filament_infill_max_crossing_speed{};
    std::vector<double> filament_infill_max_speed{};

    std::vector<bool> enable_dynamic_overhang_speeds{};
    std::vector<Domain::FloatOrPercentage> overhang_speed_0{};
    std::vector<Domain::FloatOrPercentage> overhang_speed_1{};
    std::vector<Domain::FloatOrPercentage> overhang_speed_2{};
    std::vector<Domain::FloatOrPercentage> overhang_speed_3{};

    std::vector<bool> enable_dynamic_fan_speeds{};
    std::vector<int> overhang_fan_speed_0{};
    std::vector<int> overhang_fan_speed_1{};
    std::vector<int> overhang_fan_speed_2{};
    std::vector<int> overhang_fan_speed_3{};

    Domain::GCodeFlavor gcode_flavor{};
    std::vector<double> machine_max_feedrate_x{};
    std::vector<double> machine_max_feedrate_y{};
    std::vector<double> machine_max_feedrate_z{};
    std::vector<double> machine_max_acceleration_travel{};
    std::vector<double> machine_max_acceleration_z{};
    std::vector<double> machine_max_jerk_z{};

    std::vector<Domain::Percentage> fill_density{};

    double z_offset{};
    double travel_speed{};
    double ironing_speed{};
    double support_material_speed{};
    Domain::FloatOrPercentage support_material_interface_speed{};
    std::vector<Domain::FloatOrPercentage> over_bridge_speed{};
    std::vector<Domain::FloatOrPercentage> top_solid_infill_speed{};
    std::vector<Domain::FloatOrPercentage> first_layer_infill_speed{};
    std::vector<Domain::FloatOrPercentage> first_layer_solid_infill_speed{};
    std::vector<Domain::FloatOrPercentage> first_layer_perimeter_speed{};
    std::vector<Domain::FloatOrPercentage> first_layer_external_perimeter_speed{};
    std::vector<Domain::FloatOrPercentage> first_layer_support_material_speed{};
    std::vector<Domain::FloatOrPercentage> first_layer_top_solid_infill_speed{};
    std::vector<Domain::FloatOrPercentage> first_layer_gap_fill_speed{};
    std::vector<Domain::FloatOrPercentage> first_layer_speed_over_raft{};
    double avoid_crossing_perimeters_max_detour{};
    bool gcode_comments{};
    Domain::ArcFittingType arc_fitting{};
    Domain::ScarfSeamPlacement scarf_seam_placement{};

    bool only_retract_when_crossing_perimeters{};
    bool avoid_crossing_perimeters{};
    bool avoid_crossing_curled_overhangs{};
};

} // namespace Slic3r::Biz::Slicing
