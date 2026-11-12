#pragma once

#include <Slic3r/Biz/libpgcode/Types.hpp>

#include <cstdint>
#include <map>

namespace Slic3r::App::Preview {

struct WipeTowerAndFlushFilamentUsage
{
    float wipe_tower_m{};
    float wipe_tower_g{};
    float flush_m{};
    float flush_g{};
};

using WipeTowerAndFlushFilamentUsagePerExtruder = std::map<uint8_t, WipeTowerAndFlushFilamentUsage>;

struct FdmViewerWrapperInputData
{
    Biz::libpgcode::GCodeProducer producer{ Biz::libpgcode::GCodeProducer::Unknown };
    bool sequential_print{ false };
    bool keep_layers_times{ false };
    std::string color_change_gcode;
    std::string pause_print_gcode;
    std::string template_custom_gcode;
    Domain::CustomGCode::Info custom_gcode_info;
    Biz::libpgcode::PrintSettings print_settings;

    WipeTowerAndFlushFilamentUsagePerExtruder wipe_tower_and_flush_filament_usage;
    bool has_wipe_tower_filament{false};
    bool has_flush_filament{false};
};

} // namespace Slic3r::App::Preview
