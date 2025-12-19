///|/ Copyright (c) Prusa Research 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_FiberPrintConfig_hpp_
#define slic3r_FiberPrintConfig_hpp_

// Must include PrintConfig.hpp to get the PRINT_CONFIG_CLASS_DEFINE macro
#include "PrintConfig.hpp"

namespace Slic3r {

// Forward declarations
class FiberPrintConfig;

// Enums for fiber configuration options
// These are declared here but the static maps are defined in PrintConfig.cpp
enum class FiberPatternType : int {
    fpGrid,
    fpConcentric,
    fpCustom,
};

enum class FiberPlacementZoneType : int {
    fpzPerimeter,
    fpzInfill,
    fpzBoth,
};

enum class FiberPrintMethodType : int {
    fpmDualPrinthead,
    fpmCoExtrusion,
    fpmPreEmbeddedFilament,
};

enum class FiberPrintSequenceType : int {
    fpsPlasticFirst,
    fpsAlternating,
    fpsFiberOnTop,
};

enum class FiberTypeEnum : int {
    ftCarbon,
    ftGlass,
    ftKevlar,
    ftBasalt,
    ftCustom,
};

// FiberPrintConfig class is now defined in PrintConfig.hpp
// (moved there to avoid macro #undef issues)

} // namespace Slic3r

#endif /* slic3r_FiberPrintConfig_hpp_ */

