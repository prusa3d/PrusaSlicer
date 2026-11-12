#pragma once

#include "Slic3r/Domain/ConfigDef.hpp"
namespace Slic3r::Domain {

// Next, define all enums that should be used in the config.
enum class SLADisplayOrientation {
    sladoLandscape,
    sladoPortrait
};

enum SLAMaterialSpeed { slamsSlow, slamsFast, slamsHighViscosity };

namespace sla {
    enum class SupportTreeType { Default, Branching, Organic };
    enum class PillarConnectionMode { zigzag, cross, dynamic };
}

enum TowerSpeeds : int {
    tsLayer1,
    tsLayer2,
    tsLayer3,
    tsLayer4,
    tsLayer5,
    tsLayer8,
    tsLayer11,
    tsLayer14,
    tsLayer18, 
    tsLayer22,
    tsLayer24,
};

enum TiltSpeeds : int {
    tsMove120,
    tsLayer200,
    tsMove300,
    tsLayer400,
    tsLayer600,
    tsLayer800,
    tsLayer1000,
    tsLayer1250,
    tsLayer1500,
    tsLayer1750,
    tsLayer2000,
    tsLayer2250,
    tsMove5120,
    tsMove8000,
};

const ConfigDefinitions& get_defs_sla();

} // namespace Slic3r::Domain
