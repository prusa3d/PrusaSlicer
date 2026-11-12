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
    NoExtruders,
    InvalidExtruders,
    InconsistentConfig,
    ToolOverridesInSingleToolPrint,
    InvalidExtruderOffset,
    AvoidCrossingPerimetersAndAvoidCurledOverhangs,
    SpiralVaseMultipleObjects,
    SpiralVaseMultipleMaterials,
    MachineLimitsWithKlipper,
    ShrinkageCompensationExceedsHeight,
    ObjectExceedsHeight,
    LayerExceedsHeight,
    VariableLayerHeightAndOrganicSupports,
    WipeTowerDifferentExtruderDiameters,
    WipeTowerGCodeFlavor,
    WipeTowerAbsoluteDistances,
    WipeTowerOozePreventionSingleExtruderMultiMaterial,
    WipeTowerVolumetricE,
    WipeTowerSequentialPrint,
    WipeTowerDifferentObjectsLayerHeights,
    WipeTowerDifferentObjectsRaftLayerCounts,
    WipeTowerDifferentObjectsSupportContactDistance,
    WipeTowerDifferentObjectsSlicing,
    WipeTowerDifferentObjectsVariableHeight,
    InsufficientExtrusionWidth,
    ExcesiveExtrusionWidth,
    WipeTowerSoluableUnsynchronizedLayers,
    WipeTowerSupporMaterialExtruderSet,
    OrganicSupportTipTooSmall,
    OrganicSupportBranchDiameterSmallerThanSupportMaterial,
    OrganicSupportBranchDiameterSmallerThanTreeTip,
    FirstLayerHeightTooLarge,
    FirstLayerHeightTooSmall,
    LayerHeightTooLarge,
    LayerHeightTooSmall,
    MissingG92E0,
    FoundG92E0InBeforeLayerGCode,
    FoundG92E0InLayerGCode,
    SettingMustBeEqualForAllExtruders,
    PlaceholderParser,
    MissingHwConfigNozzleDiameter,
    HwConfigLessMaterialsThanTools,
    NoHwConfigTools,
    FailedToParseCustomParameters,
    FatalError,

    // FDM
    NoLayers, // "No layers were detected. You might want to repair your STL file(s) or check their size or thickness and retry.\n"
    NoExtrusionInFirstLayer,
    NoExtrusions,
    EmptyPrint, // "The print is empty. The model is not printable with current print settings."
    InvalidThumbnailRequest,

    // SLA
    NoPadGenerated,
    UnprintableObjects,
    UnsupportedOutputFormat,
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
    StabilityIssues,
    EmptyLayers,
    CustomGCodeReservedKeywords,
    InvalidToolchange,
    CloseToPrimingRegions,
    ToolpathOutsideBuildVolume,
    GCodeConflict,
    XYSizeCompensationIgnoredMultiMaterialPainting,
    XYSizeCompensationIgnoredFuzzySkinPainting,
    VirtualExtruderMixesFilamentTypes
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
