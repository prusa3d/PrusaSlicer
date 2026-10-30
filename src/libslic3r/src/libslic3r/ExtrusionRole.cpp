#include "libslic3r/ExtrusionRole.hpp"

#include <string>
#include <cassert>


namespace Slic3r {

// Convert a rich bitmask based ExtrusionRole to a less expressive ordinal GCodeExtrusionRole.
// GCodeExtrusionRole is to be serialized into G-code and deserialized by G-code viewer,
GCodeExtrusionRole extrusion_role_to_gcode_extrusion_role(ExtrusionRole role)
{
    if (role == ExtrusionRole::None)                return GCodeExtrusionRole::None;
    if (role.is_perimeter()) {
        return role.is_bridge() ? GCodeExtrusionRole::OverhangPerimeter :
               role.is_external() ? GCodeExtrusionRole::ExternalPerimeter : GCodeExtrusionRole::Perimeter;
    }
    if (role == ExtrusionRole::InternalInfill)      return GCodeExtrusionRole::InternalInfill;
    if (role == ExtrusionRole::SolidInfill)         return GCodeExtrusionRole::SolidInfill;
    if (role == ExtrusionRole::InfillOverBridge)    return GCodeExtrusionRole::SolidInfill;
    if (role == ExtrusionRole::TopSolidInfill)      return GCodeExtrusionRole::TopSolidInfill;
    if (role == ExtrusionRole::Ironing)             return GCodeExtrusionRole::Ironing;
    if (role == ExtrusionRole::BridgeInfill)        return GCodeExtrusionRole::BridgeInfill;
    if (role == ExtrusionRole::GapFill)             return GCodeExtrusionRole::GapFill;
    if (role == ExtrusionRole::Skirt)               return GCodeExtrusionRole::Skirt;
    if (role == ExtrusionRole::SupportMaterial)     return GCodeExtrusionRole::SupportMaterial;
    if (role == ExtrusionRole::SupportMaterialInterface) return GCodeExtrusionRole::SupportMaterialInterface;
    if (role == ExtrusionRole::WipeTower)           return GCodeExtrusionRole::WipeTower;
    assert(false);
    return GCodeExtrusionRole::None;
}

std::string gcode_extrusion_role_to_string(GCodeExtrusionRole role)
{
    switch (role) {
        case GCodeExtrusionRole::None                         : return std::string("Unknown");
        case GCodeExtrusionRole::Perimeter                    : return std::string("Perimeter");
        case GCodeExtrusionRole::ExternalPerimeter            : return std::string("External perimeter");
        case GCodeExtrusionRole::OverhangPerimeter            : return std::string("Overhang perimeter");
        case GCodeExtrusionRole::InternalInfill               : return std::string("Internal infill");
        case GCodeExtrusionRole::SolidInfill                  : return std::string("Solid infill");
        case GCodeExtrusionRole::TopSolidInfill               : return std::string("Top solid infill");
        case GCodeExtrusionRole::Ironing                      : return std::string("Ironing");
        case GCodeExtrusionRole::BridgeInfill                 : return std::string("Bridge infill");
        case GCodeExtrusionRole::GapFill                      : return std::string("Gap fill");
        case GCodeExtrusionRole::Skirt                        : return std::string("Skirt/Brim");
        case GCodeExtrusionRole::SupportMaterial              : return std::string("Support material");
        case GCodeExtrusionRole::SupportMaterialInterface     : return std::string("Support material interface");
        case GCodeExtrusionRole::WipeTower                    : return std::string("Wipe tower");
        case GCodeExtrusionRole::Custom                       : return std::string("Custom");
        default                             : assert(false);
    }
    return {};
}


}
