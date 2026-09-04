#pragma once

#include <boost/container_hash/hash.hpp>
#include <cfloat>
#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "Slic3r/Domain/ConfigValue.hpp"
#include "Slic3r/Domain/PrinterTechnology.hpp"

namespace Slic3r::Domain {

class ConfigDefinitions;

enum class FDMConfigLocation
{
    None,
    Printer,
    Tool,
    Print,
    Filament,
    Project,
    Object,
    Volume,
};

enum class SLAConfigLocation
{
    None,
    Printer,
    Material,
    Print,
    Object,
};

enum class CompatibilityRule
{
    Undefined,
    IgnoreOverrides,
    Min,
    Max,
    Average
};

struct PhysicalPrinterLocation {
    bool operator==(const PhysicalPrinterLocation&) const = default;
    bool operator<(const PhysicalPrinterLocation&) const {
        return false;
    }
};

struct AppConfigLocation
{
    bool operator==(const AppConfigLocation&) const = default;

    bool operator<(const AppConfigLocation&) const
    {
        return false;
    }
};

using ConfigLocation = std::variant<FDMConfigLocation, SLAConfigLocation, PhysicalPrinterLocation, AppConfigLocation>;

std::string get_location_name(const ConfigLocation& location);

// Static definition of a single config item. Contains all info about what the item is,
// as well as rules for diplaying it in the UI. All information about the item is here,
// including where it is supposed to be.
struct ConfigItemDef
{
    bool operator<(const ConfigItemDef& other) const
    {
        return name < other.name;
    }

    std::string name{};
    const std::type_info* type{nullptr};
    std::function<ConfigValue()> init_fn;
    std::function<ConfigValue(const ConfigLocation& config_location)> init_fn_ex;
    ConfigLocation location; // Which box it belongs to. Must not be empty.
    std::set<ConfigLocation> overrides_in; // Which boxes this can be overridden in.
    std::string ratio_over;

    // Non-translated Label of the GUI input field. In case the GUI input fields are grouped in some views,
    // the label defines a short label of a grouped value, while full_label contains a label of a stand-alone field.
    // The full label is shown, when adding an override parameter for an object or a modified object.
    std::string label;
    std::string full_label;

    /**
     * @brief Translation context used for localization.
     *
     * Some localization cases require an additional context string
     * to help translators choose the correct wording.
     */
    std::string i18n_context;

    /**
     * @brief Sets the localization context and returns the source string.
     *
     * Uppercase naming is intentional, as this function mirrors the
     * L_CONTEXT localization marker used by xgettext when parsing
     * translation dictionaries.
     *
     * Ensures that all localization calls within the same ConfigItemDef
     * use the same translation context.
     *
     * @param s Source string.
     * @param context Translation context.
     *
     * @return The original source string.
     */
    const char* L_CONTEXT(const char* s, const char* context)
    {
        if (i18n_context.empty()) {
            i18n_context = context;
        } else if (i18n_context != context) {
            PANIC("All localization contexts inside one ConfigItemDef must be identical");
        }
        return s;
    }

    enum class Category : uint16_t
    {
        Unknown = 0, ///< Default category, throws an error
        Hidden  = 1, ///< Hidden from user, not visible in GUI

        Print_LayersSurfaces      = 100,
        Print_WallsPerimeters     = 101,
        Print_Infill              = 102,
        Print_BedAdhesion         = 103,
        Print_Supports            = 104,
        Print_Speed               = 105,
        Print_MotionDynamics      = 106,
        Print_ExtrusionRetraction = 107,
        Print_MultiMaterial       = 108,
        Print_PrecisionSlicing    = 109,
        Print_CustomGCode         = 110,

        // SLA
        Print_Pad       = 111,
        Print_Hollowing = 112,

        Print_OutputOptions = 120,
        Print_Notes         = 121,

        Filament_MaterialTemperatures = 200,
        Filament_ExtrusionCalibration = 201,
        Filament_Cooling              = 202,
        Filament_MultiMaterial        = 203,
        Filament_Overrides            = 204,
        Filament_CustomGCode          = 205,

        // SLA
        Filament_MaterialPrintingProfile = 206,

        Filament_Notes        = 210,
        Filament_Dependencies = 211,

        Printer_General               = 300,
        Printer_Bed                   = 301,
        Printer_CustomGCode           = 302,
        Printer_MachineLimits         = 303,
        Printer_MultipleExtruders     = 304,
        Printer_SingleExtruderMMSetup = 305,
        Printer_Notes                 = 310,

        Object_Extruders = 400,

        Volume_WipeOptions = 500,

        AppConfig_General  = 600,
        AppConfig_Services = 601,

        PhysicalPrinter_General = 700,
    };

    enum class OptionGroup : uint16_t
    {
        Unknown = 0,

        ///////////////// Print //////////////////

        Print_LayerSurfaces_LayerHeight      = 100,
        Print_LayerSurfaces_TopBottomShells  = 101,
        Print_LayerSurfaces_SurfacePatterns  = 102,
        Print_LayerSurfaces_OnlyOnePerimeter = 103,
        Print_LayerSurfaces_Ironing          = 104,

