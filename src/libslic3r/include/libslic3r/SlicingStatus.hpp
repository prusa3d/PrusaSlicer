#pragma once

#include "Slic3r/Domain/ObjectID.hpp"
#include "Slic3r/Domain/Percentage.hpp"
#include "Slic3r/Exception.hpp"
#include <array>
#include <ostream>
#include <variant>
#include <map>


namespace Slic3r::Biz::Slicing {

enum class ProgressInfo : int
{
    // FDM
    None = 0,
    Initializing,
    GeneratingWipeTower,
    GeneratingSkirtAndBrim,
    GeneratingGCode,
    GeneratingPerimeters,
    PreparingInfill,
    MakingInfill,
    SearchingSupportSpots,
    GeneratingSupportMaterial,
    EstimatingCurledExtrusions,
    CalculatingOverhangingPerimeters,
    ProcessingTriangulatedMesh,
    CheckingStability,

    // SLA
    AssemblingModel,
    HollowingModel,
    DrillingHoles,
    SlicingModel,
    GeneratingSupportPoints,
    GeneratingSupportTree,
    GeneratingPad,
    SlicingSupports,

    MergingSlicesAndCalculatingStatistics,
    RasterizingLayers
};

enum class ErrorCode
{
    // FDM validate
    None,
    NoExtruders, // _u8L("The supplied settings will cause an empty print.")
    InvalidExtruders,
    InconsistentConfig,
    ToolOverridesInSingleToolPrint,
    InvalidExtruderOffset,
    AvoidCrossingPerimetersAndAvoidCurledOverhangs, // _u8L("Avoid crossing perimeters option and avoid crossing curled overhangs option cannot be both enabled together.")
    SpiralVaseMultipleObjects, // _u8L("Only a single object may be printed at a time in Spiral Vase mode. Either remove all but the last object, or enable sequential mode by \"complete_objects\".")
    SpiralVaseMultipleMaterials, // _u8L("The Spiral Vase option can only be used when printing single material objects.")
    MachineLimitsWithKlipper, // _8L("Machine limits cannot be emitted to G-Code when Klipper firmware flavor is used. Change the value of machine_limits_usage.")
    ShrinkageCompensationExceedsHeight, // format(_u8L("While the object %1% itself fits the build volume, it exceeds the maximum build volume height because of material shrinkage compensation."), print_object.model_object()->name)
    ObjectExceedsHeight, // format(_u8L("The object %1% exceeds the maximum build volume height."), print_object.model_object()->name)
    LayerExceedsHeight, // format(_u8L("While the object %1% itself fits the build volume, its last layer exceeds the maximum build volume height."), print_object.model_object()->name) + " " + _u8L("You might want to reduce the size of your model or change current print settings and retry.")
    VariableLayerHeightAndOrganicSupports, // _u8L("Variable layer height is not supported with Organic supports.")
    WipeTowerDifferentExtruderDiameters, // _u8L("The wipe tower is only supported if all extruders use filaments of the same diameter.")
    WipeTowerGCodeFlavor, // _u8L("The Wipe Tower is currently only supported for the Marlin, PrusaBuddy, Klipper, RepRap/Sprinter, RepRapFirmware and Repetier G-code flavors.")
    WipeTowerAbsoluteDistances, // _u8L("The Wipe Tower is currently only supported with the relative extruder addressing (use_relative_e_distances=1).")
    WipeTowerOozePreventionSingleExtruderMultiMaterial, // _u8L("Ooze prevention is only supported with the wipe tower when 'single_extruder_multi_material' is off.")
    WipeTowerVolumetricE, // _u8L("The Wipe Tower currently does not support volumetric E (use_volumetric_e=0).")
    WipeTowerSequentialPrint, // _u8L("The Wipe Tower is currently not supported for multimaterial sequential prints.")
    WipeTowerDifferentObjectsLayerHeights, // _u8L("The Wipe Tower is only supported for multiple objects if they have equal layer heights")
    WipeTowerDifferentObjectsRaftLayerCounts, // _u8L("The Wipe Tower is only supported for multiple objects if they are printed over an equal number of raft layers")
    WipeTowerDifferentObjectsSupportContactDistance, // _u8L("The Wipe Tower is only supported for multiple objects if they are printed with the same support_material_contact_distance")
    WipeTowerDifferentObjectsSlicing, // _u8L("The Wipe Tower is only supported for multiple objects if they are sliced equally.")
    WipeTowerDifferentObjectsVariableHeight, // _u8L("The Wipe tower is only supported if all objects have the same variable layer height")
    InsufficientExtrusionWidth, // (boost::format(_u8L("%1%=%2% mm is too low to be printable at a layer height %3% mm")) % opt_key % extrusion_width_min % layer_height).str()
    ExcesiveExtrusionWidth, // (boost::format(_u8L("Excessive %1%=%2% mm to be printable with a nozzle diameter %3% mm")) % opt_key % extrusion_width_max % max_nozzle_diameter).str()
    WipeTowerSoluableUnsynchronizedLayers, // _u8L("For the Wipe Tower to work with the soluble supports, the support layers need to be synchronized with the object layers.")
    WipeTowerSupporMaterialExtruderSet, // _u8L("The Wipe Tower currently supports the non-soluble supports only if they are printed with the current extruder without triggering a tool change. (both support_material_extruder and support_material_interface_extruder need to be set to 0).")
    OrganicSupportTipTooSmall, // _u8L("Organic support tree tip diameter must not be smaller than support material extrusion width.")
    OrganicSupportBranchDiameterSmallerThanSupportMaterial, // _u8L("Organic support branch diameter must not be smaller than 2x support material extrusion width.")
    OrganicSupportBranchDiameterSmallerThanTreeTip, // _u8L("Organic support branch diameter must not be smaller than support tree tip diameter.")
    FirstLayerHeightTooLarge, // _u8L("First layer height can't be greater than nozzle diameter")
    LayerHeightTooLarge, // _u8L("Layer height can't be greater than nozzle diameter")
    MissingG92E0, // _u8L("Relative extruder addressing requires resetting the extruder position at each layer to prevent loss of floating point accuracy. Add \"G92 E0\" to layer_gcode.")
    FoundG92E0InBeforeLayerGCode, // _u8L("\"G92 E0\" was found in before_layer_gcode, which is incompatible with absolute extruder addressing.")
    FoundG92E0InLayerGCode, // _u8L("\"G92 E0\" was found in layer_gcode, which is incompatible with absolute extruder addressing.")
    SettingMustBeEqualForAllExtruders,
    PlaceholderParser,
    MissingHwConfigNozzleDiameter,
    HwConfigLessMaterialsThanTools,
    NoHwConfigTools,
    FailedToParseCustomParameters,
    FatalError,

