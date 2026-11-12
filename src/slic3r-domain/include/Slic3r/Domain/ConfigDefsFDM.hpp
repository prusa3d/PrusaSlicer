#pragma once

#include "Slic3r/Domain/ConfigDef.hpp"
namespace Slic3r::Domain {

// This is an example of using Config infrastructure.

// Next, define all enums that should be used in the config.

enum class ArcFittingType {
    Disabled,
    EmitCenter
};
enum class CoolingSlowdownLogicType
{
    UniformCooling,
    ConsistentSurface,
    Proportional,
};
enum class TopOnePerimeterType
{
    None,
    TopSurfaces,
    TopmostOnly,
    Count
};
enum class BrimType {
    NoBrim,
    OuterOnly,
    InnerOnly,
    OuterAndInner,
};
enum class EnsureVerticalShellThickness {
    Disabled,
    Partial,
    Enabled,
};
enum class InfillPattern {
    ipRectilinear, ipMonotonic, ipMonotonicLines, ipAlignedRectilinear, ipGrid, ipTriangles,
    ipStars, ipCubic, ipLine, ipConcentric, ipHoneycomb, ip3DHoneycomb, ipGyroid, ipHilbertCurve,
    ipArchimedeanChords, ipOctagramSpiral, ipAdaptiveCubic, ipSupportCubic, ipSupportBase,
    ipLightning, ipEnsuring, ipZigZag, ipCount,
};
enum class FuzzySkinType {
    None,
    External,
    All,
};

enum class LabelObjectsStyle {
    Disabled, Octoprint, Firmware
};
enum class IroningType {
    TopSurfaces,
    TopmostOnly,
    AllSolid,
    Count,
};
enum class MachineLimitsUsage {
    EmitToGCode,
    TimeEstimateOnly,
    Ignore,
    Count,
};
enum class SeamPosition {
    spRandom, spNearest, spAligned, spRear
};
enum class ScarfSeamPlacement {
    nowhere,
    countours,
    everywhere
};
enum class DraftShield {
    dsDisabled, dsLimited, dsEnabled
};
enum class SupportMaterialPattern {
    smpRectilinear, smpRectilinearGrid, smpHoneycomb,
};
enum class SupportMaterialInterfacePattern {
    smipAuto, smipRectilinear, smipConcentric,
};
enum class SupportMaterialStyle {
    smsGrid, smsSnug, smsTree, smsOrganic,
};
enum class PerimeterGeneratorType
{
    Classic, // Classic perimeter generator using Clipper offsets with constant extrusion width.
    Arachne  // Perimeter generator with variable extrusion width based on the paper  "A framework for
             // "adaptive width control of dense contour-parallel toolpaths in fused deposition modeling" ported from Cura.
};
enum class SupportMode
{
    None,
    EnforcersOnly,
    Everywhere,
};

enum class PressureAdvance
{
    Disabled,
    Enabled,
    AutomaticCalibration,
};

enum class ToolChangeOrderingType
{
    Optimized,
    Cyclic,
};

const ConfigDefinitions& get_defs_fdm();

} // namespace Slic3r::Domain
