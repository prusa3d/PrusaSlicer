#include "Slic3r/Domain/ConfigDefsFDM.hpp"
#include "Slic3r/Domain/ConfigCommon.hpp"
#include "Slic3r/Domain/ConfigDefUtils.hpp"

#include "Slic3r/Domain/GCodeFlavor.hpp"
#include "Slic3r/Domain/Types.hpp"

#include "boost/algorithm/string.hpp"
#include "boost/format.hpp"

namespace Slic3r::Domain {

// Implementation of FDM configs is done in this file.

// Define our own marking functions, the regular ones are not accessible in Domain.
static const std::string L(const std::string& s) { return s; }

void fdm_config_init_fn(ConfigDefinitions& defs);

using FDMConfigLocation::Printer;
using FDMConfigLocation::Tool;
using FDMConfigLocation::Print;
using FDMConfigLocation::Filament;
using FDMConfigLocation::Project;
using FDMConfigLocation::Object;
using FDMConfigLocation::Volume;

// Define the static object holding all definitions. Provide list of acceptable
// boxes and the init function.
const ConfigDefinitions& get_defs_fdm() {
    static ConfigDefinitions defs_fdm(
        {Printer, Filament, Print, Tool, Object, Volume, Project}, fdm_config_init_fn
    );
    return defs_fdm;
}

// Now define the init function. This function will be called by ConfigDefinitions
// constructor and will fill the definitions with all the necessary data.
void fdm_config_init_fn(ConfigDefinitions& defs)
{
    using Locations = std::set<ConfigLocation>;
    ConfigItemDef* def = nullptr;

    init_common_fdm_sla_config_items(defs, PrinterTechnology::FFF);

    /* TODO - where does this belong to ?
    def = defs.add("profile_vendor", typeid(std::string));
    def->label = L("Profile vendor");
    def->tooltip = L("Name of profile vendor");
    def->cli = ConfigItemDef::nocli;
    def->init_fn = SET_DEFAULT("");

    def = defs.add("profile_version", typeid(std::string));
    def->label = L("Profile version");
    def->tooltip = L("Version of profile");
    def->cli = ConfigItemDef::nocli;
    def->init_fn = SET_DEFAULT("");

    // temporary workaround for compatibility with older Slicer
    {
        def = defs.add("preset_name", typeid(std::string));
        def->init_fn = SET_DEFAULT("");
    }*/


// Defs from void PrintConfigDef::init_fff_params() follow:
    def = defs.add("arc_fitting", typeid(EnumWrapper));
    def->location = Print;
    def->label = L("Arc fitting");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_ResolutionGCodeData;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("Enable to get a G-code file which has G2 and G3 moves. "
                     "G-code resolution will be used as the fitting tolerance.");
    def->init_fn = init_with(ArcFittingType::Disabled, {
        { int(ArcFittingType::Disabled), "disabled", L("Disabled") },
        { int(ArcFittingType::EmitCenter), "emit_center", L("Enabled: G2/3 I J") }
    });

    def = defs.add("automatic_extrusion_widths", typeid(bool));
    def->location = Print;
    def->label = L("Automatic extrusion widths calculation");
    def->option_group = ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_ExtrusionWidth;
    def->category = ConfigItemDef::Category::Print_ExtrusionRetraction;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Automatically calculates extrusion widths based on the nozzle diameter of the currently used extruder. "
                     "This setting is essential for printing with different nozzle diameters.");
    def->init_fn = init_with(false);

    def = defs.add("automatic_infill_combination", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume, Tool };
    def->label = L("Automatic infill combination");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_InfillCombination;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("This feature automatically combines infill of several layers and speeds up your print by extruding thicker "
                     "infill layers while preserving thin perimeters, thus maintaining accuracy.");
    def->init_fn = init_with(false);

    def = defs.add("automatic_infill_combination_max_layer_height", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume, Tool };
    def->label = L("Automatic infill combination - Max layer height");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_InfillCombination;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("Maximum layer height for combining infill when automatic infill combining is enabled. "
                     "Maximum layer height could be specified either as an absolute in millimeters value or as a percentage of nozzle diameter. "
                     "For printing with different nozzle diameters, it is recommended to use percentage value over absolute value.");
    def->init_fn = init_with(FloatOrPercentage(Percentage{100.}));
    def->ratio_over = "nozzle_diameter";
    def->units = {L("mm"), L("%")};;

    // Maximum extruder temperature, bumped to 1500 to support printing of glass.
    const int max_temp = 1500;

    def = defs.add("avoid_crossing_curled_overhangs", typeid(bool));
    def->location = Print;
    def->label = L("Avoid crossing curled overhangs (Experimental)");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_TravelAvoidance;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    // TRN PrintSettings: "Avoid crossing curled overhangs (Experimental)"
    def->tooltip = L("Plan travel moves such that the extruder avoids areas where the filament may be curled up. "
                   "This is mostly happening on steeper rounded overhangs and may cause a crash with the nozzle. "
                   "This feature slows down both the print and the G-code generation.");
    def->init_fn = init_with(false);