        Print_WallsPerimeters_Perimeters   = 200,
        Print_WallsPerimeters_Seams        = 201,
        Print_WallsPerimeters_WallsQuality = 202,
        Print_WallsPerimeters_FuzzySkin    = 203,

        Print_Infill_DensityPattern    = 300,
        Print_Infill_InfillCombination = 301,
        Print_Infill_Overlap           = 302,
        Print_Infill_WallAnchoring     = 303,
        Print_Infill_Advanced          = 304,

        Print_BedAdhesion_Brim  = 400,
        Print_BedAdhesion_Skirt = 401,
        Print_BedAdhesion_Raft  = 402,

        Print_Supports_Generation          = 500,
        Print_Supports_SupportGeometry     = 501,
        Print_Supports_PatternDensity      = 502,
        Print_Supports_InterfaceSeparation = 503,
        Print_Supports_OrganicSupports     = 504,
        Print_Supports_SupportHead         = 505,
        Print_Supports_SupportPillar       = 506,
        Print_Supports_SticksJunctions     = 507,

        Print_Speed_FirstLayer           = 600,
        Print_Speed_MainStructure        = 601,
        Print_Speed_Travels              = 602,
        Print_Speed_SupportAndBridges    = 603,
        Print_Speed_DynamicOverhangSpeed = 604,
        Print_Speed_VolumetricSpeed      = 605,
        Print_Speed_PressureEqualizer    = 606,

        Print_MotionDynamics_VerticalLift              = 700,
        Print_MotionDynamics_TravelAvoidance           = 701,
        Print_MotionDynamics_BridgesAcceleration       = 702,
        Print_MotionDynamics_MainStructureAcceleration = 703,
        Print_MotionDynamics_FirstLayerAcceleration    = 704,
        Print_MotionDynamics_TravelsAcceleration       = 705,
        Print_MotionDynamics_WipeTowerAcceleration     = 706,

        Print_ExtrusionRetraction_Nozzle             = 800,
        Print_ExtrusionRetraction_ExtrusionWidth     = 801,
        Print_ExtrusionRetraction_Retraction         = 802,
        Print_ExtrusionRetraction_IdleToolRetraction = 803,

        Print_MultiMaterial_ExtruderAssignment  = 900,
        Print_MultiMaterial_OozePrevention      = 901,
        Print_MultiMaterial_WipeTower           = 902,
        Print_MultiMaterial_BondingInterlocking = 903,
        Print_MultiMaterial_ToolChanges         = 904,

        Print_PrecisionSlicing_SlicingStrategy           = 1000,
        Print_PrecisionSlicing_DimensionalAccuracy       = 1001,
        Print_PrecisionSlicing_PerimeterGenerator        = 1002,
        Print_PrecisionSlicing_ArachnePerimeterGenerator = 1003,
        Print_PrecisionSlicing_ResolutionGCodeData       = 1004,
        Print_PrecisionSlicing_ScriptSubstitutions       = 1005,

        Print_Pad_Pad = 1100,

        Print_Hollowing_Hollowing = 1200,

        Print_CustomGCode = 1300,

        Print_OutputOptions_OutputFile = 1400,

        Print_Notes_Notes = 1500,

        ///////////////// Filament //////////////////

        Filament_MaterialTemperatures_MaterialProperty      = 5000,
        Filament_MaterialTemperatures_BedChamberTemperature = 5001,
        Filament_MaterialTemperatures_NozzleTemperature     = 5002,
        Filament_MaterialTemperatures_Corrections           = 5003,
        Filament_MaterialTemperatures_Exposure              = 5004,

        Filament_ExtrusionCalibration_ExtrusionCalibration = 5100,
        Filament_ExtrusionCalibration_Compensation         = 5101,
        Filament_ExtrusionCalibration_PressureAdvance      = 5102,

        Filament_Cooling_CoolingLogic      = 5200,
        Filament_Cooling_FanControlLimits  = 5201,
        Filament_Cooling_FirstLayers       = 5202,
        Filament_Cooling_CoolingThresholds = 5203,
        Filament_Cooling_DynamicFanSpeed   = 5204,

        Filament_MultiMaterial_MultitoolRamming  = 5300,
        Filament_MultiMaterial_TipShapingCooling = 5301,
        Filament_MultiMaterial_MovementTiming    = 5302,
        Filament_MultiMaterial_WipeTowerPurging  = 5303,
        Filament_MultiMaterial_FlushParameters   = 5304,

        Filament_Overrides_PrintSpeedOverride = 5400,

        Filament_MaterialPrintingProfile_ProfilesSettings = 5600,

        Filament_CustomGCode = 5700,

        Filament_Notes_Notes = 6000,

        ///////////////// Printer //////////////////

        Printer_General_FirmwareGCode            = 10000,
        Printer_General_CapabilitiesFeatures     = 10001,
        Printer_General_SizeClearances           = 10002,
        Printer_General_SequentialPrintingLimits = 10003,
        Printer_General_Advanced                 = 10004,
        Printer_General_Display                  = 10005,
        Printer_General_Tilt                     = 10006,
        Printer_General_Corrections              = 10007,
        Printer_General_Exposure                 = 10008,
        Printer_General_Output                   = 10009,

