#pragma once

#include <cstdint>

namespace Slic3r::Domain {
// Be careful when editing this list as many parts of the code depend
// on the values of these ordinars, for example
// GCodeViewer::Extrusion_Role_Colors
enum class GCodeExtrusionRole : uint8_t {
    None,
    Perimeter,
    ExternalPerimeter,
    OverhangPerimeter,
    InternalInfill,
    SolidInfill,
    TopSolidInfill,
    Ironing,
    BridgeInfill,
    GapFill,
    Skirt,
    SupportMaterial,
    SupportMaterialInterface,
    WipeTower,
    // Custom (user defined) G-code block, for example start / end G-code.
    Custom,
    // Stopper to count number of enums.
    Count
};
}