    def = defs.add("avoid_crossing_perimeters", typeid(bool));
    def->location = Print;
    def->label = L("Avoid crossing perimeters");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_TravelAvoidance;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Optimize travel moves in order to minimize the crossing of perimeters. "
                   "This is mostly useful with Bowden extruders which suffer from oozing. "
                   "This feature slows down both the print and the G-code generation.");
    def->init_fn = init_with(false);

    def = defs.add("avoid_crossing_perimeters_max_detour", typeid(double));
    def->location = Print;
    def->label = L("Avoid crossing perimeters - Max detour length");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_TravelAvoidance;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("The maximum detour length for avoid crossing perimeters. "
                     "If the detour is longer than this value, avoid crossing perimeters is not applied for this travel path. ");
    def->units = {L("mm")};
    def->min = 0;
    def->max_literal = 1000;
    def->init_fn = init_with(0.0);

    def = defs.add("bed_temperature", typeid(int));
    def->location = Filament;
    def->label = L("Bed other layers");
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_BedChamberTemperature;
    def->category = ConfigItemDef::Category::Filament_MaterialTemperatures;
    def->order = 1;
    def->tooltip = L("Bed temperature for layers after the first one. "
                   "Set this to zero to disable bed temperature control commands in the output.");
    def->units = {L("°C")};
    def->full_label = L("Bed temperature");
    def->min = 0;
    def->max = 300;
    def->init_fn = init_with(0);

    def = defs.add("chamber_temperature", typeid(int));
    def->location = Filament;
    // TRN: Label of a configuration parameter: Nominal chamber temperature.
    def->label = L("Chamber Nominal temperature");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_BedChamberTemperature;
    def->category = ConfigItemDef::Category::Filament_MaterialTemperatures;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->full_label = L("Chamber temperature");
    def->tooltip = L("Required chamber temperature for the print.\nWhen set to zero, "
                     "the nominal chamber temperature is not set in the G-code.");
    def->units = {L("°C")};
    def->min = 0;
    def->max = 1000;
    def->init_fn = init_with(0);

    def = defs.add("chamber_minimal_temperature", typeid(int));
    def->location = Filament;
    // TRN: Label of a configuration parameter: Minimal chamber temperature
    def->label = L("Chamber Minimal temperature");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_BedChamberTemperature;
    def->category = ConfigItemDef::Category::Filament_MaterialTemperatures;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->full_label = L("Chamber minimal temperature");
    def->tooltip = L("Minimal chamber temperature that the printer waits for before the print starts. This allows "
                     "to start the print before the nominal chamber temperature is reached.\nWhen set to zero, "
                     "the minimal chamber temperature is not set in the G-code.");
    def->units = {L("°C")};
    def->min = 0;
    def->max = 1000;
    def->init_fn = init_with(0);

    def = defs.add("bed_temperature_extruder", typeid(int));
    def->location = Print;
    def->label = L("Bed temperature by extruder");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_ExtruderAssignment;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 6;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("The extruder which determines bed temperatures. "
                     "Set to 0 to determine temperatures based on the first printing extruder "
                     "of the first and the second layers.");
    def->min = 0;
    def->init_fn = init_with(0);

    def = defs.add("before_layer_gcode", typeid(std::string));
    def->location = Printer;
    def->label = L("Before layer change G-code");
    def->category = ConfigItemDef::Category::Printer_CustomGCode;
    def->option_group = ConfigItemDef::OptionGroup::Printer_CustomGCode;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This custom code is inserted at every layer change, right before the Z move. "
                   "Note that you can use placeholder variables for all Slic3r settings as well "
                   "as [layer_num] and [layer_z].");
    def->multiline = true;
    def->full_width = true;
    def->height = 5;
    def->init_fn = init_with("");

    def = defs.add("between_objects_gcode", typeid(std::string));
    def->location = Printer;
    def->label = L("Between objects G-code");
    def->category = ConfigItemDef::Category::Printer_CustomGCode;
    def->option_group = ConfigItemDef::OptionGroup::Printer_CustomGCode;
    def->order = 6;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This code is inserted between objects when using sequential printing. By default extruder and bed temperature are reset using non-wait command; however if M104, M109, M140 or M190 are detected in this custom code, Slic3r will not add temperature commands. Note that you can use placeholder variables for all Slic3r settings, so you can put a \"M109 S[first_layer_temperature]\" command wherever you want.");
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->init_fn = init_with("");

    def = defs.add("bottom_solid_layers", typeid(int));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Bottom solid layers");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_TopBottomShells;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->order = 3;
    // def->row_group = L("Solid layers"); Temporary removed, row_group are not supported in PrintTool
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("Number of solid layers to generate on bottom surfaces.");
    def->min = 0;
    def->init_fn = init_with(3);

    def = defs.add("bottom_solid_min_thickness", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_TopBottomShells;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->order = 4;
    // def->row_group = L("Minimum shell thickness"); Temporary removed, row_group are not supported in PrintTool
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("The number of bottom solid layers is increased above bottom_solid_layers if necessary to satisfy "
    				 "minimum thickness of bottom shell.");
    def->label = L("Minimum bottom shell thickness");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("bridge_acceleration", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool };
    def->label = L("Bridge");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_BridgesAcceleration;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This is the acceleration your printer will use for bridges. "
                   "Set zero to disable acceleration control for bridges.");
    def->units = {L("mm/s²")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("bridge_angle", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Bridging angle");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_Advanced;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Bridging angle override. If left to zero, the bridging angle will be calculated "
                   "automatically. Otherwise the provided angle will be used for all bridges. "
                   "Use 180° for zero angle.");
    def->units = {L("°")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("bridge_fan_speed", typeid(int));
    def->location = Filament;
    def->label = L("Bridges fan speed");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_FanControlLimits;
    def->category = ConfigItemDef::Category::Filament_Cooling;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("This fan speed is enforced during all bridges and overhangs.");
    def->units = {L("%")};
    def->min = 0;
    def->max = 100;
    def->init_fn = init_with( 100 );

    def = defs.add("bridge_flow_ratio", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Bridge flow ratio");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_Advanced;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This factor affects the amount of plastic for bridging. "
                   "You can decrease it slightly to pull the extrudates and prevent sagging, "
                   "although default settings are usually good and you should experiment "
                   "with cooling (use a fan) before tweaking this.");
    def->min = 0.;
    def->max = 2.;
    def->init_fn = init_with(1.);

    def = defs.add("top_one_perimeter_type", typeid(EnumWrapper));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Single perimeter on top surfaces");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_OnlyOnePerimeter;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("Use only one perimeter on flat top surface, to give more space to the top infill pattern. Could be applied on topmost surface or all top surfaces.");
    def->init_fn = init_with(
        TopOnePerimeterType::None,
        {{int(TopOnePerimeterType::None), "none", L("Disabled")},
         {int(TopOnePerimeterType::TopSurfaces), "top", L("All top surfaces")},
         {int(TopOnePerimeterType::TopmostOnly), "topmost", L("Topmost surface only")}}
    );

    def = defs.add("only_one_perimeter_first_layer", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Only one perimeter on first layer");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_OnlyOnePerimeter;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Use only one perimeter on the first layer.");
    def->init_fn = init_with(false);

    def = defs.add("bridge_speed", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Bridges");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_SupportAndBridges;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Speed for printing bridges.");
    def->units = {L("mm/s")};
    def->aliases = { "bridge_feed_rate" };
    def->min = 0;
    def->init_fn = init_with(60.);

    def = defs.add("over_bridge_speed", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    // TRN: Label for speed used to print infill above bridges.
    def->label = L("Over bridges");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_SupportAndBridges;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("Speed for printing solid infill above bridges. Set to 0 to use solid infill speed. "
                    "If set as percentage, the speed is calculated over solid infill speed. ");
    def->units = {L("mm/s"), L("%")};;
    def->min = 0;
    def->init_fn = init_with(FloatOrPercentage{0.});
    def->ratio_over = "solid_infill_speed";

    def             = defs.add("enable_dynamic_overhang_speeds", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label      = L("Enable dynamic overhang speeds");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_DynamicOverhangSpeed;
    def->category   = ConfigItemDef::Category::Print_Speed;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip    = L("This setting enables dynamic speed control on overhangs.");
    def->init_fn = init_with(false);

    // TRN PrintSettings : "Dynamic overhang speed"
    auto overhang_speed_setting_description = L("Overhang size is expressed as a percentage of overlap of the extrusion with the previous layer: "
                        "100% would be full overlap (no overhang), while 0% represents full overhang (floating extrusion, bridge). "
                        "Speeds for overhang sizes in between are calculated via linear interpolation. "
                        "If set as percentage, the speed is calculated over the external perimeter speed. "
                        "Note that the speeds generated to gcode will never exceed the max volumetric speed value.");

    def             = defs.add("overhang_speed_0", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label      = L("speed for 0% overlap (bridge)");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_DynamicOverhangSpeed;
    def->category   = ConfigItemDef::Category::Print_Speed;
    def->order = 1;
    def->gui_type   = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip    = overhang_speed_setting_description;
    def->units      = {L("mm/s"), L("%")};
    def->min        = 0;
    def->init_fn = init_with(FloatOrPercentage{15.});
    def->ratio_over = "external_perimeter_speed";

    def             = defs.add("overhang_speed_1", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label      = L("speed for 25% overlap");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_DynamicOverhangSpeed;
    def->category   = ConfigItemDef::Category::Print_Speed;
    def->order = 2;
    def->gui_type   = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip    = overhang_speed_setting_description;
    def->units      = {L("mm/s"), L("%")};
    def->min        = 0;
    def->init_fn = init_with(FloatOrPercentage{15.});
    def->ratio_over = "external_perimeter_speed";

    def             = defs.add("overhang_speed_2", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label      = L("speed for 50% overlap");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_DynamicOverhangSpeed;
    def->category   = ConfigItemDef::Category::Print_Speed;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip    = overhang_speed_setting_description;
    def->units      = {L("mm/s"), L("%")};
    def->min        = 0;
    def->init_fn = init_with(FloatOrPercentage{20.});
    def->ratio_over = "external_perimeter_speed";

    def             = defs.add("overhang_speed_3", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label      = L("speed for 75% overlap");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_DynamicOverhangSpeed;
    def->category   = ConfigItemDef::Category::Print_Speed;
    def->order = 4;
    def->gui_type   = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip    = overhang_speed_setting_description;
    def->units      = {L("mm/s"), L("%")};
    def->min        = 0;
    def->init_fn = init_with(FloatOrPercentage{25.});
    def->ratio_over = "external_perimeter_speed";

    def          = defs.add("enable_dynamic_fan_speeds", typeid(bool));
    def->location = Filament;
    def->label   = L("Enable dynamic fan speeds");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_DynamicFanSpeed;
    def->category   = ConfigItemDef::Category::Filament_Cooling;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("This setting enables dynamic fan speed control on overhangs.");
    def->init_fn = init_with(false);

    // TRN FilamentSettings : "Dynamic fan speeds"
    auto fan_speed_setting_description = L("Overhang size is expressed as a percentage of overlap of the extrusion with the previous layer: "
        "100% would be full overlap (no overhang), while 0% represents full overhang (floating extrusion, bridge). "
        "Fan speeds for overhang sizes in between are calculated via linear interpolation.");

    def           = defs.add("overhang_fan_speed_0", typeid(int));
    def->location = Filament;
    def->label    = L("speed for 0% overlap (bridge)");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_DynamicFanSpeed;
    def->category = ConfigItemDef::Category::Filament_Cooling;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip  = fan_speed_setting_description;
    def->units = {L("%")};
    def->min      = 0;
    def->max      = 100;
    def->init_fn = init_with(0);

    def           = defs.add("overhang_fan_speed_1", typeid(int));
    def->location = Filament;
    def->label    = L("speed for 25% overlap");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_DynamicFanSpeed;
    def->category = ConfigItemDef::Category::Filament_Cooling;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip  = fan_speed_setting_description;
    def->units = {L("%")};
    def->min      = 0;
    def->max      = 100;
    def->init_fn = init_with(0);

    def           = defs.add("overhang_fan_speed_2", typeid(int));
    def->location = Filament;
    def->label    = L("speed for 50% overlap");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_DynamicFanSpeed;
    def->category = ConfigItemDef::Category::Filament_Cooling;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip  = fan_speed_setting_description;
    def->units = {L("%")};
    def->min      = 0;
    def->max      = 100;
    def->init_fn = init_with(0);

    def           = defs.add("overhang_fan_speed_3", typeid(int));
    def->location = Filament;
    def->label    = L("speed for 75% overlap");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_DynamicFanSpeed;
    def->category = ConfigItemDef::Category::Filament_Cooling;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip  = fan_speed_setting_description;
    def->units = {L("%")};
    def->min      = 0;
    def->max      = 100;
    def->init_fn = init_with(0);

    def = defs.add("brim_width", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Brim width");
    def->option_group = ConfigItemDef::OptionGroup::Print_BedAdhesion_Brim;
    def->category = ConfigItemDef::Category::Print_BedAdhesion;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("The horizontal width of the brim that will be printed around each object on the first layer. "
                     "When raft is used, no brim is generated (use raft_first_layer_expansion).");
    def->units = {L("mm")};
    def->min      = 0;
    def->max      = 200;
    def->init_fn  = init_with(5.);

    def = defs.add("brim_type", typeid(EnumWrapper));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Brim type");
    def->option_group = ConfigItemDef::OptionGroup::Print_BedAdhesion_Brim;
    def->category = ConfigItemDef::Category::Print_BedAdhesion;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("The places where the brim will be printed around each object on the first layer.");
    def->init_fn = init_with(
        BrimType::NoBrim,
        {{int(BrimType::NoBrim), "no_brim", L("No brim")},
         {int(BrimType::OuterOnly), "outer_only", L("Outer brim only")},
         {int(BrimType::InnerOnly), "inner_only", L("Inner brim only")},
         {int(BrimType::OuterAndInner), "outer_and_inner", L("Outer and inner brim")}}
    );

    def = defs.add("brim_separation", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Average;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Brim separation gap");
    def->option_group = ConfigItemDef::OptionGroup::Print_BedAdhesion_Brim;
    def->category = ConfigItemDef::Category::Print_BedAdhesion;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Offset of brim from the printed object. The offset is applied after the elephant foot compensation.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(0.);

    /* TODO: Isn't this one legacy? Doesn't the legacy loader remove it ?
    * It is part of s_project_options...

    def = defs.add("colorprint_heights", typeid(std::vector<double>));
    def->label = L("Colorprint height");
    def->tooltip = L("Heights at which a filament change is to occur.");
    def->init_fn = [](ConfigItem& item) { item.vec<double>() = {}; };*/

    /* TODO: How to handle this crap?
    def = defs.add("compatible_printers", typeid(std::vector<std::string>));
    def->label = L("Compatible printers");
    def->init_fn = SET_DEFAULT( new ConfigOptionStrings());
    def->cli = ConfigOptionDef::nocli;

    def = defs.add("compatible_printers_condition", typeid(std::string));
    def->label = L("Compatible printers condition");
    def->tooltip = L("A boolean expression using the configuration values of an active printer profile. "
                   "If this expression evaluates to true, this profile is considered compatible "
                   "with the active printer profile.");
    def->init_fn = SET_DEFAULT( new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    def = defs.add("compatible_prints", typeid(std::vector<std::string>));
    def->label = L("Compatible print profiles");
    def->init_fn = SET_DEFAULT( new ConfigOptionStrings());
    def->cli = ConfigOptionDef::nocli;

    def = defs.add("compatible_prints_condition", typeid(std::string));
    def->label = L("Compatible print profiles condition");
    def->tooltip = L("A boolean expression using the configuration values of an active print profile. "
                   "If this expression evaluates to true, this profile is considered compatible "
                   "with the active print profile.");
    def->init_fn = SET_DEFAULT( new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    // The following value is to be stored into the project file (AMF, 3MF, Config ...)
    // and it contains a sum of "compatible_printers_condition" values over the print and filament profiles.
    def = defs.add("compatible_printers_condition_cummulative", typeid(std::vector<std::string>));
    def->init_fn = SET_DEFAULT( new ConfigOptionStrings());
    def->cli = ConfigOptionDef::nocli;
    def = defs.add("compatible_prints_condition_cummulative", typeid(std::vector<std::string>));
    def->init_fn = SET_DEFAULT( new ConfigOptionStrings());
    def->cli = ConfigOptionDef::nocli;*/

    def = defs.add("complete_objects", typeid(bool));
    def->location = Print;
    def->label = L("Complete individual objects");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_SlicingStrategy;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("When printing multiple objects or copies, this feature will complete "
                   "each object before moving onto next one (and starting it from its bottom layer). "
                   "This feature is useful to avoid the risk of ruined prints. "
                   "Slic3r should warn and prevent you from extruder collisions, but beware.");
    def->init_fn = init_with(false);

    def               = defs.add("cooling", typeid(bool));
    def->location     = Filament;
    def->label        = L("Enable auto cooling");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_CoolingLogic;
    def->category     = ConfigItemDef::Category::Filament_Cooling;
    def->order = 0;
    def->gui_type     = ConfigItemDef::GUIType::checkbox;
    def->tooltip =
        L("This flag enables the automatic cooling logic that adjusts print speed "
          "and fan speed according to layer printing time.");
    def->init_fn = init_with(true);

    def               = defs.add("cooling_slowdown_logic", typeid(EnumWrapper));
    def->location     = Filament;
    def->label        = L("Cooling slowdown logic");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_CoolingLogic;
    def->category     = ConfigItemDef::Category::Filament_Cooling;
    def->order = 1;
    def->gui_type     = ConfigItemDef::GUIType::combobox;
    def->tooltip      = L(
        "Determines how the printer slows down layer printing when the minimum layer time isn't reached. "
             "'Consistent surface' first tries to preserve the print speeds of the first two perimeters by slowing all other features. "
             "Only if this isn't sufficient, it also slows down those first two perimeters. "
             "'Uniform cooling' slows down all print features, including the first two perimeters."
    );
    def->init_fn = init_with(
        CoolingSlowdownLogicType::UniformCooling,
        {
            {int(CoolingSlowdownLogicType::UniformCooling),
             "uniform_cooling",
             L("Uniform cooling")},
            {int(CoolingSlowdownLogicType::ConsistentSurface),
             "consistent_surface",
             L("Consistent surface")},
        }
    );

    def               = defs.add("cooling_perimeter_transition_distance", typeid(double));
    def->location     = Filament;
    def->label        = L("Perimeter transition distance");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_CoolingLogic;
    def->category     = ConfigItemDef::Category::Filament_Cooling;
    def->order = 2;
    def->gui_type     = ConfigItemDef::GUIType::textfield;
    def->tooltip      = L(
        "Distance in millimeters before non-slowed perimeters where the original unslowed print speed is restored. "
             "This reduces print quality issues when transitioning from heavily slowed feature to fast perimeter printing."
    );
    def->units = {L("mm")};
    def->min      = 0;
    def->init_fn  = init_with(0.);

    def = defs.add("cooling_tube_retraction", typeid(double));
    def->location = Printer;
    def->label = L("Cooling tube position");
    def->option_group = ConfigItemDef::OptionGroup::Printer_SingleExtruderMMSetup_SingleExtruderMultimaterialParameters;
    def->category = ConfigItemDef::Category::Printer_SingleExtruderMMSetup;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Distance of the center-point of the cooling tube from the extruder tip.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(91.5);

    def = defs.add("cooling_tube_length", typeid(double));
    def->location = Printer;
    def->label = L("Cooling tube length");
    def->option_group = ConfigItemDef::OptionGroup::Printer_SingleExtruderMMSetup_SingleExtruderMultimaterialParameters;
    def->category = ConfigItemDef::Category::Printer_SingleExtruderMMSetup;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Length of the cooling tube to limit space for cooling moves inside it.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(5.);

    def = defs.add("default_acceleration", typeid(double));
    def->location = Print;
    def->overrides_in = { Tool };
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_MainStructureAcceleration;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->label = L("Default");
    def->tooltip = L("This is the acceleration your printer will be reset to after "
                   "the role-specific acceleration values are used (perimeter/infill). "
                   "Set zero to prevent resetting acceleration at all.");
    def->units = {L("mm/s²")};
    def->min = 0;
    def->init_fn = init_with(0.);

    /* TODO: What about these?
    def = defs.add("default_filament_profile", typeid(std::vector<std::string>));
    def->label = L("Default filament profile");
    def->tooltip = L("Default filament profile associated with the current printer profile. "
                   "On selection of the current printer profile, this filament profile will be activated.");
    def->init_fn = SET_DEFAULT( new ConfigOptionStrings());
    def->cli = ConfigOptionDef::nocli;

    def = defs.add("default_print_profile", typeid(std::string));
    def->label = L("Default print profile");
    def->tooltip = L("Default print profile associated with the current printer profile. "
                   "On selection of the current printer profile, this print profile will be activated.");
    def->init_fn = SET_DEFAULT( new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;*/

    def = defs.add("disable_fan_first_layers", typeid(int));
    def->location = Filament;
    def->label = L("Disable fan for the first");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_FirstLayers;
    def->category = ConfigItemDef::Category::Filament_Cooling;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("You can set this to a positive value to disable fan at all "
                   "during the first layers, so that it does not make adhesion worse.");
    def->units = {L("layers")};
    def->min = 0;
    def->max = 1000;
    def->init_fn = init_with(3);

    def = defs.add("dont_support_bridges", typeid(bool));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::IgnoreOverrides;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Don't support bridges");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_Generation;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Experimental option for preventing support material from being generated "
                   "under bridged areas.");
    def->init_fn = init_with(true);

    /* TODO: What is this?
    def = defs.add("duplicate_distance", typeid(double));
    def->label = L("Distance between copies");
    def->tooltip = L("Distance used for the auto-arrange feature of the plater.");
    def->sidetext = L("mm");
    def->aliases = { "multiply_distance" };
    def->min = 0;
    def->init_fn = SET_DEFAULT(6);*/

    def = defs.add("end_gcode", typeid(std::string));
    def->location = Printer;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->category = ConfigItemDef::Category::Printer_CustomGCode;
    def->option_group = ConfigItemDef::OptionGroup::Printer_CustomGCode;
    def->order = 2;
    def->label = L("End G-code");
    def->tooltip = L("This end procedure is inserted at the end of the output file. "
                   "Note that you can use placeholder variables for all PrusaSlicer settings.");
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->init_fn = init_with("M104 S0 ; turn off temperature\nG28 X0  ; home X axis\nM84     ; disable motors\n");



    /////////////////////////////////////////////////////



    
    def = defs.add("end_filament_gcode", typeid(std::string));
    def->location = Filament;
    def->label = L("End G-code");
    def->category = ConfigItemDef::Category::Filament_CustomGCode;
    def->option_group = ConfigItemDef::OptionGroup::Filament_CustomGCode;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This end procedure is inserted at the end of the output file, before the printer end gcode (and "
                   "before any toolchange from this filament in case of multimaterial printers). "
                   "Note that you can use placeholder variables for all PrusaSlicer settings. "
                   "If you have multiple extruders, the gcode is processed in extruder order.");
    def->multiline = true;
    def->full_width = true;
    def->height = 120;
    def->init_fn = init_with("; Filament-specific end gcode \n;END gcode for filament\n");

    def = defs.add("ensure_vertical_shell_thickness", typeid(EnumWrapper));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Ensure vertical shell thickness");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_WallsQuality;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 7;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("Add solid infill near sloping surfaces to guarantee the vertical shell thickness "
                   "(top+bottom solid layers).");
    def->init_fn = init_with(
        EnsureVerticalShellThickness::Enabled,
        {{int(EnsureVerticalShellThickness::Disabled), "disabled", L("Disabled")},
         {int(EnsureVerticalShellThickness::Partial), "partial", L("Partial")},
         {int(EnsureVerticalShellThickness::Enabled), "enabled", L("Enabled")}}
    );

    def = defs.add("top_fill_pattern", typeid(EnumWrapper));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Top fill pattern");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_SurfacePatterns;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->order = 0;
    def->tooltip = L("Fill pattern for top infill. This only affects the top visible layer, and not its adjacent solid shells.");
    def->cli = "top-fill-pattern|external-fill-pattern|solid-fill-pattern";
    // solid_fill_pattern is an obsolete equivalent to top_fill_pattern/bottom_fill_pattern.
    def->aliases = { "solid_fill_pattern", "external_fill_pattern" };
    def->init_fn = init_with(
        InfillPattern::ipMonotonic,
        {{int(InfillPattern::ipRectilinear), "rectilinear", L("Rectilinear")},
         {int(InfillPattern::ipMonotonic), "monotonic", L("Monotonic")},
         {int(InfillPattern::ipMonotonicLines), "monotoniclines", L("Monotonic Lines")},
         {int(InfillPattern::ipAlignedRectilinear), "alignedrectilinear", L("Aligned Rectilinear")},
         {int(InfillPattern::ipConcentric), "concentric", L("Concentric")},
         {int(InfillPattern::ipHilbertCurve), "hilbertcurve", L("Hilbert Curve")},
         {int(InfillPattern::ipArchimedeanChords), "archimedeanchords", L("Archimedean Chords")},
         {int(InfillPattern::ipOctagramSpiral), "octagramspiral", L("Octagram Spiral")}}
    );

    def = defs.add("bottom_fill_pattern", typeid(EnumWrapper));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Bottom fill pattern");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_SurfacePatterns;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("Fill pattern for bottom infill. This only affects the bottom external visible layer, and not its adjacent solid shells.");
    def->cli = "bottom-fill-pattern|external-fill-pattern|solid-fill-pattern";
    def->aliases = { "solid_fill_pattern", "external_fill_pattern" };
    def->init_fn = init_with(
        InfillPattern::ipMonotonic,
        {{int(InfillPattern::ipRectilinear), "rectilinear", L("Rectilinear")},
         {int(InfillPattern::ipMonotonic), "monotonic", L("Monotonic")},
         {int(InfillPattern::ipMonotonicLines), "monotoniclines", L("Monotonic Lines")},
         {int(InfillPattern::ipAlignedRectilinear), "alignedrectilinear", L("Aligned Rectilinear")},
         {int(InfillPattern::ipConcentric), "concentric", L("Concentric")},
         {int(InfillPattern::ipHilbertCurve), "hilbertcurve", L("Hilbert Curve")},
         {int(InfillPattern::ipArchimedeanChords), "archimedeanchords", L("Archimedean Chords")},
         {int(InfillPattern::ipOctagramSpiral), "octagramspiral", L("Octagram Spiral")}}
    );

    def = defs.add("external_perimeter_extrusion_width", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("External perimeters");
    def->option_group = ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_ExtrusionWidth;
    def->category = ConfigItemDef::Category::Print_ExtrusionRetraction;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("Set this to a non-zero value to set a manual extrusion width for external perimeters. "
                   "If left zero, default extrusion width will be used if set, otherwise 1.125 x nozzle diameter will be used. "
                   "If expressed as percentage (for example 200%), it will be computed over nozzle diameter.");
    def->units = {L("mm"), L("%")};;
    def->min = 0;
    def->max_literal = 50;
    def->init_fn = init_with(FloatOrPercentage{0.});
    def->ratio_over = "nozzle_diameter";

    def = defs.add("external_perimeter_speed", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("External perimeters");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_MainStructure;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("This separate setting will affect the speed of external perimeters (the visible ones). "
                   "If expressed as percentage (for example: 80%) it will be calculated "
                   "on the perimeters speed setting above. Set to zero for auto.");
    def->units = {L("mm/s"), L("%")};
    def->min = 0;
    def->init_fn = init_with(FloatOrPercentage(Percentage{50.}));
    def->ratio_over = "perimeter_speed";

    def = defs.add("external_perimeters_first", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("External perimeters first");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_WallsQuality;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Print contour perimeters from the outermost one to the innermost one "
                   "instead of the default inverse order.");
    def->init_fn = init_with(false);

    def = defs.add("extra_perimeters", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Extra perimeters if needed");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_WallsQuality;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Add more perimeters when needed for avoiding gaps in sloping walls. "
                   "Slic3r keeps adding perimeters, until more than 70% of the loop immediately above "
                   "is supported.");
    def->init_fn = init_with(true);

    def = defs.add("extra_perimeters_on_overhangs", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Extra perimeters on overhangs (Experimental)");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_WallsQuality;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Detect overhang areas where bridges cannot be anchored, and fill them with "
                    "extra perimeter paths. These paths are anchored to the nearby non-overhang area when possible.");
    def->init_fn = init_with(false);

    def = defs.add("extruder", typeid(int));
    def->location = Object;
    def->overrides_in = Locations{ Volume };
    def->label = L("Extruder");
    def->category = ConfigItemDef::Category::Object_Extruders;
    def->gui_type = ConfigItemDef::GUIType::extruder_selection;
    def->tooltip = L("The extruder to use (unless more specific extruder settings are specified). "
                   "This value overrides perimeter and infill extruders, but not the support extruders.");
    def->min = 0;  // 0 = inherit defaults
    def->init_fn = init_with(0);

    def = defs.add("extruder_colour", typeid(std::vector<std::string>));
    def->location = Project;
    def->label = "Extruder Color";
    def->category = ConfigItemDef::Category::Hidden;
    def->gui_type = ConfigItemDef::GUIType::color;
    def->init_fn = init_with(std::vector<std::string>{});

    def = defs.add("extruder_offset", typeid(std::vector<Vec2d>));
    def->location = Printer;
    def->gui_type = ConfigItemDef::GUIType::points;
    def->option_group = ConfigItemDef::OptionGroup::Printer_MultipleExtruder_Position;
    def->category = ConfigItemDef::Category::Printer_MultipleExtruders;
    def->label = L("Extruder offset");
    def->tooltip = L("If your firmware doesn't handle the extruder displacement you need the G-code "
                   "to take it into account. This option lets you specify the displacement of each extruder "
                   "with respect to the first one. It expects positive coordinates (they will be subtracted "
                   "from the XY coordinate).");
    def->units = {L("mm")};
    def->init_fn = init_with(std::vector{Vec2d(0,0)});
    def->require_tool_parity = true;

    /* TODO: shouldn't we remove this crap?
    def = defs.add("extrusion_axis", typeid(std::string));
    def->label = L("Extrusion axis");
    def->tooltip = L("Use this option to set the axis letter associated to your printer's extruder "
                   "(usually E but some printers use A).");
    def->init_fn = SET_DEFAULT("E"));*/

    def = defs.add("extruder_clearance_height", typeid(double));
    def->location = Printer;
    def->category = ConfigItemDef::Category::Printer_General;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_SequentialPrintingLimits;
    def->order = 0;
    def->label = L("Height");
    def->tooltip = L("Only used when 'Print Settings -> Complete individual objects' is active. Set this to the vertical "
                   "distance between your nozzle tip and (usually) the X carriage rods. Used to check for collisions "
                   "with previously printed objects and to prevent them when arranging.\n"
                   "The value is ignored for most Prusa printers, which come with more detailed extruder model.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(20.);

    def = defs.add("extruder_clearance_radius", typeid(double));
    def->location = Printer;
    def->category = ConfigItemDef::Category::Printer_General;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_SequentialPrintingLimits;
    def->order = 1;
    def->label = L("Radius");
    def->tooltip = L("Only used when 'Print Settings -> Complete individual objects' is active. Set this to a radius "
                     "of a nozzle-centered cylinder big enough to enclose the extruder assembly. Used to check for collisions "
                     "with previously printed objects and to prevent them when arranging.\n"
                     "The value is ignored for most Prusa printers, which come with more detailed extruder model.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(20.);    

    def = defs.add("extrusion_multiplier", typeid(double));
    def->location = Filament;
    def->label = L("Extrusion multiplier");
    def->option_group = ConfigItemDef::OptionGroup::Filament_ExtrusionCalibration_ExtrusionCalibration;
    def->category = ConfigItemDef::Category::Filament_ExtrusionCalibration;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This factor changes the amount of flow proportionally. You may need to tweak "
                   "this setting to get nice surface finish and correct single wall widths. "
                   "Usual values are between 0.9 and 1.1. If you think you need to change this more, "
                   "check filament diameter and your firmware E steps.");
    def->max = 2;
    def->init_fn = init_with(1.);

    def = defs.add("extrusion_width", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Default extrusion width");
    def->option_group = ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_ExtrusionWidth;
    def->category = ConfigItemDef::Category::Print_ExtrusionRetraction;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("Set this to a non-zero value to allow a manual extrusion width. "
                   "If left to zero, Slic3r derives extrusion widths from the nozzle diameter "
                   "(see the tooltips for perimeter extrusion width, infill extrusion width etc). "
                   "If expressed as percentage (for example: 230%), it will be computed over nozzle diameter.");
    def->units = {L("mm"), L("%")};;
    def->min = 0;
    def->max = 1000;
    def->max_literal = 50;
    def->init_fn = init_with(FloatOrPercentage{0.});
    def->ratio_over = "nozzle_diameter";

    def = defs.add("fan_always_on", typeid(bool));
    def->location = Filament;
    def->label = L("Keep fan always on");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_FanControlLimits;
    def->category = ConfigItemDef::Category::Filament_Cooling;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("If this is enabled, fan will never be disabled and will be kept running at least "
                   "at its minimum speed. Useful for PLA, harmful for ABS.");
    def->init_fn = init_with(false);

    def = defs.add("fan_below_layer_time", typeid(int));
    def->location = Filament;
    def->label = L("Enable fan if layer print time is below");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_CoolingThresholds;
    def->category = ConfigItemDef::Category::Filament_Cooling;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("If layer print time is estimated below this number of seconds, fan will be enabled "
                   "and its speed will be calculated by interpolating the minimum and maximum speeds.");
    def->units = {L("approximate seconds")};
    def->min = 0;
    def->max = 1000;
    def->init_fn = init_with(60);

    def = defs.add("filament_change_time", typeid(double));
    def->location = Printer;
    def->label = L("Filament change time");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_CapabilitiesFeatures;
    def->category = ConfigItemDef::Category::Printer_General;
    def->order = 5;
    def->tooltip = L("Time required for a single filament change. On a printer with"
         " multiple tools this is the time required for a single toolchange to take place."
         " On a printer with multi-material upgrade, this is the time required"
         " to unload and load a new filament.");
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->min = 0.0;
    def->units = {L("s")};
    def->init_fn = init_with(0.0);

    def = defs.add("filament_colour", typeid(std::string));
    def->location = Filament;
    def->label = L("Color");
    // TODO: This option is temporarily hidden from the UI, until we handle it properly.
    // The plan is to make it std::optional<std::string>. Currently it is used as
    // a fallback, but once it is overridden at project level, it cannot be restored
    // (there is no UI to do it).
    //def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_MaterialProperty;
    def->category = ConfigItemDef::Category::Hidden; //Filament_MaterialTemperatures;
    def->order = 1;
    def->tooltip = L("Leave unused if the filament profile isn't tied to a specific color. "
                     "Color can still be set at project-level.");
    def->gui_type = ConfigItemDef::GUIType::color;
    def->init_fn = init_with("#29B2B2");

    def = defs.add("filament_notes", typeid(std::string));
    def->location = Filament;
    def->label = L("Filament notes");
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->category = ConfigItemDef::Category::Filament_Notes;
    def->order = 2;
    def->option_group = ConfigItemDef::OptionGroup::Filament_Notes_Notes;
    def->tooltip = L("You can put your notes regarding the filament here.");
    def->multiline = true;
    def->full_width = true;
    def->height = 13;
    def->init_fn = init_with("");

    def = defs.add("filament_max_volumetric_speed", typeid(double));
    def->location = Filament;
    def->label = L("Max volumetric speed");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Overrides_PrintSpeedOverride;
    def->category = ConfigItemDef::Category::Filament_Overrides;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Maximum volumetric speed allowed for this filament. Limits the maximum volumetric "
                   "speed of a print to the minimum of print and filament volumetric speed. "
                   "Set to zero for no limit.");
    def->units = {L("mm³/s")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("filament_infill_max_speed", typeid(double));
    def->location = Filament;
    def->label = L("Max non-crossing infill speed");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Overrides_PrintSpeedOverride;
    def->category = ConfigItemDef::Category::Filament_Overrides;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Maximum speed allowed for this filament while printing infill without "
                     "any self intersections in a single layer. "
                     "Set to zero for no limit.");
    def->units = {L("mm/s")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("filament_infill_max_crossing_speed", typeid(double));
    def->location = Filament;
    def->label = L("Max crossing infill speed");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Overrides_PrintSpeedOverride;
    def->category = ConfigItemDef::Category::Filament_Overrides;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Maximum speed allowed for this filament while printing infill with "
                     "self intersections in a single layer. "
                     "Set to zero for no limit.");
    def->units = {L("mm/s")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("filament_loading_speed", typeid(double));
    def->location = Filament;
    def->label = L("Loading speed");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MultiMaterial_MovementTiming;
    def->category = ConfigItemDef::Category::Filament_MultiMaterial;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Speed used for loading the filament on the wipe tower.");
    def->units = {L("mm/s")};
    def->min = 0;
    def->init_fn = init_with(28.);

    def = defs.add("filament_loading_speed_start", typeid(double));
    def->location = Filament;
    def->label = L("Loading speed at the start");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MultiMaterial_MovementTiming;
    def->category = ConfigItemDef::Category::Filament_MultiMaterial;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Speed used at the very beginning of loading phase.");
    def->units = {L("mm/s")};
    def->min = 0;
    def->init_fn = init_with(3.);

    def = defs.add("filament_unloading_speed", typeid(double));
    def->location = Filament;
    def->label = L("Unloading speed");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MultiMaterial_MovementTiming;
    def->category = ConfigItemDef::Category::Filament_MultiMaterial;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Speed used for unloading the filament on the wipe tower (does not affect "
                      " initial part of unloading just after ramming).");
    def->units = {L("mm/s")};
    def->min = 0;
    def->init_fn = init_with(90.);

    def = defs.add("filament_unloading_speed_start", typeid(double));
    def->location = Filament;
    def->label = L("Unloading speed at the start");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MultiMaterial_MovementTiming;
    def->category = ConfigItemDef::Category::Filament_MultiMaterial;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Speed used for unloading the tip of the filament immediately after ramming.");
    def->units = {L("mm/s")};
    def->min = 0;
    def->init_fn = init_with(100.);

    def = defs.add("filament_toolchange_delay", typeid(double));
    def->location = Filament;
    def->label = L("Delay after unloading");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MultiMaterial_TipShapingCooling;
    def->category = ConfigItemDef::Category::Filament_MultiMaterial;
    def->order = 6;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Time to wait after the filament is unloaded. "
                   "May help to get reliable toolchanges with flexible materials "
                   "that may need more time to shrink to original dimensions.");
    def->units = {L("s")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("filament_stamping_loading_speed", typeid(double));
    def->location = Filament;
    def->label = L("Stamping loading speed");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MultiMaterial_TipShapingCooling;
    def->category = ConfigItemDef::Category::Filament_MultiMaterial;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Speed used for stamping.");
    def->units = {L("mm/s")};
    def->min = 0;
    def->init_fn = init_with(20.);

    def = defs.add("filament_stamping_distance", typeid(double));
    def->location = Filament;
    def->label = L("Stamping distance measured from the center of the cooling tube");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MultiMaterial_TipShapingCooling;
    def->category = ConfigItemDef::Category::Filament_MultiMaterial;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("If set to nonzero value, filament is moved toward the nozzle between the individual cooling moves (\"stamping\"). "
                     "This option configures how long this movement should be before the filament is retracted again.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("filament_cooling_moves", typeid(int));
    def->location = Filament;
    def->label = L("Number of cooling moves");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MultiMaterial_TipShapingCooling;
    def->category = ConfigItemDef::Category::Filament_MultiMaterial;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("Filament is cooled by being moved back and forth in the "
                   "cooling tubes. Specify desired number of these moves.");
    def->max = 0;
    def->max = 20;
    def->init_fn = init_with(4);

    def = defs.add("filament_cooling_initial_speed", typeid(double));
    def->location = Filament;
    def->label = L("Speed of the first cooling move");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MultiMaterial_TipShapingCooling;
    def->category = ConfigItemDef::Category::Filament_MultiMaterial;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Cooling moves are gradually accelerating beginning at this speed.");
    def->units = {L("mm/s")};
    def->min = 0;
    def->init_fn = init_with(2.2);

    def = defs.add("filament_minimal_purge_on_wipe_tower", typeid(double));
    def->location = Filament;
    def->label = L("Minimal purge on wipe tower");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MultiMaterial_WipeTowerPurging;
    def->category = ConfigItemDef::Category::Filament_MultiMaterial;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("After a tool change, the exact position of the newly loaded filament inside "
                     "the nozzle may not be known, and the filament pressure is likely not yet stable. "
                     "Before purging the print head into an infill or a sacrificial object, Slic3r will always prime "
                     "this amount of material into the wipe tower to produce successive infill or sacrificial object extrusions reliably.");
    def->units = {L("mm³")};
    def->min = 0;
    def->init_fn = init_with(15.);

    def = defs.add("filament_cooling_final_speed", typeid(double));
    def->location = Filament;
    def->label = L("Speed of the last cooling move");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MultiMaterial_TipShapingCooling;
    def->category = ConfigItemDef::Category::Filament_MultiMaterial;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Cooling moves are gradually accelerating towards this speed.");
    def->units = {L("mm/s")};
    def->min = 0;
    def->init_fn = init_with(3.4);

    def = defs.add("filament_purge_multiplier", typeid(Percentage));
    def->location = Filament;
    def->label = L("Purge volume multiplier");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MultiMaterial_WipeTowerPurging;
    def->category = ConfigItemDef::Category::Filament_MultiMaterial;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Purging volume on the wipe tower is determined by 'multimaterial_purging' in Printer Settings. "
                     "This option allows to modify the volume on filament level. "
                     "Note that the project can override this by setting project-specific values.");
    def->units = {L("%")};
    def->min = 0;
    def->init_fn = init_with(Percentage{100.});

    def = defs.add("filament_ramming_parameters", typeid(std::string));
    def->location = Filament;
    def->label = L("Ramming parameters");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MultiMaterial_TipShapingCooling;
    def->category = ConfigItemDef::Category::Filament_MultiMaterial;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::ramming_params;
    def->tooltip = L("This string is edited by RammingDialog and contains ramming specific parameters.");
    def->init_fn = init_with("120 100 6.6 6.8 7.2 7.6 7.9 8.2 8.7 9.4 9.9 10.0|"
       " 0.05 6.6 0.45 6.8 0.95 7.8 1.45 8.3 1.95 9.7 2.45 10 2.95 7.6 3.45 7.6 3.95 7.6 4.45 7.6 4.95 7.6");

    def = defs.add("filament_ramming_temperature_delta", typeid(int));
    def->location = Filament;
    def->label = L("Ramming temperature variation");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MultiMaterial_MultitoolRamming;
    def->category = ConfigItemDef::Category::Filament_MultiMaterial;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("Temperature difference to be applied right before ramming. The value can be negative.");
    def->units = {"∆°C"};
    def->init_fn = init_with(0);

    def = defs.add("filament_ramming_initial_delay", typeid(double));
    def->location = Filament;
    def->label = L("Pause before ramming");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MultiMaterial_MultitoolRamming;;
    def->category = ConfigItemDef::Category::Filament_MultiMaterial;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->units = {L("s")};
    def->tooltip = L("Time in seconds that the printer will remain idle before ramming, allowing the melt zone temperature to equalize.");
    def->init_fn = init_with(0.0);

    def = defs.add("filament_multitool_ramming", typeid(bool));
    def->location = Filament;
    def->label = L("Enable ramming for multitool setups");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MultiMaterial_MultitoolRamming;
    def->category = ConfigItemDef::Category::Filament_MultiMaterial;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Perform ramming when using multitool printer (i.e. when the 'Single Extruder Multimaterial' in Printer Settings is unchecked). "
                     "When checked, a small amount of filament is rapidly extruded on the wipe tower just before the toolchange. "
                     "This option is only used when the wipe tower is enabled.");
    def->init_fn = init_with(false);

    def = defs.add("filament_multitool_ramming_volume", typeid(double));
    def->location = Filament;
    def->label = L("Multitool ramming volume");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MultiMaterial_MultitoolRamming;
    def->category = ConfigItemDef::Category::Filament_MultiMaterial;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("The volume to be rammed before the toolchange.");
    def->units = {L("mm³")};
    def->min = 0;
    def->init_fn = init_with(10.);

    def = defs.add("filament_multitool_ramming_flow", typeid(double));
    def->location = Filament;
    def->label = L("Multitool ramming flow");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MultiMaterial_MultitoolRamming;
    def->category = ConfigItemDef::Category::Filament_MultiMaterial;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Flow used for ramming the filament before the toolchange.");
    def->units = {L("mm³/s")};
    def->min = 0;
    def->init_fn = init_with(10.);

    def               = defs.add("filament_flush_volume", typeid(double));
    def->location     = Filament;
    def->label        = L("Flush volume");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MultiMaterial_FlushParameters;
    def->category     = ConfigItemDef::Category::Filament_MultiMaterial;
    def->order        = 0;
    def->gui_type     = ConfigItemDef::GUIType::textfield;
    def->tooltip =
        L("Volume of filament to flush during a tool change. "
          "Used in custom toolchange G-code.");
    def->units = {L("mm³")};
    def->min      = 0;
    def->init_fn  = init_with(0.);

    def               = defs.add("filament_flush_speed", typeid(double));
    def->location     = Filament;
    def->label        = L("Flush speed");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MultiMaterial_FlushParameters;
    def->category     = ConfigItemDef::Category::Filament_MultiMaterial;
    def->order        = 1;
    def->gui_type     = ConfigItemDef::GUIType::textfield;
    def->tooltip =
        L("Extrusion speed used for flushing during a tool change. "
          "Used in custom toolchange G-code.");
    def->units = {L("mm/s")};
    def->min      = 0;
    def->init_fn  = init_with(0.);

    def = defs.add("filament_diameter", typeid(double));
    def->location = Filament;
    def->label = L("Diameter");
    def->option_group = ConfigItemDef::OptionGroup::Filament_ExtrusionCalibration_ExtrusionCalibration;
    def->category = ConfigItemDef::Category::Filament_ExtrusionCalibration;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Enter your filament diameter here. Good precision is required, so use a caliper "
                   "and do multiple measurements along the filament, then compute the average.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(1.75);

    def = defs.add("filament_density", typeid(double));
    def->location = Filament;
    def->label = L("Density");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_MaterialProperty;
    def->category = ConfigItemDef::Category::Filament_MaterialTemperatures;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Enter your filament density here. This is only for statistical information. "
                   "A decent way is to weigh a known length of filament and compute the ratio "
                   "of the length to volume. Better is to calculate the volume directly through displacement.");
    def->units = {L("g/cm³")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("filament_type", typeid(std::string));
    def->location = Filament;
    def->label = L("Filament type");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_MaterialProperty;
    def->category = ConfigItemDef::Category::Filament_MaterialTemperatures;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::s_enum_open;
    def->tooltip = L("The filament material type for use in custom G-codes.");
    def->choices = {
        { std::string("PLA"),  std::string("PLA")   },
        { std::string("PET"),  std::string("PET")   },
        { std::string("ABS"),  std::string("ABS")   },
        { std::string("ASA"),  std::string("ASA")   },
        { std::string("FLEX"), std::string("FLEX")  },
        { std::string("HIPS"), std::string("HIPS")  },
        { std::string("EDGE"), std::string("EDGE")  },
        { std::string("NGEN"), std::string("NGEN")  },
        { std::string("PA"),   std::string("PA")    },
        { std::string("NYLON"),std::string("NYLON") },
        { std::string("PVA"),  std::string("PVA")   },
        { std::string("PC"),   std::string("PC")    },
        { std::string("PP"),   std::string("PP")    },
        { std::string("PEI"),  std::string("PEI")   },
        { std::string("PEEK"), std::string("PEEK")  },
        { std::string("PEKK"), std::string("PEKK")  },
        { std::string("POM"),  std::string("POM")   },
        { std::string("PSU"),  std::string("PSU")   },
        { std::string("PVDF"), std::string("PVDF")  },
        { std::string("SCAFF"),std::string("SCAFF") },
    };
    def->init_fn = init_with("PLA");

    def = defs.add("filament_soluble", typeid(bool));
    def->location = Filament;
    def->label = L("Soluble material");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_MaterialProperty;
    def->category = ConfigItemDef::Category::Filament_MaterialTemperatures;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Soluble material is most likely used for a soluble support.");
    def->init_fn = init_with(false);

    def = defs.add("filament_abrasive", typeid(bool));
    def->location = Filament;
    def->label = L("Abrasive material");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_MaterialProperty;
    def->category = ConfigItemDef::Category::Filament_MaterialTemperatures;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("This flag means that the material is abrasive and requires a hardened nozzle. The value is used by the printer to check it.");
    def->init_fn = init_with(false);

    def = defs.add("filament_cost", typeid(double));
    def->location = Filament;
    def->label = L("Cost");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_MaterialProperty;
    def->category = ConfigItemDef::Category::Filament_MaterialTemperatures;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Enter your filament cost per kg here. This is only for statistical information.");
    def->units = {L("money/kg")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("filament_spool_weight", typeid(double));
    def->location = Filament;
    def->label = L("Spool weight");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_MaterialProperty;
    def->category = ConfigItemDef::Category::Filament_MaterialTemperatures;
    def->order = 6;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Enter weight of the empty filament spool. "
                     "One may weigh a partially consumed filament spool before printing and one may compare the measured weight "
                     "with the calculated weight of the filament with the spool to find out whether the amount "
                     "of filament on the spool is sufficient to finish the print.");
    def->units = {L("g")};
    def->min = 0;
    def->init_fn = init_with(0.);

    /* TODO: What about this?
    def = defs.add("filament_settings_id", typeid(std::vector<std::string>));
    def->init_fn = SET_DEFAULT( new ConfigOptionStrings { "" });
    def->cli = ConfigOptionDef::nocli;*/

    def = defs.add("filament_vendor", typeid(std::string));
    def->location = Filament;
    def->category = ConfigItemDef::Category::Hidden;
    def->cli = ConfigItemDef::nocli;
    def->init_fn = init_with(L("(Unknown)"));

    def = defs.add("filament_shrinkage_compensation_xy", typeid(Percentage));
    def->location = Filament;
    def->option_group = ConfigItemDef::OptionGroup::Filament_ExtrusionCalibration_Compensation;
    def->category = ConfigItemDef::Category::Filament_ExtrusionCalibration;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->label = L("Shrinkage compensation XY");
    def->tooltip = L("Enter your filament shrinkage percentages for the X and Y axes here to apply scaling of the object to "
                     "compensate for shrinkage in the X and Y axes. For example, if you measured 99mm instead of 100mm, "
                     "enter 1%.");
    def->units = {L("%")};
    def->min = -10.;
    def->max = 10.;
    def->init_fn = init_with(Percentage{0.});

    def = defs.add("filament_shrinkage_compensation_z", typeid(Percentage));
    def->location = Filament;
    def->option_group = ConfigItemDef::OptionGroup::Filament_ExtrusionCalibration_Compensation;
    def->category = ConfigItemDef::Category::Filament_ExtrusionCalibration;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->label = L("Shrinkage compensation Z");
    def->tooltip = L("Enter your filament shrinkage percentages for the Z axis here to apply scaling of the object to "
                     "compensate for shrinkage in the Z axis. For example, if you measured 99mm instead of 100mm, "
                     "enter 1%.");
    def->units = {L("%")};
    def->min = -10.;
    def->max = 10.;
    def->init_fn = init_with(Percentage{0.});

    def = defs.add("fill_angle", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Fill angle");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_Advanced;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Default base angle for infill orientation. Cross-hatching will be applied to this. "
                   "Bridges will be infilled using the best direction Slic3r can detect, so this setting "
                   "does not affect them.");
    def->units = {L("°")};
    def->min = 0;
    def->max = 360;
    def->init_fn = init_with(45.);

    def = defs.add("fill_density", typeid(Percentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Fill density");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_DensityPattern;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::f_enum_open;
    def->tooltip = L("Density of internal infill, expressed in the range 0% - 100%.");
    def->units = {L("%")};
    def->min = 0;
    def->max = 100;
    def->choices = {
        { 0., "0%" },
        { 5., "5%" },
        { 10., "10%" },
        { 15., "15%" },
        { 20., "20%" },
        { 25., "25%" },
        { 30., "30%" },
        { 40., "40%" },
        { 50., "50%" },
        { 60., "60%" },
        { 70., "70%" },
        { 80., "80%" },
        { 90., "90%" },
        { 100., "100%" }
    };
    def->init_fn = init_with(Percentage{20.});

    def = defs.add("fill_pattern", typeid(EnumWrapper));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Fill pattern");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_DensityPattern;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("Fill pattern for general low-density infill.");
    def->init_fn = init_with(
        InfillPattern::ipStars,
        {{int(InfillPattern::ipRectilinear), "rectilinear", L("Rectilinear")},
         {int(InfillPattern::ipAlignedRectilinear), "alignedrectilinear", L("Aligned Rectilinear")},
         {int(InfillPattern::ipGrid), "grid", L("Grid")},
         {int(InfillPattern::ipTriangles), "triangles", L("Triangles")},
         {int(InfillPattern::ipStars), "stars", L("Stars")},
         {int(InfillPattern::ipCubic), "cubic", L("Cubic")},
         {int(InfillPattern::ipLine), "line", L("Line")},
         {int(InfillPattern::ipConcentric), "concentric", L("Concentric")},
         {int(InfillPattern::ipHoneycomb), "honeycomb", L("Honeycomb")},
         {int(InfillPattern::ip3DHoneycomb), "3dhoneycomb", L("3D Honeycomb")},
         {int(InfillPattern::ipGyroid), "gyroid", L("Gyroid")},
         {int(InfillPattern::ipHilbertCurve), "hilbertcurve", L("Hilbert Curve")},
         {int(InfillPattern::ipArchimedeanChords), "archimedeanchords", L("Archimedean Chords")},
         {int(InfillPattern::ipOctagramSpiral), "octagramspiral", L("Octagram Spiral")},
         {int(InfillPattern::ipAdaptiveCubic), "adaptivecubic", L("Adaptive Cubic")},
         {int(InfillPattern::ipSupportCubic), "supportcubic", L("Support Cubic")},
         {int(InfillPattern::ipLightning), "lightning", L("Lightning")},
         {int(InfillPattern::ipZigZag), "zigzag", L("Zig Zag")}}
    );

    def = defs.add("first_layer_acceleration", typeid(double));
    def->location = Print;
    def->overrides_in = { Tool };
    def->label = L("First layer");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_FirstLayerAcceleration;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This is the acceleration your printer will use for first layer. Set zero "
                   "to disable acceleration control for first layer.");
    def->units = {L("mm/s²")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("first_layer_acceleration_over_raft", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("First object layer over raft interface");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_FirstLayerAcceleration;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This is the acceleration your printer will use for first layer of object above raft interface. Set zero "
                   "to disable acceleration control for first layer of object above raft interface.");
    def->units = {L("mm/s²")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("first_layer_bed_temperature", typeid(int));
    def->location = Filament;
    def->label = L("Bed First layer");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_BedChamberTemperature;
    def->category = ConfigItemDef::Category::Filament_MaterialTemperatures;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->full_label = L("First layer bed temperature");
    def->tooltip = L("Heated build plate temperature for the first layer. Set this to zero to disable "
                   "bed temperature control commands in the output.");
    def->units = {L("°C")};
    def->max = 0;
    def->max = 300;
    def->init_fn = init_with(0);

    def = defs.add("first_layer_extrusion_width", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = { Tool };
    def->label = L("First layer");
    def->option_group = ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_ExtrusionWidth;
    def->category = ConfigItemDef::Category::Print_ExtrusionRetraction;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("Set this to a non-zero value to set a manual extrusion width for first layer. "
                   "You can use this to force fatter extrudates for better adhesion. If expressed "
                   "as percentage (for example 120%) it will be computed over nozzle_diameter. "
                   "If set to zero, it will use the default extrusion width.");
    def->units = {L("mm"), L("%")};;
    def->min = 0;
    def->max_literal = 50;
    def->init_fn = init_with(FloatOrPercentage(Percentage{150.}));
    def->ratio_over = "nozzle_diameter";

    def = defs.add("first_layer_height", typeid(FloatOrPercentage));
    def->location = Print;
    def->label = L("First layer height");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_LayerHeight;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->order = 0;
    def->tooltip = L("When printing with very low layer heights, you might still want to print a thicker "
                   "bottom layer to improve adhesion and tolerance for non perfect build plates.");
    def->units = {L("mm"), L("%")};;
    def->min = 0;
    def->init_fn = init_with(FloatOrPercentage{0.35});
    def->ratio_over = "layer_height";

    // This parameter does not exist on the backend.
    def = defs.add("first_layer_speed", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = { Tool };
    def->label = L("First layer speed");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_FirstLayer;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("If expressed as absolute value in mm/s, this speed will be applied to all the print moves "
                   "of the first layer, regardless of their type. If expressed as a percentage "
                   "(for example: 40%) it will scale the default speeds.");
    def->units = {L("mm/s"), L("%")};
    def->min = 0;
    def->max_literal = 20;
    def->init_fn = init_with(FloatOrPercentage{30.});
    def->ratio_over = "none"; // It should be never resolved, as backend does not know about it.

    def = defs.add("first_layer_infill_speed", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = { Tool };
    def->label = L("First layer infill speed");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_FirstLayer;
    def->category = ConfigItemDef::Category::Hidden;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("If expressed as absolute value in mm/s, this speed will be applied to the infill print moves "
                   "of the first layer. If expressed as a percentage "
                   "(for example: 40%) it will be a percantage of the infill speed. "
                   "Note that 0 means that the \"First layer speed\" value will be used.");
    def->units = {L("mm/s"), L("%")};
    def->min = 0;
    def->init_fn = init_with(FloatOrPercentage{0.});
    def->ratio_over = "infill_speed";

    def = defs.add("first_layer_solid_infill_speed", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = { Tool };
    def->label = L("First layer solid infill speed");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_FirstLayer;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("If expressed as absolute value in mm/s, this speed will be applied to the solid infill print moves "
                   "of the first layer. If expressed as a percentage "
                   "(for example: 40%) it will be a percantage of the solid infill speed. "
                   "Note that 0 means that the \"First layer speed\" value will be used.");
    def->units = {L("mm/s"), L("%")};
    def->min = 0;
    def->init_fn = init_with(FloatOrPercentage{0.});
    def->ratio_over = "solid_infill_speed";

    def = defs.add("first_layer_perimeter_speed", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = { Tool };
    def->label = L("First layer perimeter speed");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_FirstLayer;
    def->category = ConfigItemDef::Category::Hidden;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("If expressed as absolute value in mm/s, this speed will be applied to the perimeter print moves "
                   "of the first layer. If expressed as a percentage "
                   "(for example: 40%) it will be a percantage of the perimeter speed. "
                   "Note that 0 means that the \"First layer speed\" value will be used.");
    def->units = {L("mm/s"), L("%")};
    def->min = 0;
    def->init_fn = init_with(FloatOrPercentage{0.});
    def->ratio_over = "perimeter_speed";

    def = defs.add("first_layer_external_perimeter_speed", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = { Tool };
    def->label = L("First layer perimeter speed");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_FirstLayer;
    def->category = ConfigItemDef::Category::Hidden;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("If expressed as absolute value in mm/s, this speed will be applied to the external perimeter print moves "
                   "of the first layer. If expressed as a percentage "
                   "(for example: 40%) it will be a percantage of the external perimeter speed. "
                   "Note that 0 means that the \"First layer speed\" value will be used.");
    def->units = {L("mm/s"), L("%")};
    def->min = 0;
    def->init_fn = init_with(FloatOrPercentage{0.});
    def->ratio_over = "external_perimeter_speed";

    def = defs.add("first_layer_support_material_speed", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = { Tool };
    def->label = L("First layer support material speed");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_FirstLayer;
    def->category = ConfigItemDef::Category::Hidden;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("If expressed as absolute value in mm/s, this speed will be applied to any support print moves "
                   "of the first layer. If expressed as a percentage "
                   "(for example: 40%) it will be a percantage of the support material speed. "
                   "Note that 0 means that the \"First layer speed\" value will be used.");
    def->units = {L("mm/s"), L("%")};
    def->min = 0;
    def->init_fn = init_with(FloatOrPercentage{0.});
    def->ratio_over = "support_material_speed";

    def = defs.add("first_layer_top_solid_infill_speed", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = { Tool };
    def->label = L("First layer top solid infill speed");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_FirstLayer;
    def->category = ConfigItemDef::Category::Hidden;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("If expressed as absolute value in mm/s, this speed will be applied to any top solid print moves "
                   "of the first layer. If expressed as a percentage "
                   "(for example: 40%) it will be a percantage of the top solid infill speed. "
                   "Note that 0 means that the \"First layer speed\" value will be used.");
    def->units = {L("mm/s"), L("%")};
    def->min = 0;
    def->init_fn = init_with(FloatOrPercentage{0.});
    def->ratio_over = "top_solid_infill_speed";

    def = defs.add("first_layer_gap_fill_speed", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = { Tool };
    def->label = L("First layer gap fill speed");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_FirstLayer;
    def->category = ConfigItemDef::Category::Hidden;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("If expressed as absolute value in mm/s, this speed will be applied to any gap fill print moves "
                   "of the first layer. If expressed as a percentage "
                   "(for example: 40%) it will be a percantage of the gap fill speed. "
                   "Note that 0 means that the \"First layer speed\" value will be used.");
    def->units = {L("mm/s"), L("%")};
    def->min = 0;
    def->init_fn = init_with(FloatOrPercentage{0.});
    def->ratio_over = "gap_fill_speed";

    def = defs.add("first_layer_speed_over_raft", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Speed of object first layer over raft interface");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_FirstLayer;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("If expressed as absolute value in mm/s, this speed will be applied to all the print moves "
                   "of the first object layer above raft interface, regardless of their type. If expressed as a percentage "
                   "(for example: 40%) it will scale the default speeds.");
    def->units = {L("mm/s"), L("%")};
    def->min = 0;
    def->init_fn = init_with(FloatOrPercentage{30.});
    def->ratio_over = "perimeter_speed";

    def = defs.add("first_layer_temperature", typeid(int));
    def->location = Filament;
    def->label = L("Nozzle First layer");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_NozzleTemperature;
    def->category = ConfigItemDef::Category::Filament_MaterialTemperatures;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->full_label = L("First layer nozzle temperature");
    def->tooltip = L("Nozzle temperature for the first layer. If you want to control temperature manually "
                     "during print, set this to zero to disable temperature control commands in the output G-code.");
    def->units = {L("°C")};
    def->min = 0;
    def->max = max_temp;
    def->init_fn = init_with(200);

    def = defs.add("full_fan_speed_layer", typeid(int));
    def->location = Filament;
    def->label = L("Full fan speed at layer");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_FirstLayers;
    def->category = ConfigItemDef::Category::Filament_Cooling;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("Fan speed will be ramped up linearly from zero at layer \"disable_fan_first_layers\" "
                   "to maximum at layer \"full_fan_speed_layer\". "
                   "\"full_fan_speed_layer\" will be ignored if lower than \"disable_fan_first_layers\", in which case "
                   "the fan will be running at maximum allowed speed at layer \"disable_fan_first_layers\" + 1.");
    def->min = 0;
    def->max = 1000;
    def->init_fn = init_with(0);

    def = defs.add("fuzzy_skin", typeid(EnumWrapper));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Fuzzy Skin");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_FuzzySkin;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("Fuzzy skin type.");
    def->init_fn = init_with(
        FuzzySkinType::None,
        {{int(FuzzySkinType::None), "none", L("None")},
         {int(FuzzySkinType::External), "external", L("Outside walls")},
         {int(FuzzySkinType::All), "all", L("All walls")}}
    );

    def = defs.add("fuzzy_skin_thickness", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Fuzzy skin thickness");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_FuzzySkin;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 2;
    def->tooltip = L("The maximum distance that each skin point can be offset (both ways), "
                     "measured perpendicular to the perimeter wall.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(0.3);

    def = defs.add("fuzzy_skin_point_dist", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Fuzzy skin point distance");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_FuzzySkin;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 1;
    def->tooltip = L("Perimeters will be split into multiple segments by inserting Fuzzy skin points. "
                     "Lowering the Fuzzy skin point distance will increase the number of randomly offset points on the perimeter wall.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(0.8);

    def = defs.add("gap_fill_enabled", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Fill gaps");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_WallsQuality;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 6;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Enables filling of gaps between perimeters and between the inner most perimeters and infill.");
    def->init_fn = init_with(true);

    def = defs.add("gap_fill_speed", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Gap fill");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_MainStructure;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 6;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Speed for filling small gaps using short zigzag moves. Keep this reasonably low "
                   "to avoid too much shaking and resonance issues. Set zero to disable gaps filling.");
    def->units = {L("mm/s")};
    def->min = 0;
    def->init_fn = init_with(20.);

    def = defs.add("gcode_comments", typeid(bool));
    def->location = Print;
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_ResolutionGCodeData;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->label = L("Verbose G-code");
    def->tooltip = L("Enable this to get a commented G-code file, with each line explained by a descriptive text. "
                   "If you print from SD card, the additional weight of the file could make your firmware "
                   "slow down.");
    def->init_fn = init_with(false);

    def = defs.add("gcode_flavor", typeid(EnumWrapper));
    def->location = Printer;
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_FirmwareGCode;
    def->category = ConfigItemDef::Category::Printer_General;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->label = L("G-code flavor");
    def->tooltip = L("Some G/M-code commands, including temperature control and others, are not universal. "
                   "Set this option to your printer's firmware to get a compatible output. "
                   "The \"No extrusion\" flavor prevents PrusaSlicer from exporting any extrusion value at all.");
    def->init_fn = init_with(
        GCodeFlavor::gcfRepRapSprinter,
        {{int(GCodeFlavor::gcfRepRapSprinter), "reprap", L("RepRap/Sprinter")},
         {int(GCodeFlavor::gcfRepRapFirmware), "reprapfirmware", L("RepRapFirmware")},
         {int(GCodeFlavor::gcfRepetier), "repetier", L("Repetier")},
         {int(GCodeFlavor::gcfTeacup), "teacup", L("Teacup")},
         {int(GCodeFlavor::gcfMakerWare), "makerware", L("MakerWare (MakerBot)")},
         {int(GCodeFlavor::gcfMarlinLegacy), "marlin", L("Marlin (legacy)")},
         {int(GCodeFlavor::gcfMarlinFirmware), "marlin2", L("Marlin 2")},
         {int(GCodeFlavor::gcfPrusaFirmwareBuddy), "prusabuddy", L("Prusa Firmware (Buddy)")},
         {int(GCodeFlavor::gcfKlipper), "klipper", L("Klipper")},
         {int(GCodeFlavor::gcfSailfish), "sailfish", L("Sailfish (MakerBot)")},
         {int(GCodeFlavor::gcfMach3), "mach3", L("Mach3/LinuxCNC")},
         {int(GCodeFlavor::gcfMachinekit), "machinekit", L("Machinekit")},
         {int(GCodeFlavor::gcfSmoothie), "smoothie", L("Smoothie")},
         {int(GCodeFlavor::gcfNoExtrusion), "no-extrusion", L("No extrusion")}}
    );

    def = defs.add("gcode_label_objects", typeid(EnumWrapper));
    def->location = Print;
    def->label = L("Label objects");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_ResolutionGCodeData;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("Selects whether labels should be exported at object boundaries and in what format.\n"
                     "OctoPrint = comments to be consumed by OctoPrint CancelObject plugin.\n"
                     "Firmware = firmware specific G-code (it will be chosen based on firmware flavor and it can end up to be empty).\n\n"
                     "This settings is NOT compatible with Single Extruder Multi Material setup and Wipe into Object / Wipe into Infill.");
    def->init_fn = init_with(
        LabelObjectsStyle::Disabled,
        {{int(LabelObjectsStyle::Disabled), "disabled", L("Disabled")},
         {int(LabelObjectsStyle::Octoprint), "octoprint", L("OctoPrint comments")},
         {int(LabelObjectsStyle::Firmware), "firmware", L("Firmware-specific")}}
    );

    def = defs.add("gcode_substitutions", typeid(std::vector<std::string>));
    def->location = Print;
    def->label = L("G-code substitutions");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_ScriptSubstitutions;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::substitutions;
    def->tooltip = L("Find / replace patterns in G-code lines and substitute them.");
    def->full_width = true;
    def->init_fn = init_with(std::vector<std::string>{});

    def = defs.add("high_current_on_filament_swap", typeid(bool));
    def->location = Printer;
    def->label = L("High extruder current on filament swap");
    def->option_group = ConfigItemDef::OptionGroup::Printer_SingleExtruderMMSetup_SingleExtruderMultimaterialParameters;
    def->category = ConfigItemDef::Category::Printer_SingleExtruderMMSetup;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("It may be beneficial to increase the extruder motor current during the filament exchange"
                   " sequence to allow for rapid ramming feed rates and to overcome resistance when loading"
                   " a filament with an ugly shaped tip.");
    def->init_fn = init_with(false);

    def = defs.add("enable_pressure_advance_during_ramming", typeid(bool));
    def->location = Printer;
    def->label = L("Enable pressure advance during ramming");
    def->option_group = ConfigItemDef::OptionGroup::Printer_SingleExtruderMMSetup_SingleExtruderMultimaterialParameters;
    def->category = ConfigItemDef::Category::Printer_SingleExtruderMMSetup;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Check to enable pressure advance during ramming.");
    def->init_fn = init_with(false);

    def = defs.add("infill_acceleration", typeid(double));
    def->location = Print;
    def->overrides_in = { Tool };
    def->label = L("Infill");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_MainStructureAcceleration;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This is the acceleration your printer will use for infill. Set zero to disable "
                     "acceleration control for infill.");
    def->units = {L("mm/s²")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("solid_infill_acceleration", typeid(double));
    def->location = Print;
    def->overrides_in = { Tool };
    def->label = L("Solid infill");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_MainStructureAcceleration;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This is the acceleration your printer will use for solid infill. Set zero to use "
                     "the value for infill.");
    def->units = {L("mm/s²")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("top_solid_infill_acceleration", typeid(double));
    def->location = Print;
    def->overrides_in = { Tool };
    def->label = L("Top solid infill");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_MainStructureAcceleration;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This is the acceleration your printer will use for top solid infill. Set zero to use "
                     "the value for solid infill.");
    def->units = {L("mm/s²")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("wipe_tower_acceleration", typeid(double));
    def->location = Print;
    def->label = L("Wipe tower");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_WipeTowerAcceleration;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This is the acceleration your printer will use for wipe tower. Set zero to disable "
                     "acceleration control for the wipe tower.");
    def->units = {L("mm/s²")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("travel_acceleration", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::IgnoreOverrides;
    def->overrides_in = Locations{Tool};
    def->label = L("Travel");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_TravelsAcceleration;
    def->category     = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This is the acceleration your printer will use for travel moves. Set zero to disable "
                     "acceleration control for travel.");
    def->units = {L("mm/s²")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def               = defs.add("travel_short_distance_acceleration", typeid(double));
    def->location     = Print;
    def->compatibility_rule = CompatibilityRule::Min;
    def->overrides_in = Locations{ Tool };
    def->label        = L("Travel short distance acceleration");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_TravelsAcceleration;
    def->category     = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 1;
    def->gui_type     = ConfigItemDef::GUIType::textfield;
    def->tooltip      = L(
        "Acceleration used for short travel moves. Short travel distance is determined by the retract_before_travel setting."
    );
    def->units = {L("mm/s²")};
    def->min      = 0;
    def->init_fn  = init_with(0.);

    def = defs.add("infill_every_layers", typeid(int));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Combine infill every");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_InfillCombination;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("This feature allows to combine infill and speed up your print by extruding thicker "
                   "infill layers while preserving thin perimeters, thus accuracy.");
    def->units = {L("layers")};
    def->full_label = L("Combine infill every n layers");
    def->min = 1;
    def->init_fn = init_with(1);

    def = defs.add("infill_anchor", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Length of the infill anchor");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_WallAnchoring;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::f_enum_open;
    def->tooltip = L("Connect an infill line to an internal perimeter with a short segment of an additional perimeter. "
                     "If expressed as percentage (example: 15%) it is calculated over infill extrusion width. "
                     "PrusaSlicer tries to connect two close infill lines to a short perimeter segment. If no such perimeter segment "
                     "shorter than infill_anchor_max is found, the infill line is connected to a perimeter segment at just one side "
                     "and the length of the perimeter segment taken is limited to this parameter, but no longer than anchor_length_max. "
                     "Set this parameter to zero to disable anchoring perimeters connected to a single infill line.");
    def->units = {L("mm"), L("%")};;
    def->max_literal = 1000;
    def->choices = {
        { 0.,      L("0 (no open anchors)") },
        { 1.,      L("1 mm") },
        { 2.,      L("2 mm") },
        { 5.,      L("5 mm") },
        { 10.,     L("10 mm") },
        { 1000.,   L("1000 (unlimited)") }
    };
    def->init_fn = init_with(FloatOrPercentage(Percentage{600.}));
    def->ratio_over = "infill_extrusion_width";
    const ConfigItemDef* def_infill_anchor_min = def;

    def = defs.add("infill_anchor_max", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Maximum length of the infill anchor");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_WallAnchoring;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::f_enum_open;
    def->tooltip = L("Connect an infill line to an internal perimeter with a short segment of an additional perimeter. "
                     "If expressed as percentage (example: 15%) it is calculated over infill extrusion width. "
                     "PrusaSlicer tries to connect two close infill lines to a short perimeter segment. If no such perimeter segment "
                     "shorter than this parameter is found, the infill line is connected to a perimeter segment at just one side "
                     "and the length of the perimeter segment taken is limited to infill_anchor, but no longer than this parameter. "
                     "Set this parameter to zero to disable anchoring.");
    def->units    = def_infill_anchor_min->units;
    def->max_literal = def_infill_anchor_min->max_literal;
    def->choices = {
        { 0.,      L("0 (not anchored)") },
        { 1.,      L("1 mm") },
        { 2.,      L("2 mm") },
        { 5.,      L("5 mm") },
        { 10.,     L("10 mm") },
        { 1000.,   L("1000 (unlimited)") }
    };
    def->init_fn = init_with(FloatOrPercentage{50.});
    def->ratio_over = "infill_extrusion_width";

    def = defs.add("infill_extruder", typeid(int));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Infill extruder");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_ExtruderAssignment;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("The extruder to use when printing infill.");
    def->min = 1;
    def->init_fn = init_with(1);

    def = defs.add("infill_extrusion_width", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Infill");
    def->option_group = ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_ExtrusionWidth;
    def->category = ConfigItemDef::Category::Print_ExtrusionRetraction;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("Set this to a non-zero value to set a manual extrusion width for infill. "
                   "If left zero, default extrusion width will be used if set, otherwise 1.125 x nozzle diameter will be used. "
                   "You may want to use fatter extrudates to speed up the infill and make your parts stronger. "
                   "If expressed as percentage (for example 90%) it will be computed over nozzle diameter.");
    def->units = {L("mm"), L("%")};;
    def->min = 0;
    def->max_literal = 50;
    def->init_fn = init_with(FloatOrPercentage{0.});
    def->ratio_over = "nozzle_diameter";

    def = defs.add("infill_first", typeid(bool));
    def->location = Print;
    def->label = L("Infill before perimeters");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_Advanced;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("This option will switch the print order of perimeters and infill, making the latter first.");
    def->init_fn = init_with(false);

    def = defs.add("infill_overlap", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Infill/perimeters overlap");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_Overlap;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("This setting applies an additional overlap between infill and perimeters for better bonding. "
                   "Theoretically this shouldn't be needed, but backlash might cause gaps. If expressed "
                   "as percentage (example: 15%) it is calculated over perimeter extrusion width.");
    def->units = {L("mm"), L("%")};;
    def->init_fn = init_with(FloatOrPercentage(Percentage{25.}));
    def->ratio_over = "perimeter_extrusion_width";

    def = defs.add("infill_speed", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Infill");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_MainStructure;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Speed for printing the internal fill. Set to zero for auto.");
    def->units = {L("mm/s")};
    def->aliases = { "print_feed_rate", "infill_feed_rate" };
    def->min = 0;
    def->init_fn = init_with(80.);

    /* TODO: Co s timhle?
    def = defs.add("inherits", typeid(std::string));
    def->label = L("Inherits profile");
    def->tooltip = L("Name of the profile, from which this profile inherits.");
    def->full_width = true;
    def->height = 5;
    def->init_fn = SET_DEFAULT( new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    // The following value is to be stored into the project file (AMF, 3MF, Config ...)
    // and it contains a sum of "inherits" values over the print and filament profiles.
    def = defs.add("inherits_cummulative", typeid(std::vector<std::string>));
    def->init_fn = SET_DEFAULT( new ConfigOptionStrings());
    def->cli = ConfigOptionDef::nocli;*/

    def = defs.add("interface_shells", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Interface shells");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_BondingInterlocking;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Force the generation of solid shells between adjacent materials/volumes. "
                   "Useful for multi-extruder prints with translucent materials or manual soluble "
                   "support material.");
    def->init_fn = init_with(false);

    def = defs.add("mmu_segmented_region_max_width", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Maximum width of a segmented region");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_BondingInterlocking;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 8;
    def->tooltip = L("Maximum width of a segmented region. Zero disables this feature.");
    def->units = {L("mm")};
    def->min = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->init_fn = init_with(0.);

    def = defs.add("mmu_segmented_region_interlocking_depth", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Interlocking depth of a segmented region");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_BondingInterlocking;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 7;
    def->tooltip = L("Interlocking depth of a segmented region. It will be ignored if "
                       "\"mmu_segmented_region_max_width\" is zero or if \"mmu_segmented_region_interlocking_depth\""
                       "is bigger then \"mmu_segmented_region_max_width\". Zero disables this feature.");
    def->units = {L("mm")};
    def->min = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->init_fn = init_with(0.);

    def = defs.add("ironing", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Enable ironing");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_Ironing;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Enable ironing of the top layers with the hot print head for smooth surface");
    def->init_fn = init_with(false);

    def           = defs.add("interlocking_beam", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label    = L("Use beam interlocking");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_BondingInterlocking;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 1;
    def->tooltip  = L("Generate interlocking beam structure at the locations where different filaments touch. This improves the adhesion between filaments, especially models printed in different materials.");
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->init_fn = init_with(false);

    def           = defs.add("interlocking_beam_width", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label    = L("Interlocking beam width");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_BondingInterlocking;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 3;
    def->tooltip  = L("The width of the interlocking structure beams.");
    def->units = {L("mm")};
    def->min      = 0.1f;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->init_fn = init_with(0.8);

    def           = defs.add("interlocking_orientation", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label    = L("Interlocking direction");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_BondingInterlocking;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 6;
    def->tooltip  = L("Orientation of interlocking beams.");
    def->units = {L("°")};
    def->min      = 0;
    def->max      = 360;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->init_fn = init_with(22.5);

    def           = defs.add("interlocking_beam_layer_count", typeid(int));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label    = L("Interlocking beam layers");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_BondingInterlocking;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip  = L("The height of the beams of the interlocking structure, measured in number of layers. Less layers is stronger, but more prone to defects.");
    def->min      = 1;
    def->init_fn = init_with(2);

    def           = defs.add("interlocking_depth", typeid(int));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label    = L("Interlocking depth");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_BondingInterlocking;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 5;
    def->tooltip  = L("The distance from the boundary between filaments to generate interlocking structure, measured in cells. Too few cells will result in poor adhesion.");
    def->min      = 1;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->init_fn = init_with(2);

    def           = defs.add("interlocking_boundary_avoidance", typeid(int));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label    = L("Interlocking boundary avoidance");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_BondingInterlocking;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 4;
    def->tooltip  = L("The distance from the outside of a model where interlocking structures will not be generated, measured in cells.");
    def->min      = 0;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->init_fn = init_with(2);

    def = defs.add("ironing_type", typeid(EnumWrapper));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Ironing Type");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_Ironing;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("Ironing Type");
    def->init_fn = init_with(
        IroningType::TopSurfaces,
        {{int(IroningType::TopSurfaces), "top", L("All top surfaces")},
         {int(IroningType::TopmostOnly), "topmost", L("Topmost surface only")},
         {int(IroningType::AllSolid), "solid", L("All solid surfaces")}}
    );

    def = defs.add("ironing_flowrate", typeid(Percentage));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Flow rate");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_Ironing;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->order = 2;
    def->tooltip = L("Percent of a flow rate relative to object's normal layer height.");
    def->units = {L("%")};
    def->min = 0;
    def->init_fn = init_with(Percentage{15.});

    def = defs.add("ironing_spacing", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Spacing between ironing passes");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_Ironing;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Distance between ironing lines");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(0.1);

    def = defs.add("ironing_speed", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Ironing speed");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_Ironing;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Ironing");
    def->units = {L("mm/s")};
    def->min = 0;
    def->init_fn = init_with(15.);

    def = defs.add("layer_gcode", typeid(std::string));
    def->location = Printer;
    def->label = L("After layer change G-code");
    def->category = ConfigItemDef::Category::Printer_CustomGCode;
    def->option_group = ConfigItemDef::OptionGroup::Printer_CustomGCode;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This custom code is inserted at every layer change, right after the Z move "
                   "and before the extruder moves to the first layer point. Note that you can use "
                   "placeholder variables for all Slic3r settings as well as [layer_num] and [layer_z].");
    def->cli = "after-layer-gcode|layer-gcode";
    def->multiline = true;
    def->full_width = true;
    def->height = 5;
    def->init_fn = init_with("");

    def = defs.add("remaining_times", typeid(bool));
    def->location = Printer;
    def->label = L("Supports remaining times");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_FirmwareGCode;
    def->category = ConfigItemDef::Category::Printer_General;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Emit M73 P[percent printed] R[remaining time in minutes] at 1 minute"
                     " intervals into the G-code to let the firmware show accurate remaining time."
                     " As of now only the Prusa i3 MK3 firmware recognizes M73."
                     " Also the i3 MK3 firmware supports M73 Qxx Sxx for the silent mode.");
    def->init_fn = init_with(false);

    def = defs.add("silent_mode", typeid(bool));
    def->location = Printer;
    def->label = L("Supports stealth mode");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_CapabilitiesFeatures;
    def->category = ConfigItemDef::Category::Printer_General;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("The firmware supports stealth mode");
    def->init_fn = init_with(true);

    def = defs.add("binary_gcode", typeid(bool));
    def->location = Printer;
    def->label = L("Supports binary G-code");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_FirmwareGCode;
    def->category = ConfigItemDef::Category::Printer_General;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Enable, if the firmware supports binary G-code format (bgcode). "
                     "To generate .bgcode files, make sure you have binary G-code enabled in Configuration->Preferences->Other.");
    def->init_fn = init_with(false);

    def = defs.add("machine_limits_usage", typeid(EnumWrapper));
    def->location = Printer;
    def->label = L("How to apply limits");
    def->full_label = L("Purpose of Machine Limits");
    def->category = ConfigItemDef::Category::Printer_MachineLimits;
    def->option_group = ConfigItemDef::OptionGroup::Printer_MachineLimits_General;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("How to apply the Machine Limits");
    def->init_fn = init_with(
        MachineLimitsUsage::TimeEstimateOnly,
        {{int(MachineLimitsUsage::EmitToGCode), "emit_to_gcode", L("Emit to G-code")},
         {int(MachineLimitsUsage::TimeEstimateOnly), "time_estimate_only",
          L("Use for time estimate")},
         {int(MachineLimitsUsage::Ignore), "ignore", L("Ignore")}}
    );

    {
        struct AxisDefault {
            std::string         name;
            std::vector<double> max_feedrate;
            std::vector<double> max_acceleration;
            std::vector<double> max_jerk;
        };
        std::vector<AxisDefault> axes {
            // name, max_feedrate,  max_acceleration, max_jerk
            { "x", { 500., 200. }, {  9000., 1000. }, { 10. , 10.  } },
            { "y", { 500., 200. }, {  9000., 1000. }, { 10. , 10.  } },
            { "z", {  12.,  12. }, {   500.,  200. }, {  0.2,  0.4 } },
            { "e", { 120., 120. }, { 10000., 5000. }, {  2.5,  2.5 } }
        };
        for (const AxisDefault &axis : axes) {
            std::string axis_upper = boost::to_upper_copy<std::string>(axis.name);
            // Add the machine feedrate limits for XYZE axes. (M203)
            def = defs.add("machine_max_feedrate_" + axis.name, typeid(std::vector<double>));
            def->location = Printer;
            def->label = (boost::format("Maximum feedrate %1%") % axis_upper).str();
            (void)L("Maximum feedrate X");
            (void)L("Maximum feedrate Y");
            (void)L("Maximum feedrate Z");
            (void)L("Maximum feedrate E");
            def->option_group = ConfigItemDef::OptionGroup::Printer_MachineLimits_MaximumFeedrates;
            def->category = ConfigItemDef::Category::Printer_MachineLimits;
            def->gui_type = ConfigItemDef::GUIType::textfields;
            def->tooltip  = (boost::format("Maximum feedrate of the %1% axis") % axis_upper).str();
            (void)L("Maximum feedrate of the X axis");
            (void)L("Maximum feedrate of the Y axis");
            (void)L("Maximum feedrate of the Z axis");
            (void)L("Maximum feedrate of the E axis");
            def->units = {L("mm/s")};
            def->min = 0;
            const std::vector<double> max_feedrate = axis.max_feedrate;
            def->init_fn = init_with(max_feedrate);

            // Add the machine acceleration limits for XYZE axes (M201)
            def = defs.add("machine_max_acceleration_" + axis.name, typeid(std::vector<double>));
            def->location = Printer;
            def->label = (boost::format("Maximum acceleration %1%") % axis_upper).str();
            (void)L("Maximum acceleration X");
            (void)L("Maximum acceleration Y");
            (void)L("Maximum acceleration Z");
            (void)L("Maximum acceleration E");
            def->option_group = ConfigItemDef::OptionGroup::Printer_MachineLimits_MaximumAccelerations;
            def->category = ConfigItemDef::Category::Printer_MachineLimits;
            def->gui_type = ConfigItemDef::GUIType::textfields;
            def->tooltip  = (boost::format("Maximum acceleration of the %1% axis") % axis_upper).str();
            (void)L("Maximum acceleration of the X axis");
            (void)L("Maximum acceleration of the Y axis");
            (void)L("Maximum acceleration of the Z axis");
            (void)L("Maximum acceleration of the E axis");
            def->units = {L("mm/s²")};
            def->min = 0;
            const std::vector<double> max_acceleration = axis.max_acceleration;
            def->init_fn = init_with(max_acceleration);

            // Add the machine jerk limits for XYZE axes (M205)
            def = defs.add("machine_max_jerk_" + axis.name, typeid(std::vector<double>));
            def->location = Printer;
            def->label = (boost::format("Maximum jerk %1%") % axis_upper).str();
            (void)L("Maximum jerk X");
            (void)L("Maximum jerk Y");
            (void)L("Maximum jerk Z");
            (void)L("Maximum jerk E");
            def->option_group = ConfigItemDef::OptionGroup::Printer_MachineLimits_JerkLimits;
            def->category = ConfigItemDef::Category::Printer_MachineLimits;
            def->gui_type = ConfigItemDef::GUIType::textfields;
            def->tooltip  = (boost::format("Maximum jerk of the %1% axis") % axis_upper).str();
            (void)L("Maximum jerk of the X axis");
            (void)L("Maximum jerk of the Y axis");
            (void)L("Maximum jerk of the Z axis");
            (void)L("Maximum jerk of the E axis");
            def->units = {L("mm/s")};
            def->min = 0;
            const std::vector<double> max_jerk = axis.max_jerk;
            def->init_fn = init_with(max_jerk);
        }
    }

    // M205 S... [mm/sec]
    def = defs.add("machine_min_extruding_rate", typeid(std::vector<double>));
    def->location = Printer;
    def->label = L("Minimum feedrate when extruding");
    def->option_group = ConfigItemDef::OptionGroup::Printer_MachineLimits_MinimumFeedrates;
    def->category = ConfigItemDef::Category::Printer_MachineLimits;
    def->gui_type = ConfigItemDef::GUIType::textfields;
    def->tooltip = L("Minimum feedrate when extruding (M205 S)");
    def->units = {L("mm/s")};
    def->min = 0;
    def->init_fn = init_with(std::vector{ 0., 0. });

    // M205 J... [mm] machine junction deviation limits
    def = defs.add("machine_max_junction_deviation", typeid(std::vector<double>));
    def->location = Printer;
    def->label = L("Maximum junction deviation");
    def->option_group = ConfigItemDef::OptionGroup::Printer_MachineLimits_JunctionDeviation;
    def->category = ConfigItemDef::Category::Printer_MachineLimits;
    def->gui_type = ConfigItemDef::GUIType::textfields;
    def->tooltip = L("Maximum junction deviation (M205 J, only apply if JD > 0 for Marlin Firmware).");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(std::vector{ 0., 0. });

    // M205 T... [mm/sec]
    def = defs.add("machine_min_travel_rate", typeid(std::vector<double>));
    def->location = Printer;
    def->label = L("Minimum travel feedrate");
    def->option_group = ConfigItemDef::OptionGroup::Printer_MachineLimits_MinimumFeedrates;
    def->category = ConfigItemDef::Category::Printer_MachineLimits;
    def->gui_type = ConfigItemDef::GUIType::textfields;
    def->tooltip = L("Minimum travel feedrate (M205 T)");
    def->units = {L("mm/s")};
    def->min = 0;
    def->init_fn = init_with(std::vector{ 0., 0. });

    // M204 P... [mm/sec^2]
    def = defs.add("machine_max_acceleration_extruding", typeid(std::vector<double>));
    def->location = Printer;
    def->label = L("Maximum acceleration when extruding");
    def->option_group = ConfigItemDef::OptionGroup::Printer_MachineLimits_MaximumAccelerations;
    def->category = ConfigItemDef::Category::Printer_MachineLimits;
    def->gui_type = ConfigItemDef::GUIType::textfields;
    def->tooltip = L("Maximum acceleration when extruding");
    def->units = {L("mm/s²")};
    def->min = 0;
    def->init_fn = init_with(std::vector{ 1500., 1250. });

    // M204 R... [mm/sec^2]
    def = defs.add("machine_max_acceleration_retracting", typeid(std::vector<double>));
    def->location = Printer;
    def->label = L("Maximum acceleration when retracting");
    def->option_group = ConfigItemDef::OptionGroup::Printer_MachineLimits_MaximumAccelerations;
    def->category = ConfigItemDef::Category::Printer_MachineLimits;
    def->gui_type = ConfigItemDef::GUIType::textfields;
    def->tooltip = L("Maximum acceleration when retracting.\n\n"
                     "Not used for RepRapFirmware, which does not support it.");
    def->units = {L("mm/s²")};
    def->min = 0;
    def->init_fn = init_with(std::vector{ 1500., 1250. });

    // M204 T... [mm/sec^2]
    def = defs.add("machine_max_acceleration_travel", typeid(std::vector<double>));
    def->location = Printer;
    def->option_group = ConfigItemDef::OptionGroup::Printer_MachineLimits_MaximumAccelerations;
    def->label = L("Maximum acceleration for travel moves");
    def->category = ConfigItemDef::Category::Printer_MachineLimits;
    def->gui_type = ConfigItemDef::GUIType::textfields;
    def->tooltip = L("Maximum acceleration for travel moves.");
    def->units = {L("mm/s²")};
    def->min = 0;
    def->init_fn = init_with(std::vector{ 1500., 1250. });

    def = defs.add("max_fan_speed", typeid(int));
    def->location = Filament;
    def->label = L("Max Fan speed");
    def->full_label = L("Max fan speed");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_FanControlLimits;
    def->category = ConfigItemDef::Category::Filament_Cooling;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("This setting represents the maximum speed of your fan.");
    def->units = {L("%")};
    def->min = 0;
    def->max = 100;
    def->init_fn = init_with(100);

    def = defs.add("max_layer_height", typeid(double));
    def->location = Print;
    def->overrides_in = { Tool };
    def->label = L("Max");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_LayerHeight;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This is the highest printable layer height for this extruder, used to cap "
                   "the variable layer height and support layer height. Maximum recommended layer height "
                   "is 75% of the extrusion width to achieve reasonable inter-layer adhesion. "
                   "If set to 0, layer height is limited to 75% of the nozzle diameter.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("max_print_speed", typeid(double));
    def->location = Print;
    def->overrides_in = { Tool };
    def->label = L("Max print speed");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_VolumetricSpeed;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("When setting other speed settings to 0 Slic3r will autocalculate the optimal speed "
                   "in order to keep constant extruder pressure. This experimental setting is used "
                   "to set the highest print speed you want to allow.");
    def->units = {L("mm/s")};
    def->min = 1;
    def->init_fn = init_with(80.);

    def = defs.add("max_volumetric_speed", typeid(double));
    def->location = Print;
    def->overrides_in = { Tool };
    def->label = L("Max volumetric speed");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_VolumetricSpeed;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This experimental setting is used to set the maximum volumetric speed your "
                   "extruder supports.");
    def->units = {L("mm³/s")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("max_volumetric_extrusion_rate_slope_positive", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::IgnoreOverrides;
    def->overrides_in = Locations{Tool};
    def->label = L("Max volumetric slope positive");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_PressureEqualizer;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This experimental setting is used to limit the speed of change in extrusion rate "
                       "for a transition from lower speed to higher speed. "
                   "A value of 1.8 mm³/s² ensures, that a change from the extrusion rate "
                   "of 1.8 mm³/s (0.45 mm extrusion width, 0.2 mm extrusion height, feedrate 20 mm/s) "
                   "to 5.4 mm³/s (feedrate 60 mm/s) will take at least 2 seconds.");
    def->units = {L("mm³/s²")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("max_volumetric_extrusion_rate_slope_negative", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::IgnoreOverrides;
    def->overrides_in = Locations{Tool};
    def->label = L("Max volumetric slope negative");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_PressureEqualizer;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This experimental setting is used to limit the speed of change in extrusion rate "
                       "for a transition from higher speed to lower speed. "
                   "A value of 1.8 mm³/s² ensures, that a change from the extrusion rate "
                   "of 5.4 mm³/s (0.45 mm extrusion width, 0.2 mm extrusion height, feedrate 60 mm/s) "
                   "to 1.8 mm³/s (feedrate 20 mm/s) will take at least 2 seconds.");
    def->units = {L("mm³/s²")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("min_fan_speed", typeid(int));
    def->location = Filament;
    def->label = L("Min fan speed");
    def->full_label = L("Min fan speed");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_FanControlLimits;
    def->category = ConfigItemDef::Category::Filament_Cooling;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("This setting represents the minimum PWM your fan needs to work.");
    def->units = {L("%")};
    def->min = 0;
    def->max = 100;
    def->init_fn = init_with(35);

    def = defs.add("min_layer_height", typeid(double));
    def->location = Print;
    def->overrides_in = { Tool };
    def->label = L("Min");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_LayerHeight;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This is the lowest printable layer height for this extruder and limits "
                   "the resolution for variable layer height. Typical values are between 0.05 mm and 0.1 mm.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(0.07);

    def = defs.add("min_print_speed", typeid(double));
    def->location = Filament;
    def->label = L("Min print speed");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_CoolingThresholds;
    def->category = ConfigItemDef::Category::Filament_Cooling;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Slic3r will not scale speed down below this speed.");
    def->units = {L("mm/s")};
    def->min = 0;
    def->init_fn = init_with(10.);

    def = defs.add("min_skirt_length", typeid(double));
    def->location = Print;
    def->label = L("Minimal filament extrusion length");
    def->option_group = ConfigItemDef::OptionGroup::Print_BedAdhesion_Skirt;
    def->category = ConfigItemDef::Category::Print_BedAdhesion;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Generate no less than the number of skirt loops required to consume "
                   "the specified amount of filament on the bottom layer. For multi-extruder machines, "
                   "this minimum applies to each extruder.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("notes", typeid(std::string));
    def->location = Print;
    def->label = L("Configuration notes");
    def->option_group = ConfigItemDef::OptionGroup::Print_Notes_Notes;
    def->category = ConfigItemDef::Category::Print_Notes;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("You can put here your personal notes. This text will be added to the G-code "
                   "header comments.");
    def->multiline = true;
    def->full_width = true;
    def->height = 13;
    def->init_fn = init_with("");

    def = defs.add("only_retract_when_crossing_perimeters", typeid(bool));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::IgnoreOverrides;
    def->overrides_in = Locations{Tool};
    def->label = L("Only retract when crossing perimeters");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_Advanced;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Disables retraction when the travel path does not exceed the upper layer's perimeters "
                   "(and thus any ooze will be probably invisible).");
    def->init_fn = init_with(false);

    def = defs.add("ooze_prevention", typeid(bool));
    def->location = Print;
    def->label = L("Enable");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_OozePrevention;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    // TRN PrintSettings: Enable ooze prevention
    def->tooltip = L("This option will drop the temperature of the inactive extruders to prevent oozing.");
    def->init_fn = init_with(false);

    def = defs.add("overhangs", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Detect bridging perimeters");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_WallsQuality;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Experimental option to adjust flow for overhangs (bridge flow will be used), "
                   "to apply bridge speed to them and enable fan.");
    def->init_fn = init_with(true);

    def = defs.add("parking_pos_retraction", typeid(double));
    def->location = Printer;
    def->label = L("Filament parking position");
    def->option_group = ConfigItemDef::OptionGroup::Printer_SingleExtruderMMSetup_SingleExtruderMultimaterialParameters;
    def->category = ConfigItemDef::Category::Printer_SingleExtruderMMSetup;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Distance of the extruder tip from the position where the filament is parked "
                      "when unloaded. This should match the value in printer firmware.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(92.);

    def = defs.add("extra_loading_move", typeid(double));
    def->location = Printer;
    def->label = L("Extra loading distance");
    def->option_group = ConfigItemDef::OptionGroup::Printer_SingleExtruderMMSetup_SingleExtruderMultimaterialParameters;
    def->category = ConfigItemDef::Category::Printer_SingleExtruderMMSetup;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("When set to zero, the distance the filament is moved from parking position during load "
                      "is exactly the same as it was moved back during unload. When positive, it is loaded further, "
                      " if negative, the loading move is shorter than unloading.");
    def->units = {L("mm")};
    def->init_fn = init_with(-2.);

    def = defs.add("multimaterial_purging", typeid(double));
    def->location = Printer;
    def->label = L("Purging volume");
    def->option_group = ConfigItemDef::OptionGroup::Printer_SingleExtruderMMSetup_SingleExtruderMultimaterialParameters;
    def->category = ConfigItemDef::Category::Printer_SingleExtruderMMSetup;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Determines purging volume on the wipe tower. This can be modified in Filament Settings "
                     "('filament_purge_multiplier') or overridden using project-specific settings.");
    def->units = {L("mm³")};
    def->init_fn = init_with(140.);

    def = defs.add("perimeter_acceleration", typeid(double));
    def->location = Print;
    def->overrides_in = { Tool };
    def->label = L("Perimeters");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_MainStructureAcceleration;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This is the acceleration your printer will use for perimeters. "
                     "Set zero to disable acceleration control for perimeters.");
    def->units = {L("mm/s²")};
    def->init_fn = init_with(0.);

    def = defs.add("external_perimeter_acceleration", typeid(double));
    def->location = Print;
    def->overrides_in = { Tool };
    def->label = L("External perimeters");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_MainStructureAcceleration;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This is the acceleration your printer will use for external perimeters. "
                     "Set zero to use the value for perimeters.");
    def->units = {L("mm/s²")};
    def->init_fn = init_with(0.);

    def = defs.add("perimeter_extruder", typeid(int));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Perimeter extruder");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_ExtruderAssignment;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("The extruder to use when printing perimeters and brim. First extruder is 1.");
    def->aliases = { "perimeters_extruder" };
    def->min = 1;
    def->init_fn = init_with(1);

    def = defs.add("perimeter_extrusion_width", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Perimeters");
    def->option_group = ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_ExtrusionWidth;
    def->category = ConfigItemDef::Category::Print_ExtrusionRetraction;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("Set this to a non-zero value to set a manual extrusion width for perimeters. "
                   "You may want to use thinner extrudates to get more accurate surfaces. "
                   "If left zero, default extrusion width will be used if set, otherwise 1.125 x nozzle diameter will be used. "
                   "If expressed as percentage (for example 200%) it will be computed over nozzle diameter.");
    def->units = {L("mm"), L("%")};;
    def->aliases = { "perimeters_extrusion_width" };
    def->min = 0;
    def->max_literal = 50;
    def->init_fn = init_with(FloatOrPercentage{0.});
    def->ratio_over = "nozzle_diameter";

    def = defs.add("perimeter_speed", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Perimeters");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_MainStructure;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Speed for perimeters (contours, aka vertical shells). Set to zero for auto.");
    def->units = {L("mm/s")};
    def->aliases = { "perimeter_feed_rate" };
    def->min = 0;
    def->init_fn = init_with(60.);

    def = defs.add("perimeters", typeid(int));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Perimeters");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_Perimeters;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("This option sets the minimal number of perimeters to generate for each layer. "
                   "Note that Slic3r may increase this number automatically when it detects "
                   "sloping surfaces which benefit from a higher number of perimeters "
                   "if the Extra Perimeters option is enabled.");
    def->aliases = { "perimeter_offsets" };
    def->min = 0;
    def->max = 10000;
    def->init_fn = init_with(3);

    def = defs.add("post_process", typeid(std::vector<std::string>));
    def->location = Print;
    def->label = L("Post-processing scripts");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_ScriptSubstitutions;
    def->category = ConfigItemDef::Category::Hidden;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("If you want to process the output G-code through custom scripts, "
                   "just list their absolute paths here. Separate multiple scripts with a semicolon. "
                   "Scripts will be passed the absolute path to the G-code file as the first argument, "
                   "and they can access the Slic3r config settings by reading environment variables.");
    def->multiline = true;
    def->full_width = true;
    def->height = 6;
    def->init_fn = init_with(std::vector<std::string>{});

    def               = defs.add("pressure_advance", typeid(EnumWrapper));
    def->location     = Filament;
    def->label        = L("Pressure advance");
    def->option_group = ConfigItemDef::OptionGroup::Filament_ExtrusionCalibration_PressureAdvance;
    def->category     = ConfigItemDef::Category::Filament_ExtrusionCalibration;
    def->order        = 0;
    def->gui_type     = ConfigItemDef::GUIType::combobox;
    def->tooltip      = L(
        "Controls how the pressure advance value is emitted into g-code for this filament. "
             "'Disabled' does not emit any pressure advance command, leaving the printer to use the pressure advance value set in the filament's start g-code (or the firmware default). "
             "'Enabled' emits the configured pressure advance value. "
             "'Automatic Calibration' emits the configured pressure advance value and then triggers the firmware's automatic pressure advance calibration."
    );
    def->init_fn = init_with(
        PressureAdvance::Disabled,
        {{int(PressureAdvance::Disabled), "disabled", L("Disabled")},
         {int(PressureAdvance::Enabled), "enabled", L("Enabled")},
         {int(PressureAdvance::AutomaticCalibration),
          "automatic_calibration",
          L("Automatic Calibration")}}
    );

    def               = defs.add("pressure_advance_value", typeid(double));
    def->location     = Filament;
    def->label        = L("Pressure advance");
    def->option_group = ConfigItemDef::OptionGroup::Filament_ExtrusionCalibration_PressureAdvance;
    def->category     = ConfigItemDef::Category::Filament_ExtrusionCalibration;
    def->order = 1;
    def->gui_type     = ConfigItemDef::GUIType::textfield;
    def->tooltip      = L(
        "Pressure Advance (also called Linear Advance in some firmware) compensates "
             "for nozzle pressure to achieve sharper corners. Proper value helps reduce "
             "bulging on deceleration and under-extrusion on acceleration."
    );

    def->min = 0.;
    def->init_fn = init_with(0.);

    def = defs.add("printer_vendor", typeid(std::string));
    def->location = Printer;
    def->label = L("Printer vendor");
    def->category = ConfigItemDef::Category::Hidden;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Name of the printer vendor.");
    def->cli = ConfigItemDef::nocli;
    def->init_fn = init_with("");


    /* TODO: What about this?
    def = defs.add("print_settings_id", typeid(std::string));
    def->init_fn = SET_DEFAULT(""));
    def->cli = ConfigOptionDef::nocli;

    def = defs.add("printer_settings_id", typeid(std::string));
    def->init_fn = SET_DEFAULT(""));
    def->cli = ConfigOptionDef::nocli;

    def = defs.add("physical_printer_settings_id", typeid(std::string));
    def->init_fn = SET_DEFAULT(""));
    def->cli = ConfigOptionDef::nocli;*/

    def = defs.add("raft_contact_distance", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Average;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Raft contact Z distance");
    def->option_group = ConfigItemDef::OptionGroup::Print_BedAdhesion_Raft;
    def->category = ConfigItemDef::Category::Print_BedAdhesion;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("The vertical distance between object and raft. Ignored for soluble interface.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(0.1);

    def = defs.add("raft_expansion", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Max;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Raft expansion");
    def->option_group = ConfigItemDef::OptionGroup::Print_BedAdhesion_Raft;
    def->category = ConfigItemDef::Category::Print_BedAdhesion;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Expansion of the raft in XY plane for better stability.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(1.5);

    def                             = defs.add("raft_first_layer_density", typeid(Percentage));
    def->location                   = Print;
    def->compatibility_rule         = CompatibilityRule::Average;
    def->overrides_in               = Locations{Tool, Object};
    def->label                      = L("First layer density");
    def->option_group               = ConfigItemDef::OptionGroup::Print_BedAdhesion_Raft;
    def->category                   = ConfigItemDef::Category::Print_BedAdhesion;
    def->order                      = 3;
    def->gui_type                   = ConfigItemDef::GUIType::textfield;
    def->tooltip                    = L("Density of the first raft layer.");
    def->units                   = {L("%")};
    def->min                        = 10;
    def->max                        = 100;
    def->init_fn                    = init_with(Percentage{90.});

    def                             = defs.add("raft_first_layer_expansion", typeid(double));
    def->location                   = Print;
    def->label                      = L("Raft first layer expansion");
    def->compatibility_rule         = CompatibilityRule::Max;
    def->overrides_in               = Locations{Tool, Object};
    def->option_group               = ConfigItemDef::OptionGroup::Print_BedAdhesion_Raft;
    def->category                   = ConfigItemDef::Category::Print_BedAdhesion;
    def->order                      = 4;
    def->gui_type                   = ConfigItemDef::GUIType::textfield;
    def->tooltip  = L("Expansion of the raft first layer to improve adhesion to print bed.");
    def->units = {L("mm")};
    def->min      = 0;
    def->init_fn  = init_with(3.);

    def = defs.add("raft_layers", typeid(int));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Raft layers");
    def->option_group = ConfigItemDef::OptionGroup::Print_BedAdhesion_Raft;
    def->category = ConfigItemDef::Category::Print_BedAdhesion;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("The object will be raised by this number of layers, and support material "
                   "will be generated under it.");
    def->units = {L("layers")};
    def->min = 0;
    def->init_fn = init_with(0);

    def = defs.add("resolution", typeid(double));
    def->location = Print;
    def->label = L("Slice resolution");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_ResolutionGCodeData;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Minimum detail resolution, used to simplify the input file for speeding up "
                   "the slicing job and reducing memory usage. High-resolution models often carry "
                   "more detail than printers can render. Set to zero to disable any simplification "
                   "and use full resolution from input.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("gcode_resolution", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::IgnoreOverrides;
    def->overrides_in = Locations{Tool};
    def->label = L("G-code resolution");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_ResolutionGCodeData;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Maximum deviation of exported G-code paths from their full resolution counterparts. "
                     "Very high resolution G-code requires huge amount of RAM to slice and preview, "
                     "also a 3D printer may stutter not being able to process a high resolution G-code in a timely manner. "
                     "On the other hand, a low resolution G-code will produce a low poly effect and because "
                     "the G-code reduction is performed at each layer independently, visible artifacts may be produced.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(0.0125);

    def = defs.add("retract_before_travel", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Filament };
    def->label = L("Minimum travel after retraction");
    def->option_group = ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_Retraction;
    def->category = ConfigItemDef::Category::Print_ExtrusionRetraction;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Retraction is not triggered when travel moves are shorter than this length.");
    def->units = {L("mm")};
    def->init_fn = init_with(2.);

    def = defs.add("retract_before_wipe", typeid(Percentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Filament };
    def->label = L("Retract amount before wipe");
    def->option_group = ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_Retraction;
    def->category = ConfigItemDef::Category::Print_ExtrusionRetraction;
    def->order = 7;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("With bowden extruders, it may be wise to do some amount of quick retract "
                   "before doing the wipe movement.");
    def->units = {L("%")};
    def->init_fn = init_with(Percentage{0.});

    def = defs.add("retract_layer_change", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Filament };
    def->label = L("Retract on layer change");
    def->option_group = ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_Retraction;
    def->category = ConfigItemDef::Category::Print_ExtrusionRetraction;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("This flag enforces a retraction whenever a Z move is done.");
    def->init_fn = init_with(false);

    def = defs.add("retract_length", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Filament };
    def->label = L("Retraction length");
    def->option_group = ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_Retraction;
    def->category = ConfigItemDef::Category::Print_ExtrusionRetraction;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->full_label = L("Retraction Length");
    def->tooltip = L("When retraction is triggered, filament is pulled back by the specified amount "
                   "(the length is measured on raw filament, before it enters the extruder).");
    def->units = {L("mm")};
    def->init_fn = init_with(2.);

    def = defs.add("retract_length_toolchange", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Filament };
    def->label = L("Length");
    def->option_group = ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_IdleToolRetraction;
    def->category = ConfigItemDef::Category::Print_ExtrusionRetraction;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->full_label = L("Retraction Length (Toolchange)");
    def->tooltip = L("When retraction is triggered before changing tool, filament is pulled back "
                   "by the specified amount (the length is measured on raw filament, before it enters "
                   "the extruder).");
    def->units = {L("mm")};
    def->init_fn = init_with(10.);

    def = defs.add("travel_slope", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Filament };
    def->label = L("Ramping slope angle");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_VerticalLift;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 6;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Slope of the ramp in the initial phase of the travel.");
    def->units = {L("°")};
    def->min = 0;
    def->max = 90;
    def->init_fn = init_with(0.);

    def = defs.add("travel_ramping_lift", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Filament };
    def->label = L("Use ramping lift");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_VerticalLift;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Generates a ramping lift instead of lifting the extruder directly upwards. "
                     "The travel is split into two phases: the ramp and the standard horizontal travel. "
                     "This option helps reduce stringing.");
    def->init_fn = init_with(false);

    def = defs.add("travel_max_lift", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Filament };
    def->label = L("Maximum ramping lift");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_VerticalLift;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Maximum lift height of the ramping lift. It may not be reached if the next position "
                     "is close to the old one.");
    def->units = {L("mm")};
    def->min = 0;
    def->max_literal = 1000;
    def->init_fn = init_with(0.);

    def = defs.add("travel_lift_before_obstacle", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Filament };
    def->label = L("Steeper ramp before obstacles");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_VerticalLift;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("If enabled, PrusaSlicer detects obstacles along the travel path and makes the slope steeper "
                     "in case an obstacle might be hit during the initial phase of the travel.");
    def->init_fn = init_with(false);

    def = defs.add("retract_lift", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Filament };
    def->label = L("Lift height");
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_VerticalLift;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Lift height applied before travel.");
    def->units = {L("mm")};
    def->min = 0;
    def->max_literal = 1000;
    def->init_fn = init_with(0.);

    def = defs.add("retract_lift_above", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Filament };
    def->label = L("Only lift Above Z");
    // def->row_group = L("Only lift"); Temporary removed, row_group are not supported in PrintTool
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_VerticalLift;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->full_label = L("Only lift Z above");
    def->tooltip = L("If you set this to a positive value, Z lift will only take place above the specified "
                   "absolute Z. You can tune this setting for skipping lift on the first layers.");
    def->units = {L("mm")};
    def->init_fn = init_with(0.);

    def = defs.add("retract_lift_below", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Filament };
    def->label = L("Only lift Below Z");
    // def->row_group = L("Only lift"); Temporary removed, row_group are not supported in PrintTool
    def->option_group = ConfigItemDef::OptionGroup::Print_MotionDynamics_VerticalLift;
    def->category = ConfigItemDef::Category::Print_MotionDynamics;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->full_label = L("Only lift Z below");
    def->tooltip = L("If you set this to a positive value, Z lift will only take place below "
                   "the specified absolute Z. You can tune this setting for limiting lift "
                   "to the first layers.");
    def->units = {L("mm")};
    def->init_fn = init_with(0.);

    def = defs.add("retract_restart_extra", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Filament };
    def->label = L("Deretraction extra length");
    def->option_group = ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_Retraction;
    def->category = ConfigItemDef::Category::Print_ExtrusionRetraction;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("When the retraction is compensated after the travel move, the extruder will push "
                   "this additional amount of filament. This setting is rarely needed.");
    def->units = {L("mm")};
    def->init_fn = init_with(0.);

    def = defs.add("retract_restart_extra_toolchange", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Filament };
    def->label = L("Extra length on restart");
    def->option_group = ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_IdleToolRetraction;
    def->category = ConfigItemDef::Category::Print_ExtrusionRetraction;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("When the retraction is compensated after changing tool, the extruder will push "
                   "this additional amount of filament.");
    def->units = {L("mm")};
    def->init_fn = init_with(0.);

    def = defs.add("retract_speed", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Filament };
    def->label = L("Retraction Speed");
    def->option_group = ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_Retraction;
    def->category = ConfigItemDef::Category::Print_ExtrusionRetraction;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->full_label = L("Retraction Speed");
    def->tooltip = L("The speed for retractions (it only applies to the extruder motor).");
    def->units = {L("mm/s")};
    def->init_fn = init_with(40.);

    def = defs.add("deretract_speed", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Filament };
    def->label = L("Deretraction Speed");
    def->option_group = ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_Retraction;
    def->category = ConfigItemDef::Category::Print_ExtrusionRetraction;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->full_label = L("Deretraction Speed");
    def->tooltip = L("The speed for loading of a filament into extruder after retraction "
                   "(it only applies to the extruder motor). If left to zero, the retraction speed is used.");
    def->units = {L("mm/s")};
    def->init_fn = init_with(0.);

    def = defs.add("seam_gap_distance", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Filament };
    def->label = L("Seam gap distance");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_Seams;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("The distance between the endpoints of a closed loop perimeter. "
                   "Positive values will shorten and interrupt the loop slightly to reduce the seam. "
                   "Negative values will extend the loop, causing the endpoints to overlap slightly. "
                   "When percents are used, the distance is derived from the nozzle diameter. "
                   "Set to zero to disable this feature.");
    def->units = {L("mm"), L("%")};;
    def->init_fn = init_with(FloatOrPercentage(Percentage{15.}));
    def->ratio_over = "nozzle_diameter";

    def = defs.add("seam_position", typeid(EnumWrapper));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::IgnoreOverrides;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Seam position");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_Seams;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("Position of perimeters starting points.");
    def->init_fn = init_with(
        SeamPosition::spAligned,
        {{int(SeamPosition::spRandom), "random", L("Random")},
         {int(SeamPosition::spNearest), "nearest", L("Nearest")},
         {int(SeamPosition::spAligned), "aligned", L("Aligned")},
         {int(SeamPosition::spRear), "rear", L("Rear")}}
    );

    def = defs.add("staggered_inner_seams", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Staggered inner seams");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_Seams;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    // TRN PrintSettings: "Staggered inner seams"
    def->tooltip = L("This option causes the inner seams to be shifted backwards based on their depth, forming a zigzag pattern.");
    def->init_fn = init_with(false);

    def = defs.add("scarf_seam_placement", typeid(EnumWrapper));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Scarf joint placement");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_Seams;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("Where to place scarf joint seam.");
    def->init_fn = init_with(
        ScarfSeamPlacement::nowhere,
        {// TRN: Drop-down option for 'Scarf joint placement' parameter.
         {int(ScarfSeamPlacement::nowhere), "nowhere", L("Nowhere")},
         // TRN: Drop-down option for 'Scarf joint placement' parameter.
         {int(ScarfSeamPlacement::countours), "contours", L("Contours")},
         {int(ScarfSeamPlacement::everywhere), "everywhere", L("Everywhere")}
        }
    );

    def = defs.add("scarf_seam_only_on_smooth", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Scarf joint only on smooth perimeters");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_Seams;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Only use the scarf joint when the perimeter is smooth.");
    def->init_fn = init_with(true);

    def = defs.add("scarf_seam_start_height", typeid(Percentage));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Scarf start height");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_Seams;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Start height of the scarf joint specified as fraction of the current layer height.");
    def->units = {L("%")};
    def->min = 0;
    def->max = 100;
    def->init_fn = init_with(Percentage{0.});

    def = defs.add("scarf_seam_entire_loop", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Scarf joint around entire perimeter");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_Seams;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 6;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Extend the scarf around entire length of the perimeter.");
    def->init_fn = init_with(false);

    def = defs.add("scarf_seam_length", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Scarf joint length");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_Seams;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 7;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Length of the scarf joint.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(20.);

    def = defs.add("scarf_seam_max_segment_length", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Max scarf joint segment length");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_Seams;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 8;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Maximum length of any scarf joint segment.");
    def->units = {L("mm")};
    def->min = 0.15f;
    def->init_fn = init_with(1.0);

    def = defs.add("scarf_seam_on_inner_perimeters", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Scarf joint on inner perimeters");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_Seams;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 9;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Use scarf joint on inner perimeters.");
    def->init_fn = init_with(false);

    def = defs.add("skirt_distance", typeid(double));
    def->location = Print;
    def->label = L("Distance from brim/object");
    def->option_group = ConfigItemDef::OptionGroup::Print_BedAdhesion_Skirt;
    def->category = ConfigItemDef::Category::Print_BedAdhesion;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Distance between skirt and brim (when draft shield is not used) or objects.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(6.);

    def = defs.add("skirt_height", typeid(int));
    def->location = Print;
    def->label = L("Skirt height");
    def->option_group = ConfigItemDef::OptionGroup::Print_BedAdhesion_Skirt;
    def->category = ConfigItemDef::Category::Print_BedAdhesion;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("Height of skirt expressed in layers.");
    def->units = {L("layers")};
    def->init_fn = init_with(1);

    def = defs.add("draft_shield", typeid(EnumWrapper));
    def->location = Print;
    def->label = L("Draft shield");
    def->option_group = ConfigItemDef::OptionGroup::Print_BedAdhesion_Skirt;
    def->category = ConfigItemDef::Category::Print_BedAdhesion;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("With draft shield active, the skirt will be printed skirt_distance from the object, possibly intersecting brim.\n"
                     "Enabled = skirt is as tall as the highest printed object.\n"
                     "Limited = skirt is as tall as specified by skirt_height.\n"
    				 "This is useful to protect an ABS or ASA print from warping and detaching from print bed due to wind draft.");
    def->init_fn = init_with(
        DraftShield::dsDisabled,
        {{int(DraftShield::dsDisabled), "disabled", L("Disabled")},
         {int(DraftShield::dsLimited), "limited", L("Limited")},
         {int(DraftShield::dsEnabled), "enabled", L("Enabled")}}
    );

    def = defs.add("skirts", typeid(int));
    def->location = Print;
    def->label = L("Loops (minimum)");
    def->option_group = ConfigItemDef::OptionGroup::Print_BedAdhesion_Skirt;
    def->category = ConfigItemDef::Category::Print_BedAdhesion;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->full_label = L("Skirt Loops");
    def->tooltip = L("Number of loops for the skirt. If the Minimum Extrusion Length option is set, "
                   "the number of loops might be greater than the one configured here. Set this to zero "
                   "to disable skirt completely.");
    def->min = 0;
    def->init_fn = init_with(1);

    def = defs.add("slowdown_below_layer_time", typeid(int));
    def->location = Filament;
    def->label = L("Slow down if layer print time is below");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_CoolingThresholds;
    def->category = ConfigItemDef::Category::Filament_Cooling;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("If layer print time is estimated below this number of seconds, print moves "
                   "speed will be scaled down to extend duration to this value.");
    def->units = {L("s")};
    def->min = 0;
    def->max = 1000;
    def->init_fn = init_with(5);

    def = defs.add("small_perimeter_speed", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Small perimeters");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_MainStructure;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("This separate setting will affect the speed of perimeters having radius <= 6.5mm "
                   "(usually holes). If expressed as percentage (for example: 80%) it will be calculated "
                   "on the perimeters speed setting above. Set to zero for auto.");
    def->units = {L("mm/s"), L("%")};
    def->min = 0;
    def->init_fn = init_with(FloatOrPercentage{15.});
    def->ratio_over = "perimeter_speed";

    def = defs.add("solid_infill_below_area", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Solid infill threshold area");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_Advanced;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 6;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Force solid infill for regions having a smaller area than the specified threshold.");
    def->units = {L("mm²")};
    def->min = 0;
    def->init_fn = init_with(70.);

    def = defs.add("solid_infill_extruder", typeid(int));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Solid infill extruder");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_ExtruderAssignment;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("The extruder to use when printing solid infill.");
    def->min = 1;
    def->init_fn = init_with(1);

    def = defs.add("solid_infill_every_layers", typeid(int));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Solid infill every");
    def->option_group = ConfigItemDef::OptionGroup::Print_Infill_Advanced;
    def->category = ConfigItemDef::Category::Print_Infill;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("This feature allows to force a solid layer every given number of layers. "
                   "Zero to disable. You can set this to any value (for example 9999); "
                   "Slic3r will automatically choose the maximum possible number of layers "
                   "to combine according to nozzle diameter and layer height.");
    def->units = {L("layers")};
    def->min = 0;
    def->init_fn = init_with(0);

    def = defs.add("solid_infill_extrusion_width", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Solid infill");
    def->option_group = ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_ExtrusionWidth;
    def->category = ConfigItemDef::Category::Print_ExtrusionRetraction;
    def->order = 6;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("Set this to a non-zero value to set a manual extrusion width for infill for solid surfaces. "
                   "If left zero, default extrusion width will be used if set, otherwise 1.125 x nozzle diameter will be used. "
                   "If expressed as percentage (for example 90%) it will be computed over nozzle diameter.");
    def->units = {L("mm"), L("%")};;
    def->min = 0;
    def->max_literal = 50;
    def->init_fn = init_with(FloatOrPercentage{0.});
    def->ratio_over = "nozzle_diameter";

    def = defs.add("solid_infill_speed", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Solid infill");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_MainStructure;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("Speed for printing solid regions (top/bottom/internal horizontal shells). "
                   "This can be expressed as a percentage (for example: 80%) over the default "
                   "infill speed above. Set to zero for auto.");
    def->units = {L("mm/s"), L("%")};
    def->aliases = { "solid_infill_feed_rate" };
    def->min = 0;
    def->init_fn = init_with(FloatOrPercentage{20.});
    def->ratio_over = "infill_speed";
    
    /* TODO : shortcut - maybe remove it?
    def = defs.add("solid_layers", typeid(int));
    def->label = L("Solid layers");
    def->tooltip = L("Number of solid layers to generate on top and bottom surfaces.");
    def->shortcut.push_back("top_solid_layers");
    def->shortcut.push_back("bottom_solid_layers");
    def->min = 0;

    def = defs.add("solid_min_thickness", typeid(double));
    def->label = L("Minimum thickness of a top / bottom shell");
    def->tooltip = L("Minimum thickness of a top / bottom shell");
    def->shortcut.push_back("top_solid_min_thickness");
    def->shortcut.push_back("bottom_solid_min_thickness");
    def->min = 0;*/

    def = defs.add("spiral_vase", typeid(bool));
    def->location = Print;
    def->label = L("Spiral vase");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_SlicingStrategy;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("This feature will raise Z gradually while printing a single-walled object "
                   "in order to remove any visible seam. This option requires a single perimeter, "
                   "no infill, no top solid layers and no support material. You can still set "
                   "any number of bottom solid layers as well as skirt/brim loops. "
                   "It won't work when printing more than one single object.");
    def->init_fn = init_with(false);

    def = defs.add("standby_temperature_delta", typeid(int));
    def->location = Print;
    def->label = L("Temperature variation");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_OozePrevention;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    // TRN PrintSettings : "Ooze prevention" > "Temperature variation"
    def->tooltip = L("Temperature difference to be applied when an extruder is not active. "
                     "The value is not used when 'idle_temperature' in filament settings "
                     "is defined.");
    def->units = {"∆°C"};
    def->min = -max_temp;
    def->max = max_temp;
    def->init_fn = init_with(-5);

    def = defs.add("autoemit_temperature_commands", typeid(bool));
    def->location = Printer;
    def->category = ConfigItemDef::Category::Printer_CustomGCode;
    def->option_group = ConfigItemDef::OptionGroup::Printer_CustomGCode;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->order = 1;
    def->label = L("Emit temperature commands automatically");
    def->tooltip = L("When enabled, PrusaSlicer will check whether your custom Start G-code contains G-codes to set "
                     "extruder, bed or chamber temperature (M104, M109, M140, M190, M141 and M191). "
                     "If so, the temperatures will not be emitted automatically so you're free to customize "
                     "the order of heating commands and other custom actions. Note that you can use "
                     "placeholder variables for all PrusaSlicer settings, so you can put "
                     "a \"M109 S[first_layer_temperature]\" command wherever you want.\n"
                     "If your custom Start G-code does NOT contain these G-codes, "
                     "PrusaSlicer will execute the Start G-code after heated chamber was set to its temperature, "
                     "bed reached its target temperature and extruder just started heating.\n\n"
                     "When disabled, PrusaSlicer will NOT emit commands to heat up extruder, bed or chamber, "
                     "leaving all to Custom Start G-code.");
    def->init_fn = init_with(true);

    def = defs.add("start_gcode", typeid(std::string));
    def->location = Printer;
    def->category = ConfigItemDef::Category::Printer_CustomGCode;
    def->option_group = ConfigItemDef::OptionGroup::Printer_CustomGCode;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->label = L("Start G-code");
    def->tooltip = L("This start procedure is inserted at the beginning, possibly prepended by "
                     "temperature-changing commands. See 'autoemit_temperature_commands'.");
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->init_fn = init_with("G28 ; home all axes\nG1 Z5 F5000 ; lift nozzle\n");

    def = defs.add("start_filament_gcode", typeid(std::string));
    def->location = Filament;
    def->label = L("Start G-code");
    def->category = ConfigItemDef::Category::Filament_CustomGCode;
    def->option_group = ConfigItemDef::OptionGroup::Filament_CustomGCode;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This start procedure is inserted at the beginning, after any printer start gcode (and "
                   "after any toolchange to this filament in case of multi-material printers). "
                   "This is used to override settings for a specific filament. If PrusaSlicer detects "
                   "M104, M109, M140 or M190 in your custom codes, such commands will "
                   "not be prepended automatically so you're free to customize the order "
                   "of heating commands and other custom actions. Note that you can use placeholder variables "
                   "for all PrusaSlicer settings, so you can put a \"M109 S[first_layer_temperature]\" command "
                   "wherever you want. If you have multiple extruders, the gcode is processed "
                   "in extruder order.");
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->init_fn = init_with("; Filament gcode\n");

    def = defs.add("color_change_gcode", typeid(std::string));
    def->location = Printer;
    def->label = L("Color change G-code");
    def->category = ConfigItemDef::Category::Printer_CustomGCode;
    def->option_group = ConfigItemDef::OptionGroup::Printer_CustomGCode;
    def->order = 7;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This G-code will be used as a code for the color change");
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->init_fn = init_with("M600");

    def = defs.add("pause_print_gcode", typeid(std::string));
    def->location = Printer;
    def->category = ConfigItemDef::Category::Printer_CustomGCode;
    def->option_group = ConfigItemDef::OptionGroup::Printer_CustomGCode;
    def->order = 8;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->label = L("Pause Print G-code");
    def->tooltip = L("This G-code will be used as a code for the pause print");
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->init_fn = init_with("M601");

    def = defs.add("template_custom_gcode", typeid(std::string));
    def->location = Printer;
    def->label = L("Custom G-code");
    def->category = ConfigItemDef::Category::Printer_CustomGCode;
    def->option_group = ConfigItemDef::OptionGroup::Printer_CustomGCode;
    def->order = 9;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This G-code will be used as a custom code");
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->init_fn = init_with("");

    def = defs.add("single_extruder_multi_material", typeid(bool));
    def->location = Printer;
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_CapabilitiesFeatures;
    def->category = ConfigItemDef::Category::Printer_General;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->label = L("Single Extruder Multi Material");
    def->tooltip = L("The printer multiplexes filaments into a single hot end.");
    def->init_fn = init_with(false);

    def = defs.add("single_extruder_multi_material_priming", typeid(bool));
    def->location = Print;
    def->label = L("Prime all printing extruders");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_WipeTower;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 8;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("If enabled, all printing extruders will be primed at the front edge of the print bed at the start of the print.");
    def->init_fn = init_with(true);

    def = defs.add("wipe_tower_no_sparse_layers", typeid(bool));
    def->location = Print;
    def->label = L("No sparse layers (EXPERIMENTAL)");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_WipeTower;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 7;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("If enabled, the wipe tower will not be printed on layers with no toolchanges. "
                     "On layers with a toolchange, extruder will travel downward to print the wipe tower. "
                     "User is responsible for ensuring there is no collision with the print.");
    def->init_fn = init_with(false);

    def               = defs.add("toolchange_ordering", typeid(EnumWrapper));
    def->location     = Print;
    def->compatibility_rule = CompatibilityRule::IgnoreOverrides;
    def->overrides_in       = Locations{Tool};
    def->label        = L("Toolchange ordering");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_ToolChanges;
    def->category     = ConfigItemDef::Category::Print_MultiMaterial;
    def->order        = 0;
    def->gui_type     = ConfigItemDef::GUIType::combobox;
    def->tooltip      = L(
        "Determines the order of tool changes on each layer.\n"
        "Optimized - Starts with the last used extruder to minimize tool changes.\n"
        "Cyclic - Uses extruders in a fixed sequential order (1, 2, 3, ...) on every layer."
    );
    def->init_fn = init_with(
        ToolChangeOrderingType::Optimized,
        {
            {int(ToolChangeOrderingType::Optimized), "optimized", L("Optimized")},
            {int(ToolChangeOrderingType::Cyclic),    "cyclic",    L("Cyclic")},
        }
    );

    def               = defs.add("support_material", typeid(EnumWrapper));
    def->location     = Print;
    def->overrides_in = Locations{Object};
    def->label        = L("Generate Supports");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_Generation;
    def->category     = ConfigItemDef::Category::Print_Supports;
    def->order        = 0;
    def->gui_type     = ConfigItemDef::GUIType::combobox;
    def->tooltip      = L(
        "Enable support material generation.\n"
             "Off - Disables support material generation.\n"
             "Manual Only - Generates supports inside the \"Support Enforcer\" volumes only.\n"
             "On - Generates supports automatically based on the overhang threshold value and also inside the \"Support Enforcer\" volumes."
    );
    def->init_fn = init_with(
        SupportMode::EnforcersOnly,
        {{int(SupportMode::None), "none", L("Off")},
         {int(SupportMode::EnforcersOnly), "enforcers_only", L("Manual Only")},
         {int(SupportMode::Everywhere), "everywhere", L("On")}}
    );

    def = defs.add("support_material_xy_spacing", typeid(FloatOrPercentage));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Max;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("XY separation between an object and its support");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_InterfaceSeparation;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("XY separation between an object and its support. If expressed as percentage "
                   "(for example 50%), it will be calculated over external perimeter width.");
    def->units = {L("mm"), L("%")};;
    def->min = 0;
    def->max_literal = 10;
    // Default is half the external perimeter width.
    def->init_fn = init_with(FloatOrPercentage(Percentage{50.}));
    def->ratio_over = "external_perimeter_extrusion_width";

    def = defs.add("support_material_angle", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Average;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Pattern angle");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_PatternDensity;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Use this setting to rotate the support material pattern on the horizontal plane.");
    def->units = {L("°")};
    def->min = 0;
    def->max = 359;
    def->init_fn = init_with(0.);

    def = defs.add("support_material_buildplate_only", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Support on build plate only");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_Generation;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Only create support if it lies on a build plate. Don't create support on a print.");
    def->init_fn = init_with(false);

    def = defs.add("support_material_contact_distance", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Average;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Top contact Z distance");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_InterfaceSeparation;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::f_enum_open;
    def->tooltip = L("The vertical distance between object and support material interface. "
                   "Setting this to 0 will also prevent Slic3r from using bridge flow and speed "
                   "for the first object layer.");
    def->units = {L("mm")};
    def->choices = {
        { 0.,     L("0 (soluble)") },
        { 0.1,    L("0.1 (detachable)") },
        { 0.2,    L("0.2 (detachable)") }
    };
    def->init_fn = init_with(0.2);

    def = defs.add("support_material_bottom_contact_distance", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Average;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Bottom contact Z distance");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_InterfaceSeparation;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::f_enum_open;
    def->tooltip = L("The vertical distance between the object top surface and the support material interface. "
                   "If set to zero, support_material_contact_distance will be used for both top and bottom contact Z distances.");
    def->units = {L("mm")};
    def->choices = {
        //TRN Print Settings: "Bottom contact Z distance". Have to be as short as possible
        { 0.,     L("Same as top") },
        { 0.1,    "0.1" },
        { 0.2,    "0.2" }
    };
    def->init_fn = init_with(0.);

    def = defs.add("support_material_enforce_layers", typeid(int));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Max;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Enforce support for the first");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_Generation;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("Generate support material for the specified number of layers counting from bottom, "
                   "regardless of whether normal support material is enabled or not and regardless "
                   "of any angle threshold. This is useful for getting more adhesion of objects "
                   "having a very thin or poor footprint on the build plate.");
    def->units = {L("layers")};
    def->full_label = L("Enforce support for the first n layers");
    def->min = 0;
    def->init_fn = init_with(0);

    def = defs.add("support_material_extruder", typeid(int));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Support material/raft/skirt extruder");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_ExtruderAssignment;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("The extruder to use when printing support material, raft and skirt "
                   "(1+, 0 to use the current extruder to minimize tool changes).");
    def->min = 0;
    def->init_fn = init_with(1);

    def = defs.add("support_material_extrusion_width", typeid(FloatOrPercentage));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Average;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Support material");
    def->option_group = ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_ExtrusionWidth;
    def->category = ConfigItemDef::Category::Print_ExtrusionRetraction;
    def->order = 8;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("Set this to a non-zero value to set a manual extrusion width for support material. "
                   "If left zero, default extrusion width will be used if set, otherwise nozzle diameter will be used. "
                   "If expressed as percentage (for example 90%) it will be computed over nozzle diameter.");
    def->units = {L("mm"), L("%")};;
    def->min = 0;
    def->max_literal = 50;
    def->init_fn = init_with(FloatOrPercentage{0.});
    def->ratio_over = "nozzle_diameter";

    def           = defs.add("support_material_first_layer_density", typeid(Percentage));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Average;
    def->overrides_in               = Locations{Tool, Object};
    def->label                      = L("First layer density");
    def->option_group               = ConfigItemDef::OptionGroup::Print_Supports_SupportGeometry;
    def->category                   = ConfigItemDef::Category::Print_Supports;
    def->order                      = 0;
    def->gui_type                   = ConfigItemDef::GUIType::textfield;
    def->tooltip                    = L("Density of the first support layer.");
    def->units                      = {L("%")};
    def->min                        = 10;
    def->max                        = 100;
    def->init_fn                    = init_with(Percentage{90.});

    def           = defs.add("support_material_first_layer_expansion", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Max;
    def->overrides_in               = Locations{Tool, Object};
    def->label                      = L("First layer expansion");
    def->option_group               = ConfigItemDef::OptionGroup::Print_Supports_SupportGeometry;
    def->category                   = ConfigItemDef::Category::Print_Supports;
    def->order                      = 1;
    def->gui_type                   = ConfigItemDef::GUIType::textfield;
    def->tooltip  = L("Expansion of the support first layer to improve adhesion to print bed.");
    def->units = {L("mm")};
    def->min      = 0;
    def->init_fn  = init_with(3.);

    def = defs.add("support_material_interface_contact_loops", typeid(bool));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::IgnoreOverrides;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Interface loops");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_InterfaceSeparation;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Cover the top contact layer of the supports with loops. Disabled by default.");
    def->init_fn = init_with(false);

    def = defs.add("support_material_interface_extruder", typeid(int));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Support material/raft interface extruder");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_ExtruderAssignment;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("The extruder to use when printing support material interface "
                   "(1+, 0 to use the current extruder to minimize tool changes). This affects raft too.");
    def->min = 0;
    def->init_fn = init_with(1);

    def = defs.add("support_material_interface_layers", typeid(int));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Max;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Top interface layers");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_InterfaceSeparation;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::i_enum_open;
    def->tooltip = L("Number of interface layers to insert between the object(s) and support material.");
    def->units = {L("layers")};
    def->min = 0;
    def->choices = {
        { 0, L("0 (off)") },
        { 1, L("1 (light)") },
        { 2, L("2 (default)") },
        { 3, L("3 (heavy)") }
    };
    def->init_fn = init_with(3);

    def = defs.add("support_material_bottom_interface_layers", typeid(int));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Max;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Bottom interface layers");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_InterfaceSeparation;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::i_enum_open;
    def->tooltip = L("Number of interface layers to insert between the object(s) and support material. "
                     "Set to -1 to use support_material_interface_layers");
    def->units = {L("layers")};
    def->min = -1;
    def->choices = {
        //TRN Print Settings: "Bottom interface layers". Have to be as short as possible
        { -1, L("Same as top") },
        { 0,  L("0 (off)") },
        { 1,  L("1 (light)") },
        { 2,  L("2 (default)") },
        { 3,  L("3 (heavy)") }
    };
    def->init_fn = init_with(-1); 

    def = defs.add("support_material_closing_radius", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Min;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Closing radius");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_SupportGeometry;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("For snug supports, the support regions will be merged using morphological closing operation."
                     " Gaps smaller than the closing radius will be filled in.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(2.);

    def = defs.add("support_material_interface_spacing", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Min;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Interface pattern spacing");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_InterfaceSeparation;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 7;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Spacing between interface lines. Set zero to get a solid interface.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("support_material_interface_speed", typeid(FloatOrPercentage));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Min;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Support material interface");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_SupportAndBridges;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("Speed for printing support material interface layers. If expressed as percentage "
                   "(for example 50%) it will be calculated over support material speed.");
    def->units = {L("mm/s"), L("%")};
    def->min = 0;
    def->init_fn = init_with(FloatOrPercentage(Percentage{100.}));
    def->ratio_over = "support_material_speed";

    def = defs.add("support_material_pattern", typeid(EnumWrapper));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::IgnoreOverrides;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Pattern");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_PatternDensity;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("Pattern used to generate support material.");
    def->init_fn = init_with(
        SupportMaterialPattern::smpRectilinear,
        {{int(SupportMaterialPattern::smpRectilinear), "rectilinear", L("Rectilinear")},
         {int(SupportMaterialPattern::smpRectilinearGrid), "rectilinear-grid", L("Rectilinear grid")},
         {int(SupportMaterialPattern::smpHoneycomb), "honeycomb", L("Honeycomb")}}
    );

    def = defs.add("support_material_interface_pattern", typeid(EnumWrapper));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::IgnoreOverrides;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Interface pattern");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_InterfaceSeparation;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 6;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("Pattern used to generate support material interface. "
                     "Default pattern for non-soluble support interface is Rectilinear, "
                     "while default pattern for soluble support interface is Concentric.");
    def->init_fn = init_with(
        SupportMaterialInterfacePattern::smipRectilinear,
        {{int(SupportMaterialInterfacePattern::smipAuto), "auto", L("Default")},
         {int(SupportMaterialInterfacePattern::smipRectilinear), "rectilinear", L("Rectilinear")},
         {int(SupportMaterialInterfacePattern::smipConcentric), "concentric", L("Concentric")}}
    );

    def = defs.add("support_material_spacing", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Min;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Pattern spacing");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_PatternDensity;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Spacing between support material lines.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(2.5);

    def = defs.add("support_material_speed", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Min;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Support material");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_SupportAndBridges;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Speed for printing support material.");
    def->units = {L("mm/s")};
    def->min = 0;
    def->init_fn = init_with(60.);

    def = defs.add("support_material_style", typeid(EnumWrapper));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::IgnoreOverrides;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Style");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_Generation;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("Style and shape of the support towers. Projecting the supports into a regular grid "
                     "will create more stable supports, while snug support towers will save material and reduce "
                     "object scarring.");
    def->init_fn = init_with(
        SupportMaterialStyle::smsGrid,
        {{int(SupportMaterialStyle::smsGrid), "grid", L("Grid")},
         {int(SupportMaterialStyle::smsSnug), "snug", L("Snug")},
         {int(SupportMaterialStyle::smsOrganic), "organic", L("Organic")}}
    );

    def = defs.add("support_material_synchronize_layers", typeid(bool));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::IgnoreOverrides;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Synchronize with object layers");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_SupportGeometry;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    // TRN PrintSettings : "Synchronize with object layers"
    def->tooltip = L("Synchronize support layers with the object print layers. This is useful "
                   "with multi-material printers, where the extruder switch is expensive. "
                   "This option is only available when top contact Z distance is set to zero.");
    def->init_fn = init_with(false);

    def = defs.add("support_material_threshold", typeid(int));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::IgnoreOverrides;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Overhang threshold");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_Generation;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("Support material will not be generated for overhangs whose slope angle "
                   "(90° = vertical) is above the given threshold. In other words, this value "
                   "represent the most horizontal slope (measured from the horizontal plane) "
                   "that you can print without support material. Set to zero for automatic detection "
                   "(recommended).");
    def->units = {L("°")};
    def->min = 0;
    def->max = 90;
    def->init_fn = init_with(0);

    def = defs.add("support_material_with_sheath", typeid(bool));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::IgnoreOverrides;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("With sheath around the support");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_SupportGeometry;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Add a sheath (a single perimeter line) around the base support. This makes "
                   "the support more reliable, but also more difficult to remove.");
    def->init_fn = init_with(true);

    def = defs.add("support_tree_angle", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Min;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Maximum Branch Angle");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_OrganicSupports;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    // TRN PrintSettings: "Organic supports" > "Maximum Branch Angle"
    def->tooltip = L("The maximum angle of the branches, when the branches have to avoid the model. "
                     "Use a lower angle to make them more vertical and more stable. Use a higher angle to be able to have more reach.");
    def->units = {L("°")};
    def->min = 0;
    def->max = 85;
    def->init_fn = init_with(40.);

    def = defs.add("support_tree_angle_slow", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Min;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Preferred Branch Angle");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_OrganicSupports;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    // TRN PrintSettings: "Organic supports" > "Preferred Branch Angle"
    def->tooltip = L("The preferred angle of the branches, when they do not have to avoid the model. "
                     "Use a lower angle to make them more vertical and more stable. Use a higher angle for branches to merge faster.");
    def->units = {L("°")};
    def->min = 10;
    def->max = 85;
    def->init_fn = init_with(25.);

    def = defs.add("support_tree_tip_diameter", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Min;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Tip Diameter");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_OrganicSupports;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 6;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    // TRN PrintSettings: "Organic supports" > "Tip Diameter"
    def->tooltip = L("Branch tip diameter for organic supports.");
    def->units = {L("mm")};
    def->min = 0.1f;
    def->max = 100.f;
    def->init_fn = init_with(0.8);

    def = defs.add("support_tree_branch_diameter", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Min;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Branch Diameter");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_OrganicSupports;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    // TRN PrintSettings: "Organic supports" > "Branch Diameter"
    def->tooltip = L("The diameter of the thinnest branches of organic support. Thicker branches are more sturdy. "
                     "Branches towards the base will be thicker than this.");
    def->units = {L("mm")};
    def->min = 0.1f;
    def->max = 100.f;
    def->init_fn = init_with(2.);

    def = defs.add("support_tree_branch_diameter_angle", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::IgnoreOverrides;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Branch Diameter Angle");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_OrganicSupports;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    // TRN PrintSettings: "Organic supports" > "Branch Diameter Angle"
    def->tooltip = L("The angle of the branches' diameter as they gradually become thicker towards the bottom. "
                     "An angle of 0 will cause the branches to have uniform thickness over their length. "
                     "A bit of an angle can increase stability of the organic support.");
    def->units = {L("°")};
    def->min = 0;
    def->max = 15;
    def->init_fn = init_with(5.);

    def = defs.add("support_tree_branch_diameter_double_wall", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::IgnoreOverrides;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Branch Diameter with double walls");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_OrganicSupports;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    // TRN PrintSettings: "Organic supports" > "Branch Diameter"
    def->tooltip = L("Branches with area larger than the area of a circle of this diameter will be printed with double walls for stability. "
                     "Set this value to zero for no double walls.");
    def->units = {L("mm")};
    def->min = 0;
    def->max = 100.f;
    def->init_fn = init_with(3.);

    // Tree Support Branch Distance
    // How far apart the branches need to be when they touch the model. Making this distance small will cause
    // the tree support to touch the model at more points, causing better overhang but making support harder to remove.
    def = defs.add("support_tree_branch_distance", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Max;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Branch Distance");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_OrganicSupports;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    // TRN PrintSettings: "Organic supports" > "Branch Distance"
    def->tooltip = L("How far apart the branches need to be when they touch the model. "
                     "Making this distance small will cause the tree support to touch the model at more points, "
                     "causing better overhang but making support harder to remove.");
    def->init_fn = init_with(1.);

    def = defs.add("support_tree_top_rate", typeid(Percentage));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::IgnoreOverrides;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Branch Density");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_OrganicSupports;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->order = 7;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    // TRN PrintSettings: "Organic supports" > "Branch Density"
    def->tooltip = L("Adjusts the density of the support structure used to generate the tips of the branches. "
                     "A higher value results in better overhangs but the supports are harder to remove, "
                     "thus it is recommended to enable top support interfaces instead of a high branch density value "
                     "if dense interfaces are needed.");
    def->units = {L("%")};
    def->min = 5;
    def->max_literal = 35;
    def->init_fn = init_with(Percentage{15.});

    def = defs.add("temperature", typeid(int));
    def->location = Filament;
    def->label = L("Nozzle other layers");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_NozzleTemperature;
    def->category = ConfigItemDef::Category::Filament_MaterialTemperatures;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("Nozzle temperature for layers after the first one. Set this to zero to disable "
                     "temperature control commands in the output G-code.");
    def->units = {L("°C")};
    def->full_label = L("Nozzle temperature");
    def->min = 0;
    def->max = max_temp;
    def->init_fn = init_with(200);

    def = defs.add("thick_bridges", typeid(bool));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::IgnoreOverrides;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Thick bridges");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_WallsQuality;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("If enabled, bridges are more reliable, can bridge longer distances, but may look worse. "
                     "If disabled, bridges look better but are reliable just for shorter bridged distances.");
    def->init_fn = init_with(true);

    def = defs.add("thin_walls", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object, Volume };
    def->label = L("Detect thin walls");
    def->option_group = ConfigItemDef::OptionGroup::Print_WallsPerimeters_WallsQuality;
    def->category = ConfigItemDef::Category::Print_WallsPerimeters;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Detect single-width walls (parts where two extrusions don't fit and we need "
                   "to collapse them into a single trace).");
    def->init_fn = init_with(true);

    def = defs.add("toolchange_gcode", typeid(std::string));
    def->location = Printer;
    def->category = ConfigItemDef::Category::Printer_CustomGCode;
    def->option_group = ConfigItemDef::OptionGroup::Printer_CustomGCode;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->label = L("Tool change G-code");
    def->tooltip = L("This custom code is inserted before every toolchange. Placeholder variables for all PrusaSlicer settings "
                     "as well as {toolchange_z}, {previous_extruder} and {next_extruder} can be used. When a tool-changing command "
                     "which changes to the correct extruder is included (such as T{next_extruder}), PrusaSlicer will emit no other such command. "
                     "It is therefore possible to script custom behaviour both before and after the toolchange.");
    def->multiline = true;
    def->full_width = true;
    def->height = 5;
    def->init_fn = init_with("");

    def = defs.add("top_infill_extrusion_width", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Top solid infill");
    def->option_group = ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_ExtrusionWidth;
    def->category = ConfigItemDef::Category::Print_ExtrusionRetraction;
    def->order = 7;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("Set this to a non-zero value to set a manual extrusion width for infill for top surfaces. "
                   "You may want to use thinner extrudates to fill all narrow regions and get a smoother finish. "
                   "If left zero, default extrusion width will be used if set, otherwise nozzle diameter will be used. "
                   "If expressed as percentage (for example 90%) it will be computed over nozzle diameter.");
    def->units = {L("mm"), L("%")};;
    def->min = 0;
    def->max_literal = 50;
    def->init_fn = init_with(FloatOrPercentage{0.});
    def->ratio_over = "nozzle_diameter";

    def = defs.add("top_solid_infill_speed", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Top solid infill");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_MainStructure;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip = L("Speed for printing top solid layers (it only applies to the uppermost "
                   "external layers and not to their internal solid layers). You may want "
                   "to slow down this to get a nicer surface finish. This can be expressed "
                   "as a percentage (for example: 80%) over the solid infill speed above. "
                   "Set to zero for auto.");
    def->units = {L("mm/s"), L("%")};
    def->min = 0;
    def->init_fn = init_with(FloatOrPercentage{15.});
    def->ratio_over = "solid_infill_speed";

    def = defs.add("top_solid_layers", typeid(int));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Top solid layers");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_TopBottomShells;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->order = 0;
    // def->row_group = L("Solid layers"); Temporary removed, row_group are not supported in PrintTool
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("Number of solid layers to generate on top surfaces.");
    def->min = 0;
    def->init_fn = init_with(3);

    def = defs.add("top_solid_min_thickness", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object, Volume };
    def->label = L("Minimum top shell thickness");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_TopBottomShells;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->order = 1;
    // def->row_group = L("Minimum shell thickness"); Temporary removed, row_group are not supported in PrintTool
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("The number of top solid layers is increased above top_solid_layers if necessary to satisfy "
    				 "minimum thickness of top shell."
    				 " This is useful to prevent pillowing effect when printing with variable layer height.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("travel_speed", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Min;
    def->overrides_in = Locations{ Tool };
    def->label = L("Travel");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_Travels;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Speed for travel moves (jumps between distant extrusion points).");
    def->units = {L("mm/s")};
    def->aliases = { "travel_feed_rate" };
    def->min = 1;
    def->init_fn = init_with(130.);

    def = defs.add("travel_speed_z", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Min;
    def->overrides_in = Locations{ Tool };
    def->label = L("Z travel");
    def->option_group = ConfigItemDef::OptionGroup::Print_Speed_Travels;
    def->category = ConfigItemDef::Category::Print_Speed;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Speed for movements along the Z axis.\nWhen set to zero, the value "
                     "is ignored and regular travel speed is used instead.");
    def->units = {L("mm/s")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("use_firmware_retraction", typeid(bool));
    def->location = Printer;
    def->label = L("Use firmware retraction");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_CapabilitiesFeatures;
    def->category = ConfigItemDef::Category::Printer_General;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("This setting uses G10 and G11 commands to have the firmware "
                   "handle the retraction. Note that this has to be supported by firmware.");
    def->init_fn = init_with(false);

    def = defs.add("stuck_filament_detection", typeid(bool));
    def->location = Printer;
    def->label = L("Supports stuck filament monitoring configuration");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_FirmwareGCode;
    def->category = ConfigItemDef::Category::Printer_General;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Enable, if the firmware supports configuring the stuck filament detection via the M591 gcode.");
    def->init_fn = init_with(false);

    def = defs.add("use_relative_e_distances", typeid(bool));
    def->location = Printer;
    def->label = L("Use relative E distances");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_FirmwareGCode;
    def->category = ConfigItemDef::Category::Printer_General;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("If your firmware requires relative E values, check this, "
                   "otherwise leave it unchecked. Most firmwares use absolute values.");
    def->init_fn = init_with(false);

    def = defs.add("use_volumetric_e", typeid(bool));
    def->location = Printer;
    def->label = L("Use volumetric E");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_FirmwareGCode;
    def->category = ConfigItemDef::Category::Printer_General;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("This experimental setting uses outputs the E values in cubic millimeters "
                   "instead of linear millimeters. If your firmware doesn't already know "
                   "filament diameter(s), you can put commands like 'M200 D[filament_diameter_0] T0' "
                   "in your start G-code in order to turn volumetric mode on and use the filament "
                   "diameter associated to the filament selected in Slic3r. This is only supported "
                   "in recent Marlin.");
    def->init_fn = init_with(false);

    def = defs.add("variable_layer_height", typeid(bool));
    def->location = Printer;
    def->category = ConfigItemDef::Category::Printer_General;
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_CapabilitiesFeatures;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->label = L("Enable variable layer height feature");
    def->tooltip = L("Some printers or printer setups may have difficulties printing "
                   "with a variable layer height. Enabled by default.");
    def->init_fn = init_with(true);

    def = defs.add("prefer_clockwise_movements", typeid(bool));
    def->location = Printer;
    def->category = ConfigItemDef::Category::Printer_General;
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_CapabilitiesFeatures;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->label = L("Prefer clockwise movements");
    def->tooltip = L("This setting makes the printer print loops clockwise instead of counterclockwise.");
    def->init_fn = init_with(false);

    def = defs.add("wipe", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Filament };
    def->label = L("Wipe while retracting");
    def->option_group = ConfigItemDef::OptionGroup::Print_ExtrusionRetraction_Retraction;
    def->category = ConfigItemDef::Category::Print_ExtrusionRetraction;
    def->order = 6;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("This flag will move the nozzle while retracting to minimize the possible blob "
                   "on leaky extruders.");
    def->init_fn = init_with(false);

    def = defs.add("wipe_tower", typeid(bool));
    def->location = Print;
    def->label = L("Enable");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_WipeTower;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Multi material printers may need to prime or purge extruders on tool changes. "
                   "Extrude the excess material into the wipe tower.");
    def->init_fn = init_with(false);

    def = defs.add("wiping_volumes_matrix", typeid(std::vector<double>));
    def->location = Project;
    def->category = ConfigItemDef::Category::Hidden;
    def->label = L("Purging volumes - matrix");
    def->tooltip = L("This matrix describes volumes (in cubic milimetres) required to purge the"
                     " new filament on the wipe tower for any given pair of tools.");
    def->init_fn = init_with(std::vector
                {   0., 140., 140., 140., 140.,
                  140.,   0., 140., 140., 140.,
                  140., 140.,   0., 140., 140.,
                  140., 140., 140.,   0., 140.,
                  140., 140., 140., 140.,   0. });

    def = defs.add("wiping_volumes_use_custom_matrix", typeid(bool));
    def->location = Project;
    def->category = ConfigItemDef::Category::Hidden;
    def->label = "";
    def->tooltip = "";
    def->init_fn = init_with(false);

    def = defs.add("wipe_tower_width", typeid(double));
    def->location = Print;
    def->label = L("Width");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_WipeTower;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Width of a wipe tower");
    def->units = {L("mm")};
    def->init_fn = init_with(60.);
    def->min = 1.0;

    def = defs.add("wipe_tower_brim_width", typeid(double));
    def->location = Print;
    def->label = L("Wipe tower brim width");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_WipeTower;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Wipe tower brim width");
    def->units = {L("mm")};
    def->min = 0.;
    def->init_fn = init_with(2.);

    def = defs.add("wipe_tower_cone_angle", typeid(double));
    def->location = Print;
    def->label = L("Stabilization cone apex angle");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_WipeTower;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Angle at the apex of the cone that is used to stabilize the wipe tower. "
                     "Larger angle means wider base.");
    def->units = {L("°")};
    def->min = 0.;
    def->max = 90.;
    def->init_fn = init_with(0.);

    def = defs.add("wipe_tower_extra_spacing", typeid(Percentage));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Max;
    def->overrides_in = Locations{Tool};
    def->label = L("Wipe tower purge lines spacing");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_WipeTower;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Spacing of purge lines on the wipe tower.");
    def->units = {L("%")};
    def->min = 100.;
    def->max = 300.;
    def->init_fn = init_with(Percentage{100.});

    def = defs.add("wipe_tower_extra_flow", typeid(Percentage));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Max;
    def->overrides_in = Locations{Tool};
    def->label = L("Extra flow for purging");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_WipeTower;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 6;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Extra flow used for the purging lines on the wipe tower. This makes the purging lines thicker or narrower "
                     "than they normally would be. The spacing is adjusted automatically.");
    def->units = {L("%")};
    def->min = 100.;
    def->max = 300.;
    def->init_fn = init_with(Percentage{100.});

    def = defs.add("wipe_into_infill", typeid(bool));
    def->location = Volume;
    def->category = ConfigItemDef::Category::Volume_WipeOptions;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->label = L("Wipe into this object's infill");
    def->tooltip = L("Purging after toolchange will be done inside this object's infills. "
                     "This lowers the amount of waste but may result in longer print time "
                     " due to additional travel moves.");
    def->init_fn = init_with(false);

    def = defs.add("wipe_into_objects", typeid(bool));
    def->location = Object;
    def->category = ConfigItemDef::Category::Volume_WipeOptions;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->label = L("Wipe into this object");
    def->tooltip = L("Object will be used to purge the nozzle after a toolchange to save material "
                     "that would otherwise end up in the wipe tower and decrease print time. "
                     "Colours of the objects will be mixed as a result.");
    def->init_fn = init_with(false);

    def = defs.add("wipe_tower_bridging", typeid(double));
    def->location = Print;
    def->compatibility_rule = CompatibilityRule::Min;
    def->overrides_in = Locations{ Tool };
    def->label = L("Maximal bridging distance");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_WipeTower;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Maximal distance between supports on sparse infill sections.");
    def->units = {L("mm")};
    def->init_fn = init_with(10.);

    def = defs.add("wipe_tower_extruder", typeid(int));
    def->location = Print;
    def->label = L("Wipe tower extruder");
    def->option_group = ConfigItemDef::OptionGroup::Print_MultiMaterial_ExtruderAssignment;
    def->category = ConfigItemDef::Category::Print_MultiMaterial;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("The extruder to use when printing perimeter of the wipe tower. "
                     "Set to 0 to use the one that is available (non-soluble would be preferred).");
    def->min = 0;
    def->init_fn = init_with(0);

    def = defs.add("xy_size_compensation", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("XY Size Compensation");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_DimensionalAccuracy;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("The object will be grown/shrunk in the XY plane by the configured value "
                   "(negative = inwards, positive = outwards). This might be useful "
                   "for fine-tuning hole sizes.");
    def->units = {L("mm")};
    def->init_fn = init_with(0.);

    def = defs.add("z_offset", typeid(double));
    def->location = Printer;
    def->label = L("Z offset");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_SizeClearances;
    def->category = ConfigItemDef::Category::Printer_General;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This value will be added (or subtracted) from all the Z coordinates "
                   "in the output G-code. It is used to compensate for bad Z endstop position: "
                   "for example, if your endstop zero actually leaves the nozzle 0.3mm far "
                   "from the print bed, set this to -0.3 (or fix your endstop).");
    def->units = {L("mm")};
    def->init_fn = init_with(0.);

    def = defs.add("perimeter_generator", typeid(EnumWrapper));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Perimeter generator");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_PerimeterGenerator;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("Classic perimeter generator produces perimeters with constant extrusion width and for "
                      "very thin areas is used gap-fill. "
                      "Arachne engine produces perimeters with variable extrusion width. "
                      "This setting also affects the Concentric infill.");

    def->init_fn = init_with(
        PerimeterGeneratorType::Arachne,
        {{int(PerimeterGeneratorType::Classic), "classic", L("Classic")},
         {int(PerimeterGeneratorType::Arachne), "arachne", L("Arachne")}}
    );

    def = defs.add("wall_transition_length", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Perimeter transition length");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_ArachnePerimeterGenerator;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 5;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip  = L("When transitioning between different numbers of perimeters as the part becomes "
                       "thinner, a certain amount of space is allotted to split or join the perimeter segments. "
                       "If expressed as a percentage (for example 100%), it will be computed based on the nozzle diameter.");
    def->units = {L("mm"), L("%")};;
    def->min = 0;
    def->init_fn = init_with(FloatOrPercentage(Percentage{100.}));
    def->ratio_over = "nozzle_diameter";

    def = defs.add("wall_transition_filter_deviation", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Perimeter transitioning filter margin");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_ArachnePerimeterGenerator;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 4;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip  = L("Prevent transitioning back and forth between one extra perimeter and one less. This "
                       "margin extends the range of extrusion widths which follow to [Minimum perimeter width "
                       "- margin, 2 * Minimum perimeter width + margin]. Increasing this margin "
                       "reduces the number of transitions, which reduces the number of extrusion "
                       "starts/stops and travel time. However, large extrusion width variation can lead to "
                       "under- or overextrusion problems. "
                       "If expressed as a percentage (for example 25%), it will be computed based on the nozzle diameter.");
    def->units = {L("mm"), L("%")};;
    def->min = 0;
    def->init_fn = init_with(FloatOrPercentage(Percentage{25.}));
    def->ratio_over = "nozzle_diameter";

    def = defs.add("wall_transition_angle", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Perimeter transitioning threshold angle");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_ArachnePerimeterGenerator;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 3;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip  = L("When to create transitions between even and odd numbers of perimeters. A wedge shape with"
                       " an angle greater than this setting will not have transitions and no perimeters will be "
                       "printed in the center to fill the remaining space. Reducing this setting reduces "
                       "the number and length of these center perimeters, but may leave gaps or overextrude.");
    def->units = {L("°")};
    def->min = 1.;
    def->max = 59.;
    def->init_fn = init_with(10.);

    def = defs.add("wall_distribution_count", typeid(int));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Perimeter distribution count");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_ArachnePerimeterGenerator;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip  = L("The number of perimeters, counted from the center, over which the variation needs to be "
                       "spread. Lower values mean that the outer perimeters don't change in width.");
    def->min = 1;
    def->init_fn = init_with(1);

    def = defs.add("min_feature_size", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Minimum feature size");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_ArachnePerimeterGenerator;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip  = L("Minimum thickness of thin features. Model features that are thinner than this value will "
                       "not be printed, while features thicker than the Minimum feature size will be widened to "
                       "the Minimum perimeter width. "
                       "If expressed as a percentage (for example 25%), it will be computed based on the biggest nozzle diameter.");
    def->units = {L("mm"), L("%")};;
    def->min = 0;
    def->init_fn = init_with(FloatOrPercentage(Percentage{25.}));
    def->ratio_over = "nozzle_diameter";

    def = defs.add("min_bead_width", typeid(FloatOrPercentage));
    def->location = Print;
    def->overrides_in = Locations{ Tool, Object };
    def->label = L("Minimum perimeter width");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_ArachnePerimeterGenerator;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::unit_or_percentage;
    def->tooltip  = L("Width of the perimeter that will replace thin features (according to the Minimum feature size) "
                       "of the model. If the Minimum perimeter width is thinner than the thickness of the feature,"
                       " the perimeter will become as thick as the feature itself. "
                       "If expressed as a percentage (for example 85%), it will be computed based on the nozzle diameter.");
    def->units = {L("mm"), L("%")};;
    def->min = 0;
    def->init_fn = init_with(FloatOrPercentage(Percentage{85.}));
    def->ratio_over = "nozzle_diameter";

    def = defs.add("idle_temperature", typeid(std::optional<int>));
    def->location = Filament;
    def->label = L("Idle temperature");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_NozzleTemperature;
    def->category = ConfigItemDef::Category::Filament_MaterialTemperatures;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("Nozzle temperature when the tool is currently not used in multi-tool setups."
                     "This is only used when 'Ooze prevention' is active in Print Settings.");
    def->units = {L("°C")};
    def->min = 0;
    def->max = max_temp;
    def->init_fn = init_with(std::optional<int>());

    def = defs.add("custom_parameters_print", typeid(std::string));
    def->location = Print;
    def->category = ConfigItemDef::Category::Print_CustomGCode;
    def->option_group = ConfigItemDef::OptionGroup::Print_CustomGCode;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->label = L("Custom print parameters");
    // FIXME: Translating the tooltip like this will likely not work.
    const std::string custom_parameter_tooltip_templ =
        L("JSON-encoded string defining extra parameters, which can later be expanded in Custom G-code macro language. "
          "Each of the parameters is prepended by %1% prefix before it is passed into the parser. "
          "For example, defining '{\"my_key\": \"my_value\"}' allows to use '%1%_my_key'.\n\n"
          "The JSON must be single level (no arrays and objects). All value types are allowed, but nulls "
          "will be rejected by the parser."
    );
    def->tooltip = (boost::format(custom_parameter_tooltip_templ) % "custom_parameter_print").str();
    def->multiline = true;
    def->full_width = true;
    def->height = 13;
    def->init_fn = init_with("");

    def = defs.add("custom_parameters_printer", typeid(std::string));
    def->location = Printer;
    def->category = ConfigItemDef::Category::Printer_CustomGCode;
    def->option_group = ConfigItemDef::OptionGroup::Printer_CustomGCode;
    def->order = 10;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->label = L("Custom printer parameters");
    def->tooltip = (boost::format(custom_parameter_tooltip_templ) % "custom_parameter_printer").str();;
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->init_fn = init_with("");

    def = defs.add("custom_parameters_filament", typeid(std::string));
    def->location = Filament;
    def->order = 2;
    def->category = ConfigItemDef::Category::Filament_CustomGCode;
    def->option_group = ConfigItemDef::OptionGroup::Filament_CustomGCode;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->label = L("Custom filament parameters");
    def->tooltip = (boost::format(custom_parameter_tooltip_templ) % "custom_parameter_filament").str();
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->init_fn = init_with("");

    def = defs.add("default_tool_print", typeid(std::string));
    def->location = Print;
    def->label = L("Default Tool Print Preset");
    def->category = ConfigItemDef::Category::Hidden;
    def->tooltip = L("Name or ID of tool print preset to use as default when this print preset is selected.");
    def->init_fn = init_with("");

}

} // namespace Slic3r::Domain
