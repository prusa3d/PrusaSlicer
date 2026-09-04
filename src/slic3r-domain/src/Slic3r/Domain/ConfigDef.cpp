#include "Slic3r/Domain/ConfigDef.hpp"

static const std::string& L(const std::string& s)
{
    return s;
}

namespace Slic3r::Domain {

std::string ConfigItemDef::translate_category(Category category, const PrinterTechnology pt)
{
    switch (category) {
    case ConfigItemDef::Category::Hidden:
        return {};
    case ConfigItemDef::Category::Unknown:
        PANIC("The app should have asserted at startup and never actually get this far.");
    default:
        PANIC("All categories must be explicitly handled here, you apparently missed one.");
    case ConfigItemDef::Category::Print_LayersSurfaces:
        return L("Layers & Surfaces");
    case ConfigItemDef::Category::Print_WallsPerimeters:
        return L("Walls & Perimeters");
    case ConfigItemDef::Category::Print_Infill:
        return L("Infill");
    case ConfigItemDef::Category::Print_BedAdhesion:
        return L("Bed adhesion");
    case ConfigItemDef::Category::Print_Supports:
        return L("Supports");
    case ConfigItemDef::Category::Print_Speed:
        return L("Speed");
    case ConfigItemDef::Category::Print_MotionDynamics:
        return L("Motion & Dynamics");
    case ConfigItemDef::Category::Print_ExtrusionRetraction:
        return L("Extrusion & Retraction");
    case ConfigItemDef::Category::Print_MultiMaterial:
        return L("Multi-Material");
    case ConfigItemDef::Category::Print_PrecisionSlicing:
        return L("Precision & Slicing");
    case ConfigItemDef::Category::Print_CustomGCode:
        return L("Custom G-code");
    case ConfigItemDef::Category::Print_Pad:
        return L("Pad");
    case ConfigItemDef::Category::Print_Hollowing:
        return L("Hollowing");
    case ConfigItemDef::Category::Print_OutputOptions:
        return L("Output options");
    case ConfigItemDef::Category::Print_Notes:
        return L("Notes");
    case ConfigItemDef::Category::Filament_MaterialTemperatures:
        return L("Material & Temperatures");
    case ConfigItemDef::Category::Filament_ExtrusionCalibration:
        return L("Extrusion & Calibration");
    case ConfigItemDef::Category::Filament_Cooling:
        return L("Cooling");
    case ConfigItemDef::Category::Filament_MultiMaterial:
        return L("Multi-Material");
    case ConfigItemDef::Category::Filament_Overrides:
        return L("Overrides");
    case ConfigItemDef::Category::Filament_CustomGCode:
        return L("Custom G-code");
    case ConfigItemDef::Category::Filament_MaterialPrintingProfile:
        return L("Material printing profile");
    case ConfigItemDef::Category::Filament_Notes:
        return L("Notes");
    case ConfigItemDef::Category::Filament_Dependencies:
        return L("Dependencies");
    case ConfigItemDef::Category::Printer_General:
        return L("General");
    case ConfigItemDef::Category::Printer_Bed:
        return L("Bed");
    case ConfigItemDef::Category::Printer_CustomGCode:
        return L("Custom G-code");
    case ConfigItemDef::Category::Printer_MachineLimits:
        return L("Machine limits");
    case ConfigItemDef::Category::Printer_MultipleExtruders:
        return L("Multiple extruders");
    case ConfigItemDef::Category::Printer_SingleExtruderMMSetup:
        return L("Single extruder MM setup");
    case ConfigItemDef::Category::Printer_Notes:
        return L("Printer notes");
    case ConfigItemDef::Category::Object_Extruders:
        return L("Extruders");
    case ConfigItemDef::Category::Volume_WipeOptions:
        return L("Wipe options");
    case ConfigItemDef::Category::AppConfig_General:
        return L("General");
    case ConfigItemDef::Category::AppConfig_Services:
        return L("Services");
    case ConfigItemDef::Category::PhysicalPrinter_General:
        return L("General");
    }

    return {};
}

std::string ConfigItemDef::translate_option_group(OptionGroup option_group)
{
    switch (option_group) {
    default:
        PANIC("All option groups must be explicitly handled here, you apparently missed one.");
    case ConfigItemDef::OptionGroup::Unknown:
        return {};
    case ConfigItemDef::OptionGroup::Print_LayerSurfaces_LayerHeight:
        return L("Layer height");
    case ConfigItemDef::OptionGroup::Print_LayerSurfaces_TopBottomShells:
        return L("Top/Bottom shells");
    case ConfigItemDef::OptionGroup::Print_LayerSurfaces_SurfacePatterns:
        return L("Surface patterns");
    case ConfigItemDef::OptionGroup::Print_LayerSurfaces_OnlyOnePerimeter:
        return L("Only one perimeter");
    case ConfigItemDef::OptionGroup::Print_LayerSurfaces_Ironing:
        return L("Ironing");
    case ConfigItemDef::OptionGroup::Print_WallsPerimeters_Perimeters:
        return L("Perimeters");
    case ConfigItemDef::OptionGroup::Print_WallsPerimeters_Seams:
        return L("Seams");
    case ConfigItemDef::OptionGroup::Print_WallsPerimeters_WallsQuality:
        return L("Walls quality");
    case ConfigItemDef::OptionGroup::Print_WallsPerimeters_FuzzySkin:
        return L("Fuzzy skin");
    case ConfigItemDef::OptionGroup::Print_Infill_DensityPattern:
        return L("Density pattern");
    case ConfigItemDef::OptionGroup::Print_Infill_InfillCombination:
        return L("Infill combination");
    case ConfigItemDef::OptionGroup::Print_Infill_Overlap:
        return L("Overlap");
    case ConfigItemDef::OptionGroup::Print_Infill_WallAnchoring:
        return L("Walls anchoring");
    case ConfigItemDef::OptionGroup::Print_Infill_Advanced:
        return L("Advanced");
    case ConfigItemDef::OptionGroup::Print_BedAdhesion_Brim:
        return L("Brim");
    case ConfigItemDef::OptionGroup::Print_BedAdhesion_Skirt:
        return L("Skirt");
    case ConfigItemDef::OptionGroup::Print_BedAdhesion_Raft:
        return L("Raft");
    case ConfigItemDef::OptionGroup::Print_Supports_Generation:
        return L("Generation");
    case ConfigItemDef::OptionGroup::Print_Supports_SupportGeometry:
        return L("Support geometry");
    case ConfigItemDef::OptionGroup::Print_Supports_PatternDensity:
        return L("Pattern density");
    case ConfigItemDef::OptionGroup::Print_Supports_InterfaceSeparation:
        return L("Interface separation");
    case ConfigItemDef::OptionGroup::Print_Supports_OrganicSupports:
        return L("Organic supports");
    case ConfigItemDef::OptionGroup::Print_Supports_SupportHead:
        return L("Support head");
    case ConfigItemDef::OptionGroup::Print_Supports_SupportPillar:
        return L("Support pillar");
    case ConfigItemDef::OptionGroup::Print_Supports_SticksJunctions:
        return L("Connection of the support sticks and junctions");
    case ConfigItemDef::OptionGroup::Print_Speed_FirstLayer:
        return L("First layer");
    case ConfigItemDef::OptionGroup::Print_Speed_MainStructure:
        return L("Main structure");
    case ConfigItemDef::OptionGroup::Print_Speed_Travels:
        return L("Travels");
    case ConfigItemDef::OptionGroup::Print_Speed_SupportAndBridges:
        return L("Support and Bridges");
    case ConfigItemDef::OptionGroup::Print_Speed_DynamicOverhangSpeed:
        return L("Dynamic overhang speed");
    case ConfigItemDef::OptionGroup::Print_Speed_VolumetricSpeed:
        return L("Volumetric speed");
    case ConfigItemDef::OptionGroup::Print_Speed_PressureEqualizer:
        return L("Pressure equalizer");
    case ConfigItemDef::OptionGroup::Print_MotionDynamics_VerticalLift:
        return L("Vertical Lift");
    case ConfigItemDef::OptionGroup::Print_MotionDynamics_TravelAvoidance:
        return L("Travel Avoidance");
    case ConfigItemDef::OptionGroup::Print_MotionDynamics_BridgesAcceleration:
        return L("Bridges Acceleration");
    case ConfigItemDef::OptionGroup::Print_MotionDynamics_MainStructureAcceleration:
        return L("Main Structure Acceleration");
    case ConfigItemDef::OptionGroup::Print_MotionDynamics_FirstLayerAcceleration:
        return L("First layer Acceleration");
    case ConfigItemDef::OptionGroup::Print_MotionDynamics_TravelsAcceleration:
        return L("Travels Acceleration");
    case ConfigItemDef::OptionGroup::Print_MotionDynamics_WipeTowerAcceleration:
        return L("Wipe Tower Acceleration");
    case ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_Nozzle:
        return L("Nozzle");
    case ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_ExtrusionWidth:
        return L("Extrusion Width");
    case ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_Retraction:
        return L("Retraction");
    case ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_IdleToolRetraction:
        return L("Idle Tool Retraction");
    case ConfigItemDef::OptionGroup::Print_MultiMaterial_ExtruderAssignment:
        return L("Extruder Assignment");
    case ConfigItemDef::OptionGroup::Print_MultiMaterial_OozePrevention:
        return L("Ooze prevention");
    case ConfigItemDef::OptionGroup::Print_MultiMaterial_WipeTower:
        return L("Wipe Tower");
    case ConfigItemDef::OptionGroup::Print_MultiMaterial_BondingInterlocking:
        return L("Bonding & Interlocking");
    case ConfigItemDef::OptionGroup::Print_MultiMaterial_ToolChanges:
        return L("Tool changes");
    case ConfigItemDef::OptionGroup::Print_PrecisionSlicing_SlicingStrategy:
        return L("Slicing Strategy");
    case ConfigItemDef::OptionGroup::Print_PrecisionSlicing_DimensionalAccuracy:
        return L("Dimensional Accuracy");
    case ConfigItemDef::OptionGroup::Print_PrecisionSlicing_PerimeterGenerator:
        return L("Perimeter Generator");
    case ConfigItemDef::OptionGroup::Print_PrecisionSlicing_ArachnePerimeterGenerator:
        return L("Arachne perimeter generator");
    case ConfigItemDef::OptionGroup::Print_PrecisionSlicing_ResolutionGCodeData:
        return L("Resolution & G-code Data");
    case ConfigItemDef::OptionGroup::Print_PrecisionSlicing_ScriptSubstitutions:
        return L("Scripts & Substitutions");
    case ConfigItemDef::OptionGroup::Print_Pad_Pad:
        return L("Pad");
    case ConfigItemDef::OptionGroup::Print_Hollowing_Hollowing:
        return L("Hollowing");
    case ConfigItemDef::OptionGroup::Print_OutputOptions_OutputFile:
        return L("Output file");
    case ConfigItemDef::OptionGroup::Print_CustomGCode:
        return L("Custom G-code");
    case ConfigItemDef::OptionGroup::Print_Notes_Notes:
        return L("Notes");
    case ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_MaterialProperty:
        return L("Material Property");
    case ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_BedChamberTemperature:
        return L("Bed & Chamber Temperature");
    case ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_NozzleTemperature:
        return L("Nozzle Temperature");
    case ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_Corrections:
        return L("Corrections");
    case ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_Exposure:
        return L("Exposure");
    case ConfigItemDef::OptionGroup::Filament_ExtrusionCalibration_ExtrusionCalibration:
        return L("Extrusion & Calibration");
    case ConfigItemDef::OptionGroup::Filament_ExtrusionCalibration_Compensation:
        return L("Compensation");
    case ConfigItemDef::OptionGroup::Filament_ExtrusionCalibration_PressureAdvance:
        return L("Pressure Advance");
    case ConfigItemDef::OptionGroup::Filament_Cooling_CoolingLogic:
        return L("Cooling logic");
    case ConfigItemDef::OptionGroup::Filament_Cooling_FanControlLimits:
        return L("Fan Control & Limits");
    case ConfigItemDef::OptionGroup::Filament_Cooling_FirstLayers:
        return L("First Layers");
    case ConfigItemDef::OptionGroup::Filament_Cooling_CoolingThresholds:
        return L("Cooling Thresholds");
    case ConfigItemDef::OptionGroup::Filament_Cooling_DynamicFanSpeed:
        return L("Dynamic Fan Speed");
    case ConfigItemDef::OptionGroup::Filament_MultiMaterial_MultitoolRamming:
        return L("Multitool Ramming");
    case ConfigItemDef::OptionGroup::Filament_MultiMaterial_TipShapingCooling:
        return L("Tip Shaping & Cooling");
    case ConfigItemDef::OptionGroup::Filament_MultiMaterial_MovementTiming:
        return L("Movement & Timing");
    case ConfigItemDef::OptionGroup::Filament_MultiMaterial_WipeTowerPurging:
        return L("Wipe Tower Purging");
    case ConfigItemDef::OptionGroup::Filament_MultiMaterial_FlushParameters:
        return L("Flush parameters (experimental)");
    case ConfigItemDef::OptionGroup::Filament_Overrides_PrintSpeedOverride:
        return L("Print Speed Override");
    case ConfigItemDef::OptionGroup::Filament_MaterialPrintingProfile_ProfilesSettings:
        return L("Profile settings");
    case ConfigItemDef::OptionGroup::Filament_CustomGCode:
        return L("Custom G-code");
    case ConfigItemDef::OptionGroup::Filament_Notes_Notes:
        return L("Notes");
    case ConfigItemDef::OptionGroup::Printer_General_FirmwareGCode:
        return L("Firmware & G-code");
    case ConfigItemDef::OptionGroup::Printer_General_SequentialPrintingLimits:
        return L("Sequential Printing Limits");
    case ConfigItemDef::OptionGroup::Printer_General_SizeClearances:
        return L("Size & Clearances");
    case ConfigItemDef::OptionGroup::Printer_General_Advanced:
        return L("Advanced");
    case ConfigItemDef::OptionGroup::Printer_General_CapabilitiesFeatures:
        return L("Capabilities & Features");
    case ConfigItemDef::OptionGroup::Printer_General_Display:
        return L("Display");
    case ConfigItemDef::OptionGroup::Printer_General_Tilt:
        return L("Tilt");
    case ConfigItemDef::OptionGroup::Printer_General_Corrections:
        return L("Corrections");
    case ConfigItemDef::OptionGroup::Printer_General_Exposure:
        return L("Exposure");
    case ConfigItemDef::OptionGroup::Printer_General_Output:
        return L("Output");
    case ConfigItemDef::OptionGroup::Printer_Bed_SizeAndCoordinates:
        return L("Size & Coordinates");
    case ConfigItemDef::OptionGroup::Printer_MachineLimits_General:
        return L("General");
    case ConfigItemDef::OptionGroup::Printer_MachineLimits_MaximumAccelerations:
        return L("Maximum Accelerations");
    case ConfigItemDef::OptionGroup::Printer_MachineLimits_MaximumFeedrates:
        return L("Maximum Feedrates");
    case ConfigItemDef::OptionGroup::Printer_MachineLimits_JerkLimits:
        return L("Jerk Limits");
    case ConfigItemDef::OptionGroup::Printer_MachineLimits_JunctionDeviation:
        return L("Junction Deviation");
    case ConfigItemDef::OptionGroup::Printer_MachineLimits_MinimumFeedrates:
        return L("Minimum Feedrates");
    case ConfigItemDef::OptionGroup::Printer_MachineLimits_MinimumAccelerations:
        return L("Minimum Accelerations");
    case ConfigItemDef::OptionGroup::Printer_MultipleExtruder_Position:
        return L("Position");
    case ConfigItemDef::OptionGroup::
        Printer_SingleExtruderMMSetup_SingleExtruderMultimaterialParameters:
        return L("Single Extruder Multimaterial parameters");
    case ConfigItemDef::OptionGroup::Printer_CustomGCode:
        return L("Custom G-code");
    case ConfigItemDef::OptionGroup::Printer_Notes_Notes:
        return L("Notes");
    case ConfigItemDef::OptionGroup::AppConfig_General_General:
        return L("General");
    case ConfigItemDef::OptionGroup::AppConfig_General_Application:
        return L("Application");
    case ConfigItemDef::OptionGroup::AppConfig_Services_General:
        return L("General");
    case ConfigItemDef::OptionGroup::AppConfig_Services_ServicesSetup:
        return L("Services Setup");
    }

    return {};
}

std::string get_location_name(const ConfigLocation& location)
{
    return std::visit(
        overloaded{
            [](const FDMConfigLocation location)
            {
                switch (location) {
                case FDMConfigLocation::Printer:
                    return "printer_settings";
                case FDMConfigLocation::Tool:
                    return "toolprint_settings";
                case FDMConfigLocation::Print:
                    return "print_settings";
                case FDMConfigLocation::Filament:
                    return "filament_settings";
                case FDMConfigLocation::Project:
                    return "project_settings";
                case FDMConfigLocation::Object:
                    return "object_settings";
                case FDMConfigLocation::Volume:
                    return "volume_settings";
                default:
                    PANIC("Unknown location");
                }
            },
            [](const SLAConfigLocation location)
            {
                switch (location) {
                case SLAConfigLocation::Printer:
                    return "sla_printer_settings";
                case SLAConfigLocation::Print:
                    return "sla_print_settings";
                case SLAConfigLocation::Material:
                    return "sla_material_settings";
                case SLAConfigLocation::Object:
                    return "sla_object_settings";
                default:
                    PANIC("Unknown location");
                }
            },
            [](const PhysicalPrinterLocation location) { return "physical_printer_settings"; },
            [](const AppConfigLocation location) { return "app_config_settings"; },
        },
        location
    );
}

ConfigDefinitions::ConfigDefinitions(
    const std::set<ConfigLocation>& acceptable_boxes,
    std::function<void(ConfigDefinitions&)> init_fn
) :
    m_acceptable_boxes{acceptable_boxes}
{
    init_fn(*this);
    std::sort(m_defs.begin(), m_defs.end());
    this->check_valid();
    m_finalized = true;
}

ConfigItemDef* ConfigDefinitions::add(const std::string_view name, const std::type_info& type)
{
    ASSERT(!m_finalized);
    return &m_defs.emplace_back(ConfigItemDef{std::string(name), &type});
}

void ConfigDefinitions::check_valid() const
{
    ASSERT(std::is_sorted(m_defs.begin(), m_defs.end()));
    ASSERT(
        std::adjacent_find(
            m_defs.begin(),
            m_defs.end(), // check for duplicates
            [](const auto& a, const auto& b) { return a.name == b.name; }
        )
        == m_defs.end()
    );

    for (const ConfigItemDef& def : m_defs) {
        ASSERT(def.type != nullptr);

        std::visit(overloaded{
            [](const FDMConfigLocation location) {ASSERT(location != FDMConfigLocation::None);},
            [](const SLAConfigLocation location) {ASSERT(location != SLAConfigLocation::None);},
            [](const PhysicalPrinterLocation location) {},
            [](const AppConfigLocation location) {}
        }, def.location);

        ASSERT(!def.overrides_in.contains(def.location));

        // Check that all items are assigned to valid boxes.
        ASSERT(m_acceptable_boxes.contains(def.location));

        ASSERT(
            std::all_of(
                def.overrides_in.begin(),
                def.overrides_in.end(),
                [this](const auto& box) { return m_acceptable_boxes.contains(box); }
            )
        );

        if (def.init_fn) {
            const ConfigValue value{def.init_fn()};
            value.visit([&](auto&& value) { ASSERT(typeid(decltype(value)) == *def.type); });
        } else if (def.init_fn_ex) {
            const ConfigValue value{def.init_fn_ex(def.location)};
            value.visit([&](auto&& value) { ASSERT(typeid(decltype(value)) == *def.type); });

            for (const ConfigLocation& override_location : def.overrides_in) {
                const ConfigValue value{def.init_fn_ex(override_location)};
                value.visit([&](auto&& value) { ASSERT(typeid(decltype(value)) == *def.type); });
            }
        } else {
            PANIC("init_fn or init_fn_ex must be defined");
        }

        // Check that all choices (if used) have the same key type and that it matches the item type.
        if (!def.choices.empty()) {
            for (const auto& [value, str] : def.choices) {
                ASSERT(
                    (*def.type == typeid(std::string) && std::holds_alternative<std::string>(value))
                    || (*def.type == typeid(int) && std::holds_alternative<int>(value))
                    || (*def.type == typeid(double) && std::holds_alternative<double>(value))
                    || (*def.type == typeid(Percentage) && std::holds_alternative<double>(value))
                    || (*def.type == typeid(FloatOrPercentage)
                        && std::holds_alternative<double>(value))
                );
            }
        }

        if (*def.type == typeid(FloatOrPercentage)) {
            // FloatOrPercentage must not have IgnoreOverrides compatibility rule,
            // as it is then impossible to resolve the *single* percentage value,
            // as the base can be a value specified per tool.
            ASSERT(def.compatibility_rule != CompatibilityRule::IgnoreOverrides);
        }

        if (def.category == ConfigItemDef::Category::Unknown) {
            PANIC("All config items must have a category (failed for item: " + def.name + ").");
        }
        if (def.category != ConfigItemDef::Category::Hidden) {
            if (def.label.empty() || def.gui_type == ConfigItemDef::GUIType::undefined) {
                PANIC(
                    "All non-hidden config items must have label and gui_type. (failed for item: "
                    + def.name
                    + ")."
                );
            }
        }
    }
}
} // namespace Slic3r::Domain