    // FDM
    NoLayers, // "No layers were detected. You might want to repair your STL file(s) or check their size or thickness and retry.\n"
    NoExtrusionInFirstLayer, // _u8L("There is an object with no extrusions in the first layer.")
    NoExtrusions, // _u8L("No extrusions were generated for objects.")
    EmptyPrint, // "The print is empty. The model is not printable with current print settings."
    InvalidThumbnailRequest,

    // SLA
    NoPadGenerated, // _u8L("No pad can be generated for this model with the current configuration")
    UnprintableObjects, // _u8L("There are unprintable objects. Try to adjust support settings to make the objects printable.")
    UnsupportedOutputFormat, // _u8L("Unsupported output format")
};

using PlaceholderParserErrorPayload = std::map<std::string, std::string>;
struct InvalidThumbnailRequestPayload {
    bool invalid_format{false};
    bool out_of_range{false};
    bool invalid_ext{false};
};

using ErrorPayload = std::variant<
    std::monostate,
    PlaceholderParserErrorPayload,
    InvalidThumbnailRequestPayload
>;

struct Error {
    ErrorCode code{ErrorCode::None};

    // Empty means general error.
    std::vector<std::string> item_keys;
    std::optional<Domain::ObjectID> model_object_id;
    ErrorPayload payload;
};

enum class WarningCode
{
    None,
    BedTempsDiffer,
    BedTempsChanged,
    FilamentShrinkageDiffer,
    WipeTowerNozzleDiameterDiffer,
    SupportNozzleDiameterDiffer,
    SupportsTurnedOff,
    StabilityIssues, // _u8L("Detected print stability issues:\n%1%")
    EmptyLayers,
    CustomGCodeReservedKeywords,
    InvalidToolchange,
    CloseToPrimingRegions, // _u8L( "Your print is very close to the priming regions. " "Make sure there is no collision.")
    ToolpathOutsideBuildVolume,
    GCodeConflict,
    XYSizeCompensationIgnoredMultiMaterialPainting,
    XYSizeCompensationIgnoredFuzzySkinPainting
};

struct StabilityWarningPayload {
    bool recommend_brim{false};
    std::string recommendations;
};

struct EmptyLayersWarningPayload {
    std::vector<std::pair<double, double>> ranges;
};

struct CustomGCodeReservedKeywordsWarningPayload {
    std::string reports;
};

struct InvalidToolchangeWarningPayload {
    std::string gcode_line;
};

struct GCodeConflictWarningPayload {
    std::array<std::string, 2> object_names;
    double height{0.};
};

using WarningPayload = std::variant<
    std::monostate,
    StabilityWarningPayload,
    EmptyLayersWarningPayload,
    CustomGCodeReservedKeywordsWarningPayload,
    InvalidToolchangeWarningPayload,
    GCodeConflictWarningPayload
    >;

enum class WarningSeverity {
    LOW,
    HIGH
};

struct Warning {
    WarningCode code{WarningCode::None};

    // Empty means general warning.
    std::vector<std::string> item_keys;
    std::optional<Domain::ObjectID> model_object_id;
    WarningPayload payload;
    WarningSeverity severity{WarningSeverity::LOW}; // only for UI-purposes
};

class Exception : public Slic3r::Exception
{
public:
    Exception(Error error) : Slic3r::Exception{"Slicing exception"}, m_error{std::move(error)} {}

    const Error& error() const
    {
        return m_error;
    }

private:
    Error m_error;
};

std::ostream& operator<<(std::ostream& output, const Error& error);

struct Progress {
    Domain::Percentage progress{0.0};
    ProgressInfo progress_info;
};
std::ostream& operator<<(std::ostream& output, const Progress& progress);

enum class StatusCode
{
    Empty,
    Updating,
    Running,
    Finished,
    Modified,
    Stopping,
    Removed,
    InvalidData
};

std::ostream& operator<<(std::ostream& output, const StatusCode& status_code);

struct Status {
    StatusCode code;

    std::vector<Biz::Slicing::Error> errors;
    std::vector<Biz::Slicing::Warning> warrnings;

    std::optional<Biz::Slicing::Progress> progress;
};

struct StatusUpdate {
    std::optional<StatusCode> code;
    bool clear_errors{false};
    std::vector<Biz::Slicing::Error> errors_to_append;
    bool clear_warnings{false};
    std::vector<Biz::Slicing::Warning> warnings_to_append;
    bool clear_progress{false};
    std::optional<Biz::Slicing::Progress> progress;
};

std::ostream& operator<<(std::ostream& output, const StatusUpdate& status);

} // namespace Slic3r::Domain
