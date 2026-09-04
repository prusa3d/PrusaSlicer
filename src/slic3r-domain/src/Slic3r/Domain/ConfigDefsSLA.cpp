#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/ConfigCommon.hpp"
#include "Slic3r/Domain/ConfigDefUtils.hpp"

#include "Slic3r/Domain/Types.hpp"

#include "boost/algorithm/string.hpp"
#include "boost/format.hpp"

namespace Slic3r::Domain {

// Implementation of SLA configs is done in this file.

// Define our own marking functions, the regular ones are not accessible in Domain.
static const std::string& L(const std::string& s) { return s; }

void sla_config_init_fn(ConfigDefinitions& defs);

using SLAConfigLocation::Printer;
using SLAConfigLocation::Material;
using SLAConfigLocation::Print;
using SLAConfigLocation::Object;

const ConfigDefinitions& get_defs_sla() {
    static ConfigDefinitions defs_sla({Printer, Print, Material, Object}, sla_config_init_fn);
    return defs_sla;
}

// Now define the init function. This function will be called by ConfigDefinitions
// constructor and will fill the definitions with all the necessary data.
void sla_config_init_fn(ConfigDefinitions& defs)
{
    using Locations = std::set<ConfigLocation>;
    ConfigItemDef* def = nullptr;

    /* TODO: These were common for FDM and SLA. Do we want to
    separate them or keep them together? They are not defined here
    at this point.
    def = defs.add("printer_technology", typeid(EnumWrapper));
    def = defs.add("bed_shape", Points);
    def = defs.add("bed_custom_texture", typeid(std::string));
    def = defs.add("bed_custom_model", typeid(std::string));
    def = defs.add("elefant_foot_compensation", typeid(double)); - has an override in sla_material_settings
    def = defs.add("thumbnails", typeid(std::string));
    def = defs.add("thumbnails_format", typeid(EnumWrapper));
    def = defs.add("layer_height", typeid(double));
    def = defs.add("max_print_height", typeid(double));*/

    init_common_fdm_sla_config_items(defs, PrinterTechnology::SLA);


    def = defs.add("display_width", typeid(double));
    def->location = Printer;
    def->label = L("Display width");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_Display;
    def->category = ConfigItemDef::Category::Printer_General;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Width of the display");
    def->min = 1;
    def->init_fn = init_with(120.);

    def = defs.add("display_height", typeid(double));
    def->location = Printer;
    def->label = L("Display height");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_Display;
    def->category = ConfigItemDef::Category::Printer_General;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Height of the display");
    def->min = 1;
    def->init_fn = init_with(68.);

    def = defs.add("display_pixels_x", typeid(int));
    def->location = Printer;
    def->full_label = L("Number of pixels in");
    def->label = ("X");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_Display;
    def->category = ConfigItemDef::Category::Printer_General;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("Number of pixels in X");
    def->min = 100;
    def->init_fn = init_with(2560);

    def = defs.add("display_pixels_y", typeid(int));
    def->location = Printer;
    def->label = ("Y");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_Display;
    def->category = ConfigItemDef::Category::Printer_General;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("Number of pixels in Y");
    def->min = 100;
    def->init_fn = init_with(1440);

    def = defs.add("display_mirror_x", typeid(bool));
    def->location = Printer;
    def->full_label = L("Display horizontal mirroring");
    def->label = L("Mirror horizontally");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_Display;
    def->category = ConfigItemDef::Category::Printer_General;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Enable horizontal mirroring of output images");
    def->init_fn = init_with(true);

    def = defs.add("display_mirror_y", typeid(bool));
    def->location = Printer;
    def->full_label = L("Display vertical mirroring");
    def->label = L("Mirror vertically");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_Display;
    def->category = ConfigItemDef::Category::Printer_General;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Enable vertical mirroring of output images");
    def->init_fn = init_with(false);

    def = defs.add("display_orientation", typeid(EnumWrapper));
    def->location = Printer;
    def->label = L("Display orientation");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_Display;
    def->category = ConfigItemDef::Category::Printer_General;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("Set the actual LCD display orientation inside the SLA printer."
                     " Portrait mode will flip the meaning of display width and height parameters"
                     " and the output images will be rotated by 90 degrees.");
    def->init_fn = init_with(
        SLADisplayOrientation::sladoPortrait,
        {{int(SLADisplayOrientation::sladoLandscape), "landscape", L("Landscape")},
         {int(SLADisplayOrientation::sladoPortrait), "portrait", L("Portrait")}}
    );

    def = defs.add("fast_tilt_time", typeid(double));
    def->location = Printer;
    def->label = L("Fast");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_Tilt;
    def->category = ConfigItemDef::Category::Printer_General;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->full_label = L("Fast tilt");
    def->tooltip = L("Time of the fast tilt");
    def->units = {L("s")};
    def->min = 0;
    def->init_fn = init_with(5.);

    def = defs.add("slow_tilt_time", typeid(double));
    def->location = Printer;
    def->label = L("Slow");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_Tilt;
    def->category = ConfigItemDef::Category::Printer_General;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->full_label = L("Slow tilt");
    def->tooltip = L("Time of the slow tilt");
    def->units = {L("s")};
    def->min = 0;
    def->init_fn = init_with(8.);

    def = defs.add("high_viscosity_tilt_time", typeid(double));
    def->location = Printer;
    def->label = L("High viscosity");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_Tilt;
    def->category = ConfigItemDef::Category::Printer_General;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->full_label = L("Tilt for high viscosity resin");
    def->tooltip = L("Time of the super slow tilt");
    def->units = {L("s")};
    def->min = 0;
    def->init_fn = init_with(10.);

    def = defs.add("area_fill", typeid(double));
    def->location = Material;
    def->label = L("Area fill threshold");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_Tilt;
    def->category = ConfigItemDef::Category::Filament_MaterialPrintingProfile;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("The value is expressed as a percentage of the bed area. If the area of a particular layer "
                     "is smaller than 'area_fill', then 'Below area fill threshold' parameters are used to determine the "
                     "layer separation (tearing) procedure. Otherwise 'Above area fill threshold' parameters are used.");
    def->units = {L("%")};
    def->min = 0;
    def->init_fn = init_with(35.);

    def = defs.add("relative_correction", typeid(std::vector<double>));
    def->location = Printer;
    def->label = L("Printer scaling correction");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_Corrections;
    def->category = ConfigItemDef::Category::Hidden;
    def->gui_type = ConfigItemDef::GUIType::textfields;
    def->full_label = L("Printer scaling correction");
    def->tooltip  = L("Printer scaling correction");
    def->min = 0;
    def->init_fn = init_with((std::vector<double>{1.,1.}));

    def = defs.add("relative_correction_x", typeid(double));
    def->location = Printer;
    def->label = L("Printer scaling correction in X axis");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_Corrections;
    def->category = ConfigItemDef::Category::Printer_General;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->full_label = L("Printer scaling X axis correction");
    def->tooltip  = L("Printer scaling correction in X axis");
    def->min = 0;
    def->init_fn = init_with(1.);

    def = defs.add("relative_correction_y", typeid(double));
    def->location = Printer;
    def->label = L("Printer scaling correction in Y axis");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_Corrections;
    def->category = ConfigItemDef::Category::Printer_General;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->full_label = L("Printer scaling Y axis correction");
    def->tooltip  = L("Printer scaling correction in Y axis");
    def->min = 0;
    def->init_fn = init_with(1.);

    def = defs.add("relative_correction_z", typeid(double));
    def->location = Printer;
    def->label = L("Printer scaling correction in Z axis");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_Corrections;
    def->category = ConfigItemDef::Category::Printer_General;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->full_label = L("Printer scaling Z axis correction");
    def->tooltip  = L("Printer scaling correction in Z axis");
    def->min = 0;
    def->init_fn = init_with(1.);

    def = defs.add("absolute_correction", typeid(double));
    def->location = Printer;
    def->overrides_in = Locations{ Material };
    def->label = L("Printer absolute correction");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_Corrections;
    def->category = ConfigItemDef::Category::Printer_General;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->full_label = L("Printer absolute correction");
    def->tooltip  = L("Will inflate or deflate the sliced 2D polygons according "
                      "to the sign of the correction.");
    def->units = {L("mm")};
    def->init_fn = init_with(0.);

    def = defs.add("elefant_foot_min_width", typeid(double));
    def->location = Printer;
    def->label = L("Elephant foot minimum width");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_Corrections;
    def->category = ConfigItemDef::Category::Printer_General;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Minimum width of features to maintain when doing elephant foot compensation.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(0.2);

    def = defs.add("zcorrection_layers", typeid(int));
    def->location = Material;
    def->label = L("Z compensation");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_Corrections;
    def->category = ConfigItemDef::Category::Filament_MaterialTemperatures;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("Number of layers to Z correct to avoid cross layer bleed");
    def->min = 0;
    def->init_fn = init_with(0);

    def = defs.add("gamma_correction", typeid(double));
    def->location = Printer;
    def->label = L("Printer gamma correction");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_Corrections;
    def->category = ConfigItemDef::Category::Printer_General;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->full_label = L("Printer gamma correction");
    def->tooltip  = L("This will apply a gamma correction to the rasterized 2D "
                      "polygons. A gamma value of zero means thresholding with "
                      "the threshold in the middle. This behaviour eliminates "
                      "antialiasing without losing holes in polygons.");
    def->min = 0;
    def->max = 1;
    def->init_fn = init_with(1.);


    def = defs.add("material_colour", typeid(std::string));
    def->location = Material;
    def->label = L("Color");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_MaterialProperty;
    def->category = ConfigItemDef::Category::Filament_MaterialTemperatures;
    def->gui_type = ConfigItemDef::GUIType::color;
    def->tooltip = L("This is only used in the Slic3r interface as a visual help.");
    def->init_fn = init_with("#29B2B2");

    def = defs.add("material_type", typeid(std::string));
    def->location = Material;
    def->label = L("SLA material type");
    def->category = ConfigItemDef::Category::Hidden;
    def->tooltip = L("SLA material type");
    def->gui_type = ConfigItemDef::GUIType::s_enum_open;
    def->choices = {
        { std::string("Tough"),  std::string("Tough")   },
        { std::string("Flexible"),  std::string("Flexible")   },
        { std::string("Casting"),  std::string("Casting")   },
        { std::string("Dental"),  std::string("Dental")   },
        { std::string("Heat-resistant"),  std::string("Heat-resistant")   } };
    def->init_fn = init_with("Tough");

    def = defs.add("initial_layer_height", typeid(double));
    def->location = Material;
    def->label = L("Initial layer height");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Cooling_FirstLayers;
    def->category = ConfigItemDef::Category::Filament_MaterialTemperatures;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Initial layer height");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(0.3);

    def = defs.add("bottle_volume", typeid(double));
    def->location = Material;
    def->label = L("Bottle volume");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_MaterialProperty;
    def->category = ConfigItemDef::Category::Filament_MaterialTemperatures;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Bottle volume");
    def->units = {L("ml")};
    def->min = 50;
    def->init_fn = init_with(1000.);

    def = defs.add("bottle_weight", typeid(double));
    def->location = Material;
    def->label = L("Bottle weight");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_MaterialProperty;
    def->category = ConfigItemDef::Category::Filament_MaterialTemperatures;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Bottle weight");
    def->units = {L("kg")};
    def->min = 0;
    def->init_fn = init_with(1.);

    def = defs.add("material_density", typeid(double));
    def->location = Material;
    def->label = L("Density");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_MaterialProperty;
    def->category = ConfigItemDef::Category::Filament_MaterialTemperatures;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Density");
    def->units = {L("g/ml")};
    def->min = 0;
    def->init_fn = init_with(1.);

    def = defs.add("bottle_cost", typeid(double));
    def->location = Material;
    def->label = L("Cost");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_MaterialProperty;
    def->category = ConfigItemDef::Category::Filament_MaterialTemperatures;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Cost");
    def->units = {L("money/bottle")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("faded_layers", typeid(int));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Faded layers");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_LayerHeight;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("Number of the layers needed for the exposure time fade from initial exposure time to the exposure time");
    def->min = 3;
    def->max = 20;
    def->init_fn = init_with(10);

    def = defs.add("min_exposure_time", typeid(double));
    def->location = Printer;
    def->label = L("Minimum exposure time");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_Exposure;
    def->category = ConfigItemDef::Category::Printer_General;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Minimum exposure time");
    def->units = {L("s")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("max_exposure_time", typeid(double));
    def->location = Printer;
    def->label = L("Maximum exposure time");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_Exposure;
    def->category = ConfigItemDef::Category::Printer_General;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Maximum exposure time");
    def->units = {L("s")};
    def->min = 0;
    def->init_fn = init_with(100.);

    def = defs.add("exposure_time", typeid(double));
    def->location = Material;
    def->label = L("Exposure time");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_Exposure;
    def->category = ConfigItemDef::Category::Filament_MaterialTemperatures;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Exposure time");
    def->units = {L("s")};
    def->min = 0;
    def->init_fn = init_with(10.);

    def = defs.add("min_initial_exposure_time", typeid(double));
    def->location = Printer;
    def->label = L("Minimum initial exposure time");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_Exposure;
    def->category = ConfigItemDef::Category::Printer_General;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Minimum initial exposure time");
    def->units = {L("s")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("max_initial_exposure_time", typeid(double));
    def->location = Printer;
    def->label = L("Maximum initial exposure time");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_Exposure;
    def->category = ConfigItemDef::Category::Printer_General;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Maximum initial exposure time");
    def->units = {L("s")};
    def->min = 0;
    def->init_fn = init_with(150.);

    def = defs.add("initial_exposure_time", typeid(double));
    def->location = Material;
    def->label = L("Initial exposure time");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_Exposure;
    def->category = ConfigItemDef::Category::Filament_MaterialTemperatures;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Initial exposure time");
    def->units = {L("s")};
    def->min = 0;
    def->init_fn = init_with(15.);

    def = defs.add("material_correction", typeid(std::vector<double>));
    def->location = Material;
    def->category = ConfigItemDef::Category::Hidden;
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_Corrections;
    def->full_label = L("Correction for expansion");
    def->tooltip  = L("Correction for expansion");
    def->min = 0;
    def->init_fn = init_with((std::vector<double>{ 1., 1., 1. }));

    def = defs.add("material_correction_x", typeid(double));
    def->location = Material;
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_Corrections;
    def->label = L("X");
    def->category = ConfigItemDef::Category::Filament_MaterialTemperatures;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->full_label = L("Correction for expansion in X axis");
    def->tooltip  = L("Correction for expansion in X axis");
    def->min = 0;
    def->init_fn = init_with(1.);

    def = defs.add("material_correction_y", typeid(double));
    def->location = Material;
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_Corrections;
    def->label = L("Y");
    def->category = ConfigItemDef::Category::Filament_MaterialTemperatures;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->full_label = L("Correction for expansion in Y axis");
    def->tooltip  = L("Correction for expansion in Y axis");
    def->min = 0;
    def->init_fn = init_with(1.);

    def = defs.add("material_correction_z", typeid(double));
    def->location = Material;
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialTemperatures_Corrections;
    def->label = L("Z");
    def->category = ConfigItemDef::Category::Filament_MaterialTemperatures;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->full_label = L("Correction for expansion in Z axis");
    def->tooltip  = L("Correction for expansion in Z axis");
    def->min = 0;
    def->init_fn = init_with(1.);

    def = defs.add("material_notes", typeid(std::string));
    def->location = Material;
    def->label = L("SLA print material notes");
    def->option_group = ConfigItemDef::OptionGroup::Filament_Notes_Notes;
    def->category = ConfigItemDef::Category::Filament_Notes;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("You can put your notes regarding the SLA print material here.");
    def->multiline = true;
    def->full_width = true;
    def->height = 13;
    // TODO currently notes are the only way to pass data
    // for non-PrusaResearch printers. We therefore need to always show them
    def->init_fn = init_with("");

    def = defs.add("material_vendor", typeid(std::string));
    def->location = Material;
    def->category = ConfigItemDef::Category::Hidden;
    def->cli = ConfigItemDef::nocli;
    def->init_fn = init_with(L("(Unknown)"));

    /* TODO: what about this?
    def = defs.add("default_sla_material_profile", cotypeid(std::string));
    def->label = L("Default SLA material profile");
    def->tooltip = L("Default print profile associated with the current printer profile. "
                   "On selection of the current printer profile, this print profile will be activated.");
    def->set_default_value(new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    def = defs.add("sla_material_settings_id", cotypeid(std::string));
    def->set_default_value(new ConfigOptionString(""));
    def->cli = ConfigOptionDef::nocli;

    def = defs.add("default_sla_print_profile", cotypeid(std::string));
    def->label = L("Default SLA material profile");
    def->tooltip = L("Default print profile associated with the current printer profile. "
                   "On selection of the current printer profile, this print profile will be activated.");
    def->set_default_value(new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    def = defs.add("sla_print_settings_id", cotypeid(std::string));
    def->set_default_value(new ConfigOptionString(""));
    def->cli = ConfigOptionDef::nocli;*/

    def = defs.add("supports_enable", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Generate supports");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_Generation;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Generate supports for the models");
    def->init_fn = init_with(true);

    def = defs.add("support_tree_type", typeid(EnumWrapper));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Support tree type");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_Generation;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("Support tree building strategy");
    def->init_fn = init_with(
        sla::SupportTreeType::Default,
        {
            {int(sla::SupportTreeType::Default), "default", L("Default")},
            // TRN One of the "Support tree type"s on SLAPrintSettings : Supports
            {int(sla::SupportTreeType::Branching), "branching", L("Branching (experimental)")},
            // TODO: { int(sla::SupportTreeType::Organic),  "organic",  L("Organic")  }
        }
    );

    def = defs.add("support_enforcers_only", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Support only in enforced regions");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_Generation;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Only create support if it lies in a support enforcer.");
    def->init_fn = init_with(false);

    def = defs.add("support_points_density_relative", typeid(int));
    def->location = Print;
    def->overrides_in = Locations{ Material, Object};
    def->label = L("Support points density");
    def->option_group = ConfigItemDef::OptionGroup::Print_Supports_Generation;
    def->category = ConfigItemDef::Category::Print_Supports;
    def->gui_type = ConfigItemDef::GUIType::spinbox;
    def->tooltip = L("This is a relative measure of support points density.");
    def->units = {L("%")};
    def->min = 0;
    def->init_fn = init_with(100);

    def = defs.add("pad_enable", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Use pad");
    def->option_group = ConfigItemDef::OptionGroup::Print_Pad_Pad;
    def->category = ConfigItemDef::Category::Print_Pad;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Add a pad underneath the supported model");
    def->init_fn = init_with(true);

    def = defs.add("pad_wall_thickness", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Pad wall thickness");
    def->option_group = ConfigItemDef::OptionGroup::Print_Pad_Pad;
    def->category = ConfigItemDef::Category::Print_Pad;
    def->gui_type = ConfigItemDef::GUIType::textfield;
     def->tooltip = L("The thickness of the pad and its optional cavity walls.");
    def->units = {L("mm")};
    def->min = 0;
    def->max = 30;
    def->init_fn = init_with(2.);

    def = defs.add("pad_wall_height", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Pad wall height");
    def->option_group = ConfigItemDef::OptionGroup::Print_Pad_Pad;
    def->category = ConfigItemDef::Category::Print_Pad;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Defines the pad cavity depth. Set to zero to disable the cavity. "
                     "Be careful when enabling this feature, as some resins may "
                     "produce an extreme suction effect inside the cavity, "
                     "which makes peeling the print off the vat foil difficult.");
//     def->tooltip = L("");
    def->units = {L("mm")};
    def->min = 0;
    def->max = 30;
    def->init_fn = init_with(0.);

    def = defs.add("pad_brim_size", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Pad brim size");
    def->option_group = ConfigItemDef::OptionGroup::Print_Pad_Pad;
    def->category = ConfigItemDef::Category::Print_Pad;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("How far should the pad extend around the contained geometry");
    //     def->tooltip = L("");
    def->units = {L("mm")};
    def->min = 0;
    def->max = 30;
    def->init_fn = init_with(1.6);

    def = defs.add("pad_max_merge_distance", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Max merge distance");
    def->option_group = ConfigItemDef::OptionGroup::Print_Pad_Pad;
    def->category = ConfigItemDef::Category::Print_Pad;
    def->gui_type = ConfigItemDef::GUIType::textfield;
     def->tooltip = L("Some objects can get along with a few smaller pads "
                      "instead of a single big one. This parameter defines "
                      "how far the center of two smaller pads should be. If they"
                      "are closer, they will get merged into one pad.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(50.);

    // This is disabled on the UI. I hope it will never be enabled.
//    def = defs.add("pad_edge_radius", typeid(double));
//    def->label = L("Pad edge radius");
//    def->category = ConfigItemDef::Category::Pad;
////     def->tooltip = L("");
//    def->sidetext = L("mm");
//    def->min = 0;
//    def->init_fn = init_with(1.0));

    def = defs.add("pad_wall_slope", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Pad wall slope");
    def->option_group = ConfigItemDef::OptionGroup::Print_Pad_Pad;
    def->category = ConfigItemDef::Category::Print_Pad;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("The slope of the pad wall relative to the bed plane. "
                     "90 degrees means straight walls.");
    def->units = {L("°")};
    def->min = 45;
    def->max = 90;
    def->init_fn = init_with(90.);

    def = defs.add("pad_around_object", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Pad around object");
    def->option_group = ConfigItemDef::OptionGroup::Print_Pad_Pad;
    def->category = ConfigItemDef::Category::Print_Pad;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Create pad around object and ignore the support elevation");
    def->init_fn = init_with(false);

    def = defs.add("pad_around_object_everywhere", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Pad around object everywhere");
    def->option_group = ConfigItemDef::OptionGroup::Print_Pad_Pad;
    def->category = ConfigItemDef::Category::Print_Pad;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Force pad around object everywhere");
    def->init_fn = init_with(false);

    def = defs.add("pad_object_gap", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Pad object gap");
    def->option_group = ConfigItemDef::OptionGroup::Print_Pad_Pad;
    def->category = ConfigItemDef::Category::Print_Pad;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip  = L("The gap between the object bottom and the generated "
                      "pad in zero elevation mode.");
    def->units = {L("mm")};
    def->min = 0;
    def->max = 10;
    def->init_fn = init_with(1.);

    def = defs.add("pad_object_connector_stride", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Pad object connector stride");
    def->option_group = ConfigItemDef::OptionGroup::Print_Pad_Pad;
    def->category = ConfigItemDef::Category::Print_Pad;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Distance between two connector sticks which connect the object and the generated pad.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(10.);

    def = defs.add("pad_object_connector_width", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Pad object connector width");
    def->option_group = ConfigItemDef::OptionGroup::Print_Pad_Pad;
    def->category = ConfigItemDef::Category::Print_Pad;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip  = L("Width of the connector sticks which connect the object and the generated pad.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(0.5);

    def = defs.add("pad_object_connector_penetration", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Pad object connector penetration");
    def->option_group = ConfigItemDef::OptionGroup::Print_Pad_Pad;
    def->category = ConfigItemDef::Category::Print_Pad;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip  = L(
        "How much should the tiny connectors penetrate into the model body.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(0.3);

    def = defs.add("hollowing_enable", typeid(bool));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Enable hollowing");
    def->option_group = ConfigItemDef::OptionGroup::Print_Hollowing_Hollowing;
    def->category = ConfigItemDef::Category::Print_Hollowing;
    def->gui_type = ConfigItemDef::GUIType::checkbox;
    def->tooltip = L("Hollow out a model to have an empty interior");
    def->init_fn = init_with(false);

    def = defs.add("hollowing_min_thickness", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Wall thickness");
    def->option_group = ConfigItemDef::OptionGroup::Print_Hollowing_Hollowing;
    def->category = ConfigItemDef::Category::Print_Hollowing;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip  = L("Minimum wall thickness of a hollowed model.");
    def->units = {L("mm")};
    def->min = 1;
    def->max = 10;
    def->init_fn = init_with(3.);

    def = defs.add("hollowing_quality", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Accuracy");
    def->option_group = ConfigItemDef::OptionGroup::Print_Hollowing_Hollowing;
    def->category = ConfigItemDef::Category::Print_Hollowing;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip  = L("Performance vs accuracy of calculation. Lower values may produce unwanted artifacts.");
    def->min = 0;
    def->max = 1;
    def->init_fn = init_with(0.5);

    def = defs.add("hollowing_closing_distance", typeid(double));
    def->location = Print;
    def->overrides_in = Locations{ Object };
    def->label = L("Closing distance");
    def->option_group = ConfigItemDef::OptionGroup::Print_Hollowing_Hollowing;
    def->category = ConfigItemDef::Category::Print_Hollowing;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip  = L(
        "Hollowing is done in two steps: first, an imaginary interior is "
        "calculated deeper (offset plus the closing distance) in the object and "
        "then it's inflated back to the specified offset. A greater closing "
        "distance makes the interior more rounded. At zero, the interior will "
        "resemble the exterior the most.");
    def->units = {L("mm")};
    def->min = 0;
    def->max = 10;
    def->init_fn = init_with(2.);

    def = defs.add("material_print_speed", typeid(EnumWrapper));
    def->location = Material;
    def->label = L("Print speed");
    def->category = ConfigItemDef::Category::Filament_MaterialPrintingProfile;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L(
        "A slower printing profile might be necessary when using materials with higher viscosity "
        "or with some hollowed parts. It slows down the tilt movement and adds a delay before exposure.");
    def->init_fn = init_with(
        SLAMaterialSpeed::slamsFast,
        {{int(SLAMaterialSpeed::slamsSlow), "slow", L("Slow")},
         {int(SLAMaterialSpeed::slamsFast), "fast", L("Fast")},
         {int(SLAMaterialSpeed::slamsHighViscosity), "high_viscosity", L("High viscosity")}}
    );

    def = defs.add("sla_archive_format", typeid(std::string));
    def->location = Printer;
    def->label = L("Format of the output SLA archive");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_Output;
    def->category = ConfigItemDef::Category::Printer_General;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->init_fn = init_with("SL1");

    def = defs.add("sla_output_precision", typeid(double));
    def->location = Printer;
    def->label = L("SLA output precision");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_Output;
    def->category = ConfigItemDef::Category::Printer_General;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Minimum resolution in nanometers");
    def->units = {L("mm")};
    def->min = 0.000001f;
    def->init_fn = init_with(0.001);

    def = defs.add("delay_before_exposure", typeid(std::vector<double>));
    def->location = Material;
    def->label = L("Delay before exposure");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialPrintingProfile_ProfilesSettings;
    def->category = ConfigItemDef::Category::Filament_MaterialPrintingProfile;
    def->gui_type = ConfigItemDef::GUIType::textfields;
    def->tooltip = L("Delay before exposure after previous layer separation.");
    def->units = {L("s")};
    def->min = 0;
    def->max = 30;
    def->init_fn = init_with((std::vector<double>{ 3., 3.}));

    def = defs.add("delay_after_exposure", typeid(std::vector<double>));
    def->location = Material;
    def->label = L("Delay after exposure");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialPrintingProfile_ProfilesSettings;
    def->category = ConfigItemDef::Category::Filament_MaterialPrintingProfile;
    def->gui_type = ConfigItemDef::GUIType::textfields;
    def->tooltip = L("Delay after exposure before layer separation.");
    def->units = {L("s")};
    def->min = 0;
    def->max = 30;
    def->init_fn = init_with((std::vector<double>{ 0., 0.}));

    def = defs.add("tower_hop_height", typeid(std::vector<double>));
    def->location = Material;
    def->label = L("Tower hop height");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialPrintingProfile_ProfilesSettings;
    def->category = ConfigItemDef::Category::Filament_MaterialPrintingProfile;
    def->gui_type = ConfigItemDef::GUIType::textfields;
    def->tooltip = L("The height of the tower raise.");
    def->units = {L("mm")};
    def->min = 0;
    def->max = 100;
    def->init_fn = init_with((std::vector<double>{ 0., 0.}));

    def = defs.add("tower_speed", typeid(EnumVectorWrapper));
    def->location = Material;
    def->label = L("Tower speed");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialPrintingProfile_ProfilesSettings;
    def->category = ConfigItemDef::Category::Filament_MaterialPrintingProfile;
    def->gui_type = ConfigItemDef::GUIType::comboboxes;
    def->tooltip = L("Tower speed used for tower raise.");
    def->units = {L("mm/s")};
    def->init_fn = init_with(
        std::vector<TowerSpeeds>{tsLayer22, tsLayer22},
        {{int(TowerSpeeds::tsLayer1), "layer1", "1"},
         {int(TowerSpeeds::tsLayer2), "layer2", "2"},
         {int(TowerSpeeds::tsLayer3), "layer3", "3"},
         {int(TowerSpeeds::tsLayer4), "layer4", "4"},
         {int(TowerSpeeds::tsLayer5), "layer5", "5"},
         {int(TowerSpeeds::tsLayer8), "layer8", "8"},
         {int(TowerSpeeds::tsLayer11), "layer11", "11"},
         {int(TowerSpeeds::tsLayer14), "layer14", "14"},
         {int(TowerSpeeds::tsLayer18), "layer18", "18"},
         {int(TowerSpeeds::tsLayer22), "layer22", "22"},
         {int(TowerSpeeds::tsLayer24), "layer24", "24"}}
    );

    get_enum_defs().push_back(std::make_unique<EnumValueDefs>(
        EnumValueDefs{
            {int(TiltSpeeds::tsMove120), "move120", "120"},
            {int(TiltSpeeds::tsLayer200), "layer200", "200"},
            {int(TiltSpeeds::tsMove300), "move300", "300"},
            {int(TiltSpeeds::tsLayer400), "layer400", "400"},
            {int(TiltSpeeds::tsLayer600), "layer600", "600"},
            {int(TiltSpeeds::tsLayer800), "layer800", "800"},
            {int(TiltSpeeds::tsLayer1000), "layer1000", "1000"},
            {int(TiltSpeeds::tsLayer1250), "layer1250", "1250"},
            {int(TiltSpeeds::tsLayer1500), "layer1500", "1500"},
            {int(TiltSpeeds::tsLayer1750), "layer1750", "1750"},
            {int(TiltSpeeds::tsLayer2000), "layer2000", "2000"},
            {int(TiltSpeeds::tsLayer2250), "layer2250", "2250"},
            {int(TiltSpeeds::tsMove5120), "move5120", "5120"},
            {int(TiltSpeeds::tsMove8000), "move8000", "8000"},
        }
    ));
    const EnumValueDefs* tilt_speeds_enum_def{get_enum_defs().back().get()};

    def = defs.add("tilt_down_initial_speed", typeid(EnumVectorWrapper));
    def->location = Material;
    def->label = L("Tilt down initial speed");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialPrintingProfile_ProfilesSettings;
    def->category = ConfigItemDef::Category::Filament_MaterialPrintingProfile;
    def->gui_type = ConfigItemDef::GUIType::comboboxes;
    def->tooltip = L("Tilt speed used for an initial portion of tilt down move.");
    def->units = {L("μ-steps/s")};
    def->init_fn = init_with(std::vector<TiltSpeeds>{ tsLayer1750, tsLayer1750 }, tilt_speeds_enum_def);

    def = defs.add("tilt_down_finish_speed", typeid(EnumVectorWrapper));
    def->location = Material;
    def->label = L("Tilt down finish speed");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialPrintingProfile_ProfilesSettings;
    def->category = ConfigItemDef::Category::Filament_MaterialPrintingProfile;
    def->gui_type = ConfigItemDef::GUIType::comboboxes;
    def->tooltip = L("Tilt speed used for the rest of the tilt down move.");
    def->units = {L("μ-steps/s")};
    def->init_fn = init_with(std::vector<TiltSpeeds>{ tsLayer1750, tsLayer1750 }, tilt_speeds_enum_def);

    def = defs.add("tilt_up_initial_speed", typeid(EnumVectorWrapper));
    def->location = Material;
    def->label = L("Tilt up initial speed");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialPrintingProfile_ProfilesSettings;
    def->category = ConfigItemDef::Category::Filament_MaterialPrintingProfile;
    def->gui_type = ConfigItemDef::GUIType::comboboxes;
    def->tooltip = L("Tilt speed used for an initial portion of tilt up move.");
    def->units = {L("μ-steps/s")};
    def->init_fn = init_with(std::vector<TiltSpeeds>{ tsMove8000, tsMove8000 }, tilt_speeds_enum_def);

    def = defs.add("tilt_up_finish_speed", typeid(EnumVectorWrapper));
    def->location = Material;
    def->label = L("Tilt up finish speed");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialPrintingProfile_ProfilesSettings;
    def->category = ConfigItemDef::Category::Filament_MaterialPrintingProfile;
    def->gui_type = ConfigItemDef::GUIType::comboboxes;
    def->tooltip = L("Tilt speed used for the rest of the tilt-up.");
    def->units = {L("μ-steps/s")};
    def->init_fn = init_with((std::vector<TiltSpeeds>{ tsLayer1750, tsLayer1750 }), tilt_speeds_enum_def);

    def = defs.add("use_tilt", typeid(std::vector<bool>));
    def->location = Material;
    def->label = L("Use tilt");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialPrintingProfile_ProfilesSettings;
    def->category = ConfigItemDef::Category::Filament_MaterialPrintingProfile;
    def->gui_type = ConfigItemDef::GUIType::checkboxes;
    def->tooltip = L("If enabled, tilt is used for layer separation. Otherwise, all the parameters below are ignored.");
    def->init_fn = init_with((std::vector<bool>{ true, true }));

    def = defs.add("tilt_down_offset_steps", typeid(std::vector<int>));
    def->location = Material;
    def->label = L("Tilt down offset steps");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialPrintingProfile_ProfilesSettings;
    def->category = ConfigItemDef::Category::Filament_MaterialPrintingProfile;
    def->gui_type = ConfigItemDef::GUIType::spinboxes;
    def->tooltip = L("Number of steps to move down from the calibrated (horizontal) position with 'tilt_down_initial_speed'.");
    def->units = {L("μ-steps")};
    def->min = 0;
    def->max = 10000;
    def->init_fn = init_with((std::vector<int>{ 0, 0 }));

    def = defs.add("tilt_down_offset_delay", typeid(std::vector<double>));
    def->location = Material;
    def->label = L("Tilt down offset delay");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialPrintingProfile_ProfilesSettings;
    def->category = ConfigItemDef::Category::Filament_MaterialPrintingProfile;
    def->gui_type = ConfigItemDef::GUIType::textfields;
    def->tooltip = L("Delay after the tilt reaches 'tilt_down_offset_steps' position.");
    def->units = {L("s")};
    def->min = 0;
    def->max = 20;
    def->init_fn = init_with((std::vector<double>{ 0., 0. }));

    def = defs.add("tilt_down_cycles", typeid(std::vector<int>));
    def->location = Material;
    def->label = L("Tilt down cycles");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialPrintingProfile_ProfilesSettings;
    def->category = ConfigItemDef::Category::Filament_MaterialPrintingProfile;
    def->gui_type = ConfigItemDef::GUIType::spinboxes;
    def->tooltip = L("Number of cycles to split the rest of the tilt down move.");
    def->min = 0;
    def->max = 10;
    def->init_fn = init_with((std::vector<int>{ 1, 1 }));

    def = defs.add("tilt_down_delay", typeid(std::vector<double>));
    def->location = Material;
    def->label = L("Tilt down delay");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialPrintingProfile_ProfilesSettings;
    def->category = ConfigItemDef::Category::Filament_MaterialPrintingProfile;
    def->gui_type = ConfigItemDef::GUIType::textfields;
    def->tooltip = L("The delay between tilt-down cycles.");
    def->units = {L("s")};
    def->min = 0;
    def->max = 20;
    def->init_fn = init_with((std::vector<double>{ 0., 0. }));

    def = defs.add("tilt_up_offset_steps", typeid(std::vector<int>));
    def->location = Material;
    def->label = L("Tilt up offset steps");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialPrintingProfile_ProfilesSettings;
    def->category = ConfigItemDef::Category::Filament_MaterialPrintingProfile;
    def->gui_type = ConfigItemDef::GUIType::spinboxes;
    def->tooltip = L("Move tilt up to calibrated (horizontal) position minus this offset.");
    def->units = {L("μ-steps")};
    def->min = 0;
    def->max = 10000;
    def->init_fn = init_with((std::vector<int>{ 1200, 1200 }));

    def = defs.add("tilt_up_offset_delay", typeid(std::vector<double>));
    def->location = Material;
    def->label = L("Tilt up offset delay");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialPrintingProfile_ProfilesSettings;
    def->category = ConfigItemDef::Category::Filament_MaterialPrintingProfile;
    def->gui_type = ConfigItemDef::GUIType::textfields;
    def->tooltip = L("Delay after the tilt reaches 'tilt_up_offset_steps' position.");
    def->units = {L("s")};
    def->min = 0;
    def->max = 20;
    def->init_fn = init_with((std::vector<double>{ 0., 0. }));

    def = defs.add("tilt_up_cycles", typeid(std::vector<int>));
    def->location = Material;
    def->label = L("Tilt up cycles");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialPrintingProfile_ProfilesSettings;
    def->category = ConfigItemDef::Category::Filament_MaterialPrintingProfile;
    def->gui_type = ConfigItemDef::GUIType::spinboxes;
    def->tooltip = L("Number of cycles to split the rest of the tilt-up.");
    def->min = 0;
    def->max = 10;
    def->init_fn = init_with((std::vector<int>{ 1, 1 }));

    def = defs.add("tilt_up_delay", typeid(std::vector<double>));
    def->location = Material;
    def->label = L("Tilt up delay");
    def->option_group = ConfigItemDef::OptionGroup::Filament_MaterialPrintingProfile_ProfilesSettings;
    def->category = ConfigItemDef::Category::Filament_MaterialPrintingProfile;
    def->gui_type = ConfigItemDef::GUIType::textfields;
    def->tooltip = L("The delay between tilt-up cycles.");
    def->units = {L("s")};
    def->min = 0;
    def->max = 20;
    def->init_fn = init_with((std::vector<double>{ 0., 0. }));

    for (const std::pair<std::string, std::string> prefix : { std::make_pair("", L("Default")), std::make_pair("branching", L("Branching")) }) {
        def = defs.add(prefix.first + "support_head_front_diameter", typeid(double));
        def->label = prefix.second;
        def->location = Print;
        def->overrides_in = Locations{ Material, Object };
        def->row_group = L("Pinhead front diameter");
        def->option_group = ConfigItemDef::OptionGroup::Print_Supports_SupportHead;
        def->category = ConfigItemDef::Category::Print_Supports;
        def->gui_type = ConfigItemDef::GUIType::textfield;
        def->tooltip = L("Diameter of the pointing side of the head");
        def->units = {L("mm")};
        def->min = 0;
        def->init_fn = init_with(0.4);

        def = defs.add(prefix.first + "support_head_penetration", typeid(double));
        def->label = prefix.second;
        def->location = Print;
        def->overrides_in = Locations{ Material, Object };
        def->row_group = L("Head penetration");
        def->option_group = ConfigItemDef::OptionGroup::Print_Supports_SupportHead;
        def->category = ConfigItemDef::Category::Print_Supports;
        def->gui_type = ConfigItemDef::GUIType::textfield;
        def->tooltip = L("How much the pinhead has to penetrate the model surface");
        def->units = {L("mm")};
        def->min = 0;
        def->init_fn = init_with(0.2);

        def = defs.add(prefix.first + "support_head_width", typeid(double));
        def->label = prefix.second;
        def->location = Print;
        def->overrides_in = Locations{ Material, Object };
        def->row_group = L("Pinhead width");
        def->option_group = ConfigItemDef::OptionGroup::Print_Supports_SupportHead;
        def->category = ConfigItemDef::Category::Print_Supports;
        def->gui_type = ConfigItemDef::GUIType::textfield;
        def->tooltip = L("Width from the back sphere center to the front sphere center");
        def->units = {L("mm")};
        def->min = 0;
        def->max = 20;
        def->init_fn = init_with(1.);

        def = defs.add(prefix.first + "support_pillar_diameter", typeid(double));
        def->label = prefix.second;
        def->location = Print;
        def->overrides_in = Locations{ Material, Object };
        def->row_group = L("Pillar diameter");
        def->option_group = ConfigItemDef::OptionGroup::Print_Supports_SupportPillar;
        def->category = ConfigItemDef::Category::Print_Supports;
        def->gui_type = ConfigItemDef::GUIType::textfield;
        def->tooltip = L("Diameter in mm of the support pillars");
        def->units = {L("mm")};
        def->min = 0;
        def->max = 15;
        def->init_fn = init_with(1.);

        def = defs.add(prefix.first + "support_small_pillar_diameter_percent", typeid(Percentage));
        def->label = prefix.second;
        def->location = Print;
        def->overrides_in = Locations{ Object };
        def->row_group = L("Small pillar diameter percent");
        def->option_group = ConfigItemDef::OptionGroup::Print_Supports_SupportPillar;
        def->category = ConfigItemDef::Category::Print_Supports;
        def->gui_type = ConfigItemDef::GUIType::textfield;
        def->tooltip = L("The percentage of smaller pillars compared to the normal pillar diameter "
            "which are used in problematic areas where a normal pilla cannot fit.");
        def->units = {L("%")};
        def->min = 1;
        def->max = 100;
        def->init_fn = init_with(Percentage{50.});

        def = defs.add(prefix.first + "support_max_bridges_on_pillar", typeid(int));
        def->label = prefix.second;
        def->location = Print;
        def->overrides_in = Locations{ Object };
        def->row_group = L("Max bridges on a pillar");
        def->option_group = ConfigItemDef::OptionGroup::Print_Supports_SupportPillar;
        def->category = ConfigItemDef::Category::Print_Supports;
        def->gui_type = ConfigItemDef::GUIType::spinbox;
        def->tooltip = L(
            "Maximum number of bridges that can be placed on a pillar. Bridges "
            "hold support point pinheads and connect to pillars as small branches.");
        def->min = 0;
        def->max = 50;
        if (prefix.first == "branching")
            def->init_fn = init_with(2);
        else
            def->init_fn = init_with(3);

        def = defs.add(prefix.first + "support_max_weight_on_model", typeid(double));
        def->label = prefix.second;
        def->location = Print;
        def->overrides_in = Locations{ Object };
        def->row_group = L("Max weight on model");
        def->option_group = ConfigItemDef::OptionGroup::Print_Supports_SupportPillar;
        def->category = ConfigItemDef::Category::Print_Supports;
        def->gui_type = ConfigItemDef::GUIType::textfield;
        def->tooltip = L(
            "Maximum weight of sub-trees that terminate on the model instead of the print bed. The weight is the sum of the lenghts of all "
            "branches emanating from the endpoint.");
        def->units = {L("mm")};
        def->min = 0;
        def->init_fn = init_with(10.);

        def = defs.add(prefix.first + "support_pillar_connection_mode", typeid(EnumWrapper));
        def->label = prefix.second;
        def->location = Print;
        def->overrides_in = Locations{ Object };
        def->row_group = L("Pillar connection mode");
        def->option_group = ConfigItemDef::OptionGroup::Print_Supports_SupportPillar;
        def->category = ConfigItemDef::Category::Print_Supports;
        def->gui_type = ConfigItemDef::GUIType::combobox;
        def->tooltip = L("Controls the bridge type between two neighboring pillars."
            " Can be zig-zag, cross (double zig-zag) or dynamic which"
            " will automatically switch between the first two depending"
            " on the distance of the two pillars.");
        def->init_fn = init_with(
            sla::PillarConnectionMode::dynamic,
            {{int(sla::PillarConnectionMode::zigzag), "zigzag", L("Zig-Zag")},
             {int(sla::PillarConnectionMode::cross), "cross", L("Cross")},
             {int(sla::PillarConnectionMode::dynamic), "dynamic", L("Dynamic")}}
        );

        def = defs.add(prefix.first + "support_buildplate_only", typeid(bool));
        def->label = prefix.second;
        def->location = Print;
        def->overrides_in = Locations{ Object };
        def->row_group = L("Support on build plate only");
        def->option_group = ConfigItemDef::OptionGroup::Print_Supports_SupportPillar;
        def->category = ConfigItemDef::Category::Print_Supports;
        def->gui_type = ConfigItemDef::GUIType::checkbox;
        def->tooltip = L("Only create support if it lies on a build plate. Don't create support on a print.");
        def->init_fn = init_with(false);

        def = defs.add(prefix.first + "support_pillar_widening_factor", typeid(double));
        def->label = prefix.second;
        def->location = Print;
        def->overrides_in = Locations{ Object };
        def->row_group = L("Pillar widening factor");
        def->option_group = ConfigItemDef::OptionGroup::Print_Supports_SupportPillar;
        def->category = ConfigItemDef::Category::Print_Supports;
        def->gui_type = ConfigItemDef::GUIType::textfield;
        def->tooltip =
            L("Merging bridges or pillars into another pillars can "
                "increase the radius. Zero means no increase, one means "
                "full increase. The exact amount of increase is unspecified and can "
                "change in the future.");
        def->min = 0;
        def->max = 1;
        def->init_fn = init_with(0.5);

        def = defs.add(prefix.first + "support_base_diameter", typeid(double));
        def->label = prefix.second;
        def->location = Print;
        def->overrides_in = Locations{ Object };
        def->row_group = L("Support base diameter");
        def->option_group = ConfigItemDef::OptionGroup::Print_Supports_SupportPillar;
        def->category = ConfigItemDef::Category::Print_Supports;
        def->gui_type = ConfigItemDef::GUIType::textfield;
        def->tooltip = L("Diameter in mm of the pillar base");
        def->units = {L("mm")};
        def->min = 0;
        def->max = 30;
        def->init_fn = init_with(4.);

        def = defs.add(prefix.first + "support_base_height", typeid(double));
        def->label = prefix.second;
        def->location = Print;
        def->overrides_in = Locations{ Object };
        def->row_group = L("Support base height");
        def->option_group = ConfigItemDef::OptionGroup::Print_Supports_SupportPillar;
        def->category = ConfigItemDef::Category::Print_Supports;
        def->gui_type = ConfigItemDef::GUIType::textfield;
        def->tooltip = L("The height of the pillar base cone");
        def->units = {L("mm")};
        def->min = 0;
        def->init_fn = init_with(1.);

        def = defs.add(prefix.first + "support_base_safety_distance", typeid(double));
        def->label = prefix.second;
        def->location = Print;
        def->overrides_in = Locations{ Object };
        def->row_group = L("Support base safety distance");
        def->option_group = ConfigItemDef::OptionGroup::Print_Supports_SupportPillar;
        def->category = ConfigItemDef::Category::Print_Supports;
        def->gui_type = ConfigItemDef::GUIType::textfield;
        def->tooltip = L(
            "The minimum distance of the pillar base from the model in mm. "
            "Makes sense in zero elevation mode where a gap according "
            "to this parameter is inserted between the model and the pad.");
        def->units = {L("mm")};
        def->min = 0;
        def->max = 10;
        def->init_fn = init_with(1.);

        def = defs.add(prefix.first + "support_critical_angle", typeid(double));
        def->label = prefix.second;
        def->location = Print;
        def->overrides_in = Locations{ Object };
        def->row_group = L("Critical angle");
        def->option_group = ConfigItemDef::OptionGroup::Print_Supports_SticksJunctions;
        def->category = ConfigItemDef::Category::Print_Supports;
        def->gui_type = ConfigItemDef::GUIType::textfield;
        def->tooltip = L("The default angle for connecting support sticks and junctions.");
        def->units = {L("°")};
        def->min = 0;
        def->max = 90;
        def->init_fn = init_with(45.);

        def = defs.add(prefix.first + "support_max_bridge_length", typeid(double));
        def->label = prefix.second;
        def->location = Print;
        def->overrides_in = Locations{ Object };
        def->row_group = L("Max bridge length");
        def->option_group = ConfigItemDef::OptionGroup::Print_Supports_SticksJunctions;
        def->category = ConfigItemDef::Category::Print_Supports;
        def->gui_type = ConfigItemDef::GUIType::textfield;
        def->tooltip = L("The max length of a bridge");
        def->units = {L("mm")};
        def->min = 0;
        if (prefix.first == "branching")
            def->init_fn = init_with(5.0);
        else
            def->init_fn = init_with(15.0);

        def = defs.add(prefix.first + "support_max_pillar_link_distance", typeid(double));
        def->label = prefix.second;
        def->location = Print;
        def->overrides_in = Locations{ Object };
        def->row_group = L("Max pillar linking distance");
        def->option_group = ConfigItemDef::OptionGroup::Print_Supports_SticksJunctions;
        def->category = ConfigItemDef::Category::Print_Supports;
        def->gui_type = ConfigItemDef::GUIType::textfield;
        def->tooltip = L("The max distance of two pillars to get linked with each other."
            " A zero value will prohibit pillar cascading.");
        def->units = {L("mm")};
        def->min = 0;   // 0 means no linking
        def->init_fn = init_with(10.);

        def = defs.add(prefix.first + "support_object_elevation", typeid(double));
        def->label = prefix.second;
        def->location = Print;
        def->overrides_in = Locations{ Object };
        def->row_group = L("Object elevation");
        def->category = ConfigItemDef::Category::Print_Supports;
        def->gui_type = ConfigItemDef::GUIType::textfield;
        def->tooltip = L("How much the supports should lift up the supported object. "
            "If \"Pad around object\" is enabled, this value is ignored.");
        def->units = {L("mm")};
        def->min = 0;
        def->max = 150; // This is the max height of print on SL1
        def->init_fn = init_with(5.);
    }
}



} // namespace Slic3r::Domain
