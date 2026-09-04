#include "libslic3r/ExtrusionRole.hpp"
#include "libslic3r/I18N_private.hpp"

#include <string>
#include <string_view>
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
        case GCodeExtrusionRole::None                         : return L("Unknown");
        case GCodeExtrusionRole::Perimeter                    : return L("Perimeter");
        case GCodeExtrusionRole::ExternalPerimeter            : return L("External perimeter");
        case GCodeExtrusionRole::OverhangPerimeter            : return L("Overhang perimeter");
        case GCodeExtrusionRole::InternalInfill               : return L("Internal infill");
        case GCodeExtrusionRole::SolidInfill                  : return L("Solid infill");
        case GCodeExtrusionRole::TopSolidInfill               : return L("Top solid infill");
        case GCodeExtrusionRole::Ironing                      : return L("Ironing");
        case GCodeExtrusionRole::BridgeInfill                 : return L("Bridge infill");
        case GCodeExtrusionRole::GapFill                      : return L("Gap fill");
        case GCodeExtrusionRole::Skirt                        : return L("Skirt/Brim");
        case GCodeExtrusionRole::SupportMaterial              : return L("Support material");
        case GCodeExtrusionRole::SupportMaterialInterface     : return L("Support material interface");
        case GCodeExtrusionRole::WipeTower                    : return L("Wipe tower");
        case GCodeExtrusionRole::Custom                       : return L("Custom");
        default                             : assert(false);
    }
    return {};
}


}
