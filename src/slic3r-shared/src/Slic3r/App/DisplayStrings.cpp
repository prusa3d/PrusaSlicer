#include "Slic3r/App/DisplayStrings.hpp"
#include <boost/algorithm/string/join.hpp>
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Domain/Project.hpp"

using namespace Slic3r::Biz;

namespace Slic3r::App {

std::string to_display_string(Biz::Slicing::ErrorCode code)
{
    using Biz::Slicing::ErrorCode;

    switch (code) {
    case ErrorCode::None:
        return _u8L("No error.");
    case ErrorCode::FatalError:
        return _u8L("Fatal slicing error, if you see this, please restart the application!");
    case ErrorCode::NoExtruders:
        return _u8L("The supplied settings will cause an empty print.");
    case ErrorCode::InvalidExtruders:
        return _u8L("Some extruder settings refer to non-existent tools.");
    case ErrorCode::InconsistentConfig:
        return _u8L(
            "The supplied config is internally inconsistent. "
            "This can happen if the 3mf or presets are corrupted !"
        );
    case ErrorCode::ToolOverridesInSingleToolPrint:
        return _u8L(
            "There are tool overrides, even though the print is single tool."
            "This can happen if the 3mf or presets are corrupted !"
        );
    case ErrorCode::InvalidExtruderOffset:
        return _u8L("The count of extruder offsets does not match the tool count.");
    case ErrorCode::AvoidCrossingPerimetersAndAvoidCurledOverhangs:
        return _u8L(
            "Avoid crossing perimeters option and avoid crossing curled overhangs option cannot be "
            "both enabled together."
        );
    case ErrorCode::SpiralVaseMultipleObjects:
        return _u8L(
            "Only a single object may be printed at a time in Spiral Vase mode. Either remove all "
            "but the last object, or enable sequential mode by \"complete_objects\"."
        );
    case ErrorCode::SpiralVaseMultipleMaterials:
        return _u8L(
            "The Spiral Vase option can only be used when printing single material objects."
        );
    case ErrorCode::MachineLimitsWithKlipper:
        return _u8L(
            "Machine limits cannot be emitted to G-Code when Klipper firmware flavor is used. "
            "Change the value of machine_limits_usage."
        );
    case ErrorCode::ShrinkageCompensationExceedsHeight:
        return _u8L(
            "While the object itself fits the build volume, it exceeds the maximum build volume "
            "height because of material shrinkage compensation."
        );
    case ErrorCode::ObjectExceedsHeight:
        return _u8L("The object exceeds the maximum build volume height.");
    case ErrorCode::LayerExceedsHeight:
        return _u8L(
            "While the object itself fits the build volume, its last layer exceeds the maximum "
            "build volume height. You might want to reduce the size of your model or change "
            "current print settings and retry."
        );
    case ErrorCode::VariableLayerHeightAndOrganicSupports:
        return _u8L("Variable layer height is not supported with Organic supports.");
    case ErrorCode::WipeTowerDifferentExtruderDiameters:
        return _u8L(
            "The wipe tower is only supported if all extruders use filaments of the same diameter."
        );
    case ErrorCode::WipeTowerGCodeFlavor:
        return _u8L(
            "The Wipe Tower is currently only supported for the Marlin, Klipper, RepRap/Sprinter, "
            "RepRapFirmware and Repetier G-code flavors."
        );
    case ErrorCode::WipeTowerAbsoluteDistances:
        return _u8L(
            "The Wipe Tower is currently only supported with the relative extruder addressing "
            "(use_relative_e_distances=1)."
        );
    case ErrorCode::WipeTowerOozePreventionSingleExtruderMultiMaterial:
        return _u8L(
            "Ooze prevention is only supported with the wipe tower when "
            "'single_extruder_multi_material' is off."
        );
    case ErrorCode::WipeTowerVolumetricE:
        return _u8L("The Wipe Tower currently does not support volumetric E (use_volumetric_e=0).");
    case ErrorCode::WipeTowerSequentialPrint:
        return _u8L(
            "The Wipe Tower is currently not supported for multimaterial sequential prints."
        );
    case ErrorCode::WipeTowerDifferentObjectsLayerHeights:
        return _u8L(
            "The Wipe Tower is only supported for multiple objects if they have equal layer heights."
        );
    case ErrorCode::WipeTowerDifferentObjectsRaftLayerCounts:
        return _u8L(
            "The Wipe Tower is only supported for multiple objects if they are printed over an "
            "equal number of raft layers."
        );
    case ErrorCode::WipeTowerDifferentObjectsSupportContactDistance:
        return _u8L(
            "The Wipe Tower is only supported for multiple objects if they are printed with the "
            "same support_material_contact_distance."
        );
    case ErrorCode::WipeTowerDifferentObjectsSlicing:
        return _u8L(
            "The Wipe Tower is only supported for multiple objects if they are sliced equally."
        );
    case ErrorCode::WipeTowerDifferentObjectsVariableHeight:
        return _u8L(
            "The Wipe tower is only supported if all objects have the same variable layer height."
        );
    case ErrorCode::InsufficientExtrusionWidth:
        return _u8L("Extrusion width is too low to be printable at current layer height.");
    case ErrorCode::ExcesiveExtrusionWidth:
        return _u8L("Extrusion width is too large to be printable with the nozzle diameter.");
    case ErrorCode::WipeTowerSoluableUnsynchronizedLayers:
        return _u8L(
            "For the Wipe Tower to work with the soluble supports, the support layers need to be "
            "synchronized with the object layers."
        );
    case ErrorCode::WipeTowerSupporMaterialExtruderSet:
        return _u8L(
            "The Wipe Tower currently supports the non-soluble supports only if they are printed "
            "with the current extruder without triggering a tool change. (both "
            "support_material_extruder and support_material_interface_extruder need to be set to 0)."
        );
    case ErrorCode::OrganicSupportTipTooSmall:
        return _u8L(
            "Organic support tree tip diameter must not be smaller than support material extrusion "
            "width."
        );
    case ErrorCode::OrganicSupportBranchDiameterSmallerThanSupportMaterial:
        return _u8L(
            "Organic support branch diameter must not be smaller than 2x support material "
            "extrusion width."
        );
    case ErrorCode::OrganicSupportBranchDiameterSmallerThanTreeTip:
        return _u8L(
            "Organic support branch diameter must not be smaller than support tree tip diameter."
        );
    case ErrorCode::FirstLayerHeightTooLarge:
        return _u8L("First layer height can't be greater than nozzle diameter");
    case ErrorCode::LayerHeightTooLarge:
        return _u8L("Layer height can't be greater than nozzle diameter");
    case ErrorCode::MissingG92E0:
        return _u8L(
            "Relative extruder addressing requires resetting the extruder position at each layer "
            "to prevent loss of floating point accuracy. Add \"G92 E0\" to layer_gcode."
        );
    case ErrorCode::FoundG92E0InBeforeLayerGCode:
        return _u8L(
            "\"G92 E0\" was found in before_layer_gcode, which is incompatible with absolute "
            "extruder addressing."
        );
    case ErrorCode::FoundG92E0InLayerGCode:
        return _u8L(
            "\"G92 E0\" was found in layer_gcode, which is incompatible with absolute extruder "
            "addressing."
        );
    case ErrorCode::NoLayers:
        return _u8L(
            "No layers were detected. You might want to repair your STL file(s) or check their "
            "size or thickness and retry."
        );
    case ErrorCode::NoExtrusionInFirstLayer:
        return _u8L("There is an object with no extrusions in the first layer.");
    case ErrorCode::NoExtrusions:
        return _u8L("No extrusions were generated for objects.");
    case ErrorCode::EmptyPrint:
        return _u8L("The print is empty. The model is not printable with current print settings.");
    case ErrorCode::InvalidThumbnailRequest:
        return _u8L("Unable to parse 'thumbnails' config option.");
    case ErrorCode::NoPadGenerated:
        return _u8L("No pad can be generated for this model with the current configuration.");
    case ErrorCode::UnprintableObjects:
        return _u8L(
            "There are unprintable objects. Try to adjust support settings to make the objects "
            "printable."
        );
    case ErrorCode::SettingMustBeEqualForAllExtruders:
        return _u8L("The value needs to be the same for all extruders.");
    case ErrorCode::PlaceholderParser:
        return _u8L("Placeholder parser substitution failed.");
    case ErrorCode::MissingHwConfigNozzleDiameter:
        return _u8L("A tool configuration is missing nozzle diameter in hardware config.");
    case ErrorCode::NoHwConfigTools:
        return _u8L("Invalid hardware configuration, there are no tools");
    case ErrorCode::HwConfigLessMaterialsThanTools:
        return _u8L("Material count is less than tool count");
    case ErrorCode::FailedToParseCustomParameters:
        return _u8L("Unable to parse custom parameters.");
    case ErrorCode::UnsupportedOutputFormat:
        return _u8L("Unsupported output format.");
    }
    return _u8L("Unknown error.");
}

std::string to_display_string(Biz::Slicing::Error error, const Domain::Project& project)
{
    const Domain::ModelObject* object{
        error.model_object_id ? project.find_object_by_id(error.model_object_id->id) : nullptr
    };

    const std::string item_keys_info{
        !error.item_keys.empty() ?
            _u8L("\nSee config keys: ") + boost::join(error.item_keys, ", ") :
            ""
    };
    const std::string object_info{object != nullptr ? _u8L("\nObject: ") + object->name : ""};

    switch (error.code) {
    case Biz::Slicing::ErrorCode::InvalidThumbnailRequest: {
        const auto& payload = std::get<Biz::Slicing::InvalidThumbnailRequestPayload>(error.payload);
        std::string error_str = to_display_string(error.code);
        if (payload.invalid_format)
            error_str += "\n - " + fmt::format(fmt::runtime(_u8L("Invalid input format. Expected vector of dimensions in the following format: \"{}\"")), "XxY/EXT, XxY/EXT, ...");
        if (payload.out_of_range)
            error_str += "\n - " + _u8L("Input value is out of range");
        if (payload.invalid_ext)
            error_str += "\n - " + _u8L("Some extension in the input is invalid");
        return error_str;
    }
    default: return to_display_string(error.code) + item_keys_info + object_info;
    }
}

std::string to_display_string(Biz::Slicing::Warning warning, const Domain::Project& project)
{
    using Biz::Slicing::CustomGCodeReservedKeywordsWarningPayload;
    using Biz::Slicing::EmptyLayersWarningPayload;
    using Biz::Slicing::InvalidToolchangeWarningPayload;
    using Biz::Slicing::StabilityWarningPayload;
    using Biz::Slicing::WarningCode;

    const Domain::ModelObject* object{
        warning.model_object_id ? project.find_object_by_id(warning.model_object_id->id) : nullptr
    };

    std::string message;

    switch (warning.code) {
    case WarningCode::StabilityIssues: {
        auto payload = std::get_if<StabilityWarningPayload>(&warning.payload);
        ASSERT(payload != nullptr, "Expected StabilityWarningPayload for StabilityIssues warning");
        message = _u8L("Detected print stability issues") + ":\n" + payload->recommendations;
        break;
    }

    case WarningCode::EmptyLayers: {
        auto payload = std::get_if<EmptyLayersWarningPayload>(&warning.payload);
        ASSERT(payload != nullptr, "Expected EmptyLayersWarningPayload for EmptyLayers warning");
        size_t shown = std::min(payload->ranges.size(), size_t(3));
        for (size_t i = 0; i < shown; ++i) {
            message += fmt::format(
                           fmt::runtime(_u8L("Empty layers between {0:.2f} and {1:.2f}.")),
                           payload->ranges[i].first,
                           payload->ranges[i].second
                       )
                + "\n";
        }
        if (shown < payload->ranges.size()) {
            message += _u8L("(Some lines not shown)") + std::string("\n");
        }
        message += "\n"
            + _u8L("Make sure the object is printable. "
                   "This is usually caused by negligibly small extrusions or by a faulty model. "
                   "Try to repair the model or change its orientation on the bed.");
        break;
    }

    case WarningCode::CustomGCodeReservedKeywords: {
        auto payload = std::get_if<CustomGCodeReservedKeywordsWarningPayload>(&warning.payload);
        ASSERT(payload != nullptr, "Expected CustomGCodeReservedKeywordsWarningPayload");
        message = fmt::format(
        // TRN {} is list of found reserved keywords
            fmt::runtime(_u8L(
                "In the custom G-code were found reserved keywords:\n"
                "{} \n"
                "This may cause problems in g-code visualization and printing time estimation."
            )),
            payload->reports
        );
        break;
    }

    case WarningCode::InvalidToolchange: {
        auto payload = std::get_if<InvalidToolchangeWarningPayload>(&warning.payload);
        ASSERT(payload != nullptr, "Expected InvalidToolchangeWarningPayload");
        message = fmt::format(
            // TRN {} is a custom G-code
            fmt::runtime(_u8L(
                "G-code Post-Processor encountered an invalid toolchange, maybe from a custom G-code: {}\n"
                "Generated M104 lines may be incorrect."
            )),
            payload->gcode_line
        );
        break;
    }

    case WarningCode::CloseToPrimingRegions:
        message = _u8L(
            "Your print is very close to the priming regions. "
            "Make sure there is no collision."
        );
        break;

    case WarningCode::SupportsTurnedOff:
        message = _u8L(
            "An object has custom support enforcers which will not be used "
            "because supports are disabled.\n"
        );
        break;

    case WarningCode::BedTempsChanged: [[fallthrough]];
    case WarningCode::BedTempsDiffer:
        message = _u8L(
            "Bed temperatures for the used filaments differ significantly.\n"
            "For multi-material prints it is recommended to set the 'Bed temperature by extruder' "
            "and 'Wipe tower extruder'."
        );
        break;

    case WarningCode::FilamentShrinkageDiffer:
        message = _u8L(
            "Filament shrinkage will not be used because filament shrinkage "
            "for the used filaments differs significantly."
        );
        break;

    case WarningCode::WipeTowerNozzleDiameterDiffer:
        message = _u8L(
            "Using the wipe tower for extruders with different nozzle diameters "
            "is experimental, so proceed with caution."
        );
        break;

    case WarningCode::SupportNozzleDiameterDiffer:
        message = _u8L(
            "Printing supports with different nozzle diameters is experimental. "
            "For best results, switch to Organic supports and assign a specific extruder "
            "for supports."
        );
        break;
    case WarningCode::ToolpathOutsideBuildVolume:
        message = _u8L("A toolpath outside the print area was detected.");
        break;

    case WarningCode::GCodeConflict: {
        const auto& detail = std::get<Biz::Slicing::GCodeConflictWarningPayload>(warning.payload);
        // TRN {1} is name of Object1, {2} is name of Object2
        return fmt::format(fmt::runtime(_u8L("Conflicts in G-code paths have been detected at "
            "print height {0:.2f} mm. Please reposition the conflicting objects ({1} <-> {2}) further apart.")),
            detail.height, detail.object_names[0], detail.object_names[1]);
    }

    case WarningCode::XYSizeCompensationIgnoredMultiMaterialPainting:
        message = _u8L(
            "An object has enabled XY Size compensation which will not be used "
            "because it is also multi-material painted.\n"
            "XY Size compensation cannot be combined with multi-material painting."
        );
        break;

    case WarningCode::XYSizeCompensationIgnoredFuzzySkinPainting:
        message = _u8L(
            "An object has enabled XY Size compensation which will not be used "
            "because it is also fuzzy skin painted.\n"
            "XY Size compensation cannot be combined with fuzzy skin painting."
        );
        break;

    case WarningCode::None:
        message = _u8L("Warning: unspecified issue detected.");
        break;
    }

    if (!warning.item_keys.empty())
        message += "\n" + _u8L("See config keys") + ": " + boost::join(warning.item_keys, ", ");

    if (object != nullptr)
        message += "\n" + _u8L("Object") + ": " + object->name;

    return message;
}

std::string to_display_string(Biz::Slicing::ProgressInfo info)
{
    using Biz::Slicing::ProgressInfo;

    switch (info) {
    case ProgressInfo::None:
        return _u8L("Idle");
    case ProgressInfo::Initializing:
        return _u8L("Initializing slicing");
    case ProgressInfo::GeneratingWipeTower:
        return _u8L("Generating wipe tower");
    case ProgressInfo::GeneratingSkirtAndBrim:
        return _u8L("Generating skirt and brim");
    case ProgressInfo::GeneratingGCode:
        return _u8L("Generating G-code");
    case ProgressInfo::GeneratingPerimeters:
        return _u8L("Generating perimeters");
    case ProgressInfo::PreparingInfill:
        return _u8L("Preparing infill");
    case ProgressInfo::MakingInfill:
        return _u8L("Generating infill");
    case ProgressInfo::SearchingSupportSpots:
        return _u8L("Searching for support spots");
    case ProgressInfo::GeneratingSupportMaterial:
        return _u8L("Generating support material");
    case ProgressInfo::EstimatingCurledExtrusions:
        return _u8L("Estimating curled extrusions");
    case ProgressInfo::CalculatingOverhangingPerimeters:
        return _u8L("Calculating overhanging perimeters");
    case ProgressInfo::ProcessingTriangulatedMesh:
        return _u8L("Processing triangulated mesh");
    case ProgressInfo::CheckingStability:
        return _u8L("Checking stability");

    case ProgressInfo::AssemblingModel:
        return _u8L("Assembling model");
    case ProgressInfo::HollowingModel:
        return _u8L("Hollowing model");
    case ProgressInfo::DrillingHoles:
        return _u8L("Drilling holes");
    case ProgressInfo::SlicingModel:
        return _u8L("Slicing model");
    case ProgressInfo::GeneratingSupportPoints:
        return _u8L("Generating support points");
    case ProgressInfo::GeneratingSupportTree:
        return _u8L("Generating support tree");
    case ProgressInfo::GeneratingPad:
        return _u8L("Generating pad");
    case ProgressInfo::SlicingSupports:
        return _u8L("Slicing supports");

    case ProgressInfo::MergingSlicesAndCalculatingStatistics:
        return _u8L("Merging slices and calculating statistics");
    case ProgressInfo::RasterizingLayers:
        return _u8L("Rasterizing layers");
    }
    return _u8L("Unknown progress");
}
} // namespace Slic3r::App
