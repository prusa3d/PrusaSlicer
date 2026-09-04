#pragma once

namespace Slic3r::Domain {
enum class GCodeFlavor : unsigned char
{
    gcfRepRapSprinter,
    gcfRepRapFirmware,
    gcfRepetier,
    gcfTeacup,
    gcfMakerWare,
    gcfMarlinLegacy,
    gcfMarlinFirmware,
    gcfPrusaFirmwareBuddy,
    gcfKlipper,
    gcfSailfish,
    gcfMach3,
    gcfMachinekit,
    gcfSmoothie,
    gcfNoExtrusion,
};
}
