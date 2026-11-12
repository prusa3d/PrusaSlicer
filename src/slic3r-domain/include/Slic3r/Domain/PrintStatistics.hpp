#pragma once

#include <cstdint>
#include <variant>
#include <vector>
#include <string>
#include <optional>
#include <map>
#include "Slic3r/Domain/CustomGCode.hpp"
#include "Slic3r/Domain/GCodeExtrusionRole.hpp"

namespace Slic3r::Domain {

struct TimeStatistics
{
    float first_layer_time{};
    float time{};
    std::vector<std::pair<Domain::CustomGCode::Type, std::pair<float, float>>>
        custom_gcode_times;
};

struct BasicPrintStatistics
{
    TimeStatistics normal_mode_time;
    std::optional<TimeStatistics> silent_mode_time;
    std::vector<float> volumes_per_color_change;
    std::map<uint8_t, float> volumes_per_extruder;
    std::map<uint8_t, float> wipe_tower_volumes_per_extruder;
    std::map<uint8_t, float> flush_volumes_per_extruder;
    std::map<uint8_t, float> cost_per_extruder;
    std::map<Domain::GCodeExtrusionRole, std::pair<float, float>> used_filaments_per_role;
};

struct ExtraPrintStatistics
{
    int total_toolchanges{};
    double total_wipe_tower_cost{};
    double total_wipe_tower_filament{};
    double total_wipe_tower_filament_volume{};
    double total_wipe_tower_filament_weight{};
    std::vector<unsigned int> printing_extruders;
    unsigned int initial_extruder_id{};
    std::string initial_filament_type;
    std::vector<std::string> printing_filament_types;
};

struct FullPrintStatistics
{
    std::vector<float> used_filament_per_extruder_mm;
    std::vector<float> used_filament_per_extruder_cm3;
    std::vector<float> used_filament_per_extruder_g;
    std::vector<float> used_filament_for_wipe_tower_per_extruder_mm;
    std::vector<float> used_filament_for_wipe_tower_per_extruder_g;
    std::vector<float> used_filament_for_flush_per_extruder_mm;
    std::vector<float> used_filament_for_flush_per_extruder_g;
    std::map<Domain::GCodeExtrusionRole, std::pair<float, float>> used_filaments_per_role;

    std::vector<float> used_filament_per_color_change_cm3;

    std::vector<float> filament_cost_per_extruder;

    float total_used_filament_mm{};
    float total_used_filament_cm3{};
    float total_used_filament_g{};

    float total_filament_cost{};
    float total_wipe_tower_cost{};

    float total_used_filament_for_wipe_tower_mm{};
    float total_used_filament_for_wipe_tower_cm3{};
    float total_used_filament_for_wipe_tower_g{};
    float total_used_filament_for_flush_mm{};
    float total_used_filament_for_flush_cm3{};
    float total_used_filament_for_flush_g{};

    int total_toolchanges{};

    TimeStatistics normal_mode_time;
    std::optional<TimeStatistics> silent_mode_time;

    float estimated_first_layer_printing_time_normal{};
    std::optional<float> estimated_first_layer_printing_time_silent;

    std::string initial_filament_type;
    std::vector<std::string> printing_filament_types;
    unsigned int initial_extruder_id{};
    std::vector<unsigned int> printing_extruders;
};

using PrintStatistics = std::variant<BasicPrintStatistics, FullPrintStatistics>;

} // namespace Slic3r::Domain
