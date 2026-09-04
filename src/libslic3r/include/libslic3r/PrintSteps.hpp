#pragma once

namespace Slic3r {

// Print step IDs for keeping track of the print state.
// The Print steps are applied in this order.
enum FDMPrintStep : unsigned int
{
    psWipeTower,
    // Ordering of the tools on PrintObjects for a multi-material print.
    // psToolOrdering is a synonym to psWipeTower, as the Wipe Tower calculates and modifies the ToolOrdering,
    // while if printing without the Wipe Tower, the ToolOrdering is calculated as well.
    psToolOrdering = psWipeTower,
    psAlertWhenSupportsNeeded,
    psSkirtBrim,
    // Last step before G-code export, after this step is finished, the initial extrusion path preview
    // should be refreshed.
    psSlicingFinished = psSkirtBrim,
    psGCodeExport,
    psCount,
};

using FDMPrintSteps = std::set<FDMPrintStep>;

enum FDMPrintObjectStep : unsigned int
{
    posSlice,
    posPerimeters,
    posPrepareInfill,
    posInfill,
    posIroning,
    posSupportSpotsSearch,
    posSupportMaterial,
    posEstimateCurledExtrusions,
    posCalculateOverhangingPerimeters,
    posCount,
};

using FDMPrintObjectSteps = std::set<FDMPrintObjectStep>;

enum SLAPrintStep : unsigned int
{
    slapsMergeSlicesAndEval,
    slapsRasterize,
    slapsCount
};

enum SLAPrintObjectStep : unsigned int
{
    slaposAssembly,
    slaposHollowing,
    slaposDrillHoles,
    slaposObjectSlice,
    slaposSupportPoints,
    slaposSupportTree,
    slaposPad,
    slaposSliceSupports,
    slaposCount
};

} // namespace Slic3r
