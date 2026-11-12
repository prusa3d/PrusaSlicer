#include "Slic3r/Biz/libpgcode/Types.hpp"

#include <string>

namespace Slic3r::Biz::libpgcode {

using Domain::GCodeExtrusionRole;

float MoveVertex::volumetric_rate() const { return feedrate * mm3_per_mm; }
float MoveVertex::actual_volumetric_rate() const { return actual_feedrate * mm3_per_mm; }
bool MoveVertex::is_extrusion() const { return type == MoveType::Extrude; }
bool MoveVertex::is_travel() const { return type == MoveType::Travel; }

bool MoveVertex::is_option() const
{
    switch (type)
    {
    case MoveType::Retract:
    case MoveType::Unretract:
    case MoveType::Seam:
    case MoveType::ToolChange:
    case MoveType::ColorChange:
    case MoveType::PausePrint:
    case MoveType::CustomGCode: { return true; }
    default:                    { return false; }
    }
}

bool MoveVertex::is_wipe() const { return type == MoveType::Wipe; }
bool MoveVertex::is_custom_gcode() const { return type == MoveType::Extrude && extrusion_role == GCodeExtrusionRole::Custom; }

bool PrintSettings::has_data() const
{
    bool ret = false;
    ret |= !print.empty();
    ret |= !printer.empty();
    for (const std::string& s : filament) {
        ret |= !s.empty();
    }
    return ret;
}

void PrintSettings::reset()
{
    print.clear();
    printer.clear();
    filament.clear();
}

} // namespace Slic3r::Biz::libpgcode
