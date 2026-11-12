#pragma once

#include "Types.hpp"

#include <Slic3r/Biz/libpgcode/LineView.hpp>

#include <map>

namespace Slic3r::App::libvgcode {

struct GCodeEvent
{
    Domain::CustomGCode::Type type{ Domain::CustomGCode::Type::Custom };
    uint8_t extruder_id{ 0 };
    Biz::libpgcode::Times times{};
    std::array<float, 2> used_filament{ 0.0f, 0.0f };
};

using GCodeEvents = std::vector<GCodeEvent>;

struct FdmViewerInputData
{
    //
    // Whether or not the gcode was generated with spiral vase mode enabled.
    // Required to properly detect fictitious layer changes when spiral vase mode is enabled.
    //
    bool spiral_vase_enabled{ false };
    //
    // List of path vertices (gcode moves)
    // See: Biz::libpgcode::MoveVertex
    //
    std::shared_ptr<const Biz::libpgcode::MoveVertices> vertices;
    //
    // gcode lines
    //
    std::shared_ptr<const Biz::libpgcode::LineView> gcode;
    //
    // List of custom gcode events
    // See: GCodeEvent
    //
    GCodeEvents gcode_events;
    //
    // Used filament by roles
    // first = length in mm
    // second = mass in g
    //
    std::map<Domain::GCodeExtrusionRole, std::pair<float, float>> used_filament_by_roles;
    //
    // Used filament by extruder
    // first = length in mm
    // second = mass in g
    //
    std::map<uint8_t, std::pair<float, float>> used_filament_by_extruders;
    //
    // Palette for extruders colors
    //
    Palette tools_colors;
    //
    // Palette for color print colors
    //
    Palette color_print_colors;
    //
    // Extruders count
    // 
    uint8_t extruders_count{ Biz::libpgcode::MIN_EXTRUDERS_COUNT };
};

} // namespace Slic3r::App::libvgcode