        Printer_Bed_SizeAndCoordinates = 10100,

        Printer_MachineLimits_General              = 10300,
        Printer_MachineLimits_MaximumAccelerations = 10301,
        Printer_MachineLimits_MaximumFeedrates     = 10302,
        Printer_MachineLimits_JerkLimits           = 10303,
        Printer_MachineLimits_JunctionDeviation    = 10304,
        Printer_MachineLimits_MinimumFeedrates     = 10305,
        Printer_MachineLimits_MinimumAccelerations = 10306,

        Printer_MultipleExtruder_Position = 10400,

        Printer_SingleExtruderMMSetup_SingleExtruderMultimaterialParameters = 10500,

        Printer_CustomGCode = 10600,

        Printer_Notes_Notes = 10700,

        ///////////////// AppConfig //////////////////

        AppConfig_General_General     = 15000,
        AppConfig_General_Application = 15001,

        AppConfig_Services_General       = 15100,
        AppConfig_Services_ServicesSetup = 15101,
    };

    static std::string translate_category(Category category, const PrinterTechnology pt);
    static std::string translate_option_group(OptionGroup option_group);

    // Category of a configuration field, from the GUI perspective. One of: "Layers and Perimeters",
    // "Infill", "Support material", "Speed", "Extruders", "Advanced", "Extrusion Width"
    Category category        = Category::Unknown;
    OptionGroup option_group = OptionGroup::Unknown;
    int order                = 0;
    std::string row_group;
    std::string tooltip; // A tooltip text shown in the GUI.
    std::vector<std::string> units; // Text right from the input field.
    std::string cli; // Format of this parameter on a command line.

    // For text only:
    bool multiline = false; // True for multiline strings.
    bool full_width =
        false; // For text input: If true, the GUI text box spans the complete page width.
    int height = -1; // Height of a multiline GUI text box.

    std::optional<double> min; // <min, max> limit of a numeric input.
    std::optional<double> max; // If not set, the <min, max> is set to <INT_MIN, INT_MAX>

    bool require_tool_parity = false; // Requires number of elements to be same as number of tools

    // The backend expects a single option, but it can be specified multiple times on the frontend.
    // Hence a compatibility rule needs to be specified to obtain a single value from multiple.
    CompatibilityRule compatibility_rule = CompatibilityRule::Undefined;

    static constexpr const char* nocli = "~~nocli";

    // In case we want to show list of choices, the following holds pairs of value - GUI string.
    std::vector<std::pair<std::variant<int, double, std::string>, std::string>> choices;

    // NEEDS MORE WORK (TODO):
    std::vector<std::string>
        aliases; // We can probably clear them in all cases and start fresh. Legacy loading will handle them.
    double max_literal =
        1; // // To check if it's not a typo and a % is missing - TODO Check how this is used.

    enum class GUIType
    { // TODO Go through this one after everything is ported and remove what we don't use.
        undefined,
        i_enum_open, ///< Open enums, integer value could be one of the enumerated values or something else.
        f_enum_open, ///< Open enums, float value could be one of the enumerated values or something else.
        s_enum_open, ///< Open enums, string value could be one of the enumerated values or something else.
        color, ///< Color picker, string value.
        one_string, ///< @deprecated Vector value, but edited as a single string.
        select_close, ///< @deprecated Close parameter, string value could be one of the list values.
        password, ///< Password, string vaule is hidden by asterisk.
        textfield,
        textfields,
        checkbox,
        checkboxes,
        spinbox,
        spinboxes,
        combobox,
        comboboxes,
        points,
        file_picker,
        bed_shape,
        substitutions,
        ramming_params,
        extruder_selection,
        language_selection,
        unit_or_percentage,
    };
    GUIType gui_type = GUIType::undefined;
};

// A collection of definitions of all config items. ConfigItems will keep references into it,
// it has to outlive everything. For the same reason it is read-only after it is constructed.
// It is therefore safe to be used from multiple threads.
class ConfigDefinitions
{
public:
    ConfigDefinitions()                                    = delete;
    ConfigDefinitions(const ConfigDefinitions&)            = delete;
    ConfigDefinitions(ConfigDefinitions&&)                 = delete;
    ConfigDefinitions& operator=(const ConfigDefinitions&) = delete;
    ConfigDefinitions& operator=(ConfigDefinitions&&)      = delete;

    ConfigDefinitions(
        const std::set<ConfigLocation>& acceptable_boxes,
        std::function<void(ConfigDefinitions&)> init_fn
    );

    const std::vector<ConfigItemDef>& defs() const
    {
        return m_defs;
    }

    // Add a config definition. Calling this after ctr finishes is an error.
    ConfigItemDef* add(const std::string_view name, const std::type_info& type);

private:
    void check_valid() const;
    std::vector<ConfigItemDef> m_defs;
    bool m_finalized{false};
    std::set<ConfigLocation> m_acceptable_boxes;
};
} // namespace Slic3r::Domain
