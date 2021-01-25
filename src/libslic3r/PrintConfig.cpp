#include "PrintConfig.hpp"
#include "Flow.hpp"
#include "I18N.hpp"

#include <set>
#include <unordered_set>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/thread.hpp>
#include <boost/nowide/iostream.hpp>

#include <float.h>

namespace Slic3r {

//! macro used to mark string used at localization,
//! return same string
#define L(s) (s)
#define _(s) Slic3r::I18N::translate(s)

static void assign_printer_technology_to_unknown(t_optiondef_map &options, PrinterTechnology printer_technology)
{
    for (std::pair<const t_config_option_key, ConfigOptionDef> &kvp : options)
        if (kvp.second.printer_technology == ptUnknown)
            kvp.second.printer_technology = printer_technology;
}

PrintConfigDef::PrintConfigDef()
{
    this->init_common_params();
    //assign params that are not already allocated to FFF+SLA (default from slic3rPE)
    assign_printer_technology_to_unknown(this->options, ptFFF | ptSLA);
    this->init_fff_params();
    this->init_extruder_option_keys();
    assign_printer_technology_to_unknown(this->options, ptFFF);
    this->init_sla_params();
    assign_printer_technology_to_unknown(this->options, ptSLA);
    this->init_milling_params();
    assign_printer_technology_to_unknown(this->options, ptMill);
}

void PrintConfigDef::init_common_params()
{
    ConfigOptionDef* def;

    def = this->add("printer_technology", coEnum);
    def->label = L("Printer technology");
    def->tooltip = L("Printer technology");
    def->category = OptionCategory::general;
    def->enum_keys_map = &ConfigOptionEnum<PrinterTechnology>::get_enum_values();
    def->enum_values.push_back("FFF");
    def->enum_values.push_back("SLA");
    def->set_default_value(new ConfigOptionEnum<PrinterTechnology>(ptFFF));

    def = this->add("bed_shape", coPoints);
    def->label = L("Bed shape");
    def->category = OptionCategory::general;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPoints{ Vec2d(0, 0), Vec2d(200, 0), Vec2d(200, 200), Vec2d(0, 200) });

    def = this->add("bed_custom_texture", coString);
    def->label = L("Bed custom texture");
    def->category = OptionCategory::general;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("bed_custom_model", coString);
    def->label = L("Bed custom model");
    def->category = OptionCategory::general;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("thumbnails", coPoints);
    def->label = L("Thumbnails size");
    def->tooltip = L("Picture sizes to be stored into a .gcode and .sl1 files, in the following format: \"XxY, XxY, ...\"");
    def->mode = comExpert;
    //def->gui_type = "one_string"; //supermerill: test/see what this does.
    def->set_default_value(new ConfigOptionPoints{ Vec2d(0,0), Vec2d(0,0) });

    def = this->add("thumbnails_color", coString);
    def->label = L("Color");
    def->full_label = L("Thumbnail color");
    def->category = OptionCategory::filament;
    def->tooltip = L("This is the color that will be enforce on objects in the thumbnails.");
    def->gui_type = "color";
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionString("#018aff"));

    def = this->add("thumbnails_custom_color", coBool);
    def->label = L("Enforce thumbnail color");
    def->tooltip = L("Enforce a specific color on thumbnails."
        " If not enforced, their color will be the one defined by the filament.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("thumbnails_with_bed", coBool);
    def->label = L("Bed on thumbnail");
    def->tooltip = L("Show the bed texture on the thumbnail picture.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("thumbnails_with_support", coBool);
    def->label = L("Support on thumbnail");
    def->tooltip = L("Show the supports (and pads) on the thumbnail picture.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("layer_height", coFloat);
    def->label = L("Base Layer height");
    def->category = OptionCategory::perimeter;
    def->tooltip = L("This setting controls the height (and thus the total number) of the slices/layers. "
        "Thinner layers give better accuracy but take more time to print.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.2));

    def = this->add("max_print_height", coFloat);
    def->label = L("Max print height");
    def->category = OptionCategory::general;
    def->tooltip = L("Set this to the maximum height that can be reached by your extruder while printing.");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 1200;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(200.0));

    def = this->add("slice_closing_radius", coFloat);
    def->label = L("Slice gap closing radius");
    def->category = OptionCategory::slicing;
    def->tooltip = L("Cracks smaller than 2x gap closing radius are being filled during the triangle mesh slicing. "
                     "The gap closing operation may reduce the final print resolution, therefore it is advisable to keep the value reasonably low.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.049));

    def = this->add("print_host", coString);
    def->label = L("Hostname, IP or URL");
    def->category = OptionCategory::general;
    def->tooltip = L("Slic3r can upload G-code files to a printer host. This field should contain "
                   "the hostname, IP address or URL of the printer host instance. "
                   "Print host behind HAProxy with basic auth enabled can be accessed by putting the user name and password into the URL "
                   "in the following format: https://username:password@your-octopi-address/");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("printhost_apikey", coString);
    def->label = L("API Key / Password");
    def->category = OptionCategory::general;
    def->tooltip = L("Slic3r can upload G-code files to a printer host. This field should contain "
                   "the API Key or the password required for authentication.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString(""));
    
    def = this->add("printhost_port", coString);
    def->label = L("Printer");
    def->tooltip = L("Name of the printer");
    def->gui_type = "select_open";
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString(""));
    
    def = this->add("printhost_cafile", coString);
    def->label = L("HTTPS CA File");
    def->category = OptionCategory::general;
    def->tooltip = L("Custom CA certificate file can be specified for HTTPS OctoPrint connections, in crt/pem format. "
                   "If left blank, the default OS CA certificate repository is used.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString(""));


    // Options used by physical printers
    
    def = this->add("printhost_user", coString);
    def->label = L("User");
//    def->tooltip = L("");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString(""));
    
    def = this->add("printhost_password", coString);
    def->label = L("Password");
//    def->tooltip = L("");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString(""));
    
    def = this->add("preset_name", coString);
    def->label = L("Printer preset name");
    def->tooltip = L("Related printer preset name");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("printhost_authorization_type", coEnum);
    def->label = L("Authorization Type");
//    def->tooltip = L("");
    def->enum_keys_map = &ConfigOptionEnum<AuthorizationType>::get_enum_values();
    def->enum_values.push_back("key");
    def->enum_values.push_back("user");
    def->enum_labels.push_back(L("API key"));
    def->enum_labels.push_back(L("HTTP digest"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<AuthorizationType>(atKeyPassword));
}

void PrintConfigDef::init_fff_params()
{
    ConfigOptionDef* def;

    // Maximum extruder temperature, bumped to 1500 to support printing of glass.
    const int max_temp = 1500;

    def = this->add("allow_empty_layers", coBool);
    def->label = L("Allow empty layers");
    def->full_label = L("Allow empty layers");
    def->category = OptionCategory::slicing;
    def->tooltip = L("Do not prevent the gcode builder to trigger an exception if a full layer is empty and so the print will have to start from thin air afterward.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("avoid_crossing_perimeters", coBool);
    def->label = L("Avoid crossing perimeters");
    def->category = OptionCategory::perimeter;
    def->tooltip = L("Optimize travel moves in order to minimize the crossing of perimeters. "
        "This is mostly useful with Bowden extruders which suffer from oozing. "
        "This feature slows down both the print and the G-code generation.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("avoid_crossing_not_first_layer", coBool);
    def->label = L("Don't avoid crossing on 1st layer");
    def->full_label = L("Don't avoid crossing on 1st layer");
    def->category = OptionCategory::perimeter;
    def->tooltip = L("Do not use the 'Avoid crossing perimeters' on the first layer.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(true));
    def = this->add("avoid_crossing_perimeters_max_detour", coFloatOrPercent);
    def->label = L("Avoid crossing perimeters - Max detour length");
    def->category = OptionCategory::perimeter;
    def->tooltip = L("The maximum detour length for avoid crossing perimeters. "
                     "If the detour is longer than this value, avoid crossing perimeters is not applied for this travel path. "
                     "Detour length could be specified either as an absolute value or as percentage (for example 50%) of a direct travel path.");
    def->sidetext = L("mm or % (zero to disable)");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloatOrPercent(0., false));

    def = this->add("bed_temperature", coInts);
    def->label = L("Other layers");
    def->category = OptionCategory::filament;
    def->tooltip = L("Bed temperature for layers after the first one. "
                   "Set this to zero to disable bed temperature control commands in the output.");
    def->sidetext = L("°C");
    def->full_label = L("Bed temperature");
    def->sidetext = L("°C");
    def->min = 0;
    def->max = 300;
    def->set_default_value(new ConfigOptionInts { 0 });

    def = this->add("before_layer_gcode", coString);
    def->label = L("Before layer change G-code");
    def->category = OptionCategory::customgcode;
    def->tooltip = L("This custom code is inserted at every layer change, right before the Z move. "
                   "Note that you can use placeholder variables for all Slic3r settings as well "
                   "as [layer_num] and [layer_z].");
    def->multiline = true;
    def->full_width = true;
    def->height = 5;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("between_objects_gcode", coString);
    def->label = L("Between objects G-code");
    def->category = OptionCategory::customgcode;
    def->tooltip = L("This code is inserted between objects when using sequential printing. By default extruder and bed temperature are reset using non-wait command; however if M104, M109, M140 or M190 are detected in this custom code, Slic3r will not add temperature commands. Note that you can use placeholder variables for all Slic3r settings, so you can put a \"M109 S[first_layer_temperature]\" command wherever you want.");
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("bottom_solid_layers", coInt);
    //TRN To be shown in Print Settings "Bottom solid layers"
    def->label = L("Bottom");
    def->category = OptionCategory::perimeter;
    def->tooltip = L("Number of solid layers to generate on bottom surfaces.");
    def->full_label = L("Bottom solid layers");    def->min = 0;
    def->set_default_value(new ConfigOptionInt(3));

    def = this->add("bottom_solid_min_thickness", coFloat);
    //TRN To be shown in Print Settings "Top solid layers"
    def->label = L("Bottom");
    def->category = OptionCategory::perimeter;
    def->tooltip = L("The number of bottom solid layers is increased above bottom_solid_layers if necessary to satisfy "
    				 "minimum thickness of bottom shell.");
    def->full_label = L("Minimum bottom shell thickness");
    def->sidetext = L("mm");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(0.));

    def = this->add("bridge_acceleration", coFloatOrPercent);
    def->label = L("Bridge");
    def->full_label = L("Bridge acceleration");
    def->category = OptionCategory::speed;
    def->tooltip = L("This is the acceleration your printer will use for bridges."
                "\nCan be a % of the default acceleration"
                "\nSet zero to disable acceleration control for bridges.");
    def->sidetext = L("mm/s² or %");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloatOrPercent(0,false));

    def = this->add("bridge_angle", coFloat);
    def->label = L("Bridging");
    def->full_label = L("Bridging angle");
    def->category = OptionCategory::infill;
    def->tooltip = L("Bridging angle override. If left to zero, the bridging angle will be calculated "
                   "automatically. Otherwise the provided angle will be used for all bridges. "
                   "Use 180° for zero angle.");
    def->sidetext = L("°");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.));

    def = this->add("bridge_fan_speed", coInts);
    def->label = L("Bridges fan speed");
    def->category = OptionCategory::cooling;
    def->tooltip = L("This fan speed is enforced during all bridges and overhangs. It won't slow down the fan if it's currently running at a higher speed."
        "\nSet to 1 to disable the fan."
        "\nSet to -1 to disable this override."
        "\nCan only be overriden by disable_fan_first_layers.");
    def->sidetext = L("%");
    def->min = -1;
    def->max = 100;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInts { 100 });

    def = this->add("top_fan_speed", coInts);
    def->label = L("Top fan speed");
    def->category = OptionCategory::cooling;
    def->tooltip = L("This fan speed is enforced during all top fills."
        "\nSet to 1 to disable the fan."
        "\nSet to -1 to disable this override."
        "\nCan only be overriden by disable_fan_first_layers.");
    def->sidetext = L("%");
    def->min = -1;
    def->max = 100;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInts{ -1 });

    def = this->add("bridge_flow_ratio", coPercent);
    def->label = L("Bridge");
    def->full_label = L("Bridge flow ratio");
    def->sidetext = L("%");
    def->category = OptionCategory::width;
    def->tooltip = L("This factor affects the amount of plastic for bridging. "
                   "You can decrease it slightly to pull the extrudates and prevent sagging, "
                   "although default settings are usually good and you should experiment "
                   "with cooling (use a fan) before tweaking this.");
    def->min = 1;
    def->max = 200;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPercent(100));

    def = this->add("over_bridge_flow_ratio", coPercent);
    def->label = L("Above the bridges");
    def->full_label = L("Above bridge flow ratio");
    def->sidetext = L("%");
    def->category = OptionCategory::width;
    def->tooltip = L("Flow ratio to compensate for the gaps in a bridged top surface. Used for ironing infill"
        "pattern to prevent regions where the low-flow pass does not provide a smooth surface due to a lack of plastic."
        " You can increase it slightly to pull the top layer at the correct height. Recommended maximum: 120%.");
    def->min = 1;
    def->max = 200;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPercent(100));

    def = this->add("bridge_overlap", coPercent);
    def->label = L("Bridge overlap");
    def->full_label = L("Bridge overlap");
    def->sidetext = L("%");
    def->category = OptionCategory::width;
    def->tooltip = L("Amount of overlap between lines of the bridge. If want more space between line (or less), you can modify it. Default to 100%. A value of 50% will create two times less lines.");
    def->min = 50;
    def->max = 200;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionPercent(100));

    def = this->add("bridge_speed", coFloat);
    def->label = L("Bridges");
    def->full_label = L("Bridge speed");
    def->category = OptionCategory::speed;
    def->tooltip = L("Speed for printing bridges.");
    def->sidetext = L("mm/s");
    def->aliases = { "bridge_feed_rate" };
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(60));

    def = this->add("bridge_speed_internal", coFloatOrPercent);
    def->label = L("Internal bridges");
    def->full_label = L("Internal bridge speed");
    def->category = OptionCategory::speed;
    def->tooltip = L("Speed for printing the bridges that support the top layer.\nCan be a % of the bridge speed.");
    def->sidetext = L("mm/s");
    def->ratio_over = "bridge_speed";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(150,true));
    
    def = this->add("brim_inside_holes", coBool);
    def->label = L("Brim inside holes");
    def->full_label = L("Brim inside holes");
    def->category = OptionCategory::skirtBrim;
    def->tooltip = L("Allow to create a brim over an island when it's inside a hole (or surrounded by an object).");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("brim_width", coFloat);
    def->label = L("Brim width");
    def->category = OptionCategory::skirtBrim;
    def->tooltip = L("Horizontal width of the brim that will be printed around each object on the first layer.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("brim_width_interior", coFloat);
    def->label = L("Interior Brim width");
    def->category = OptionCategory::skirtBrim;
    def->tooltip = L("Horizontal width of the brim that will be printed inside each object on the first layer.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("brim_ears", coBool);
    def->label = ("");
    def->full_label = L("Brim ears");
    def->category = OptionCategory::skirtBrim;
    def->tooltip = L("Only draw brim over the sharp edges of the model.");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("brim_ears_max_angle", coFloat);
    def->label = L("Max angle");
    def->full_label = L("Brim ear max angle");
    def->category = OptionCategory::skirtBrim;
    def->tooltip = L("Maximum angle to let a brim ear appear. \nIf set to 0, no brim will be created. \nIf set to ~178, brim will be created on everything but strait sections.");
    def->sidetext = L("°");
    def->min = 0;
    def->max = 180;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(125));

    def = this->add("brim_ears_detection_length", coFloat);
    def->label = L("Detection radius");
    def->full_label = L("Brim ear detection length");
    def->category = OptionCategory::skirtBrim;
    def->tooltip = L("The geometry will be decimated before dectecting sharp angles. This parameter indicate the minimum length of strait sections after decimation."
                    "\n0 to deactivate");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1));

    def = this->add("brim_ears_pattern", coEnum);
    def->label = L("Pattern");
    def->full_label = L("Ear pattern");
    def->category = OptionCategory::infill;
    def->tooltip = L("Pattern for the ear. The concentric id the default one."
                    " The rectilinear has a perimeter around it, you can try it if the concentric has too many problems to stick to the build plate.");
    def->enum_keys_map = &ConfigOptionEnum<InfillPattern>::get_enum_values();
    def->enum_values.push_back("concentric");
    def->enum_values.push_back("rectilinear");
    def->enum_labels.push_back(L("Concentric"));
    def->enum_labels.push_back(L("Rectilinear"));
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionEnum<InfillPattern>(ipConcentric));

    def = this->add("brim_offset", coFloat);
    def->label = L("Brim offset");
    def->category = OptionCategory::skirtBrim;
    def->tooltip = L("Distance between the brim and the part. Should be kept at 0 unless you encounter great difficulties to separate them. It's subtracted to brim_width and brim_width_interior., so it has to be lower than them");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("chamber_temperature", coInts);
    def->label = L("Chamber");
    def->full_label = L("Chamber temperature");
    def->category = OptionCategory::cooling;
    def->tooltip = L("Chamber temperature0. Note that this setting doesn't do anything, but you can access it in Start G-code, Tool change G-code and the other ones, like for other temperature settings.");
    def->sidetext = L("°C");
    def->min = 0;
    def->max = 300;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionInts{ 0 });

    def = this->add("clip_multipart_objects", coBool);
    def->label = L("Clip multi-part objects");
    def->category = OptionCategory::slicing;
    def->tooltip = L("When printing multi-material objects, this settings will make Slic3r "
                   "to clip the overlapping object parts one by the other "
                   "(2nd part will be clipped by the 1st, 3rd part will be clipped by the 1st and 2nd etc).");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("colorprint_heights", coFloats);
    def->label = L("Colorprint height");
    def->category = OptionCategory::slicing;
    def->tooltip = L("Heights at which a filament change is to occur. ");
    def->set_default_value(new ConfigOptionFloats { });

    def = this->add("compatible_printers", coStrings);
    def->label = L("Compatible printers");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionStrings());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("compatible_printers_condition", coString);
    def->label = L("Compatible printers condition");
    def->tooltip = L("A boolean expression using the configuration values of an active printer profile. "
                   "If this expression evaluates to true, this profile is considered compatible "
                   "with the active printer profile.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("compatible_prints", coStrings);
    def->label = L("Compatible print profiles");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionStrings());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("compatible_prints_condition", coString);
    def->label = L("Compatible print profiles condition");
    def->tooltip = L("A boolean expression using the configuration values of an active print profile. "
                   "If this expression evaluates to true, this profile is considered compatible "
                   "with the active print profile.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    // The following value is to be stored into the project file (AMF, 3MF, Config ...)
    // and it contains a sum of "compatible_printers_condition" values over the print and filament profiles.
    def = this->add("compatible_printers_condition_cummulative", coStrings);
    def->set_default_value(new ConfigOptionStrings());
    def->cli = ConfigOptionDef::nocli;
    def = this->add("compatible_prints_condition_cummulative", coStrings);
    def->set_default_value(new ConfigOptionStrings());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("complete_objects", coBool);
    def->label = L("Complete individual objects");
    def->category = OptionCategory::output;
    def->tooltip = L("When printing multiple objects or copies, this feature will complete "
        "each object before moving onto next one (and starting it from its bottom layer). "
        "This feature is useful to avoid the risk of ruined prints. "
        "Slic3r should warn and prevent you from extruder collisions, but beware.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("complete_objects_one_skirt", coBool);
    def->label = L("Allow only one skirt loop");
    def->category = OptionCategory::output;
    def->tooltip = L("When using 'Complete individual objects', the default behavior is to draw the skirt around each object."
        " if you prefer to have only one skirt for the whole plater, use this option.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("complete_objects_sort", coEnum);
    def->label = L("Object sort");
    def->category = OptionCategory::output;
    def->tooltip = L("When printing multiple objects or copies on after another, this will help you to choose how it's ordered."
        "\nObject will sort them by the order of the right panel."
        "\nLowest Y will sort them by their lowest Y point. Useful for printers with a X-bar."
        "\nLowest Z will sort them by their height, useful for delta printers.");
    def->mode = comAdvanced;
    def->enum_keys_map = &ConfigOptionEnum<CompleteObjectSort>::get_enum_values();
    def->enum_values.push_back("object");
    def->enum_values.push_back("lowy");
    def->enum_values.push_back("lowz");
    def->enum_labels.push_back(L("Right panel"));
    def->enum_labels.push_back(L("lowest Y"));
    def->enum_labels.push_back(L("lowest Z"));
    def->set_default_value(new ConfigOptionEnum<CompleteObjectSort>(cosObject));

    //not used anymore, to remove !! @DEPRECATED
    def = this->add("cooling", coBools);
    def->label = L("Enable auto cooling");
    def->category = OptionCategory::cooling;
    def->tooltip = L("This flag enables the automatic cooling logic that adjusts print speed "
                   "and fan speed according to layer printing time.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBools { true });

    def = this->add("cooling_tube_retraction", coFloat);
    def->label = L("Cooling tube position");
    def->category = OptionCategory::mmsetup;
    def->tooltip = L("Distance of the center-point of the cooling tube from the extruder tip.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(91.5f));

    def = this->add("cooling_tube_length", coFloat);
    def->label = L("Cooling tube length");
    def->category = OptionCategory::mmsetup;
    def->tooltip = L("Length of the cooling tube to limit space for cooling moves inside it.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(5.f));

    def = this->add("default_acceleration", coFloatOrPercent);
    def->label = L("Default");
    def->category = OptionCategory::speed;
    def->full_label = L("Default acceleration");
    def->tooltip = L("This is the acceleration your printer will be reset to after "
                   "the role-specific acceleration values are used (perimeter/infill). "
                   "\nYou can set it as a % of the max of the X/Y machine acceleration limit."
                   "\nSet zero to prevent resetting acceleration at all.");
    def->sidetext = L("mm/s² or %");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloatOrPercent(0,false));

    def = this->add("default_filament_profile", coStrings);
    def->label = L("Default filament profile");
    def->tooltip = L("Default filament profile associated with the current printer profile. "
                   "On selection of the current printer profile, this filament profile will be activated.");
    def->set_default_value(new ConfigOptionStrings());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("default_print_profile", coString);
    def->label = L("Default print profile");
    def->tooltip = L("Default print profile associated with the current printer profile. "
                   "On selection of the current printer profile, this print profile will be activated.");
    def->set_default_value(new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("disable_fan_first_layers", coInts);
    def->label = L("Disable fan for the first");
    def->category = OptionCategory::cooling;
    def->tooltip = L("You can set this to a positive value to disable fan at all "
                   "during the first layers, so that it does not make adhesion worse.");
    def->sidetext = L("layers");
    def->min = 0;
    def->max = 1000;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionInts { 3 });

    def = this->add("dont_support_bridges", coBool);
    def->label = L("Don't support bridges");
    def->category = OptionCategory::support;
    def->tooltip = L("Experimental option for preventing support material from being generated "
                   "under bridged areas.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("duplicate_distance", coFloat);
    def->label = L("Distance between objects");
    def->category = OptionCategory::output;
    def->tooltip = L("Distance used for the auto-arrange feature of the plater.");
    def->sidetext = L("mm");
    def->aliases = { "multiply_distance" };
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(6));

    def = this->add("end_gcode", coString);
    def->label = L("End G-code");
    def->category = OptionCategory::customgcode;
    def->tooltip = L("This end procedure is inserted at the end of the output file. "
                   "Note that you can use placeholder variables for all Slic3r settings.");
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionString("M104 S0 ; turn off temperature\nG28 X0  ; home X axis\nM84     ; disable motors\n"));

    def = this->add("end_filament_gcode", coStrings);
    def->label = L("End G-code");
    def->full_label = L("Filament end G-code");
    def->category = OptionCategory::customgcode;
    def->tooltip = L("This end procedure is inserted at the end of the output file, before the printer end gcode (and "
                   "before any toolchange from this filament in case of multimaterial printers). "
                   "Note that you can use placeholder variables for all Slic3r settings. "
                   "If you have multiple extruders, the gcode is processed in extruder order.");
    def->multiline = true;
    def->full_width = true;
    def->height = 120;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionStrings { "; Filament-specific end gcode \n;END gcode for filament\n" });

    def = this->add("ensure_vertical_shell_thickness", coBool);
    def->label = L("Ensure vertical shell thickness");
    def->category = OptionCategory::perimeter;
    def->tooltip = L("Add solid infill near sloping surfaces to guarantee the vertical shell thickness "
                   "(top+bottom solid layers).");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("top_fill_pattern", coEnum);
    def->label = L("Top");
    def->full_label = L("Top Pattern");
    def->category = OptionCategory::infill;
    def->tooltip = L("Fill pattern for top infill. This only affects the top visible layer, and not its adjacent solid shells.");
    def->cli = "top-fill-pattern|external-fill-pattern=s";
    def->enum_keys_map = &ConfigOptionEnum<InfillPattern>::get_enum_values();
    def->enum_values.push_back("rectilinear");
    def->enum_values.push_back("monotonicgapfillgapfill");
    def->enum_values.push_back("monotonic");
    def->enum_values.push_back("concentric");
    def->enum_values.push_back("concentricgapfill");
    def->enum_values.push_back("hilbertcurve");
    def->enum_values.push_back("archimedeanchords");
    def->enum_values.push_back("octagramspiral");
    def->enum_values.push_back("sawtooth");
    def->enum_values.push_back("smooth");
    def->enum_values.push_back("smoothtriple");
    def->enum_values.push_back("smoothhilbert");
    def->enum_labels.push_back(L("Rectilinear"));
    def->enum_labels.push_back(L("Monotonic (filled)"));
    def->enum_labels.push_back(L("Monotonic"));
    def->enum_labels.push_back(L("Concentric"));
    def->enum_labels.push_back(L("Concentric (filled)"));
    def->enum_labels.push_back(L("Hilbert Curve"));
    def->enum_labels.push_back(L("Archimedean Chords"));
    def->enum_labels.push_back(L("Octagram Spiral"));
    def->enum_labels.push_back(L("Sawtooth"));
    def->enum_labels.push_back(L("Ironing"));
    def->set_default_value(new ConfigOptionEnum<InfillPattern>(ipMonotonic));

    def = this->add("bottom_fill_pattern", coEnum);
    def->label = L("Bottom");
    def->full_label = L("Bottom fill pattern");
    def->category = OptionCategory::infill;
    def->tooltip = L("Fill pattern for bottom infill. This only affects the bottom visible layer, and not its adjacent solid shells.");
    def->cli = "bottom-fill-pattern|external-fill-pattern=s";
    def->enum_keys_map = &ConfigOptionEnum<InfillPattern>::get_enum_values();
    def->enum_values.push_back("rectilinear");
    def->enum_values.push_back("monotonicgapfill");
    def->enum_values.push_back("monotonic");
    def->enum_values.push_back("concentric");
    def->enum_values.push_back("concentricgapfill");
    def->enum_values.push_back("hilbertcurve");
    def->enum_values.push_back("archimedeanchords");
    def->enum_values.push_back("octagramspiral");
    def->enum_values.push_back("smooth");
    def->enum_labels.push_back(L("Rectilinear"));
    def->enum_labels.push_back(L("Monotonic (filled)"));
    def->enum_labels.push_back(L("Monotonic"));
    def->enum_labels.push_back(L("Concentric"));
    def->enum_labels.push_back(L("Concentric (filled)"));
    def->enum_labels.push_back(L("Hilbert Curve"));
    def->enum_labels.push_back(L("Archimedean Chords"));
    def->enum_labels.push_back(L("Octagram Spiral"));
    def->enum_labels.push_back(L("Ironing"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<InfillPattern>(ipMonotonic));

    def = this->add("solid_fill_pattern", coEnum);
    def->label = L("Solid pattern");
    def->category = OptionCategory::infill;
    def->tooltip = L("Fill pattern for solid (internal) infill. This only affects the solid not-visible layers. You should use rectilinear is most cases. You can try ironing for transluscnet material."
        " Rectilinear (filled) replace zig-zag patterns by a single big line & is more efficient for filling little spaces.");
    def->enum_keys_map = &ConfigOptionEnum<InfillPattern>::get_enum_values();
    def->enum_values.push_back("smooth");
    def->enum_values.push_back("rectilinear");
    def->enum_values.push_back("rectilineargapfill");
    def->enum_values.push_back("monotonic");
    def->enum_values.push_back("concentric");
    def->enum_values.push_back("concentricgapfill");
    def->enum_values.push_back("hilbertcurve");
    def->enum_values.push_back("archimedeanchords");
    def->enum_values.push_back("octagramspiral");
    def->enum_labels.push_back(L("Ironing"));
    def->enum_labels.push_back(L("Rectilinear"));
    def->enum_labels.push_back(L("Rectilinear (filled)"));
    def->enum_labels.push_back(L("Monotonic"));
    def->enum_labels.push_back(L("Concentric"));
    def->enum_labels.push_back(L("Concentric (filled)"));
    def->enum_labels.push_back(L("Hilbert Curve"));
    def->enum_labels.push_back(L("Archimedean Chords"));
    def->enum_labels.push_back(L("Octagram Spiral"));
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionEnum<InfillPattern>(ipRectilinearWGapFill));

    def = this->add("enforce_full_fill_volume", coBool);
    def->label = L("Enforce 100% fill volume");
    def->category = OptionCategory::infill;
    def->tooltip = L("Experimental option which modifies (in solid infill) fill flow to have the exact amount of plastic inside the volume to fill "
        "(it generally changes the flow from -7% to +4%, depending on the size of the surface to fill and the overlap parameters, "
        "but it can go as high as +50% for infill in very small areas where rectilinear doesn't have good coverage). It has the advantage "
        "to remove the over-extrusion seen in thin infill areas, from the overlap ratio");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("external_infill_margin", coFloatOrPercent);
    def->label = L("Default");
    def->full_label = L("Default infill margin");
    def->category = OptionCategory::infill;
    def->tooltip = L("This parameter grows the top/bottom/solid layers by the specified MM to anchor them into the part. Put 0 to deactivate it. Can be a % of the width of the perimeters.");
    def->sidetext = L("mm/%");
    def->ratio_over = "perimeter_extrusion_width";
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloatOrPercent(150, true));

    def = this->add("bridged_infill_margin", coFloatOrPercent);
    def->label = L("Bridged");
    def->full_label = L("Bridge margin");
    def->category = OptionCategory::infill;
    def->tooltip = L("This parameter grows the bridged solid infill layers by the specified MM to anchor them into the part. Put 0 to deactivate it. Can be a % of the width of the external perimeter.");
    def->sidetext = L("mm/%");
    def->ratio_over = "external_perimeter_extrusion_width";
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloatOrPercent(200, true));

    def = this->add("external_perimeter_extrusion_width", coFloatOrPercent);
    def->label = L("External perimeters");
    def->full_label = L("External perimeters width");
    def->category = OptionCategory::width;
    def->tooltip = L("Set this to a non-zero value to set a manual extrusion width for external perimeters. "
        "If left zero, default extrusion width will be used if set, otherwise 1.05 x nozzle diameter will be used. "
        "If expressed as percentage (for example 112.5%), it will be computed over nozzle diameter.");
    def->sidetext = L("mm or %");
    def->ratio_over = "nozzle_diameter";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(0, false));

    def = this->add("external_perimeter_cut_corners", coPercent);
    def->label = L("Cutting corners");
    def->full_label = L("Ext. peri. cut corners");
    def->category = OptionCategory::width;
    def->tooltip = L("Activate this option to modify the flow to acknoledge that the nozzle is round and the corners will have a round shape, and so change the flow to realized that and avoid over-extrusion."
        " 100% is activated, 0% is deactivated and 50% is half-activated."
        "\nNote: At 100% this change the flow by ~5% over a very small distance (~nozzle diameter), so it shouldn't be noticeable unless you have a very big nozzle and a very precise printer."
        "\nIt's very experimental, please report about the usefulness. It may be removed if there is no use of it.");
    def->sidetext = L("%");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionPercent(0));

    def = this->add("external_perimeter_fan_speed", coInts);
    def->label = L("External perimeter fan speed");
    def->tooltip = L("When set to a non-zero value this fan speed is used only for external perimeters (visible ones). "
                    "\nSet to 1 to disable the fan."
                    "\nSet to -1 to use the normal fan speed on external perimeters."
                    "External perimeters can benefit from higher fan speed to improve surface finish, "
                    "while internal perimeters, infill, etc. benefit from lower fan speed to improve layer adhesion.");
    def->sidetext = L("%");
    def->min = -1;
    def->max = 100;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInts { -1 });

    def = this->add("external_perimeter_overlap", coPercent);
    def->label = L("external perimeter overlap");
    def->full_label = L("Ext. peri. overlap");
    def->category = OptionCategory::width;
    def->tooltip = L("This setting allow you to reduce the overlap between the perimeters and the external one, to reduce the impact of the perimeters artifacts."
        " 100% means that no gaps is left, and 0% means that the external perimeter isn't contributing to the overlap with the 'inner' one."
        "\nIt's very experimental, please report about the usefulness. It may be removed if there is no use of it.");
    def->sidetext = L("%");
    def->min = 0;
    def->max = 100;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionPercent(100));

    def = this->add("perimeter_overlap", coPercent);
    def->label = L("perimeter overlap");
    def->full_label = L("Perimeter overlap");
    def->category = OptionCategory::width;
    def->tooltip = L("This setting allow you to reduce the overlap between the perimeters, to reduce the impact of the perimeters artifacts."
        " 100% means that no gaps is left, and 0% means that perimeters are not touching each other anymore."
        "\nIt's very experimental, please report about the usefulness. It may be removed if there is no use for it.");
    def->sidetext = L("%");
    def->min = 0;
    def->max = 100;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionPercent(100));

    def = this->add("perimeter_bonding", coPercent);
    def->label = L("Better bonding");
    def->full_label = L("Perimeter bonding");
    def->category = OptionCategory::perimeter;
    def->tooltip = L("This setting may degrad a bit the quality of your external perimeter, in exchange for a better bonding between perimeters."
        "Use it if you have great difficulties with perimeter bonding, for example with high temperature filaments."
        "\nThis percentage is the % of overlap between perimeters, a bit like perimeter_overlap and external_perimeter_overlap, but in reverse."
        " You have to set perimeter_overlap and external_perimeter_overlap to 100%, or this setting has no effect."
        " 0: no effect, 50%: half of the nozzle will be over an already extruded perimeter while extruding a new one"
        ", unless it's an external ones)."
        "\nIt's very experimental, please report about the usefulness. It may be removed if there is no use for it.");
    def->sidetext = L("%");
    def->min = 0;
    def->max = 50;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionPercent(0));

    def = this->add("external_perimeter_speed", coFloatOrPercent);
    def->label = L("External");
    def->full_label = L("External perimeters speed");
    def->category = OptionCategory::speed;
    def->tooltip = L("This separate setting will affect the speed of external perimeters (the visible ones). "
                   "If expressed as percentage (for example: 80%) it will be calculated "
                   "on the perimeters speed setting above. Set to zero for auto.");
    def->sidetext = L("mm/s or %");
    def->ratio_over = "perimeter_speed";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(50, true));

    def = this->add("external_perimeters_first", coBool);
    def->label = L("first");
    def->full_label = L("External perimeters first");
    def->category = OptionCategory::perimeter;
    def->tooltip = L("Print contour perimeters from the outermost one to the innermost one "
        "instead of the default inverse order.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("external_perimeters_vase", coBool);
    def->label = L("In vase mode (no seam)");
    def->full_label = L("ExternalPerimeter in vase mode");
    def->category = OptionCategory::perimeter;
    def->tooltip = L("Print contour perimeters in two circle, in a contiunous way, like for a vase mode. It needs the external_perimeters_first parameter do work."
        " \nDoesn't work for the first layer, as it may damage the bed overwise."
        " \nNote that It will use min_layer_height from your hardware setting as the base height (it doesn't start at 0)"
        ", so be sure to put here the lowest value your printer can handle."
        " if it's not lower than two times the current layer height, it falls back to the normal algorithm, as there are not enough room to do two loops.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("external_perimeters_nothole", coBool);
    def->label = L("Only for outer side");
    def->full_label = L("Ext peri first for outer side");
    def->category = OptionCategory::perimeter;
    def->tooltip = L("Only do the vase trick on the external side. Useful when the thikness is too low.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("external_perimeters_hole", coBool);
    def->label = L("Only for inner side");
    def->full_label = L("ext peri first for inner side");
    def->category = OptionCategory::perimeter;
    def->tooltip = L("Only do the vase trick on the external side. Useful when you only want to remode seam from screw hole.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(true));

    ConfigOptionBool                external_perimeters_nothole;
    ConfigOptionBool                external_perimeters_hole;

    def = this->add("perimeter_loop", coBool);
    def->label = L("");
    def->full_label = L("Perimeters loop");
    def->category = OptionCategory::perimeter;
    def->tooltip = L("Join the perimeters to create only one continuous extrusion without any z-hop."
        " Long inside travel (from external to holes) are not extruded to give some space to the infill.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));
    
    def = this->add("perimeter_loop_seam", coEnum);
    def->label = L("Seam position");
    def->full_label = L("Perimeter loop seam");
    def->category = OptionCategory::perimeter;
    def->tooltip = L("Position of perimeters starting points.");
    def->enum_keys_map = &ConfigOptionEnum<SeamPosition>::get_enum_values();
    def->enum_values.push_back("nearest");
    def->enum_values.push_back("rear");
    def->enum_labels.push_back(L("Nearest"));
    def->enum_labels.push_back(L("Rear"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<SeamPosition>(spRear));

    def = this->add("extra_perimeters", coBool);
    def->label = L("filling horizontal gaps on slopes");
    def->full_label = L("Extra perimeters (do nothing)");
    def->category = OptionCategory::perimeter;
    def->tooltip = L("Add more perimeters when needed for avoiding gaps in sloping walls. "
        "Slic3r keeps adding perimeters, until more than 70% of the loop immediately above "
        "is supported."
        "\nIf you succeed to trigger the algorihtm behind this setting, please send me a message."
        " Personally, i think it's useless.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("extra_perimeters_overhangs", coBool);
    def->label = L("On overhangs");
    def->full_label = L("Extra perimeters over overhangs");
    def->category = OptionCategory::perimeter;
    def->tooltip = L("Add more perimeters when needed for avoiding gaps in sloping walls. "
        "Slic3r keeps adding perimeter until all overhangs are filled."
        "\n!! this is a very slow algorithm !!"
        "\nIf you use this setting, consider strongly using also overhangs_reverse.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("extra_perimeters_odd_layers", coBool);
    def->label = L("On odd layers");
    def->full_label = L("Extra perimeter on odd layers");
    def->category = OptionCategory::perimeter;
    def->tooltip = L("Add one perimeter every odd layer. With this, infill is taken into sandwitch"
        " and you may be able to reduce drastically the infill/perimeter overlap setting. ");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("only_one_perimeter_top", coBool);
    def->label = L("Only one perimeter on Top surfaces");
    def->category = OptionCategory::perimeter;
    def->tooltip = L("Use only one perimeter on flat top surface, to let more space to the top infill pattern.");
    def->set_default_value(new ConfigOptionBool(true));


    def = this->add("extruder", coInt);
    def->gui_type = "i_enum_open";
    def->label = L("Extruder");
    def->category = OptionCategory::extruders;
    def->tooltip = L("The extruder to use (unless more specific extruder settings are specified). "
        "This value overrides perimeter and infill extruders, but not the support extruders.");
    def->min = 0;  // 0 = inherit defaults
    def->enum_labels.push_back(L("default"));  // override label for item 0
    def->enum_labels.push_back("1");
    def->enum_labels.push_back("2");
    def->enum_labels.push_back("3");
    def->enum_labels.push_back("4");
    def->enum_labels.push_back("5");
    def->enum_labels.push_back("6");
    def->enum_labels.push_back("7");
    def->enum_labels.push_back("8");
    def->enum_labels.push_back("9");

    def = this->add("extruder_clearance_height", coFloat);
    def->label = L("Height");
    def->full_label = L("Extruder clearance height");
    def->category = OptionCategory::output;
    def->tooltip = L("Set this to the vertical distance between your nozzle tip and (usually) the X carriage rods. "
                   "In other words, this is the height of the clearance cylinder around your extruder, "
                   "and it represents the maximum depth the extruder can peek before colliding with "
                   "other printed objects.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(20));

    def = this->add("extruder_clearance_radius", coFloat);
    def->label = L("Radius");
    def->category = OptionCategory::output;
    def->full_label = L("Extruder clearance radius");
    def->tooltip = L("Set this to the clearance radius around your extruder. "
                   "If the extruder is not centered, choose the largest value for safety. "
                   "This setting is used to check for collisions and to display the graphical preview "
                   "in the plater.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(20));

    def = this->add("extruder_colour", coStrings);
    def->label = L("Extruder Color");
    def->category = OptionCategory::extruders;
    def->tooltip = L("This is only used in the Slic3r interface as a visual help.");
    def->gui_type = "color";
    // Empty string means no color assigned yet.
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionStrings{ "" });

    def = this->add("extruder_offset", coPoints);
    def->label = L("Extruder offset");
    def->category = OptionCategory::extruders;
    def->tooltip = L("If your firmware doesn't handle the extruder displacement you need the G-code "
        "to take it into account. This option lets you specify the displacement of each extruder "
        "with respect to the first one. It expects positive coordinates (they will be subtracted "
        "from the XY coordinate).");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPoints{ Vec2d(0,0) });

    def = this->add("extruder_temperature_offset", coFloats);
    def->label = L("Extruder temp offset");
    def->category = OptionCategory::extruders;
    def->tooltip = L("This offset will be added to all extruder temperature set by the filament settings."
        "\nNote that you should set 'M104 S{first_layer_temperature[initial_extruder] + extruder_temperature_offset[initial_extruder]}'"
        "\ninstead of 'M104 S[first_layer_temperature]' in the start_gcode");
    def->sidetext = L("°C");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloats{ 0 });

    def = this->add("extruder_fan_offset", coPercents);
    def->label = L("Extruder fan offset");
    def->category = OptionCategory::extruders;
    def->tooltip = L("This offset wil be add to all fan value set by the filament properties. It won't make them go higher than 100% and lower than 0%.");
    def->sidetext = L("%");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionPercents{ 0 });


    def = this->add("extrusion_axis", coString);
    def->label = L("Extrusion axis");
    def->category = OptionCategory::extruders;
    def->tooltip = L("Use this option to set the axis letter associated to your printer's extruder "
                   "(usually E but some printers use A).");
    def->set_default_value(new ConfigOptionString("E"));

    def = this->add("extrusion_multiplier", coFloats);
    def->label = L("Extrusion multiplier");
    def->category = OptionCategory::filament;
    def->tooltip = L("This factor changes the amount of flow proportionally. You may need to tweak "
        "this setting to get nice surface finish and correct single wall widths. "
        "Usual values are between 0.9 and 1.1. If you think you need to change this more, "
        "check filament diameter and your firmware E steps.");
    def->mode = comSimple;
    def->max = 2;
    def->set_default_value(new ConfigOptionFloats { 1. });

    def = this->add("print_extrusion_multiplier", coPercent);
    def->label = L("Extrusion multiplier");
    def->category = OptionCategory::filament;
    def->tooltip = L("This factor changes the amount of flow proportionally. You may need to tweak "
        "this setting to get nice surface finish and correct single wall widths. "
        "Usual values are between 90% and 110%. If you think you need to change this more, "
        "check filament diameter and your firmware E steps."
        " This print setting is multiplied against the extrusion_multiplier from the filament tab."
        " Its only purpose is to offer the same functionality but with a per-object basis.");
    def->sidetext = L("%");
    def->mode = comSimple;
    def->min = 2;
    def->set_default_value(new ConfigOptionPercent(100));

    def = this->add("extrusion_width", coFloatOrPercent);
    def->label = L("Default extrusion width");
    def->category = OptionCategory::width;
    def->tooltip = L("Set this to a non-zero value to allow a manual extrusion width. "
                   "If left to zero, Slic3r derives extrusion widths from the nozzle diameter "
                   "(see the tooltips for perimeter extrusion width, infill extrusion width etc). "
                   "If expressed as percentage (for example: 105%), it will be computed over nozzle diameter.");
    def->sidetext = L("mm or %");
    def->ratio_over = "nozzle_diameter";
    def->min = 0;
    def->max = 1000;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(0, false));

    def = this->add("fan_always_on", coBools);
    def->label = L("Keep fan always on");
    def->category = OptionCategory::cooling;
    def->tooltip = L("If this is enabled, fan will continuously run at base speed if no setting override the speed."
                " Useful for PLA, harmful for ABS.");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBools{ false });

    def = this->add("fan_below_layer_time", coInts);
    def->label = L("Enable fan if layer print time is below");
    def->category = OptionCategory::cooling;
    def->tooltip = L("If layer print time is estimated below this number of seconds, fan will be enabled "
                "and its speed will be calculated by interpolating the default and maximum speeds."
                "\nSet to 0 to disable.");
    def->sidetext = L("approximate seconds");
    def->min = 0;
    def->max = 1000;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionInts { 60 });

    def = this->add("filament_colour", coStrings);
    def->label = L("Color");
    def->full_label = L("Filament color");
    def->category = OptionCategory::filament;
    def->tooltip = L("This is only used in the Slic3r interface as a visual help.");
    def->gui_type = "color";
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionStrings{ "#29B2B2" });

    def = this->add("filament_notes", coStrings);
    def->label = L("Filament notes");
    def->category = OptionCategory::notes;
    def->tooltip = L("You can put your notes regarding the filament here.");
    def->multiline = true;
    def->full_width = true;
    def->height = 13;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionStrings { "" });

    def = this->add("filament_max_speed", coFloats);
    def->label = L("Max speed");
    def->category = OptionCategory::filament;
    def->tooltip = L("Maximum speed allowed for this filament. Limits the maximum "
        "speed of a print to the minimum of the print speed and the filament speed. "
        "Set to zero for no limit.");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats{ 0. });

    def = this->add("filament_max_volumetric_speed", coFloats);
    def->label = L("Max volumetric speed");
    def->category = OptionCategory::filament;
    def->tooltip = L("Maximum volumetric speed allowed for this filament. Limits the maximum volumetric "
        "speed of a print to the minimum of print and filament volumetric speed. "
        "Set to zero for no limit.");
    def->sidetext = L("mm³/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats{ 0. });

    def = this->add("filament_max_wipe_tower_speed", coFloats);
    def->label = L("Max speed on the wipe tower");
    def->tooltip = L("This setting is used to set the maximum speed when extruding inside the wipe tower (use M220)."
        " In %, set 0 to disable and use the Filament type instead."
        "\nIf disabled, these filament types will have a defaut value of:"
        "\n - PVA: 80% to 60%"
        "\n - SCAFF: 35%"
        "\n - FLEX: 35%"
        "\n - OTHERS: 100%"
        "\nNote that the wipe tower reset the speed at 100% for the unretract in any case."
        "\nIf using marlin, M220 B/R is used to save the speed override before the wipe tower print.");
    def->sidetext = L("%");
    def->min = 0;
    def->max = 200;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloats { 0 });

    def = this->add("filament_loading_speed", coFloats);
    def->label = L("Loading speed");
    def->tooltip = L("Speed used for loading the filament on the wipe tower. ");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloats { 28. });

    //skinnydip section starts
    def = this->add("filament_enable_toolchange_temp", coBools);
    def->label = L("Toolchange temperature enabled");
    def->tooltip = L("Determines whether toolchange temperatures will be applied");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBools { false });

    def = this->add("filament_use_fast_skinnydip", coBools);
    def->label = L("Fast mode");
    def->tooltip = L("Experimental: drops nozzle temperature during cooling moves instead of prior to extraction to reduce wait time.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBools { false });

    def = this->add("filament_enable_toolchange_part_fan", coBools);
    def->label = L("Use part fan to cool hotend");
    def->tooltip = L("Experimental setting.  May enable the hotend to cool down faster during toolchanges");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBools { false });

    def = this->add("filament_toolchange_part_fan_speed", coInts);
    def->label = L("Toolchange part fan speed");
    def->tooltip = L("Experimental setting.  Fan speeds that are too high can clash with the hotend's PID routine.");
    def->sidetext = L("%");
    def->min = 0;
    def->max = 100;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionInts { 50 });

    def = this->add("filament_use_skinnydip", coBools);
    def->label = L("Enable Skinnydip string reduction");
    def->tooltip = L("Skinnydip performs a secondary dip into the meltzone to burn off fine strings of filament");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBools { false });

    def = this->add("filament_melt_zone_pause", coInts);
    def->label = L("Pause in melt zone");
    def->tooltip = L("Stay in melt zone for this amount of time before extracting the filament.  Not usually necessary.");
    def->sidetext = L("milliseconds");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionInts { 0 });

    def = this->add("filament_cooling_zone_pause", coInts);
    def->label = L("Pause before extraction ");
    def->tooltip = L("Can be useful to avoid bondtech gears deforming hot tips, but not ordinarily needed");
    def->sidetext = L("milliseconds");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionInts { 0 });

    def = this->add("filament_dip_insertion_speed", coFloats);
    def->label = L("Speed to move into melt zone");
    def->tooltip = L("usually not necessary to change this");
    def->sidetext = L("mm/sec");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloats { 33. });

    def = this->add("filament_dip_extraction_speed", coFloats);
    def->label = L("Speed to extract from melt zone");
    def->tooltip = L("usually not necessary to change this");
    def->sidetext = L("mm/sec");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloats { 70. });

    def = this->add("filament_toolchange_temp", coInts);
    def->label = L("Toolchange temperature");
    def->tooltip = L("To further reduce stringing, it can be helpful to set a lower temperature just prior to extracting filament from the hotend.");
    def->sidetext = L("°C");
    def->min = 175;
    def->max = 285;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInts { 200 });

    def = this->add("filament_skinnydip_distance", coFloats);
    def->label = L("Insertion distance");
    def->tooltip = L("For stock extruders, usually 40-42mm.  For bondtech extruder upgrade, usually 30-32mm.  Start with a low value and gradually increase it until strings are gone.  If there are blobs on your wipe tower, your value is too high.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 31. });
    //skinnydip section ends

    def = this->add("filament_loading_speed_start", coFloats);
    def->label = L("Loading speed at the start");
    def->tooltip = L("Speed used at the very beginning of loading phase. ");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloats { 3. });

    def = this->add("filament_unloading_speed", coFloats);
    def->label = L("Unloading speed");
    def->tooltip = L("Speed used for unloading the filament on the wipe tower (does not affect "
                      " initial part of unloading just after ramming). ");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloats { 90. });

    def = this->add("filament_unloading_speed_start", coFloats);
    def->label = L("Unloading speed at the start");
    def->tooltip = L("Speed used for unloading the tip of the filament immediately after ramming. ");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloats { 100. });

    def = this->add("filament_toolchange_delay", coFloats);
    def->label = L("Delay after unloading");
    def->tooltip = L("Time to wait after the filament is unloaded. "
                   "May help to get reliable toolchanges with flexible materials "
                   "that may need more time to shrink to original dimensions. ");
    def->sidetext = L("s");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloats { 0. });

    def = this->add("filament_cooling_moves", coInts);
    def->label = L("Number of cooling moves");
    def->tooltip = L("Filament is cooled by being moved back and forth in the "
                   "cooling tubes. Specify desired number of these moves.");
    def->max = 0;
    def->max = 20;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionInts { 4 });

    def = this->add("filament_cooling_initial_speed", coFloats);
    def->label = L("Speed of the first cooling move");
    def->tooltip = L("Cooling moves are gradually accelerating beginning at this speed. ");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloats { 2.2f });

    def = this->add("filament_minimal_purge_on_wipe_tower", coFloats);
    def->label = L("Minimal purge on wipe tower");
    def->tooltip = L("After a tool change, the exact position of the newly loaded filament inside "
                     "the nozzle may not be known, and the filament pressure is likely not yet stable. "
                     "Before purging the print head into an infill or a sacrificial object, Slic3r will always prime "
                     "this amount of material into the wipe tower to produce successive infill or sacrificial object extrusions reliably.");
    def->sidetext = L("mm³");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloats { 15.f });

    def = this->add("filament_cooling_final_speed", coFloats);
    def->label = L("Speed of the last cooling move");
    def->tooltip = L("Cooling moves are gradually accelerating towards this speed. ");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloats { 3.4f });

    def = this->add("filament_load_time", coFloats);
    def->label = L("Filament load time");
    def->tooltip = L("Time for the printer firmware (or the Multi Material Unit 2.0) to load a new filament during a tool change (when executing the T code). This time is added to the total print time by the G-code time estimator.");
    def->sidetext = L("s");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloats { 0.0f });

    def = this->add("filament_ramming_parameters", coStrings);
    def->label = L("Ramming parameters");
    def->tooltip = L("This string is edited by RammingDialog and contains ramming specific parameters.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionStrings { "120 100 6.6 6.8 7.2 7.6 7.9 8.2 8.7 9.4 9.9 10.0|"
       " 0.05 6.6 0.45 6.8 0.95 7.8 1.45 8.3 1.95 9.7 2.45 10 2.95 7.6 3.45 7.6 3.95 7.6 4.45 7.6 4.95 7.6" });

    def = this->add("filament_unload_time", coFloats);
    def->label = L("Filament unload time");
    def->tooltip = L("Time for the printer firmware (or the Multi Material Unit 2.0) to unload a filament during a tool change (when executing the T code). This time is added to the total print time by the G-code time estimator.");
    def->sidetext = L("s");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloats { 0.0f });

    def = this->add("filament_diameter", coFloats);
    def->label = L("Diameter");
    def->tooltip = L("Enter your filament diameter here. Good precision is required, so use a caliper "
                   "and do multiple measurements along the filament, then compute the average.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats{ 1.75 });

    def = this->add("filament_shrink", coPercents);
    def->label = L("Shrinkage");
    def->tooltip = L("Enter the shrinkage percentage that the filament will get after cooling (94% if you measure 94mm instead of 100mm)."
                    " The part will be scaled in xy to conpensate."
                    " Only the filament used for the perimeter is taken into account."
                    "\nBe sure to let enough space between objects, as this compensation is done after the checks.");
    def->sidetext = L("%");
    def->min = 10;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionPercents{ 100 });

    def = this->add("filament_density", coFloats);
    def->label = L("Density");
    def->category = OptionCategory::filament;
    def->tooltip = L("Enter your filament density here. This is only for statistical information. "
                   "A decent way is to weigh a known length of filament and compute the ratio "
                   "of the length to volume. Better is to calculate the volume directly through displacement.");
    def->sidetext = L("g/cm³");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats{ 0. });

    def = this->add("filament_type", coStrings);
    def->label = L("Filament type");
    def->category = OptionCategory::filament;
    def->tooltip = L("The filament material type for use in custom G-codes.");
    def->gui_type = "f_enum_open";
    def->gui_flags = "show_value";
    def->enum_values.push_back("PLA");
    def->enum_values.push_back("PET");
    def->enum_values.push_back("ABS");
    def->enum_values.push_back("ASA");
    def->enum_values.push_back("FLEX");
    def->enum_values.push_back("HIPS");
    def->enum_values.push_back("EDGE");
    def->enum_values.push_back("NGEN");
    def->enum_values.push_back("NYLON");
    def->enum_values.push_back("PVA");
    def->enum_values.push_back("PC");
    def->enum_values.push_back("PP");
    def->enum_values.push_back("PEI");
    def->enum_values.push_back("PEEK");
    def->enum_values.push_back("PEKK");
    def->enum_values.push_back("POM");
    def->enum_values.push_back("PSU");
    def->enum_values.push_back("PVDF");
    def->enum_values.push_back("SCAFF");
    def->enum_values.push_back("other0");
    def->enum_values.push_back("other1");
    def->enum_values.push_back("other2");
    def->enum_values.push_back("other3");
    def->enum_values.push_back("other4");
    def->enum_values.push_back("other5");
    def->enum_values.push_back("other6");
    def->enum_values.push_back("other7");
    def->enum_values.push_back("other8");
    def->enum_values.push_back("other9");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionStrings { "PLA" });

    def = this->add("filament_soluble", coBools);
    def->label = L("Soluble material");
    def->category = OptionCategory::filament;
    def->tooltip = L("Soluble material is most likely used for a soluble support.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBools { false });

    def = this->add("filament_cost", coFloats);
    def->label = L("Cost");
    def->full_label = L("Filament cost");
    def->category = OptionCategory::filament;
    def->tooltip = L("Enter your filament cost per kg here. This is only for statistical information.");
    def->sidetext = L("money/kg");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloats { 0. });

    def = this->add("filament_spool_weight", coFloats);
    def->label = L("Spool weight");
    def->category = OptionCategory::filament;
    def->tooltip = L("Enter weight of the empty filament spool. "
                     "One may weigh a partially consumed filament spool before printing and one may compare the measured weight "
                     "with the calculated weight of the filament with the spool to find out whether the amount "
                     "of filament on the spool is sufficient to finish the print.");
    def->sidetext = L("g");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloats { 0. });

    def = this->add("filament_settings_id", coStrings);
    def->set_default_value(new ConfigOptionStrings { "" });
    def->cli = ConfigOptionDef::nocli;

    def = this->add("filament_vendor", coString);
    def->set_default_value(new ConfigOptionString(L("(Unknown)")));
    def->cli = ConfigOptionDef::nocli;

    def = this->add("fill_angle", coFloat);
    def->label = L("Fill");
    def->full_label = L("Fill angle");
    def->category = OptionCategory::infill;
    def->tooltip = L("Default base angle for infill orientation. Cross-hatching will be applied to this. "
                   "Bridges will be infilled using the best direction Slic3r can detect, so this setting "
                   "does not affect them.");
    def->sidetext = L("°");
    def->min = 0;
    def->max = 360;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(45));

    def = this->add("fill_angle_increment", coFloat);
    def->label = L("Fill");
    def->full_label = L("Fill angle increment");
    def->category = OptionCategory::infill;
    def->tooltip = L("Add this angle each layer to the base angle for infill. "
                    "May be useful for art, or to be sure to hit every object's feature even with very low infill. "
                    "Still experiemental, tell me what makes it useful, or the problems that arise using it.");
    def->sidetext = L("°");
    def->min = 0;
    def->max = 360;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("fill_density", coPercent);
    def->gui_type = "f_enum_open";
    def->gui_flags = "show_value";
    def->label = L("Fill density");
    def->category = OptionCategory::infill;
    def->tooltip = L("Density of internal infill, expressed in the range 0% - 100%.");
    def->sidetext = L("%");
    def->min = 0;
    def->max = 100;
    def->enum_values.push_back("0");
    def->enum_values.push_back("4");
    def->enum_values.push_back("5.5");
    def->enum_values.push_back("7.5");
    def->enum_values.push_back("10");
    def->enum_values.push_back("13");
    def->enum_values.push_back("18");
    def->enum_values.push_back("23");
    def->enum_values.push_back("31");
    def->enum_values.push_back("42");
    def->enum_values.push_back("55");
    def->enum_values.push_back("75");
    def->enum_values.push_back("100");
    def->enum_labels.push_back("0");
    def->enum_labels.push_back("4");
    def->enum_labels.push_back("5.5");
    def->enum_labels.push_back("7.5");
    def->enum_labels.push_back("10");
    def->enum_labels.push_back("13");
    def->enum_labels.push_back("18");
    def->enum_labels.push_back("23");
    def->enum_labels.push_back("31");
    def->enum_labels.push_back("42");
    def->enum_labels.push_back("55");
    def->enum_labels.push_back("75");
    def->enum_labels.push_back("100");
    def->set_default_value(new ConfigOptionPercent(18));

    def = this->add("fill_pattern", coEnum);
    def->label = L("Pattern");
    def->full_label = L("Fill pattern");
    def->category = OptionCategory::infill;
    def->tooltip = L("Fill pattern for general low-density infill.");
    def->enum_keys_map = &ConfigOptionEnum<InfillPattern>::get_enum_values();
    def->enum_values.push_back("rectilinear");
    def->enum_values.push_back("monotonic");
    def->enum_values.push_back("grid");
    def->enum_values.push_back("triangles");
    def->enum_values.push_back("stars");
    def->enum_values.push_back("cubic");
    def->enum_values.push_back("line");
    def->enum_values.push_back("concentric");
    def->enum_values.push_back("honeycomb");
    def->enum_values.push_back("3dhoneycomb");
    def->enum_values.push_back("gyroid");
    def->enum_values.push_back("hilbertcurve");
    def->enum_values.push_back("archimedeanchords");
    def->enum_values.push_back("octagramspiral");
    def->enum_values.push_back("scatteredrectilinear"); 
    def->enum_values.push_back("adaptivecubic");
    def->enum_values.push_back("supportcubic");
    def->enum_labels.push_back(L("Rectilinear"));
    def->enum_labels.push_back(L("Monotonic"));
    def->enum_labels.push_back(L("Grid"));
    def->enum_labels.push_back(L("Triangles"));
    def->enum_labels.push_back(L("Stars"));
    def->enum_labels.push_back(L("Cubic"));
    def->enum_labels.push_back(L("Line"));
    def->enum_labels.push_back(L("Concentric"));
    def->enum_labels.push_back(L("Honeycomb"));
    def->enum_labels.push_back(L("3D Honeycomb"));
    def->enum_labels.push_back(L("Gyroid"));
    def->enum_labels.push_back(L("Hilbert Curve"));
    def->enum_labels.push_back(L("Archimedean Chords"));
    def->enum_labels.push_back(L("Octagram Spiral"));
    def->enum_labels.push_back(L("Scattered Rectilinear"));
    def->enum_labels.push_back(L("Adaptive Cubic"));
    def->enum_labels.push_back(L("Support Cubic"));
    def->set_default_value( new ConfigOptionEnum<InfillPattern>(ipStars));

    def = this->add("fill_top_flow_ratio", coPercent);
    def->label = L("Top fill");
    def->full_label = L("Top fill flow ratio");
    def->sidetext = L("%");
    def->category = OptionCategory::width;
    def->tooltip = L("You can increase this to over-extrude on the top layer if there are not enough plastic to make a good fill.");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionPercent(100));

    def = this->add("first_layer_flow_ratio", coPercent);
    def->label = L("First layer");
    def->full_label = L("First layer flow ratio");
    def->sidetext = L("%");
    def->category = OptionCategory::width;
    def->tooltip = L("You can increase this to over-extrude on the first layer if there are not enough plastic because your bed isn't levelled."
                    "\nNote: DON'T USE THIS if your only problem is bed levelling, LEVEL YOUR BED!"
                    " Use this setting only as last resort after all calibrations failed.");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionPercent(100));

    def = this->add("first_layer_size_compensation", coFloat);
    def->label = L("First layer");
    def->full_label = L("XY First layer compensation");
    def->category = OptionCategory::slicing;
    def->tooltip = L("The first layer will be grown / shrunk in the XY plane by the configured value "
        "to compensate for the 1st layer squish aka an Elephant Foot effect. (should be negative = inwards)");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("fill_smooth_width", coFloatOrPercent);
    def->label = L("Width");
    def->full_label = L("Ironing width");
    def->category = OptionCategory::infill;
    def->tooltip = L("This is the width of the ironing pass, in a % of the top infill extrusion width, should not be more than 50%"
        " (two times more lines, 50% overlap). It's not necessary to go below 25% (four times more lines, 75% overlap). \nIf you have problems with your ironing process,"
        " don't forget to look at the flow->above bridge flow, as this setting should be set to min 110% to let you have enough plastic in the top layer."
        " A value too low will make your extruder eat the filament.");
    def->ratio_over = "top_infill_extrusion_width";
    def->min = 0;
    def->mode = comExpert;
    def->sidetext = L("mm/%");
    def->set_default_value(new ConfigOptionFloatOrPercent(50, true));

    def = this->add("fill_smooth_distribution", coPercent);
    def->label = L("Distribution");
    def->full_label = L("Ironing flow distribution");
    def->category = OptionCategory::infill;
    def->tooltip = L("This is the percentage of the flow that is used for the second ironing pass. Typical 10-20%. "
        "Should not be higher than 20%, unless you have your top extrusion width greatly superior to your nozzle width. "
        "A value too low and your extruder will eat the filament. A value too high and the first pass won't print well.");
    //def->min = 0;
    //def->max = 0.9;
    def->mode = comExpert;
    def->sidetext = L("%");
    def->set_default_value(new ConfigOptionPercent(10));

    def = this->add("first_layer_acceleration", coFloatOrPercent);
    def->label = L("First layer");
    def->full_label = L("First layer acceleration");
    def->category = OptionCategory::speed;
    def->tooltip = L("This is the acceleration your printer will use for first layer."
                "\nCan be a % of the default acceleration"
                "\nSet zero to disable acceleration control for first layer.");
    def->sidetext = L("mm/s² or %");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloatOrPercent(0, false));

    def = this->add("first_layer_bed_temperature", coInts);
    def->label = L("First layer");
    def->full_label = L("First layer bed temperature");
    def->category = OptionCategory::filament;
    def->tooltip = L("Heated build plate temperature for the first layer. Set this to zero to disable "
                   "bed temperature control commands in the output.");
    def->sidetext = L("°C");
    def->max = 0;
    def->max = 300;
    def->set_default_value(new ConfigOptionInts { 0 });

    def = this->add("first_layer_extrusion_width", coFloatOrPercent);
    def->label = L("First layer");
    def->full_label = L("First layer width");
    def->category = OptionCategory::width;
    def->tooltip = L("Set this to a non-zero value to set a manual extrusion width for first layer. "
                   "You can use this to force fatter extrudates for better adhesion. If expressed "
                   "as percentage (for example 140%) it will be computed over the nozzle diameter "
                   "of the nozzle used for the type of extrusion. "
                   "If set to zero, it will use the default extrusion width.");
    def->sidetext = L("mm or %");
    def->ratio_over = "nozzle_diameter";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(140, true));

    def = this->add("first_layer_height", coFloatOrPercent);
    def->label = L("First layer height");
    def->category = OptionCategory::perimeter;
    def->tooltip = L("When printing with very low layer heights, you might still want to print a thicker "
                   "bottom layer to improve adhesion and tolerance for non perfect build plates. "
                   "This can be expressed as an absolute value or as a percentage (for example: 75%) "
                   "over the default nozzle width.");
    def->sidetext = L("mm or %");
    def->ratio_over = "nozzle_diameter";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(75, true));

    def = this->add("first_layer_speed", coFloatOrPercent);
    def->label = L("Default");
    def->full_label = L("Default first layer speed");
    def->category = OptionCategory::speed;
    def->tooltip = L("If expressed as absolute value in mm/s, this speed will be applied to all the print moves "
                   "but infill of the first layer, it can be overwrite by the 'default' (default depends of the type of the path) "
                   "speed if it's lower than that. If expressed as a percentage "
                   "it will scale the current speed."
                   "\nSet it at 100% to remove any first layer speed modification (with first_layer_infill_speed).");
    def->sidetext = L("mm/s or %");
    def->ratio_over = "depends";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(30, false));
    
    def = this->add("first_layer_infill_speed", coFloatOrPercent);
    def->label = L("Infill");
    def->full_label = L("Infill first layer speed");
    def->category = OptionCategory::speed;
    def->tooltip = L("If expressed as absolute value in mm/s, this speed will be applied to infill moves "
                   "of the first layer, it can be overwrite by the 'default' (solid infill or infill if not bottom) "
                   "speed if it's lower than that. If expressed as a percentage "
                   "(for example: 40%) it will scale the current infill speed.");
    def->sidetext = L("mm/s or %");
    def->ratio_over = "depends";
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloatOrPercent(30, false));
    
    def = this->add("first_layer_temperature", coInts);
    def->label = L("First layer");
    def->full_label = L("First layer nozzle temperature");
    def->category = OptionCategory::filament;
    def->tooltip = L("Extruder nozzle temperature for first layer. If you want to control temperature manually "
                   "during print, set this to zero to disable temperature control commands in the output file.");
    def->sidetext = L("°C");
    def->min = 0;
    def->max = max_temp;
    def->set_default_value(new ConfigOptionInts { 200 });

    def = this->add("full_fan_speed_layer", coInts);
    def->label = L("Full fan speed at layer");
    def->tooltip = L("Fan speed will be ramped up linearly from zero at layer \"disable_fan_first_layers\" "
                   "to maximum at layer \"full_fan_speed_layer\". "
                   "\"full_fan_speed_layer\" will be ignored if lower than \"disable_fan_first_layers\", in which case "
                   "the fan will be running at maximum allowed speed at layer \"disable_fan_first_layers\" + 1.");
    def->min = 0;
    def->max = 1000;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionInts { 0 });

    def = this->add("gap_fill", coBool);
    def->label = L("");
    def->full_label = L("Enable Gap Fill");
    def->category = OptionCategory::perimeter;
    def->tooltip = L("Enable gap fill algorithm. It will extrude small lines between perimeters "
        "when there is not enough space for another perimeter or an infill.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("gap_fill_min_area", coFloatOrPercent);
    def->label = L("Min surface");
    def->full_label = ("Min gap-fill surface");
    def->category = OptionCategory::perimeter;
    def->tooltip = L("This setting represents the minimum mm² for a gapfill extrusion to be created.\nCan be a % of (perimeter width)²");
    def->ratio_over = "perimeter_width_square";
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloatOrPercent{ 100,true });

    def = this->add("gap_fill_overlap", coPercent);
    def->label = L("Gap fill overlap");
    def->full_label = L("Gap fill overlap");
    def->category = OptionCategory::width;
    def->tooltip = L("This setting allow you to reduce the overlap between the perimeters and the gap fill."
        " 100% means that no gaps is left, and 0% means that the gap fill won't touch the perimeters."
        "\nIt's very experimental, please report about the usefulness. It may be removed if there is no use for it.");
    def->sidetext = L("%");
    def->min = 0;
    def->max = 100;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionPercent(100));

    def = this->add("gap_fill_speed", coFloat);
    def->label = L("Gap fill");
    def->full_label = L("Gap fill speed");
    def->category = OptionCategory::speed;
    def->tooltip = L("Speed for filling small gaps using short zigzag moves. Keep this reasonably low "
        "to avoid too much shaking and resonance issues.");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(20));

    def = this->add("gcode_comments", coBool);
    def->label = L("Verbose G-code");
    def->category = OptionCategory::output;
    def->tooltip = L("Enable this to get a commented G-code file, with each line explained by a descriptive text. "
        "If you print from SD card, the additional weight of the file could make your firmware "
        "slow down.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(0));

    def = this->add("gcode_flavor", coEnum);
    def->label = L("G-code flavor");
    def->category = OptionCategory::general;
    def->tooltip = L("Some G/M-code commands, including temperature control and others, are not universal. "
                   "Set this option to your printer's firmware to get a compatible output. "
                   "The \"No extrusion\" flavor prevents Slic3r from exporting any extrusion value at all.");
    def->enum_keys_map = &ConfigOptionEnum<GCodeFlavor>::get_enum_values();
    def->enum_values.push_back("reprap");
    def->enum_values.push_back("repetier");
    def->enum_values.push_back("teacup");
    def->enum_values.push_back("makerware");
    def->enum_values.push_back("marlin");
    def->enum_values.push_back("klipper");
    def->enum_values.push_back("sailfish");
    def->enum_values.push_back("mach3");
    def->enum_values.push_back("machinekit");
    def->enum_values.push_back("smoothie");
    def->enum_values.push_back("sprinter");
    def->enum_values.push_back("lerdge");
    def->enum_values.push_back("no-extrusion");
    def->enum_labels.push_back("RepRapFirmware");
    def->enum_labels.push_back("Repetier");
    def->enum_labels.push_back("Teacup");
    def->enum_labels.push_back("MakerWare (MakerBot)");
    def->enum_labels.push_back("Marlin");
    def->enum_labels.push_back("Klipper");
    def->enum_labels.push_back("Sailfish (MakerBot)");
    def->enum_labels.push_back("Mach3/LinuxCNC");
    def->enum_labels.push_back("Machinekit");
    def->enum_labels.push_back("Smoothie");
    def->enum_labels.push_back("Sprinter");
    def->enum_labels.push_back("Lerdge");
    def->enum_labels.push_back(L("No extrusion"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<GCodeFlavor>(gcfSprinter));

    def = this->add("gcode_label_objects", coBool);
    def->label = L("Label objects");
    def->category = OptionCategory::output;
    def->tooltip = L("Enable this to add comments into the G-Code labeling print moves with what object they belong to,"
                   " which is useful for the Octoprint CancelObject plugin. This settings is NOT compatible with "
                   "Single Extruder Multi Material setup and Wipe into Object / Wipe into Infill.");
    def->aliases = { "label_printed_objects" };
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(1));

    def = this->add("high_current_on_filament_swap", coBool);
    def->label = L("High extruder current on filament swap");
    def->category = OptionCategory::general;
    def->tooltip = L("It may be beneficial to increase the extruder motor current during the filament exchange"
                   " sequence to allow for rapid ramming feed rates and to overcome resistance when loading"
                   " a filament with an ugly shaped tip.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(0));

    def = this->add("infill_acceleration", coFloatOrPercent);
    def->label = L("Infill");
    def->full_label = L("Infill acceleration");
    def->category = OptionCategory::speed;
    def->tooltip = L("This is the acceleration your printer will use for infill."
                "\nCan be a % of the default acceleration"
                "\nSet zero to disable acceleration control for infill.");
    def->sidetext = L("mm/s² or %");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloatOrPercent(0,false));

    def = this->add("infill_every_layers", coInt);
    def->label = L("Combine infill every");
    def->category = OptionCategory::infill;
    def->tooltip = L("This feature allows to combine infill and speed up your print by extruding thicker "
                   "infill layers while preserving thin perimeters, thus accuracy.");
    def->sidetext = L("layers");
    def->full_label = L("Combine infill every n layers");
    def->min = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(1));

    auto def_infill_anchor_min = def = this->add("infill_anchor", coFloatOrPercent);
    def->label = L("Length of the infill anchor");
    def->category = OptionCategory::infill;
    def->tooltip = L("Connect an infill line to an internal perimeter with a short segment of an additional perimeter. "
                     "If expressed as percentage (example: 15%) it is calculated over infill extrusion width. "
                     "PrusaSlicer tries to connect two close infill lines to a short perimeter segment. If no such perimeter segment "
                     "shorter than infill_anchor_max is found, the infill line is connected to a perimeter segment at just one side "
                     "and the length of the perimeter segment taken is limited to this parameter, but no longer than anchor_length_max. "
                     "\nSet this parameter to zero to disable anchoring perimeters connected to a single infill line.");
    def->sidetext = L("mm or %");
    def->ratio_over = "infill_extrusion_width";
    def->gui_type = "f_enum_open";
    def->enum_values.push_back("0");
    def->enum_values.push_back("1");
    def->enum_values.push_back("2");
    def->enum_values.push_back("5");
    def->enum_values.push_back("10");
    def->enum_values.push_back("1000");
    def->enum_labels.push_back(L("0 (no open anchors)"));
    def->enum_labels.push_back("1 mm");
    def->enum_labels.push_back("2 mm");
    def->enum_labels.push_back("5 mm");
    def->enum_labels.push_back("10 mm");
    def->enum_labels.push_back(L("1000 (unlimited)"));
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloatOrPercent(600, true));

    def = this->add("infill_anchor_max", coFloatOrPercent);
    def->label = L("Maximum length of the infill anchor");
    def->category    = def_infill_anchor_min->category;
    def->tooltip = L("Connect an infill line to an internal perimeter with a short segment of an additional perimeter. "
                     "If expressed as percentage (example: 15%) it is calculated over infill extrusion width. "
                     "PrusaSlicer tries to connect two close infill lines to a short perimeter segment. If no such perimeter segment "
                     "shorter than this parameter is found, the infill line is connected to a perimeter segment at just one side "
                     "and the length of the perimeter segment taken is limited to infill_anchor, but no longer than this parameter. "
                     "\nIf set to 0, the old algorithm for infill connection will be used, it should create the same result as with 1000 & 0.");
    def->sidetext    = def_infill_anchor_min->sidetext;
    def->ratio_over  = def_infill_anchor_min->ratio_over;
    def->gui_type    = def_infill_anchor_min->gui_type;
    def->enum_values = def_infill_anchor_min->enum_values;
    def->enum_labels.push_back(L("0 (Simple connect)"));
    def->enum_labels.push_back("1 mm");
    def->enum_labels.push_back("2 mm");
    def->enum_labels.push_back("5 mm");
    def->enum_labels.push_back("10 mm");
    def->enum_labels.push_back(L("1000 (unlimited)"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(0, false));

    def = this->add("infill_dense", coBool);
    def->label = ("");
    def->full_label = ("Dense infill layer");
    def->category = OptionCategory::infill;
    def->tooltip = L("Enables the creation of a support layer under the first solid layer. This allows you to use a lower infill ratio without compromising the top quality."
        " The dense infill is laid out with a 50% infill density.");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("infill_connection", coEnum);
    def->label = L("Connection of sparse infill lines");
    def->category = OptionCategory::infill;
    def->tooltip = L("Give to the infill algorithm if the infill needs to be connected, and on which periemters"
        " Can be useful for art or with high infill/perimeter overlap."
        " The result may varies between infill typers.");
    def->enum_keys_map = &ConfigOptionEnum<InfillConnection>::get_enum_values();
    def->enum_values.push_back("connected");
    def->enum_values.push_back("holes");
    def->enum_values.push_back("outershell");
    def->enum_values.push_back("notconnected");
    def->enum_labels.push_back(L("Connected"));
    def->enum_labels.push_back(L("Connected to hole perimeters"));
    def->enum_labels.push_back(L("Connected to outer perimeters"));
    def->enum_labels.push_back(L("Not connected"));
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionEnum<InfillConnection>(icConnected));

    def = this->add("infill_connection_top", coEnum);
    def->label = L("Connection of top infill lines");
    def->category = OptionCategory::infill;
    def->tooltip = L("Give to the infill algorithm if the infill needs to be connected, and on which periemters"
        " Can be useful for art or with high infill/perimeter overlap."
        " The result may varies between infill typers.");
    def->enum_keys_map = &ConfigOptionEnum<InfillConnection>::get_enum_values();
    def->enum_values.push_back("connected");
    def->enum_values.push_back("holes");
    def->enum_values.push_back("outershell");
    def->enum_values.push_back("notconnected");
    def->enum_labels.push_back(L("Connected"));
    def->enum_labels.push_back(L("Connected to hole perimeters"));
    def->enum_labels.push_back(L("Connected to outer perimeters"));
    def->enum_labels.push_back(L("Not connected"));
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionEnum<InfillConnection>(icConnected));

    def = this->add("infill_connection_bottom", coEnum);
    def->label = L("Connection of bottom infill lines");
    def->category = OptionCategory::infill;
    def->tooltip = L("Give to the infill algorithm if the infill needs to be connected, and on which periemters"
        " Can be useful for art or with high infill/perimeter overlap."
        " The result may varies between infill typers.");
    def->enum_keys_map = &ConfigOptionEnum<InfillConnection>::get_enum_values();
    def->enum_values.push_back("connected");
    def->enum_values.push_back("holes");
    def->enum_values.push_back("outershell");
    def->enum_values.push_back("notconnected");
    def->enum_labels.push_back(L("Connected"));
    def->enum_labels.push_back(L("Connected to hole perimeters"));
    def->enum_labels.push_back(L("Connected to outer perimeters"));
    def->enum_labels.push_back(L("Not connected"));
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionEnum<InfillConnection>(icConnected));

    def = this->add("infill_connection_solid", coEnum);
    def->label = L("Connection of solid infill lines");
    def->category = OptionCategory::infill;
    def->tooltip = L("Give to the infill algorithm if the infill needs to be connected, and on which periemters"
        " Can be useful for art or with high infill/perimeter overlap."
        " The result may varies between infill typers.");
    def->enum_keys_map = &ConfigOptionEnum<InfillConnection>::get_enum_values();
    def->enum_values.push_back("connected");
    def->enum_values.push_back("holes");
    def->enum_values.push_back("outershell");
    def->enum_values.push_back("notconnected");
    def->enum_labels.push_back(L("Connected"));
    def->enum_labels.push_back(L("Connected to hole perimeters"));
    def->enum_labels.push_back(L("Connected to outer perimeters"));
    def->enum_labels.push_back(L("Not connected"));
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionEnum<InfillConnection>(icConnected));
    
    def = this->add("infill_dense_algo", coEnum);
    def->label = L("Algorithm");
    def->full_label = ("Dense infill algorithm");
    def->category = OptionCategory::infill;
    def->tooltip = L("Choose the way the dense layer is lay out."
        " The automatic option let it try to draw the smallest surface with only strait lines inside the sparse infill."
        " The anchored just enlarge a bit (by 'Default infill margin') the surfaces that need a better support.");
    def->enum_keys_map = &ConfigOptionEnum<DenseInfillAlgo>::get_enum_values();
    def->enum_values.push_back("automatic");
    def->enum_values.push_back("autosmall");
    def->enum_values.push_back("enlarged");
    def->enum_labels.push_back(L("Automatic"));
    def->enum_labels.push_back(L("Automatic, only for small areas"));
    def->enum_labels.push_back(L("Anchored"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<DenseInfillAlgo>(dfaAutoNotFull));

    def = this->add("infill_extruder", coInt);
    def->label = L("Infill extruder");
    def->category = OptionCategory::extruders;
    def->tooltip = L("The extruder to use when printing infill.");
    def->min = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(1));

    def = this->add("infill_extrusion_width", coFloatOrPercent);
    def->label = L("Infill");
    def->category = OptionCategory::width;
    def->tooltip = L("Set this to a non-zero value to set a manual extrusion width for infill. "
                   "If left zero, default extrusion width will be used if set, otherwise 1.125 x nozzle diameter will be used. "
                   "You may want to use fatter extrudates to speed up the infill and make your parts stronger. "
                   "If expressed as percentage (for example 110%) it will be computed over nozzle diameter.");
    def->sidetext = L("mm or %");
    def->ratio_over = "nozzle_diameter";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(0, false));

    def = this->add("infill_first", coBool);
    def->label = L("Infill before perimeters");
    def->category = OptionCategory::infill;
    def->tooltip = L("This option will switch the print order of perimeters and infill, making the latter first.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("infill_only_where_needed", coBool);
    def->label = L("Only infill where needed");
    def->category = OptionCategory::infill;
    def->tooltip = L("This option will limit infill to the areas actually needed for supporting ceilings "
                   "(it will act as internal support material). If enabled, slows down the G-code generation "
                   "due to the multiple checks involved.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("infill_overlap", coFloatOrPercent);
    def->label = L("Infill/perimeters overlap");
    def->category = OptionCategory::width;
    def->tooltip = L("This setting applies an additional overlap between infill and perimeters for better bonding. "
                   "Theoretically this shouldn't be needed, but backlash might cause gaps. If expressed "
                   "as percentage (example: 15%) it is calculated over perimeter extrusion width.");
    def->sidetext = L("mm or %");
    def->ratio_over = "perimeter_extrusion_width";
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloatOrPercent(25, true));

    def = this->add("infill_speed", coFloat);
    def->label = L("Sparse");
    def->full_label = ("Sparse infill speed");
    def->category = OptionCategory::speed;
    def->tooltip = L("Speed for printing the internal fill. Set to zero for auto.");
    def->sidetext = L("mm/s");
    def->aliases = { "print_feed_rate", "infill_feed_rate" };
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(80));

    def = this->add("inherits", coString);
    def->label = L("Inherits profile");
    def->tooltip = L("Name of the profile, from which this profile inherits.");
    def->full_width = true;
    def->height = 5;
    def->set_default_value(new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    // The following value is to be stored into the project file (AMF, 3MF, Config ...)
    // and it contains a sum of "inherits" values over the print and filament profiles.
    def = this->add("inherits_cummulative", coStrings);
    def->set_default_value(new ConfigOptionStrings());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("interface_shells", coBool);
    def->label = L("Interface shells");
    def->tooltip = L("Force the generation of solid shells between adjacent materials/volumes. "
                   "Useful for multi-extruder prints with translucent materials or manual soluble "
                   "support material.");
    def->category = OptionCategory::perimeter;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("ironing", coBool);
    def->label = L("Enable ironing");
    def->tooltip = L("Enable ironing of the top layers with the hot print head for smooth surface");
    def->category = OptionCategory::ironing;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("ironing_type", coEnum);
    def->label = L("Ironing Type");
    def->category = OptionCategory::ironing;
    def->tooltip = L("Ironing Type");
    def->enum_keys_map = &ConfigOptionEnum<IroningType>::get_enum_values();
    def->enum_values.push_back("top");
    def->enum_values.push_back("topmost");
    def->enum_values.push_back("solid");
    def->enum_labels.push_back(L("All top surfaces"));
    def->enum_labels.push_back(L("Topmost surface only"));
    def->enum_labels.push_back(L("All solid surfaces"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<IroningType>(IroningType::TopSurfaces));

    def = this->add("ironing_flowrate", coPercent);
    def->label = L("Flow rate");
    def->category = OptionCategory::ironing;
    def->tooltip = L("Percent of a flow rate relative to object's normal layer height."
                " It's the percentage of the layer that will be over-extruded on top to do the ironing.");
    def->sidetext = L("%");
    def->ratio_over = "layer_height";
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionPercent(15));

    def = this->add("ironing_spacing", coFloat);
    def->label = L("Spacing between ironing lines");
    def->category = OptionCategory::ironing;
    def->tooltip = L("Distance between ironing lines");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0.1));

    def = this->add("ironing_speed", coFloat);
    def->label = L("Ironing");
    def->category = OptionCategory::ironing;
    def->tooltip = L("Ironing");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(15));

    def = this->add("layer_gcode", coString);
    def->label = L("After layer change G-code");
    def->category = OptionCategory::customgcode;
    def->tooltip = L("This custom code is inserted at every layer change, right after the Z move "
        "and before the extruder moves to the first layer point. Note that you can use "
        "placeholder variables for all Slic3r settings as well as [layer_num] and [layer_z].");
    def->cli = "after-layer-gcode|layer-gcode";
    def->multiline = true;
    def->full_width = true;
    def->height = 5;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("feature_gcode", coString);
    def->label = L("After layer change G-code");
    def->category = OptionCategory::customgcode;
    def->tooltip = L("This custom code is inserted at every extrusion type change."
        "Note that you can use placeholder variables for all Slic3r settings as well as [extrusion_role], [layer_num] and [layer_z] that can take these string values:"
        " { Perimeter, ExternalPerimeter, OverhangPerimeter, InternalInfill, SolidInfill, TopSolidInfill, BridgeInfill, GapFill, Skirt, SupportMaterial, SupportMaterialInterface, WipeTower, Mixed }."
        " Mixed is only used when the role of the extrusion is not unique, not exactly inside an other category or not known.");
    def->cli = "feature-gcode";
    def->multiline = true;
    def->full_width = true;
    def->height = 5;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("exact_last_layer_height", coBool);
    def->label = L("Exact last layer height");
    def->category = OptionCategory::perimeter;
    def->tooltip = L("This setting controls the height of last object layers to put the last layer at the exact highest height possible. Experimental.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("remaining_times", coBool);
    def->label = L("Supports remaining times");
    def->category = OptionCategory::firmware;
    def->tooltip = L("Emit M73 P[percent printed] R[remaining time in minutes] at 1 minute"
                     " intervals into the G-code to let the firmware show accurate remaining time."
                     " As of now only the Prusa i3 MK3 firmware recognizes M73."
                     " Also the i3 MK3 firmware supports M73 Qxx Sxx for the silent mode.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("silent_mode", coBool);
    def->label = L("Supports stealth mode");
    def->category = OptionCategory::firmware;
    def->tooltip = L("The firmware supports stealth mode");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("fan_speedup_time", coFloat);
    def->label = L("Fan startup delay");
    def->category = OptionCategory::firmware;
    def->tooltip = L("Move the fan start in the past by at least this delay (in seconds, you can use decimals)."
        " It assumes infinite acceleration for this time estimation, and will only take into account G1 and G0 moves. Use 0 to deactivate.");
    def->sidetext = L("s");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("fan_speedup_overhangs", coBool);
    def->label = L("Fan startup delay");
    def->category = OptionCategory::firmware;
    def->tooltip = L("Will only take into account the delay for the cooling of overhangs.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("fan_kickstart", coFloat);
    def->label = L("Fan KickStart time");
    def->category = OptionCategory::firmware;
    def->tooltip = L("Add a M106 S255 (max speed for fan) for this amount of seconds before gonig down to the desired speed to kick-start the cooling fan."
                    "\nSet to 0 to deactivate.");
    def->sidetext = L("s");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("machine_limits_usage", coEnum);
    def->label = L("How to apply limits");
    def->full_label = L("Purpose of Machine Limits");
    def->category = OptionCategory::limits;
    def->tooltip = L("How to apply the Machine Limits."
                    "\n* In every case, they will be used as safeguard: Even if you use a print profile that set an acceleration of 5000,"
                    " if your machine limits the acceleration is 4000, the outputted gcode will use the 4000 limit."
                    "\n* You can also use it as safeguard and to have a better printing time estimate."
                    "\n* You can also use it as safeguard, to have a better printing time estimate and emit the limits as the begining of the gcode file, with M201 M202 M203 M204 and M205 commands."
                    " If you want only to write a sub-set, choose the 'for time estimate' option and write yourself gcodes in the custom gcode section.");
    def->enum_keys_map = &ConfigOptionEnum<MachineLimitsUsage>::get_enum_values();
    def->enum_values.push_back("emit_to_gcode");
    def->enum_values.push_back("time_estimate_only");
    def->enum_values.push_back("limits");
    def->enum_values.push_back("ignore");
    def->enum_labels.push_back(L("Also emit limits to G-code"));
    def->enum_labels.push_back(L("Use also for time estimate"));
    def->enum_labels.push_back(L("Use only as safeguards"));
    def->enum_labels.push_back(L("Disable"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<MachineLimitsUsage>(MachineLimitsUsage::TimeEstimateOnly));

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
            def = this->add("machine_max_feedrate_" + axis.name, coFloats);
            def->full_label = (boost::format("Maximum feedrate %1%") % axis_upper).str();
            (void)L("Maximum feedrate X");
            (void)L("Maximum feedrate Y");
            (void)L("Maximum feedrate Z");
            (void)L("Maximum feedrate E");
            def->category = OptionCategory::limits;
            def->tooltip  = (boost::format("Maximum feedrate of the %1% axis") % axis_upper).str();
            (void)L("Maximum feedrate of the X axis");
            (void)L("Maximum feedrate of the Y axis");
            (void)L("Maximum feedrate of the Z axis");
            (void)L("Maximum feedrate of the E axis");
            def->sidetext = L("mm/s");
            def->min = 0;
            def->mode = comAdvanced;
            def->set_default_value(new ConfigOptionFloats(axis.max_feedrate));
            // Add the machine acceleration limits for XYZE axes (M201)
            def = this->add("machine_max_acceleration_" + axis.name, coFloats);
            def->full_label = (boost::format("Maximum acceleration %1%") % axis_upper).str();
            (void)L("Maximum acceleration X");
            (void)L("Maximum acceleration Y");
            (void)L("Maximum acceleration Z");
            (void)L("Maximum acceleration E");
            def->category = OptionCategory::limits;
            def->tooltip  = (boost::format("Maximum acceleration of the %1% axis") % axis_upper).str();
            (void)L("Maximum acceleration of the X axis");
            (void)L("Maximum acceleration of the Y axis");
            (void)L("Maximum acceleration of the Z axis");
            (void)L("Maximum acceleration of the E axis");
            def->sidetext = L("mm/s²");
            def->min = 0;
            def->mode = comAdvanced;
            def->set_default_value(new ConfigOptionFloats(axis.max_acceleration));
            // Add the machine jerk limits for XYZE axes (M205)
            def = this->add("machine_max_jerk_" + axis.name, coFloats);
            def->full_label = (boost::format("Maximum jerk %1%") % axis_upper).str();
            (void)L("Maximum jerk X");
            (void)L("Maximum jerk Y");
            (void)L("Maximum jerk Z");
            (void)L("Maximum jerk E");
            def->category = OptionCategory::limits;
            def->tooltip  = (boost::format("Maximum jerk of the %1% axis") % axis_upper).str();
            (void)L("Maximum jerk of the X axis");
            (void)L("Maximum jerk of the Y axis");
            (void)L("Maximum jerk of the Z axis");
            (void)L("Maximum jerk of the E axis");
            def->sidetext = L("mm/s");
            def->min = 0;
            def->mode = comAdvanced;
            def->set_default_value(new ConfigOptionFloats(axis.max_jerk));
        }
    }

    // M205 S... [mm/sec]
    def = this->add("machine_min_extruding_rate", coFloats);
    def->full_label = L("Minimum feedrate when extruding");
    def->category = OptionCategory::limits;
    def->tooltip = L("Minimum feedrate when extruding (M205 S)");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats{ 0., 0. });

    // M205 T... [mm/sec]
    def = this->add("machine_min_travel_rate", coFloats);
    def->full_label = L("Minimum travel feedrate");
    def->category = OptionCategory::limits;
    def->tooltip = L("Minimum travel feedrate (M205 T)");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats{ 0., 0. });

    // M204 S... [mm/sec^2]
    def = this->add("machine_max_acceleration_extruding", coFloats);
    def->full_label = L("Maximum acceleration when extruding");
    def->category = OptionCategory::limits;
    def->tooltip = L("Maximum acceleration when extruding (M204 P)");
    def->sidetext = L("mm/s²");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats{ 1500., 1250. });

    // M204 R... [mm/sec^2]
    def = this->add("machine_max_acceleration_retracting", coFloats);
    def->full_label = L("Maximum acceleration when retracting");
    def->category = OptionCategory::limits;
    def->tooltip = L("Maximum acceleration when retracting (M204 R)");
    def->sidetext = L("mm/s²");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats{ 1500., 1250. });

    // M204 T... [mm/sec^2]
    def = this->add("machine_max_acceleration_travel", coFloats);
    def->full_label = L("Maximum acceleration when travelling");
    def->category = OptionCategory::limits;
    def->tooltip = L("Maximum acceleration when travelling (M204 T)");
    def->sidetext = L("mm/s²");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats{ 1500., 1250. });

    def = this->add("max_fan_speed", coInts);
    def->label = L("Max");
    def->full_label = ("Max fan speed");
    def->category = OptionCategory::cooling;
    def->tooltip = L("This setting represents the maximum speed of your fan, used when the layer print time is Very short.");
    def->sidetext = L("%");
    def->min = 0;
    def->max = 100;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInts { 100 });

    def = this->add("max_layer_height", coFloats);
    def->label = L("Max");
    def->full_label = ("Max layer height");
    def->category = OptionCategory::general;
    def->tooltip = L("This is the highest printable layer height for this extruder, used to cap "
                   "the variable layer height and support layer height. Maximum recommended layer height "
                   "is 75% of the extrusion width to achieve reasonable inter-layer adhesion. "
                   "If set to 0, layer height is limited to 75% of the nozzle diameter.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloats { 0. });

    def = this->add("max_print_speed", coFloat);
    def->label = L("Max print speed");
    def->category = OptionCategory::speed;
    def->tooltip = L("When setting other speed settings to 0 Slic3r will autocalculate the optimal speed "
        "in order to keep constant extruder pressure. This experimental setting is used "
        "to set the highest print speed you want to allow.");
    def->sidetext = L("mm/s");
    def->min = 1;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(80));

    def = this->add("max_speed_reduction", coPercents);
    def->label = L("Max speed reduction");
    def->category = OptionCategory::speed;
    def->tooltip = L("Set to 90% if you don't want the speed to be reduced by more than 90%.");
    def->sidetext = L("%");
    def->min = 0;
    def->max = 100;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionPercents{ 90 });

    def = this->add("max_volumetric_speed", coFloat);
    def->label = L("Max volumetric speed");
    def->category = OptionCategory::extruders;
    def->tooltip = L("This experimental setting is used to set the maximum volumetric speed your "
                   "extruder supports.");
    def->sidetext = L("mm³/s");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0));

#ifdef HAS_PRESSURE_EQUALIZER
    def = this->add("max_volumetric_extrusion_rate_slope_positive", coFloat);
    def->label = L("Max volumetric slope positive");
    def->tooltip = L("This experimental setting is used to limit the speed of change in extrusion rate. "
                   "A value of 1.8 mm³/s² ensures, that a change from the extrusion rate "
                   "of 1.8 mm³/s (0.45mm extrusion width, 0.2mm extrusion height, feedrate 20 mm/s) "
                   "to 5.4 mm³/s (feedrate 60 mm/s) will take at least 2 seconds.");
    def->sidetext = L("mm³/s²");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0);
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("max_volumetric_extrusion_rate_slope_negative", coFloat);
    def->label = L("Max volumetric slope negative");
    def->tooltip = L("This experimental setting is used to limit the speed of change in extrusion rate. "
                   "A value of 1.8 mm³/s² ensures, that a change from the extrusion rate "
                   "of 1.8 mm³/s (0.45mm extrusion width, 0.2mm extrusion height, feedrate 20 mm/s) "
                   "to 5.4 mm³/s (feedrate 60 mm/s) will take at least 2 seconds.");
    def->sidetext = L("mm³/s²");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0);
    def->set_default_value(new ConfigOptionFloat(0));
#endif /* HAS_PRESSURE_EQUALIZER */

    def = this->add("min_fan_speed", coInts);
    def->label = L("Default fan speed");
    def->full_label = ("Default fan speed");
    def->category = OptionCategory::cooling;
    def->tooltip = L("This setting represents the base fan speed this filament needs, or at least the minimum PWM your fan needs to work.");
    def->sidetext = L("%");
    def->min = 0;
    def->max = 100;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionInts{ 35 });

    def = this->add("min_layer_height", coFloats);
    def->label = L("Min");
    def->full_label = ("Min layer height");
    def->category = OptionCategory::extruders;
    def->tooltip = L("This is the lowest printable layer height for this extruder and limits "
                   "the resolution for variable layer height. Typical values are between 0.05 mm and 0.1 mm.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloats { 0.07 });

    def = this->add("min_length", coFloat);
    def->label = L("Minimum extrusion length");
    def->category = OptionCategory::speed;
    def->tooltip = L("Too many too small commands may overload the firmware / connection. Put a higher value here if you see strange slowdown."
                     "\n0 to disable.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0.035));

    def = this->add("min_width_top_surface", coFloatOrPercent);
    def->label = L("Minimum top width for infill");
    def->category = OptionCategory::speed;
    def->tooltip = L("If a top surface has to be printed and it's partially covered by an other layer, it won't be considered at a top layer where his width is below this value."
        " This can be useful to not let the 'one perimeter on top' trigger on surface that should be covered only by perimeters."
        " This value can be a mm or a % of the perimeter extrusion width.");
    def->sidetext = L("mm or %");
    def->ratio_over = "perimeter_extrusion_width";
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloatOrPercent(200, true));

    def = this->add("min_print_speed", coFloats);
    def->label = L("Min print speed");
    def->category = OptionCategory::speed;
    def->tooltip = L("Slic3r will never scale the speed below this one.");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloats{ 10. });

    def = this->add("min_skirt_length", coFloat);
    def->label = L("Minimal filament extrusion length");
    def->category = OptionCategory::skirtBrim;
    def->tooltip = L("Generate no less than the number of skirt loops required to consume "
                   "the specified amount of filament on the bottom layer. For multi-extruder machines, "
                   "this minimum applies to each extruder.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("notes", coString);
    def->label = L("Configuration notes");
    def->category = OptionCategory::notes;
    def->tooltip = L("You can put here your personal notes. This text will be added to the G-code "
                   "header comments.");
    def->multiline = true;
    def->full_width = true;
    def->height = 13;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("nozzle_diameter", coFloats);
    def->label = L("Nozzle diameter");
    def->category = OptionCategory::extruders;
    def->tooltip = L("This is the diameter of your extruder nozzle (for example: 0.5, 0.35 etc.)");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats{ 0.4 });

    def = this->add("host_type", coEnum);
    def->label = L("Host Type");
    def->category = OptionCategory::general;
    def->tooltip = L("Slic3r can upload G-code files to a printer host. This field must contain "
                   "the kind of the host.");
    def->enum_keys_map = &ConfigOptionEnum<PrintHostType>::get_enum_values();
    def->enum_values.push_back("octoprint");
    def->enum_values.push_back("duet");
    def->enum_values.push_back("flashair");
    def->enum_values.push_back("astrobox");
    def->enum_values.push_back("repetier");
    def->enum_labels.push_back("OctoPrint");
    def->enum_labels.push_back("Duet");
    def->enum_labels.push_back("FlashAir");
    def->enum_labels.push_back("AstroBox");
    def->enum_labels.push_back("Repetier");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<PrintHostType>(htOctoPrint));

    def = this->add("printhost_apikey", coString);
    def->label = L("API Key / Password");
    def->category = OptionCategory::general;
    def->tooltip = L("Slic3r can upload G-code files to a printer host. This field should contain "
                   "the API Key or the password required for authentication.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString(""));
    
    def = this->add("printhost_cafile", coString);
    def->label = L("HTTPS CA File");
    def->tooltip = L("Custom CA certificate file can be specified for HTTPS OctoPrint connections, in crt/pem format. "
                   "If left blank, the default OS CA certificate repository is used.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("print_host", coString);
    def->label = L("Hostname, IP or URL");
    def->category = OptionCategory::general;
    def->tooltip = L("Slic3r can upload G-code files to a printer host. This field should contain "
        "the hostname, IP address or URL of the printer host instance.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("only_retract_when_crossing_perimeters", coBool);
    def->label = L("Only retract when crossing perimeters");
    def->category = OptionCategory::extruders;
    def->tooltip = L("Disables retraction when the travel path does not exceed the upper layer's perimeters "
                   "(and thus any ooze will be probably invisible).");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("ooze_prevention", coBool);
    def->label = L("Enable");
    def->category = OptionCategory::extruders;
    def->tooltip = L("This option will drop the temperature of the inactive extruders to prevent oozing. "
                   "It will enable a tall skirt automatically and move extruders outside such "
                   "skirt when changing temperatures.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("output_filename_format", coString);
    def->label = L("Output filename format");
    def->category = OptionCategory::output;
    def->tooltip = L("You can use all configuration options as variables inside this template. "
                   "For example: [layer_height], [fill_density] etc. You can also use [timestamp], "
                   "[year], [month], [day], [hour], [minute], [second], [version], [input_filename], "
                   "[input_filename_base].");
    def->full_width = true;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionString("[input_filename_base].gcode"));

    def = this->add("overhangs_speed", coFloatOrPercent);
    def->label = L("Overhangs");
    def->full_label = L("Overhangs speed");
    def->category = OptionCategory::speed;
    def->tooltip = L("Speed for printing overhangs.\nCan be a % of the bridge speed.");
    def->sidetext = L("mm/s");
    def->ratio_over = "bridge_speed";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(100, true));

    def = this->add("overhangs_width_speed", coFloatOrPercent);
    def->label = L("'As bridge' speed threshold");
    def->full_label = L("Overhang bridge speed threshold");
    def->category = OptionCategory::perimeter;
    def->tooltip = L("Minimum unsupported width for an extrusion to apply the bridge fan & overhang speed to this overhang."
        " Can be in mm or in a % of the nozzle diameter."
        " Set to 0 to deactivate.");
    def->ratio_over = "nozzle_diameter";
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloatOrPercent(55,true));

    def = this->add("overhangs_width", coFloatOrPercent);
    def->label = L("'As bridge' flow threshold");
    def->full_label = L("Overhang bridge flow threshold");
    def->category = OptionCategory::perimeter;
    def->tooltip = L("Minimum unsupported width for an extrusion to apply the bridge flow to this overhang."
        " Can be in mm or in a % of the nozzle diameter."
        " Set to 0 to deactivate.");
    def->ratio_over = "nozzle_diameter";
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloatOrPercent(75, true));

    def = this->add("overhangs_reverse", coBool);
    def->label = L("Reverse on odd");
    def->full_label = L("Overhang reversal");
    def->category = OptionCategory::perimeter;
    def->tooltip = L("Extrude perimeters that have a part over an overhang in the reverse direction in odd layers. That alternating pattern can drastically improve steep overhang."
        "\n!! this is a very slow algorithm (it uses the same results as extra_perimeters_overhangs) !!");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("overhangs_reverse_threshold", coFloatOrPercent);
    def->label = L("Reverse threshold");
    def->full_label = L("Overhang reversal threshold");
    def->category = OptionCategory::perimeter;
    def->tooltip = L("Number of mm the overhang need to be for the reversal to be considered useful. Can be a % of the perimeter width.");
    def->ratio_over = "perimeter_extrusion_width";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(250, true));

    def = this->add("no_perimeter_unsupported_algo", coEnum);
    def->label = L("No perimeters on bridge areas");
    def->category = OptionCategory::perimeter;
    def->tooltip = L("Experimental option to remove perimeters where there is nothing under it and where a bridged infill should be better. "
        "\n * Remove perimeters: remove the unsupported perimeters, let the bridge area as-is."
        "\n * Keep only bridges: remove the perimeters in the bridge areas, keep only bridges that end in solid area."
        "\n * Keep bridges and overhangs: remove the unsupported perimeters, keep only bridges that end in solid area, fill the rest with overhang perimeters+bridges."
        "\n * Fill the voids with bridges: remove the unsupported perimeters, draw bridges over the whole hole.*"
        " !! this one can escalate to problems with overhangs shape like  /\\, so you should use it only on one layer at a time via the height-range modifier!"
        "\n!!Computationally intensive!!. ");
    def->enum_keys_map = &ConfigOptionEnum<NoPerimeterUnsupportedAlgo>::get_enum_values();
    def->enum_values.push_back("none");
    def->enum_values.push_back("noperi");
    def->enum_values.push_back("bridges");
    def->enum_values.push_back("bridgesoverhangs");
    def->enum_values.push_back("filled");
    def->enum_labels.push_back(L("Disabled"));
    def->enum_labels.push_back(L("Remove perimeters"));
    def->enum_labels.push_back(L("Keep only bridges"));
    def->enum_labels.push_back(L("Keep bridges and overhangs"));
    def->enum_labels.push_back(L("Fill the voids with bridges"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<NoPerimeterUnsupportedAlgo>(npuaNone));

    def = this->add("parking_pos_retraction", coFloat);
    def->label = L("Filament parking position");
    def->tooltip = L("Distance of the extruder tip from the position where the filament is parked "
                      "when unloaded. This should match the value in printer firmware. ");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(92.f));

    def = this->add("extra_loading_move", coFloat);
    def->label = L("Extra loading distance");
    def->tooltip = L("When set to zero, the distance the filament is moved from parking position during load "
                      "is exactly the same as it was moved back during unload. When positive, it is loaded further, "
                      " if negative, the loading move is shorter than unloading. ");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(-2.f));

    def = this->add("perimeter_acceleration", coFloatOrPercent);
    def->label = L("Perimeters");
    def->full_label = ("Perimeter acceleration");
    def->category = OptionCategory::speed;
    def->tooltip = L("This is the acceleration your printer will use for perimeters. "
                "\nCan be a % of the default acceleration"
                "\nSet zero to disable acceleration control for perimeters.");
    def->sidetext = L("mm/s² or %");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloatOrPercent(0,false));

    def = this->add("perimeter_extruder", coInt);
    def->label = L("Perimeter extruder");
    def->category = OptionCategory::extruders;
    def->tooltip = L("The extruder to use when printing perimeters and brim. First extruder is 1.");
    def->aliases = { "perimeters_extruder" };
    def->min = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(1));

    def = this->add("perimeter_extrusion_width", coFloatOrPercent);
    def->label = L("Perimeters");
    def->full_label = ("Perimeter width");
    def->category = OptionCategory::width;
    def->tooltip = L("Set this to a non-zero value to set a manual extrusion width for perimeters. "
                   "You may want to use thinner extrudates to get more accurate surfaces. "
                   "If left zero, default extrusion width will be used if set, otherwise 1.125 x nozzle diameter will be used. "
                   "If expressed as percentage (for example 105%) it will be computed over nozzle diameter.");
    def->sidetext = L("mm or %");
    def->aliases = { "perimeters_extrusion_width" };
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(0, false));

    def = this->add("perimeter_speed", coFloat);
    def->label = L("Default");
    def->full_label = ("Default speed");
    def->category = OptionCategory::speed;
    def->tooltip = L("Speed for perimeters (contours, aka vertical shells). Set to zero for auto.");
    def->sidetext = L("mm/s");
    def->aliases = { "perimeter_feed_rate" };
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(60));

    def = this->add("perimeters", coInt);
    def->label = L("Perimeters");
    def->full_label = ("Perimeters count");
    def->category = OptionCategory::perimeter;
    def->tooltip = L("This option sets the number of perimeters to generate for each layer. "
                   "Note that Slic3r may increase this number automatically when it detects "
                   "sloping surfaces which benefit from a higher number of perimeters "
                   "if the Extra Perimeters option is enabled.");
    def->sidetext = L("(minimum).");
    def->aliases = { "perimeter_offsets" };
    def->min = 0;
    def->max = 10000;
    def->set_default_value(new ConfigOptionInt(3));

    def = this->add("post_process", coStrings);
    def->label = L("Post-processing scripts");
    def->category = OptionCategory::customgcode;
    def->tooltip = L("If you want to process the output G-code through custom scripts, "
                   "just list their absolute paths here. Separate multiple scripts with a semicolon. "
                   "Scripts will be passed the absolute path to the G-code file as the first argument, "
                   "and they can access the Slic3r config settings by reading environment variables.");
    def->gui_flags = "serialized";
    def->multiline = true;
    def->full_width = true;
    def->height = 6;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionStrings());

    def = this->add("printer_model", coString);
    def->label = L("Printer type");
    def->tooltip = L("Type of the printer.");
    def->set_default_value(new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("printer_notes", coString);
    def->label = L("Printer notes");
    def->category = OptionCategory::notes;
    def->tooltip = L("You can put your notes regarding the printer here.");
    def->multiline = true;
    def->full_width = true;
    def->height = 13;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("printer_vendor", coString);
    def->label = L("Printer vendor");
    def->tooltip = L("Name of the printer vendor.");
    def->set_default_value(new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("printer_variant", coString);
    def->label = L("Printer variant");
    def->tooltip = L("Name of the printer variant. For example, the printer variants may be differentiated by a nozzle diameter.");
    def->set_default_value(new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("print_settings_id", coString);
    def->set_default_value(new ConfigOptionString(""));
    def->cli = ConfigOptionDef::nocli;

    def = this->add("printer_settings_id", coString);
    def->set_default_value(new ConfigOptionString(""));
    def->cli = ConfigOptionDef::nocli;

    def = this->add("physical_printer_settings_id", coString);
    def->set_default_value(new ConfigOptionString(""));
    def->cli = ConfigOptionDef::nocli;

    def = this->add("support_material_solid_first_layer", coBool);
    def->label = L("Solid first layer");
    def->category = OptionCategory::support;
    def->tooltip = L("Use a solid layer instead of a raft for the layer that touch the build plate.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("raft_layers", coInt);
    def->label = L("Raft layers");
    def->category = OptionCategory::support;
    def->tooltip = L("The object will be raised by this number of layers, and support material "
        "will be generated under it.");
    def->sidetext = L("layers");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(0));

    def = this->add("resolution", coFloat);
    def->label = L("Resolution");
    def->category = OptionCategory::slicing;
    def->tooltip = L("Minimum detail resolution, used to simplify the input file for speeding up "
        "the slicing job and reducing memory usage. High-resolution models often carry "
        "more detail than printers can render. Set to zero to disable any simplification "
        "and use full resolution from input. "
        "\nNote: SuperSlicer has an internal resolution of 0.000001mm."
        "\nInfill & Thin areas are simplified up to 0.0125mm.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0.0125));

    def = this->add("retract_before_travel", coFloats);
    def->label = L("Minimum travel after retraction");
    def->category = OptionCategory::extruders;
    def->tooltip = L("Retraction is not triggered when travel moves are shorter than this length.");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->min = 0;
    def->set_default_value(new ConfigOptionFloats { 2. });

    def = this->add("retract_before_wipe", coPercents);
    def->label = L("Retract amount before wipe");
    def->category = OptionCategory::extruders;
    def->tooltip = L("With bowden extruders, it may be wise to do some amount of quick retract "
                   "before doing the wipe movement.");
    def->sidetext = L("%");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPercents { 0. });

    def = this->add("retract_layer_change", coBools);
    def->label = L("Retract on layer change");
    def->category = OptionCategory::extruders;
    def->tooltip = L("This flag enforces a retraction whenever a Z move is done (before it).");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBools { false });

    def = this->add("retract_length", coFloats);
    def->label = L("Length");
    def->full_label = L("Retraction Length");
    def->category = OptionCategory::extruders;
    def->tooltip = L("When retraction is triggered, filament is pulled back by the specified amount "
                   "(the length is measured on raw filament, before it enters the extruder).");
    def->sidetext = L("mm (zero to disable)");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloats { 2. });

    def = this->add("print_retract_length", coFloat);
    def->label = L("Retraction length");
    def->category = OptionCategory::filament;
    def->tooltip = L("Override the retract_length settign from the printer config. Used for calibration. Set negative to disable");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat( -1.f));

    def = this->add("retract_length_toolchange", coFloats);
    def->label = L("Length");
    def->full_label = L("Retraction Length (Toolchange)");
    def->tooltip = L("When retraction is triggered before changing tool, filament is pulled back "
                   "by the specified amount (the length is measured on raw filament, before it enters "
                   "the extruder).");
    def->sidetext = L("mm (zero to disable)");
    def->mode = comExpert;
    def->min = 0;
    def->set_default_value(new ConfigOptionFloats { 10. });

    def = this->add("retract_lift", coFloats);
    def->label = L("Lift Z");
    def->category = OptionCategory::extruders;
    def->tooltip = L("If you set this to a positive value, Z is quickly raised every time a retraction "
                   "is triggered. When using multiple extruders, only the setting for the first extruder "
                   "will be considered.");
    def->sidetext = L("mm");
    def->set_default_value(new ConfigOptionFloats { 0. });

    def = this->add("retract_lift_above", coFloats);
    def->label = L("Above Z");
    def->full_label = L("Only lift Z above");
    def->category = OptionCategory::extruders;
    def->tooltip = L("If you set this to a positive value, Z lift will only take place above the specified "
                   "absolute Z. You can tune this setting for skipping lift on the first layers.");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 0. });

    def = this->add("retract_lift_below", coFloats);
    def->label = L("Below Z");
    def->full_label = L("Only lift Z below");
    def->category = OptionCategory::extruders;
    def->tooltip = L("If you set this to a positive value, Z lift will only take place below "
                   "the specified absolute Z. You can tune this setting for limiting lift "
                   "to the first layers.");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 0. });

    def = this->add("retract_lift_first_layer", coBools);
    def->label = L("Enforce on first layer");
    def->full_label = L("Enforce lift on first layer");
    def->category = OptionCategory::extruders;
    def->tooltip = L("Select this option to enforce z-lift on the first layer.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBools{ false });

    def = this->add("retract_lift_top", coStrings);
    def->label = L("On surfaces");
    def->full_label = L("Lift only on");
    def->category = OptionCategory::extruders;
    def->tooltip = L("Select this option to not use/enforce the z-lift on a top surface.");
    def->gui_type = "f_enum_open";
    def->gui_flags = "show_value";
    def->enum_values.push_back(("All surfaces"));
    def->enum_values.push_back(("Not on top"));
    def->enum_values.push_back(("Only on top"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionStrings{ "All surfaces" });


    def = this->add("retract_restart_extra", coFloats);
    def->label = L("Extra length on restart");
    def->tooltip = L("When the retraction is compensated after the travel move, the extruder will push "
                   "this additional amount of filament. This setting is rarely needed.");
    def->sidetext = L("mm");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloats { 0. });

    def = this->add("retract_restart_extra_toolchange", coFloats);
    def->label = L("Extra length on restart");
    def->full_label = L("Extrat length on toolchange restart");
    def->tooltip = L("When the retraction is compensated after changing tool, the extruder will push "
                   "this additional amount of filament.");
    def->sidetext = L("mm");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloats { 0. });

    def = this->add("retract_speed", coFloats);
    def->label = L("Retraction Speed");
    def->full_label = L("Retraction Speed");
    def->category = OptionCategory::extruders;
    def->tooltip = L("The speed for retractions (it only applies to the extruder motor).");
    def->sidetext = L("mm/s");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 40. });

    def = this->add("deretract_speed", coFloats);
    def->label = L("Deretraction Speed");
    def->full_label = L("Deretraction Speed");
    def->category = OptionCategory::extruders;
    def->tooltip = L("The speed for loading of a filament into extruder after retraction "
                   "(it only applies to the extruder motor). If left to zero, the retraction speed is used.");
    def->sidetext = L("mm/s");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 0. });

    def = this->add("seam_position", coEnum);
    def->label = L("Seam position");
    def->category = OptionCategory::perimeter;
    def->tooltip = L("Position of perimeters starting points."
                    "\n ");
    def->enum_keys_map = &ConfigOptionEnum<SeamPosition>::get_enum_values();
    def->enum_values.push_back("near");
    def->enum_values.push_back("random");
    def->enum_values.push_back("aligned");
    def->enum_values.push_back("rear");
    def->enum_labels.push_back(L("Cost-based"));
    def->enum_labels.push_back(L("Random"));
    def->enum_labels.push_back(L("Aligned"));
    def->enum_labels.push_back(L("Rear"));
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionEnum<SeamPosition>(spNearest));

    def = this->add("seam_angle_cost", coPercent);
    def->label = L("Angle cost");
    def->full_label = L("Seam angle cost");
    def->category = OptionCategory::perimeter;
    def->tooltip = L("Cost of placing the seam at a bad angle. The worst angle (max penalty) is when it's flat.");
    def->sidetext = L("%");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionPercent(100));

    def = this->add("seam_travel_cost", coPercent);
    def->label = L("Travel cost");
    def->full_label = L("Seam travel cost");
    def->category = OptionCategory::perimeter;
    def->tooltip = L("Cost of moving the extruder. The highest penalty is when the point is the farest from the position of the extruder before extruding the external periemter");
    def->sidetext = L("%");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionPercent(100));

#if 0
    def = this->add("seam_preferred_direction", coFloat);
//    def->gui_type = "slider";
    def->label = L("Direction");
    def->sidetext = L("°");
    def->full_label = L("Preferred direction of the seam");
    def->tooltip = L("Seam preferred direction");
    def->min = 0;
    def->max = 360;
    def->set_default_value(new ConfigOptionFloat(0);
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("seam_preferred_direction_jitter", coFloat);
//    def->gui_type = "slider";
    def->label = L("Jitter");
    def->sidetext = L("°");
    def->full_label = L("Seam preferred direction jitter");
    def->tooltip = L("Preferred direction of the seam - jitter");
    def->min = 0;
    def->max = 360;
    def->set_default_value(new ConfigOptionFloat(30);
    def->set_default_value(new ConfigOptionFloat(30));
#endif

    def = this->add("skirt_distance", coFloat);
    def->label = L("Distance from object");
    def->category = OptionCategory::skirtBrim;
    def->tooltip = L("Distance between skirt and object(s). Set this to zero to attach the skirt "
                   "to the object(s) and get a brim for better adhesion.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(6));

    def = this->add("skirt_height", coInt);
    def->label = L("Skirt height");
    def->category = OptionCategory::skirtBrim;
    def->tooltip = L("Height of skirt expressed in layers. Set this to a tall value to use skirt "
                   "as a shield against drafts.");
    def->sidetext = L("layers");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(1));

    def = this->add("skirt_extrusion_width", coFloatOrPercent);
    def->label = L("Skirt");
    def->category = OptionCategory::width;
    def->tooltip = L("Horizontal width of the skirt that will be printed around each object.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(0, false));

    def = this->add("draft_shield", coBool);
    def->label = L("Draft shield");
    def->tooltip = L("If enabled, the skirt will be as tall as a highest printed object. "
    				 "This is useful to protect an ABS or ASA print from warping and detaching from print bed due to wind draft.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("skirts", coInt);
    def->label = L("Loops (minimum)");
    def->full_label = L("Skirt Loops");
    def->category = OptionCategory::skirtBrim;
    def->tooltip = L("Number of loops for the skirt. If the Minimum Extrusion Length option is set, "
                   "the number of loops might be greater than the one configured here. Set this to zero "
                   "to disable skirt completely.");
    def->min = 0;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionInt(1));

    def = this->add("slowdown_below_layer_time", coInts);
    def->label = L("Slow down if layer print time is below");
    def->category = OptionCategory::cooling;
    def->tooltip = L("If layer print time is estimated below this number of seconds, print moves "
        "speed will be scaled down to extend duration to this value, if possible."
        "\nSet to 0 to disable.");
    def->sidetext = L("approximate seconds");
    def->min = 0;
    def->max = 1000;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionInts{ 5 });

    def = this->add("small_perimeter_speed", coFloatOrPercent);
    def->label = L("Speed");
    def->full_label = ("Small perimeters speed");
    def->category = OptionCategory::speed;
    def->tooltip = L("This separate setting will affect the speed of perimeters having radius <= 6.5mm "
                   "(usually holes). If expressed as percentage (for example: 80%) it will be calculated "
                   "on the perimeters speed setting above. Set to zero for auto.");
    def->sidetext = L("mm/s or %");
    def->ratio_over = "perimeter_speed";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(15, false));

    def = this->add("small_perimeter_min_length", coFloatOrPercent);
    def->label = L("Min length");
    def->full_label = ("Min small perimeters length");
    def->category = OptionCategory::speed;
    def->tooltip = L("This set the threshold for small periemter length. Every loop with a length lower than that will be printed at small perimeter speed"
        "\nCan be a mm or a % of the nozzle diameter.");
    def->sidetext = L("mm or %");
    def->ratio_over = "nozzle_diameter";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(6, false));


    def = this->add("small_perimeter_max_length", coFloatOrPercent);
    def->label = L("Max length");
    def->full_label = ("Max small perimeters length");
    def->category = OptionCategory::speed;
    def->tooltip = L("This set the end of the threshold for small periemter length."
        " Every periemter loop lower than that will see their speed reduced a bit, from their normal spee at this length down to small perimeter speed."
        "\nCan be a mm or a % of the nozzle diameter.");
    def->sidetext = L("mm or %");
    def->ratio_over = "nozzle_diameter";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(20, false));

    def = this->add("curve_smoothing_angle_convex", coFloat);
    def->label = L("Min convex angle");
    def->full_label = L("Curve smoothing minimum angle (convex)");
    def->category = OptionCategory::slicing;
    def->tooltip = L("Minimum (convex) angle at a vertex to enable smoothing"
        " (trying to create a curve around the vertex). "
        "180 : nothing will be smooth, 0 : all angles will be smoothen.");
    def->sidetext = L("°");
    def->aliases = { "curve_smoothing_angle" };
    def->cli = "curve-smoothing-angle-convex=f";
    def->min = 0;
    def->max = 180;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("curve_smoothing_angle_concave", coFloat);
    def->label = L("Min concave angle");
    def->full_label = L("Curve smoothing minimum angle (concave)");
    def->category = OptionCategory::slicing;
    def->tooltip = L("Minimum (concave) angle at a vertex to enable smoothing"
        " (trying to create a curve around the vertex). "
        "180 : nothing will be smooth, 0 : all angles will be smoothen.");
    def->sidetext = L("°");
    def->cli = "curve-smoothing-angle-concave=f";
    def->min = 0;
    def->max = 180;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("curve_smoothing_precision", coFloat);
    def->label = L("Precision");
    def->full_label = L("Curve smoothing precision");
    def->category = OptionCategory::slicing;
    def->tooltip = L("These parameter allow the slicer to smooth the angles in each layer. "
        "The precision will be at least the new precision of the curve. Set to 0 to deactivate."
        "\nNote: as it use the polygon's edges and only work in the 2D planes, "
        "you must have a very clean or hand-made 3D model."
        "\nIt's really only useful to smoothen functional models or very wide angles.");
    def->sidetext = L("mm");
    def->min = 0;
    def->cli = "curve-smoothing-precision=f";
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("curve_smoothing_cutoff_dist", coFloat);
    def->label = L("cutoff");
    def->full_label = L("Curve smoothing cutoff dist");
    def->category = OptionCategory::slicing;
    def->tooltip = L("Maximum distance between two points to allow adding new ones. Allow to avoid distording long strait areas. 0 to disable.");
    def->sidetext = L("mm");
    def->min = 0;
    def->cli = "curve-smoothing-cutoff-dist=f";
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(2));

    def = this->add("solid_infill_below_area", coFloat);
    def->label = L("Solid infill threshold area");
    def->category = OptionCategory::infill;
    def->tooltip = L("Force solid infill for regions having a smaller area than the specified threshold.");
    def->sidetext = L("mm²");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(70));

    def = this->add("solid_infill_extruder", coInt);
    def->label = L("Solid infill extruder");
    def->category = OptionCategory::extruders;
    def->tooltip = L("The extruder to use when printing solid infill.");
    def->min = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(1));

    def = this->add("solid_infill_every_layers", coInt);
    def->label = L("Solid infill every");
    def->category = OptionCategory::infill;
    def->tooltip = L("This feature allows to force a solid layer every given number of layers. "
                   "Zero to disable. You can set this to any value (for example 9999); "
                   "Slic3r will automatically choose the maximum possible number of layers "
                   "to combine according to nozzle diameter and layer height.");
    def->sidetext = L("layers");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionInt(0));

    def = this->add("solid_infill_extrusion_width", coFloatOrPercent);
    def->label = L("Solid infill");
    def->full_label = ("Solid infill width");
    def->category = OptionCategory::width;
    def->tooltip = L("Set this to a non-zero value to set a manual extrusion width for infill for solid surfaces. "
                   "If left zero, default extrusion width will be used if set, otherwise 1.125 x nozzle diameter will be used. "
                   "If expressed as percentage (for example 110%) it will be computed over nozzle diameter.");
    def->sidetext = L("mm or %");
    def->ratio_over = "nozzle_diameter";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(0, false));

    def = this->add("solid_infill_speed", coFloatOrPercent);
    def->label = L("Solid");
    def->full_label = ("Solid infill speed");
    def->category = OptionCategory::speed;
    def->tooltip = L("Speed for printing solid regions (top/bottom/internal horizontal shells). "
                   "This can be expressed as a percentage (for example: 80%) over the default infill speed."
                   " Set to zero for auto.");
    def->sidetext = L("mm/s or %");
    def->ratio_over = "infill_speed";
    def->aliases = { "solid_infill_feed_rate" };
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(20, false));

    def = this->add("solid_layers", coInt);
    def->label = L("Solid layers");
    def->category = OptionCategory::slicing;
    def->tooltip = L("Number of solid layers to generate on top and bottom surfaces.");
    def->shortcut.push_back("top_solid_layers");
    def->shortcut.push_back("bottom_solid_layers");
    def->min = 0;

    def = this->add("solid_min_thickness", coFloat);
    def->label = L("Minimum thickness of a top / bottom shell");
    def->tooltip = L("Minimum thickness of a top / bottom shell");
    def->shortcut.push_back("top_solid_min_thickness");
    def->shortcut.push_back("bottom_solid_min_thickness");
    def->min = 0;

    def = this->add("spiral_vase", coBool);
    def->label = L("Spiral vase");
    def->category = OptionCategory::perimeter;
    def->tooltip = L("This feature will raise Z gradually while printing a single-walled object "
                   "in order to remove any visible seam. This option requires a single perimeter, "
                   "no infill, no top solid layers and no support material. You can still set "
                   "any number of bottom solid layers as well as skirt/brim loops. "
                   "It won't work when printing more than one single object.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("standby_temperature_delta", coInt);
    def->label = L("Temperature variation");
    def->tooltip = L("Temperature difference to be applied when an extruder is not active. "
                   "Enables a full-height \"sacrificial\" skirt on which the nozzles are periodically wiped.");
    def->sidetext = "∆°C";
    def->min = -max_temp;
    def->max = max_temp;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionInt(-5));

    def = this->add("start_gcode", coString);
    def->label = L("Start G-code");
    def->category = OptionCategory::customgcode;
    def->tooltip = L("This start procedure is inserted at the beginning, after bed has reached "
                   "the target temperature and extruder just started heating, and before extruder "
                   "has finished heating. If Slic3r detects M104 or M190 in your custom codes, "
                   "such commands will not be prepended automatically so you're free to customize "
                   "the order of heating commands and other custom actions. Note that you can use "
                   "placeholder variables for all Slic3r settings, so you can put "
                   "a \"M109 S[first_layer_temperature]\" command wherever you want."
                    "\n placeholders: initial_extruder, total_layer_count, has_wipe_tower, has_single_extruder_multi_material_priming, total_toolchanges, bounding_box[minx,miny,maxx,maxy]");
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionString("G28 ; home all axes\nG1 Z5 F5000 ; lift nozzle\n"));

    def = this->add("start_filament_gcode", coStrings);
    def->label = L("Start G-code");
    def->full_label = ("Filament start G-code");
    def->category = OptionCategory::customgcode;
    def->tooltip = L("This start procedure is inserted at the beginning, after any printer start gcode (and "
                   "after any toolchange to this filament in case of multi-material printers). "
                   "This is used to override settings for a specific filament. If Slic3r detects "
                   "M104, M109, M140 or M190 in your custom codes, such commands will "
                   "not be prepended automatically so you're free to customize the order "
                   "of heating commands and other custom actions. Note that you can use placeholder variables "
                   "for all Slic3r settings, so you can put a \"M109 S[first_layer_temperature]\" command "
                   "wherever you want. If you have multiple extruders, the gcode is processed "
                   "in extruder order.");
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionStrings { "; Filament gcode\n" });

    def = this->add("model_precision", coFloat);
    def->label = L("Model rounding precision");
    def->full_label = L("Model rounding precision");
    def->category = OptionCategory::slicing;
    def->tooltip = L("This is the rounding error of the input object."
        " It's used to align points that should be in the same line."
        " Put 0 to disable.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.0001));

    def = this->add("color_change_gcode", coString);
    def->label = L("Color change G-code");
    def->tooltip = L("This G-code will be used as a code for the color change");
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionString("M600"));

    def = this->add("pause_print_gcode", coString);
    def->label = L("Pause Print G-code");
    def->tooltip = L("This G-code will be used as a code for the pause print");
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionString("M601"));

    def = this->add("template_custom_gcode", coString);
    def->label = L("Custom G-code");
    def->tooltip = L("This G-code will be used as a custom code");
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("single_extruder_multi_material", coBool);
    def->label = L("Single Extruder Multi Material");
    def->category = OptionCategory::mmsetup;
    def->tooltip = L("The printer multiplexes filaments into a single hot end.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("single_extruder_multi_material_priming", coBool);
    def->label = L("Prime all printing extruders");
    def->category = OptionCategory::mmsetup;
    def->tooltip = L("If enabled, all printing extruders will be primed at the front edge of the print bed at the start of the print.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("wipe_tower_no_sparse_layers", coBool);
    def->label = L("No sparse layers (EXPERIMENTAL)");
    def->category = OptionCategory::mmsetup;
    def->tooltip = L("If enabled, the wipe tower will not be printed on layers with no toolchanges. "
                     "On layers with a toolchange, extruder will travel downward to print the wipe tower. "
                     "User is responsible for ensuring there is no collision with the print.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("support_material", coBool);
    def->label = L("Generate support material");
    def->category = OptionCategory::support;
    def->tooltip = L("Enable support material generation.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("support_material_auto", coBool);
    def->label = L("Auto generated supports");
    def->category = OptionCategory::support;
    def->tooltip = L("If checked, supports will be generated automatically based on the overhang threshold value."\
                     " If unchecked, supports will be generated inside the \"Support Enforcer\" volumes only.");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("support_material_xy_spacing", coFloatOrPercent);
    def->label = L("XY separation between an object and its support");
    def->category = OptionCategory::support;
    def->tooltip = L("XY separation between an object and its support. If expressed as percentage "
                   "(for example 50%), it will be calculated over external perimeter width.");
    def->sidetext = L("mm or %");
    def->ratio_over = "external_perimeter_extrusion_width";
    def->min = 0;
    def->mode = comAdvanced;
    // Default is half the external perimeter width.
    def->set_default_value(new ConfigOptionFloatOrPercent(50, true));

    def = this->add("support_material_angle", coFloat);
    def->label = L("Pattern angle");
    def->full_label = ("Support pattern angle");
    def->category = OptionCategory::support;
    def->tooltip = L("Use this setting to rotate the support material pattern on the horizontal plane.");
    def->sidetext = L("°");
    def->min = 0;
    def->max = 359;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("support_material_buildplate_only", coBool);
    def->label = L("Support on build plate only");
    def->category = OptionCategory::support;
    def->tooltip = L("Only create support if it lies on a build plate. Don't create support on a print.");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("support_material_contact_distance_type", coEnum);
    def->label = L("Type");
    def->full_label = ("Support contact distance type");
    def->category = OptionCategory::support;
    def->tooltip = L("How to compute the vertical z-distance.\n"
        "From filament: it use the nearest bit of the filament. When a bridge is extruded, it goes below the current plane.\n"
        "From plane: it use the plane-z. Same than 'from filament' if no 'bridge' is extruded.\n"
        "None: No z-offset. Useful for Soluble supports.\n");
    def->enum_keys_map = &ConfigOptionEnum<SupportZDistanceType>::get_enum_values();
    def->enum_values.push_back("filament");
    def->enum_values.push_back("plane");
    def->enum_values.push_back("none");
    def->enum_labels.push_back("From filament");
    def->enum_labels.push_back("From plane");
    def->enum_labels.push_back("None");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<SupportZDistanceType>(zdPlane));

    def = this->add("support_material_contact_distance_top", coFloatOrPercent);
    def->label = L("Top");
    def->full_label = ("Contact distance on top of supports");
    def->category = OptionCategory::support;
    def->tooltip = L("The vertical distance between support material interface and the object"
        "(when the object is printed on top of the support). "
        "Setting this to 0 will also prevent Slic3r from using bridge flow and speed "
        "for the first object layer. Can be a % of the extruding width used for the interface layers.");
    def->ratio_over = "top_infill_extrusion_width";
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->aliases = { "support_material_contact_distance" };
    def->set_default_value(new ConfigOptionFloatOrPercent(0.2, false));

    def = this->add("support_material_contact_distance_bottom", coFloatOrPercent);
    def->label = L("Bottom");
    def->full_label = ("Contact distance under the bottom of supports");
    def->category = OptionCategory::support;
    def->tooltip = L("The vertical distance between object and support material interface"
        "(when the support is printed on top of the object). Can be a % of the extruding width used for the interface layers.");
    def->ratio_over = "top_infill_extrusion_width";
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(0.2,false));

    def = this->add("support_material_enforce_layers", coInt);
    def->label = L("Enforce support for the first");
    def->category = OptionCategory::support;
    def->tooltip = L("Generate support material for the specified number of layers counting from bottom, "
                   "regardless of whether normal support material is enabled or not and regardless "
                   "of any angle threshold. This is useful for getting more adhesion of objects "
                   "having a very thin or poor footprint on the build plate.");
    def->sidetext = L("layers");
    def->full_label = L("Enforce support for the first n layers");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionInt(0));

    def = this->add("support_material_extruder", coInt);
    def->label = L("Support material/raft/skirt extruder");
    def->category = OptionCategory::extruders;
    def->tooltip = L("The extruder to use when printing support material, raft and skirt "
                   "(1+, 0 to use the current extruder to minimize tool changes).");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(1));

    def = this->add("support_material_extrusion_width", coFloatOrPercent);
    def->label = L("Support material");
    def->full_label = L("Support material width");
    def->category = OptionCategory::width;
    def->tooltip = L("Set this to a non-zero value to set a manual extrusion width for support material. "
                   "If left zero, default extrusion width will be used if set, otherwise nozzle diameter will be used. "
                   "If expressed as percentage (for example 110%) it will be computed over nozzle diameter.");
    def->sidetext = L("mm or %");
    def->ratio_over = "nozzle_diameter";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(0, false));

    def = this->add("support_material_interface_contact_loops", coBool);
    def->label = L("Interface loops");
    def->category = OptionCategory::support;
    def->tooltip = L("Cover the top contact layer of the supports with loops. Disabled by default.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("support_material_interface_extruder", coInt);
    def->label = L("Support material/raft interface extruder");
    def->category = OptionCategory::extruders;
    def->tooltip = L("The extruder to use when printing support material interface "
                   "(1+, 0 to use the current extruder to minimize tool changes). This affects raft too.");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(1));

    def = this->add("support_material_interface_layers", coInt);
    def->label = L("Interface layers");
    def->category = OptionCategory::support;
    def->tooltip = L("Number of interface layers to insert between the object(s) and support material.");
    def->sidetext = L("layers");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(3));

    def = this->add("support_material_interface_spacing", coFloat);
    def->label = L("Interface pattern spacing");
    def->category = OptionCategory::support;
    def->tooltip = L("Spacing between interface lines. Set zero to get a solid interface.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("support_material_interface_speed", coFloatOrPercent);
    def->label = L("Interface");
    def->full_label = L("Support interface speed");
    def->category = OptionCategory::support;
    def->tooltip = L("Speed for printing support material interface layers. If expressed as percentage "
                   "(for example 50%) it will be calculated over support material speed.");
    def->sidetext = L("mm/s or %");
    def->ratio_over = "support_material_speed";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(100, true));

    def = this->add("support_material_pattern", coEnum);
    def->label = L("Pattern");
    def->full_label = L("Support pattern");
    def->category = OptionCategory::support;
    def->tooltip = L("Pattern used to generate support material.");
    def->enum_keys_map = &ConfigOptionEnum<SupportMaterialPattern>::get_enum_values();
    def->enum_values.push_back("rectilinear");
    def->enum_values.push_back("rectilinear-grid");
    def->enum_values.push_back("honeycomb");
    def->enum_labels.push_back(L("Rectilinear"));
    def->enum_labels.push_back(L("Rectilinear grid"));
    def->enum_labels.push_back(L("Honeycomb"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<SupportMaterialPattern>(smpRectilinear));

    def = this->add("support_material_interface_pattern", coEnum);
    def->label = L("Pattern");
    def->full_label = L("Support interface pattern");
    def->category = OptionCategory::support;
    def->tooltip = L("Pattern for interface layer.");
    def->enum_keys_map = &ConfigOptionEnum<InfillPattern>::get_enum_values();
    def->enum_values.push_back("rectilinear");
    def->enum_values.push_back("monotonic");
    def->enum_values.push_back("concentric");
    def->enum_values.push_back("concentricgapfill");
    def->enum_values.push_back("hilbertcurve");
    def->enum_values.push_back("sawtooth");
    def->enum_values.push_back("smooth");
    def->enum_labels.push_back(L("Rectilinear"));
    def->enum_labels.push_back(L("Monotonic"));
    def->enum_labels.push_back(L("Concentric"));
    def->enum_labels.push_back(L("Concentric (filled)"));
    def->enum_labels.push_back(L("Hilbert Curve"));
    def->enum_labels.push_back(L("Sawtooth"));
    def->enum_labels.push_back(L("Ironing"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<InfillPattern>(ipRectilinear));
    
    def = this->add("support_material_spacing", coFloat);
    def->label = L("Pattern spacing");
    def->category = OptionCategory::support;
    def->tooltip = L("Spacing between support material lines.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(2.5));

    def = this->add("support_material_speed", coFloat);
    def->label = L("Default");
    def->full_label = L("Support speed");
    def->category = OptionCategory::support;
    def->tooltip = L("Speed for printing support material.");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(60));

    def = this->add("support_material_synchronize_layers", coBool);
    def->label = L("Synchronize with object layers");
    def->category = OptionCategory::support;
    def->tooltip = L("Synchronize support layers with the object print layers. This is useful "
                   "with multi-material printers, where the extruder switch is expensive.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("support_material_threshold", coInt);
    def->label = L("Overhang threshold");
    def->category = OptionCategory::support;
    def->tooltip = L("Support material will not be generated for overhangs whose slope angle "
                   "(90° = vertical) is above the given threshold. In other words, this value "
                   "represent the most horizontal slope (measured from the horizontal plane) "
                   "that you can print without support material. Set to zero for automatic detection "
                   "(recommended).");
    def->sidetext = L("°");
    def->min = 0;
    def->max = 90;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(0));

    def = this->add("support_material_with_sheath", coBool);
    def->label = L("With sheath around the support");
    def->category = OptionCategory::support;
    def->tooltip = L("Add a sheath (a single perimeter line) around the base support. This makes "
                   "the support more reliable, but also more difficult to remove.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("temperature", coInts);
    def->label = L("Other layers");
    def->full_label = L("Temperature");
    def->category = OptionCategory::filament;
    def->tooltip = L("Extruder nozzle temperature for layers after the first one. Set this to zero to disable "
                   "temperature control commands in the output G-code.");
    def->sidetext = L("°C");
    def->full_label = L("Nozzle temperature");
    def->min = 0;
    def->max = max_temp;
    def->set_default_value(new ConfigOptionInts { 200 });

    def = this->add("print_temperature", coInt);
    def->label = L("Temperature");
    def->category = OptionCategory::filament;
    def->tooltip = L("Override the temperature of the extruder. Avoid doing too many changes, it won't stop for cooling/heating. 0 to disable. May only works on Height range modifiers.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionInt(0));

    def = this->add("print_retract_lift", coFloat);
    def->label = L("Z-lift override");
    def->category = OptionCategory::filament;
    def->tooltip = L("Set the new lift-z value for this override. 0 will disable the z-lift. -& to disable. May only works on Height range modifiers.");
    def->sidetext = L("mm");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(-1));

    def = this->add("thin_perimeters", coBool);
    def->label = L("Overlapping external perimeter");
    def->full_label = L("Overlapping external perimeter");
    def->category = OptionCategory::perimeter;
    def->tooltip = L("Allow outermost perimeter to overlap itself to avoid the use of thin walls. Note that their flow isn't adjusted and so it will result in over-extruding and undefined behavior.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("thin_perimeters_all", coBool);
    def->label = L("Overlapping all perimeters");
    def->full_label = L("Overlapping all perimeters");
    def->category = OptionCategory::perimeter;
    def->tooltip = L("Allow all perimeters to overlap, instead of just external ones.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("thin_walls", coBool);
    def->label = L("");
    def->full_label = L("Thin walls");
    def->category = OptionCategory::perimeter;
    def->tooltip = L("Detect single-width walls (parts where two extrusions don't fit and we need "
        "to collapse them into a single trace). If unchecked, slic3r may try to fit perimeters "
        "where it's not possible, creating some overlap leading to over-extrusion.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("thin_walls_min_width", coFloatOrPercent);
    def->label = L("Min width");
    def->full_label = L("Thin walls min width");
    def->category = OptionCategory::perimeter;
    def->tooltip = L("Minimum width for the extrusion to be extruded (widths lower than the nozzle diameter will be over-extruded at the nozzle diameter)."
        " If expressed as percentage (for example 110%) it will be computed over nozzle diameter."
        " The default behavior of slic3r and slic3rPE is with a 33% value. Put 100% to avoid any sort of over-extrusion.");
    def->ratio_over = "nozzle_diameter";
    def->mode = comExpert;
    def->min = 0;
    def->set_default_value(new ConfigOptionFloatOrPercent(33, true));

    def = this->add("thin_walls_overlap", coFloatOrPercent);
    def->label = L("Overlap");
    def->full_label = L("Thin wall overlap");
    def->category = OptionCategory::perimeter;
    def->tooltip = L("Overlap between the thin wall and the perimeters. Can be a % of the external perimeter width (default 50%)");
    def->ratio_over = "external_perimeter_extrusion_width";
    def->mode = comExpert;
    def->min = 0;
    def->set_default_value(new ConfigOptionFloatOrPercent(50, true));

    def = this->add("thin_walls_merge", coBool);
    def->label = L("Merging with perimeters");
    def->full_label = L("Thin wall merge");
    def->category = OptionCategory::perimeter;
    def->tooltip = L("Allow the external perimeter to merge the thin walls in the path."
                    " You can deactivate it if you use thin walls as a custom support, to reduce adhesion a little bit.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("thin_walls_speed", coFloat);
    def->label = L("Thin walls");
    def->full_label = L("Thin walls speed");
    def->category = OptionCategory::speed;
    def->tooltip = L("Speed for thin wall (external extrusion that are alone because the obect is too thin at these places).");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(30));

    def = this->add("threads", coInt);
    def->label = L("Threads");
    def->tooltip = L("Threads are used to parallelize long-running tasks. Optimal threads number "
                   "is slightly above the number of available cores/processors.");
    def->readonly = true;
    def->min = 1;
    {
        int threads = (unsigned int)boost::thread::hardware_concurrency();
        def->set_default_value(new ConfigOptionInt(threads > 0 ? threads : 2));
        def->cli = ConfigOptionDef::nocli;
    }

    def = this->add("time_estimation_compensation", coPercent);
    def->label = L("Time estimation compensation");
    def->category = OptionCategory::firmware;
    def->tooltip = L("This setting allow you to modify the time estiamtion by a % amount. As slic3r only use the marlin algorithm, it's not precise enough if an other firmware is used.");
    def->mode = comAdvanced;
    def->sidetext = L("%");
    def->min = 0;
    def->set_default_value(new ConfigOptionPercent(100));

    def = this->add("toolchange_gcode", coString);
    def->label = L("Tool change G-code");
    def->category = OptionCategory::customgcode;
    def->tooltip = L("This custom code is inserted at every extruder change. If you don't leave this empty, you are "
        "expected to take care of the toolchange yourself - slic3r will not output any other G-code to "
        "change the filament. You can use placeholder variables for all Slic3r settings as well as [previous_extruder] "
        "and [next_extruder], so e.g. the standard toolchange command can be scripted as T[next_extruder].");
    def->multiline = true;
    def->full_width = true;
    def->height = 5;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("tool_name", coStrings);
    def->label = L("Tool name");
    def->category = OptionCategory::extruders;
    def->tooltip = L("Only used for klipper, where you can name the extruder. If not set, will be 'extruderX' with 'X' replaced by the extruder number.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionStrings(""));

    def = this->add("top_infill_extrusion_width", coFloatOrPercent);
    def->label = L("Top solid infill");
    def->category = OptionCategory::width;
    def->tooltip = L("Set this to a non-zero value to set a manual extrusion width for infill for top surfaces. "
                   "You may want to use thinner extrudates to fill all narrow regions and get a smoother finish. "
                   "If left zero, default extrusion width will be used if set, otherwise nozzle diameter will be used. "
                   "If expressed as percentage (for example 110%) it will be computed over nozzle diameter.");
    def->sidetext = L("mm or %");
    def->ratio_over = "nozzle_diameter";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(0, false));

    def = this->add("top_solid_infill_speed", coFloatOrPercent);
    def->label = L("Top solid");
    def->full_label = L("Top solid speed");
    def->category = OptionCategory::speed;
    def->tooltip = L("Speed for printing top solid layers (it only applies to the uppermost "
                   "external layers and not to their internal solid layers). You may want "
                   "to slow down this to get a nicer surface finish. This can be expressed "
                   "as a percentage (for example: 80%) over the solid infill speed above. "
                   "Set to zero for auto.");
    def->sidetext = L("mm/s or %");
    def->ratio_over = "solid_infill_speed";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(15, false));

    def = this->add("top_solid_layers", coInt);
    //TRN To be shown in Print Settings "Top solid layers"
    def->label = L("Top");
    def->full_label = L("Top layers");
    def->category = OptionCategory::perimeter;
    def->tooltip = L("Number of solid layers to generate on top surfaces.");
    def->full_label = L("Top solid layers");
    def->min = 0;
    def->set_default_value(new ConfigOptionInt(3));

    def = this->add("top_solid_min_thickness", coFloat);
    //TRN To be shown in Print Settings "Top solid layers"
    def->label = L("Top");
    def->category = OptionCategory::perimeter;
    def->tooltip = L("The number of top solid layers is increased above top_solid_layers if necessary to satisfy "
                     "minimum thickness of top shell."
                     " This is useful to prevent pillowing effect when printing with variable layer height.");
    def->full_label = L("Minimum top shell thickness");
    def->sidetext = L("mm");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(0.));

    def = this->add("travel_speed", coFloat);
    def->label = L("Travel");
    def->full_label = L("Travel speed");
    def->category = OptionCategory::speed;
    def->tooltip = L("Speed for travel moves (jumps between distant extrusion points).");
    def->sidetext = L("mm/s");
    def->aliases = { "travel_feed_rate" };
    def->min = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(130));

    def = this->add("use_firmware_retraction", coBool);
    def->label = L("Use firmware retraction");
    def->category = OptionCategory::general;
    def->tooltip = L("This experimental setting uses G10 and G11 commands to have the firmware "
                   "handle the retraction. This is only supported in recent Marlin.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("use_relative_e_distances", coBool);
    def->label = L("Use relative E distances");
    def->category = OptionCategory::general;
    def->tooltip = L("If your firmware requires relative E values, check this, "
                   "otherwise leave it unchecked. Most firmwares use absolute values.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("use_volumetric_e", coBool);
    def->label = L("Use volumetric E");
    def->category = OptionCategory::general;
    def->tooltip = L("This experimental setting uses outputs the E values in cubic millimeters "
                   "instead of linear millimeters. If your firmware doesn't already know "
                   "filament diameter(s), you can put commands like 'M200 D[filament_diameter_0] T0' "
                   "in your start G-code in order to turn volumetric mode on and use the filament "
                   "diameter associated to the filament selected in Slic3r. This is only supported "
                   "in recent Marlin.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("variable_layer_height", coBool);
    def->label = L("Enable variable layer height feature");
    def->category = OptionCategory::general;
    def->tooltip = L("Some printers or printer setups may have difficulties printing "
                   "with a variable layer height. Enabled by default.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("wipe", coBools);
    def->label = L("Wipe while retracting");
    def->category = OptionCategory::general;
    def->tooltip = L("This flag will move the nozzle while retracting to minimize the possible blob "
                   "on leaky extruders.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBools { false });

    def = this->add("wipe_tower", coBool);
    def->label = L("Enable");
    def->full_label = L("Enable wipe tower");
    def->category = OptionCategory::general;
    def->tooltip = L("Multi material printers may need to prime or purge extruders on tool changes. "
                   "Extrude the excess material into the wipe tower.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("wiping_volumes_extruders", coFloats);
    def->label = L("Purging volumes - load/unload volumes");
    def->tooltip = L("This vector saves required volumes to change from/to each tool used on the "
                     "wipe tower. These values are used to simplify creation of the full purging "
                     "volumes below. ");
    def->set_default_value(new ConfigOptionFloats { 70.f, 70.f, 70.f, 70.f, 70.f, 70.f, 70.f, 70.f, 70.f, 70.f  });

    def = this->add("wiping_volumes_matrix", coFloats);
    def->label = L("Purging volumes - matrix");
    def->tooltip = L("This matrix describes volumes (in cubic milimetres) required to purge the"
                     " new filament on the wipe tower for any given pair of tools. ");
    def->set_default_value(new ConfigOptionFloats {   0.f, 140.f, 140.f, 140.f, 140.f,
                                                  140.f,   0.f, 140.f, 140.f, 140.f,
                                                  140.f, 140.f,   0.f, 140.f, 140.f,
                                                  140.f, 140.f, 140.f,   0.f, 140.f,
                                                    140.f, 140.f, 140.f, 140.f,   0.f });


    def = this->add("wipe_advanced", coBool);
    def->label = L("Enable advanced wiping volume");
    def->tooltip = L("Allow slic3r to compute the purge volume via smart computations. Use the pigment% of each filament and following parameters");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("wipe_advanced_nozzle_melted_volume", coFloat);
    def->label = L("Nozzle volume");
    def->tooltip = L("The volume of melted plastic inside your nozlle. Used by 'advanced wiping'.");
    def->sidetext = L("mm3");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(120));

    def = this->add("filament_wipe_advanced_pigment", coFloats);
    def->label = L("Pigment percentage");
    def->tooltip = L("The pigment % for this filament (bewteen 0 and 1, 1=100%). 0 for translucent/natural, 0.2-0.5 for white and 1 for black.");
    def->mode = comExpert;
    def->min = 0;
    def->max = 1;
    def->set_default_value(new ConfigOptionFloats{ 0.5 });

    def = this->add("wipe_advanced_multiplier", coFloat);
    def->label = L("Multiplier");
    def->full_label = L("Auto-wipe multiplier");
    def->tooltip = L("The volume multiplier used to compute the final volume to extrude by the algorithm.");
    def->sidetext = L("mm3");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(60));


    def = this->add("wipe_advanced_algo", coEnum);
    def->label = L("Algorithm");
    def->full_label = L("Auto-wipe algorithm");
    def->tooltip = L("Algo for the advanced wipe.\n"
        "Linear : volume = nozzle + volume_mult * (pigmentBefore-pigmentAfter)\n"
        "Quadratic: volume = nozzle + volume_mult * (pigmentBefore-pigmentAfter)+ volume_mult * (pigmentBefore-pigmentAfter)^3\n"
        "Hyperbola: volume = nozzle + volume_mult * (0.5+pigmentBefore) / (0.5+pigmentAfter)");
    def->enum_keys_map = &ConfigOptionEnum<WipeAlgo>::get_enum_values();
    def->enum_values.push_back("linear");
    def->enum_values.push_back("quadra");
    def->enum_values.push_back("expo");
    def->enum_labels.push_back(L("Linear"));
    def->enum_labels.push_back(L("Quadratric"));
    def->enum_labels.push_back(L("Hyperbola"));
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionEnum<WipeAlgo>(waLinear));

    def = this->add("wipe_tower_brim", coFloatOrPercent);
    def->label = L("Wipe tower brim width");
    def->tooltip = L("Width of the brim for the wipe tower. Can be in mm of in % of the (assumed) only one nozzle diameter.");
    def->ratio_over = "nozzle_diameter";
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(150,true));

    def = this->add("wipe_tower_x", coFloat);
    def->label = L("X");
    def->full_label = L("Wipe tower X");
    def->tooltip = L("X coordinate of the left front corner of a wipe tower");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(180.));

    def = this->add("wipe_tower_y", coFloat);
    def->label = L("Y");
    def->full_label = L("Wipe tower Y");
    def->tooltip = L("Y coordinate of the left front corner of a wipe tower");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(140.));

    def = this->add("wipe_tower_width", coFloat);
    def->label = L("Width");
    def->full_label = L("Wipe tower Width");
    def->tooltip = L("Width of a wipe tower");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(60.));

    def = this->add("wipe_tower_rotation_angle", coFloat);
    def->label = L("Wipe tower rotation angle");
    def->tooltip = L("Wipe tower rotation angle with respect to x-axis.");
    def->sidetext = L("°");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.));

    def = this->add("wipe_into_infill", coBool);
    def->category = OptionCategory::wipe;
    def->label = L("Wipe into this object's infill");
    def->tooltip = L("Purging after toolchange will done inside this object's infills. "
                     "This lowers the amount of waste but may result in longer print time "
                     " due to additional travel moves.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("wipe_into_objects", coBool);
    def->category = OptionCategory::wipe;
    def->label = L("Wipe into this object");
    def->tooltip = L("Object will be used to purge the nozzle after a toolchange to save material "
        "that would otherwise end up in the wipe tower and decrease print time. "
        "Colours of the objects will be mixed as a result.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("wipe_extra_perimeter", coFloats);
    def->category = OptionCategory::extruders;
    def->label = L("Extra Wipe for external perimeters");
    def->tooltip = L("When the external perimeter loop extrusion end, a wipe is done, going a bit inside the print."
        " The number put in this setting increase the wipe by moving the nozzle again along the loop before the final wipe.");
    def->min = 0;
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats{ 0.f });

    def = this->add("wipe_tower_bridging", coFloat);
    def->label = L("Maximal bridging distance");
    def->tooltip = L("Maximal distance between supports on sparse infill sections. ");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(10.));

    def = this->add("xy_size_compensation", coFloat);
    def->label = L("Outer");
    def->full_label = L("Outer XY size compensation");
    def->category = OptionCategory::slicing;
    def->tooltip = L("The object will be grown/shrunk in the XY plane by the configured value "
        "(negative = inwards, positive = outwards). This might be useful for fine-tuning sizes."
        "\nThis one only applies to the 'exterior' shell of the object."
        "\n !!! it's recommended you put the same value into the 'Inner XY size compensation', unless you are sure you don't have horizontal holes. !!! ");
    def->sidetext = L("mm");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("xy_inner_size_compensation", coFloat);
    def->label = L("Inner");
    def->full_label = L("Inner XY size compensation");
    def->category = OptionCategory::slicing;
    def->tooltip = L("The object will be grown/shrunk in the XY plane by the configured value "
        "(negative = inwards, positive = outwards). This might be useful for fine-tuning sizes."
        "\nThis one only applies to the 'inner' shell of the object (!!! horizontal holes break the shell !!!)");
    def->sidetext = L("mm");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("hole_size_compensation", coFloat);
    def->label = L("XY compensation");
    def->full_label = L("XY holes compensation");
    def->category = OptionCategory::slicing;
    def->tooltip = L("The convex holes will be grown / shrunk in the XY plane by the configured value"
        " (negative = inwards, positive = outwards, should be negative as the holes are always a bit smaller irl)."
        " This might be useful for fine-tuning hole sizes."
        "\nThis setting behave the same as 'Inner XY size compensation' but only for convex shapes. It's added to 'Inner XY size compensation', it does not replace it. ");
    def->sidetext = L("mm");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("hole_size_threshold", coFloat);
    def->label = L("Threshold");
    def->full_label = L("XY holes threshold");
    def->category = OptionCategory::slicing;
    def->tooltip = L("Maximum area for the hole where the hole_size_compensation will apply fully."
            " After that, it will decraese down to 0 for four time this area."
            " Set to 0 to let the hole_size_compensation apply fully for all detected holes");
    def->sidetext = L("mm²");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(100));

    def = this->add("hole_to_polyhole", coBool);
    def->label = L("Convert round holes to polyholes");
    def->full_label = L("Convert round holes to polyholes");
    def->category = OptionCategory::slicing;
    def->tooltip = L("Search for almost-circular holes that span more than one layer and convert the geometry to polyholes."
        " Use the nozzle size and the (biggest) diameter to compute the polyhole."
        "\nSee http://hydraraptor.blogspot.com/2011/02/polyholes.html");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("z_offset", coFloat);
    def->label = L("Z offset");
    def->category = OptionCategory::general;
    def->tooltip = L("This value will be added (or subtracted) from all the Z coordinates "
                   "in the output G-code. It is used to compensate for bad Z endstop position: "
                   "for example, if your endstop zero actually leaves the nozzle 0.3mm far "
                   "from the print bed, set this to -0.3 (or fix your endstop).");
    def->sidetext = L("mm");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("z_step", coFloat);
    def->label = L("Z full step");
    def->tooltip = L("Set this to the height moved when your Z motor (or equivalent) turns one step."
                    "If your motor needs 200 steps to move your head/plater by 1mm, this field have to be 1/200 = 0.005."
                    "\nNote that the gcode will write the z values with 6 digits after the dot if z_step is set (it's 3 digits if it's disabled)."
                    "\nPut 0 to disable.");
    def->cli = "z-step=f";
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0.005));

    // Declare retract values for filament profile, overriding the printer's extruder profile.
    for (const char *opt_key : {
        // floats
        "retract_length", "retract_lift", "retract_lift_above", "retract_lift_below", "retract_speed", "deretract_speed", "retract_restart_extra", "retract_before_travel",
        "wipe_extra_perimeter",
        // bools
        "retract_layer_change", "wipe",
        // percents
        "retract_before_wipe"}) {
        auto it_opt = options.find(opt_key);
        assert(it_opt != options.end());
        def = this->add_nullable(std::string("filament_") + opt_key, it_opt->second.type);
        def->label         = it_opt->second.label;
        def->full_label = it_opt->second.full_label;
        def->tooltip     = it_opt->second.tooltip;
        def->sidetext   = it_opt->second.sidetext;
        def->mode       = it_opt->second.mode;
        switch (def->type) {
        case coFloats   : def->set_default_value(new ConfigOptionFloatsNullable  (static_cast<const ConfigOptionFloats*  >(it_opt->second.default_value.get())->values)); break;
        case coPercents : def->set_default_value(new ConfigOptionPercentsNullable(static_cast<const ConfigOptionPercents*>(it_opt->second.default_value.get())->values)); break;
        case coBools    : def->set_default_value(new ConfigOptionBoolsNullable   (static_cast<const ConfigOptionBools*   >(it_opt->second.default_value.get())->values)); break;
        default: assert(false);
        }
    }
}

void PrintConfigDef::init_extruder_option_keys()
{
    // ConfigOptionFloats, ConfigOptionPercents, ConfigOptionBools, ConfigOptionStrings
    m_extruder_option_keys = {
        "nozzle_diameter",
        "min_layer_height",
        "max_layer_height",
        "extruder_offset",
        "extruder_fan_offset",
        "extruder_temperature_offset",
        "tool_name",
        "retract_length",
        "retract_lift",
        "retract_lift_above",
        "retract_lift_below",
        "retract_lift_first_layer",
        "retract_lift_top",
        "retract_speed",
        "deretract_speed",
        "retract_before_wipe",
        "retract_restart_extra",
        "retract_before_travel",
        "wipe",
		"wipe_extra_perimeter",
        "retract_layer_change",
        "retract_length_toolchange",
        "retract_restart_extra_toolchange",
        "extruder_colour",
        "default_filament_profile"
    };

    m_extruder_retract_keys = {
        "deretract_speed",
        "retract_before_travel",
        "retract_before_wipe",
        "retract_layer_change",
        "retract_length",
        "retract_lift",
        "retract_lift_above",
        "retract_lift_below",
        "retract_restart_extra",
        "retract_speed",
        "wipe",
        "wipe_extra_perimeter"
    };
    assert(std::is_sorted(m_extruder_retract_keys.begin(), m_extruder_retract_keys.end()));
}

void PrintConfigDef::init_milling_params()
{
    // ConfigOptionFloats, ConfigOptionPercents, ConfigOptionBools, ConfigOptionStrings
    m_milling_option_keys = {
        "milling_diameter",
        "milling_toolchange_end_gcode",
        "milling_toolchange_start_gcode",
        //"milling_offset",
        //"milling_z_offset",
        "milling_z_lift",

    };

    ConfigOptionDef* def;

    // Milling Printer settings

    def = this->add("milling_cutter", coInt);
    def->gui_type = "i_enum_open";
    def->label = L("Milling cutter");
    def->category = OptionCategory::general;
    def->tooltip = L("The milling cutter to use (unless more specific extruder settings are specified). ");
    def->min = 0;  // 0 = inherit defaults
    def->enum_labels.push_back("default");  // override label for item 0
    def->enum_labels.push_back("1");
    def->enum_labels.push_back("2");
    def->enum_labels.push_back("3");
    def->enum_labels.push_back("4");
    def->enum_labels.push_back("5");
    def->enum_labels.push_back("6");
    def->enum_labels.push_back("7");
    def->enum_labels.push_back("8");
    def->enum_labels.push_back("9");

    def = this->add("milling_diameter", coFloats);
    def->label = L("Milling diameter");
    def->category = OptionCategory::milling_extruders;
    def->tooltip = L("This is the diameter of your cutting tool.");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats(3.14));

    def = this->add("milling_offset", coPoints);
    def->label = L("Tool offset");
    def->category = OptionCategory::extruders;
    def->tooltip = L("If your firmware doesn't handle the extruder displacement you need the G-code "
        "to take it into account. This option lets you specify the displacement of each extruder "
        "with respect to the first one. It expects positive coordinates (they will be subtracted "
        "from the XY coordinate).");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPoints( Vec2d(0,0) ));

    def = this->add("milling_z_offset", coFloats);
    def->label = L("Tool z offset");
    def->category = OptionCategory::extruders;
    def->tooltip = L(".");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats(0));

    def = this->add("milling_z_lift", coFloats);
    def->label = L("Tool z lift");
    def->category = OptionCategory::extruders;
    def->tooltip = L("Amount of lift for travel.");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats(2));

    def = this->add("milling_toolchange_start_gcode", coStrings);
    def->label = L("G-Code to switch to this toolhead");
    def->category = OptionCategory::milling_extruders;
    def->tooltip = L("Put here the gcode to change the toolhead (called after the g-code T[next_extruder]). You have access to [next_extruder] and [previous_extruder]."
        " next_extruder is the 'extruder number' of the new milling tool, it's equal to the index (begining at 0) of the milling tool plus the number of extruders."
        " previous_extruder is the 'extruder number' of the previous tool, it may be a normal extruder, if it's below the number of extruders."
        " The number of extruder is available at [extruder] and the number of milling tool is available at [milling_cutter].");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionStrings(""));

    def = this->add("milling_toolchange_end_gcode", coStrings);
    def->label = L("G-Code to switch from this toolhead");
    def->category = OptionCategory::milling_extruders;
    def->tooltip = L("Put here the gcode to end the toolhead action, like stopping the spindle. You have access to [next_extruder] and [previous_extruder]."
        " previous_extruder is the 'extruder number' of the current milling tool, it's equal to the index (begining at 0) of the milling tool plus the number of extruders."
        " next_extruder is the 'extruder number' of the next tool, it may be a normal extruder, if it's below the number of extruders."
        " The number of extruder is available at [extruder]and the number of milling tool is available at [milling_cutter].");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionStrings(""));

    def = this->add("milling_post_process", coBool);
    def->label = L("Milling post-processing");
    def->category = OptionCategory::milling;
    def->tooltip = L("If activated, at the end of each layer, the printer will switch to a milling head and mill the external perimeters."
        "\nYou should set the 'Milling extra XY size' to a value high enough to have enough plastic to mill. Also, be sure that your piece is firmly glued to the bed.");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("milling_extra_size", coFloatOrPercent);
    def->label = L("Milling extra XY size");
    def->category = OptionCategory::milling;
    def->tooltip = L("This increase the size of the object by a certain amount to have enough plastic to mill."
        " You can set a number of mm or a percentage of the calculated optimal extra width (from flow calculation).");
    def->sidetext = L("mm or %");
    def->ratio_over = "computed_on_the_fly";
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(150, true));

    def = this->add("milling_after_z", coFloatOrPercent);
    def->label = L("Milling only after");
    def->category = OptionCategory::milling;
    def->tooltip = L("This setting restrict the post-process milling to a certain height, to avoid milling the bed. It can be a mm of a % of the first layer height (so it can depends of the object).");
    def->sidetext = L("mm or %");
    def->ratio_over = "first_layer_height";
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(200, true));

    def = this->add("milling_speed", coFloat);
    def->label = L("Milling Speed");
    def->category = OptionCategory::milling;
    def->tooltip = L("Speed for milling tool.");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(30));
}

void PrintConfigDef::init_sla_params()
{
    ConfigOptionDef* def;

    // SLA Printer settings

    def = this->add("display_width", coFloat);
    def->label = L("Display width");
    def->tooltip = L("Width of the display");
    def->min = 1;
    def->set_default_value(new ConfigOptionFloat(120.));

    def = this->add("display_height", coFloat);
    def->label = L("Display height");
    def->tooltip = L("Height of the display");
    def->min = 1;
    def->set_default_value(new ConfigOptionFloat(68.));

    def = this->add("display_pixels_x", coInt);
    def->full_label = L("Number of pixels in");
    def->label = ("X");
    def->tooltip = L("Number of pixels in X");
    def->min = 100;
    def->set_default_value(new ConfigOptionInt(2560));

    def = this->add("display_pixels_y", coInt);
    def->label = ("Y");
    def->tooltip = L("Number of pixels in Y");
    def->min = 100;
    def->set_default_value(new ConfigOptionInt(1440));

    def = this->add("display_mirror_x", coBool);
    def->full_label = L("Display horizontal mirroring");
    def->label = L("Mirror horizontally");
    def->tooltip = L("Enable horizontal mirroring of output images");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("display_mirror_y", coBool);
    def->full_label = L("Display vertical mirroring");
    def->label = L("Mirror vertically");
    def->tooltip = L("Enable vertical mirroring of output images");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("display_orientation", coEnum);
    def->label = L("Display orientation");
    def->tooltip = L("Set the actual LCD display orientation inside the SLA printer."
                     " Portrait mode will flip the meaning of display width and height parameters"
                     " and the output images will be rotated by 90 degrees.");
    def->enum_keys_map = &ConfigOptionEnum<SLADisplayOrientation>::get_enum_values();
    def->enum_values.push_back("landscape");
    def->enum_values.push_back("portrait");
    def->enum_labels.push_back(L("Landscape"));
    def->enum_labels.push_back(L("Portrait"));
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionEnum<SLADisplayOrientation>(sladoPortrait));

    def = this->add("fast_tilt_time", coFloat);
    def->label = L("Fast");
    def->full_label = L("Fast tilt");
    def->tooltip = L("Time of the fast tilt");
    def->sidetext = L("s");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(5.));

    def = this->add("slow_tilt_time", coFloat);
    def->label = L("Slow");
    def->full_label = L("Slow tilt");
    def->tooltip = L("Time of the slow tilt");
    def->sidetext = L("s");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(8.));

    def = this->add("area_fill", coFloat);
    def->label = L("Area fill");
    def->tooltip = L("The percentage of the bed area. \nIf the print area exceeds the specified value, \nthen a slow tilt will be used, otherwise - a fast tilt");
    def->sidetext = L("%");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(50.));

    def = this->add("relative_correction", coFloats);
    def->label = L("Printer scaling correction");
    def->full_label = L("Printer scaling correction");
    def->tooltip  = L("Printer scaling correction");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloats( { 1., 1. } ));

    def = this->add("absolute_correction", coFloat);
    def->label = L("Printer absolute correction");
    def->full_label = L("Printer absolute correction");
    def->tooltip  = L("Will inflate or deflate the sliced 2D polygons according "
                      "to the sign of the correction.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0.0));
    
    def = this->add("elephant_foot_min_width", coFloat);
    def->label = L("minimum width");
    def->category = OptionCategory::slicing;
    def->tooltip = L("Minimum width of features to maintain when doing the first layer compensation.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.2));

    def = this->add("gamma_correction", coFloat);
    def->label = L("Printer gamma correction");
    def->full_label = L("Printer gamma correction");
    def->tooltip  = L("This will apply a gamma correction to the rasterized 2D "
                      "polygons. A gamma value of zero means thresholding with "
                      "the threshold in the middle. This behaviour eliminates "
                      "antialiasing without losing holes in polygons.");
    def->min = 0;
    def->max = 1;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(1.0));


    // SLA Material settings.
    def = this->add("material_type", coString);
    def->label = L("SLA material type");
    def->tooltip = L("SLA material type");
    def->gui_type = "f_enum_open";   // TODO: ???
    def->gui_flags = "show_value";
    def->enum_values.push_back("Tough");
    def->enum_values.push_back("Flexible");
    def->enum_values.push_back("Casting");
    def->enum_values.push_back("Dental");
    def->enum_values.push_back("Heat-resistant");
    def->set_default_value(new ConfigOptionString("Tough"));

    def = this->add("initial_layer_height", coFloat);
    def->label = L("Initial layer height");
    def->tooltip = L("Initial layer height");
    def->sidetext = L("mm");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(0.3));

    def = this->add("bottle_volume", coFloat);
    def->label = L("Bottle volume");
    def->tooltip = L("Bottle volume");
    def->sidetext = L("ml");
    def->min = 50;
    def->set_default_value(new ConfigOptionFloat(1000.0));

    def = this->add("bottle_weight", coFloat);
    def->label = L("Bottle weight");
    def->tooltip = L("Bottle weight");
    def->sidetext = L("kg");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(1.0));

    def = this->add("material_density", coFloat);
    def->label = L("Density");
    def->tooltip = L("Density");
    def->sidetext = L("g/ml");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(1.0));

    def = this->add("bottle_cost", coFloat);
    def->label = L("Cost");
    def->tooltip = L("Cost");
    def->sidetext = L("money/bottle");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(0.0));

    def = this->add("faded_layers", coInt);
    def->label = L("Faded layers");
    def->tooltip = L("Number of the layers needed for the exposure time fade from initial exposure time to the exposure time");
    def->min = 3;
    def->max = 20;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionInt(10));

    def = this->add("min_exposure_time", coFloat);
    def->label = L("Minimum exposure time");
    def->tooltip = L("Minimum exposure time");
    def->sidetext = L("s");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("max_exposure_time", coFloat);
    def->label = L("Maximum exposure time");
    def->tooltip = L("Maximum exposure time");
    def->sidetext = L("s");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(100));

    def = this->add("exposure_time", coFloat);
    def->label = L("Exposure time");
    def->tooltip = L("Exposure time");
    def->sidetext = L("s");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(10));

    def = this->add("min_initial_exposure_time", coFloat);
    def->label = L("Minimum initial exposure time");
    def->tooltip = L("Minimum initial exposure time");
    def->sidetext = L("s");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("max_initial_exposure_time", coFloat);
    def->label = L("Maximum initial exposure time");
    def->tooltip = L("Maximum initial exposure time");
    def->sidetext = L("s");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(150));

    def = this->add("initial_exposure_time", coFloat);
    def->label = L("Initial exposure time");
    def->tooltip = L("Initial exposure time");
    def->sidetext = L("s");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(15));

    def = this->add("material_correction", coFloats);
    def->full_label = L("Correction for expansion");
    def->tooltip  = L("Correction for expansion");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloats( { 1. , 1. } ));

    def = this->add("material_notes", coString);
    def->label = L("SLA print material notes");
    def->tooltip = L("You can put your notes regarding the SLA print material here.");
    def->multiline = true;
    def->full_width = true;
    def->height = 13;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("material_vendor", coString);
    def->set_default_value(new ConfigOptionString(L("(Unknown)")));
    def->cli = ConfigOptionDef::nocli;

    def = this->add("default_sla_material_profile", coString);
    def->label = L("Default SLA material profile");
    def->tooltip = L("Default print profile associated with the current printer profile. "
                   "On selection of the current printer profile, this print profile will be activated.");
    def->set_default_value(new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("sla_material_settings_id", coString);
    def->set_default_value(new ConfigOptionString(""));
    def->cli = ConfigOptionDef::nocli;

    def = this->add("default_sla_print_profile", coString);
    def->label = L("Default SLA material profile");
    def->tooltip = L("Default print profile associated with the current printer profile. "
                   "On selection of the current printer profile, this print profile will be activated.");
    def->set_default_value(new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("sla_print_settings_id", coString);
    def->set_default_value(new ConfigOptionString(""));
    def->cli = ConfigOptionDef::nocli;

    def = this->add("supports_enable", coBool);
    def->label = L("Generate supports");
    def->category = OptionCategory::support;
    def->tooltip = L("Generate supports for the models");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("support_head_front_diameter", coFloat);
    def->label = L("Pinhead front diameter");
    def->category = OptionCategory::support;
    def->tooltip = L("Diameter of the pointing side of the head");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.4));

    def = this->add("support_head_penetration", coFloat);
    def->label = L("Head penetration");
    def->category = OptionCategory::support;
    def->tooltip = L("How much the pinhead has to penetrate the model surface");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(0.2));

    def = this->add("support_head_width", coFloat);
    def->label = L("Pinhead width");
    def->category = OptionCategory::support;
    def->tooltip = L("Width from the back sphere center to the front sphere center");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 20;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1.0));

    def = this->add("support_pillar_diameter", coFloat);
    def->label = L("Pillar diameter");
    def->category = OptionCategory::support;
    def->tooltip = L("Diameter in mm of the support pillars");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 15;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloat(1.0));

    def = this->add("support_small_pillar_diameter_percent", coPercent);
    def->label = L("Small pillar diameter percent");
    def->category = OptionCategory::support;
    def->tooltip = L("The percentage of smaller pillars compared to the normal pillar diameter "
                     "which are used in problematic areas where a normal pilla cannot fit.");
    def->sidetext = L("%");
    def->min = 1;
    def->max = 100;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionPercent(50));
    
    def = this->add("support_max_bridges_on_pillar", coInt);
    def->label = L("Max bridges on a pillar");
    def->tooltip = L(
        "Maximum number of bridges that can be placed on a pillar. Bridges "
        "hold support point pinheads and connect to pillars as small branches.");
    def->min = 0;
    def->max = 50;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionInt(3));

    def = this->add("support_pillar_connection_mode", coEnum);
    def->label = L("Pillar connection mode");
    def->tooltip = L("Controls the bridge type between two neighboring pillars."
                     " Can be zig-zag, cross (double zig-zag) or dynamic which"
                     " will automatically switch between the first two depending"
                     " on the distance of the two pillars.");
    def->enum_keys_map = &ConfigOptionEnum<SLAPillarConnectionMode>::get_enum_values();
    def->enum_values.push_back("zigzag");
    def->enum_values.push_back("cross");
    def->enum_values.push_back("dynamic");
    def->enum_labels.push_back(L("Zig-Zag"));
    def->enum_labels.push_back(L("Cross"));
    def->enum_labels.push_back(L("Dynamic"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<SLAPillarConnectionMode>(slapcmDynamic));

    def = this->add("support_buildplate_only", coBool);
    def->label = L("Support on build plate only");
    def->category = OptionCategory::support;
    def->tooltip = L("Only create support if it lies on a build plate. Don't create support on a print.");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("support_pillar_widening_factor", coFloat);
    def->label = L("Pillar widening factor");
    def->category = OptionCategory::support;
    def->tooltip = L("Merging bridges or pillars into another pillars can "
                     "increase the radius. Zero means no increase, one means "
                     "full increase.");
    def->min = 0;
    def->max = 1;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0.0));

    def = this->add("support_base_diameter", coFloat);
    def->label = L("Support base diameter");
    def->category = OptionCategory::support;
    def->tooltip = L("Diameter in mm of the pillar base");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 30;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(4.0));

    def = this->add("support_base_height", coFloat);
    def->label = L("Support base height");
    def->category = OptionCategory::support;
    def->tooltip = L("The height of the pillar base cone");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1.0));

    def = this->add("support_base_safety_distance", coFloat);
    def->label = L("Support base safety distance");
    def->category = OptionCategory::support;
    def->tooltip  = L(
        "The minimum distance of the pillar base from the model in mm. "
        "Makes sense in zero elevation mode where a gap according "
        "to this parameter is inserted between the model and the pad.");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 10;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(1));

    def = this->add("support_critical_angle", coFloat);
    def->label = L("Critical angle");
    def->category = OptionCategory::support;
    def->tooltip = L("The default angle for connecting support sticks and junctions.");
    def->sidetext = L("°");
    def->min = 0;
    def->max = 90;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(45));

    def = this->add("support_max_bridge_length", coFloat);
    def->label = L("Max bridge length");
    def->category = OptionCategory::support;
    def->tooltip = L("The max length of a bridge");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(15.0));

    def = this->add("support_max_pillar_link_distance", coFloat);
    def->label = L("Max pillar linking distance");
    def->category = OptionCategory::support;
    def->tooltip = L("The max distance of two pillars to get linked with each other."
                     " A zero value will prohibit pillar cascading.");
    def->sidetext = L("mm");
    def->min = 0;   // 0 means no linking
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(10.0));

    def = this->add("support_object_elevation", coFloat);
    def->label = L("Object elevation");
    def->category = OptionCategory::support;
    def->tooltip = L("How much the supports should lift up the supported object. "
                     "If this value is zero, the bottom of the model geometry "
                     "will be considered as part of the pad."
                     "If \"Pad around object\" is enabled, this value is ignored.");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 150; // This is the max height of print on SL1
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(5.0));

    def = this->add("support_points_density_relative", coInt);
    def->label = L("Support points density");
    def->category = OptionCategory::support;
    def->tooltip = L("This is a relative measure of support points density.");
    def->sidetext = L("%");
    def->min = 0;
    def->set_default_value(new ConfigOptionInt(100));

    def = this->add("support_points_minimal_distance", coFloat);
    def->label = L("Minimal distance of the support points");
    def->category = OptionCategory::support;
    def->tooltip = L("No support points will be placed closer than this threshold.");
    def->sidetext = L("mm");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(1.f));

    def = this->add("pad_enable", coBool);
    def->label = L("Use pad");
    def->category = OptionCategory::pad;
    def->tooltip = L("Add a pad underneath the supported model");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("pad_wall_thickness", coFloat);
    def->label = L("Pad wall thickness");
    def->category = OptionCategory::pad;
     def->tooltip = L("The thickness of the pad and its optional cavity walls.");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 30;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloat(2.0));

    def = this->add("pad_wall_height", coFloat);
    def->label = L("Pad wall height");
    def->tooltip = L("Defines the pad cavity depth. Set to zero to disable the cavity. "
                     "Be careful when enabling this feature, as some resins may "
                     "produce an extreme suction effect inside the cavity, "
                     "which makes peeling the print off the vat foil difficult.");
    def->category = OptionCategory::pad;
//     def->tooltip = L("");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 30;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0.));
    
    def = this->add("pad_brim_size", coFloat);
    def->label = L("Pad brim size");
    def->tooltip = L("How far should the pad extend around the contained geometry");
    def->category = OptionCategory::pad;
    //     def->tooltip = L("");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 30;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1.6));

    def = this->add("pad_max_merge_distance", coFloat);
    def->label = L("Max merge distance");
    def->category = OptionCategory::pad;
     def->tooltip = L("Some objects can get along with a few smaller pads "
                      "instead of a single big one. This parameter defines "
                      "how far the center of two smaller pads should be. If they"
                      "are closer, they will get merged into one pad.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(50.0));

    // This is disabled on the UI. I hope it will never be enabled.
//    def = this->add("pad_edge_radius", coFloat);
//    def->label = L("Pad edge radius");
//    def->category = OptionCategory::pad;
////     def->tooltip = L("");
//    def->sidetext = L("mm");
//    def->min = 0;
//    def->mode = comAdvanced;
//    def->set_default_value(new ConfigOptionFloat(1.0));

    def = this->add("pad_wall_slope", coFloat);
    def->label = L("Pad wall slope");
    def->category = OptionCategory::pad;
    def->tooltip = L("The slope of the pad wall relative to the bed plane. "
                     "90 degrees means straight walls.");
    def->sidetext = L("°");
    def->min = 45;
    def->max = 90;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(90.0));

    def = this->add("pad_around_object", coBool);
    def->label = L("Pad around object");
    def->category = OptionCategory::pad;
    def->tooltip = L("Create pad around object and ignore the support elevation");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(false));
    
    def = this->add("pad_around_object_everywhere", coBool);
    def->label = L("Pad around object everywhere");
    def->category = OptionCategory::pad;
    def->tooltip = L("Force pad around object everywhere");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("pad_object_gap", coFloat);
    def->label = L("Pad object gap");
    def->category = OptionCategory::pad;
    def->tooltip  = L("The gap between the object bottom and the generated "
                      "pad in zero elevation mode.");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 10;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(1));

    def = this->add("pad_object_connector_stride", coFloat);
    def->label = L("Pad object connector stride");
    def->category = OptionCategory::pad;
    def->tooltip = L("Distance between two connector sticks which connect the object and the generated pad.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(10));

    def = this->add("pad_object_connector_width", coFloat);
    def->label = L("Pad object connector width");
    def->category = OptionCategory::pad;
    def->tooltip  = L("Width of the connector sticks which connect the object and the generated pad.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0.5));

    def = this->add("pad_object_connector_penetration", coFloat);
    def->label = L("Pad object connector penetration");
    def->category = OptionCategory::pad;
    def->tooltip  = L(
        "How much should the tiny connectors penetrate into the model body.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0.3));
    
    def = this->add("hollowing_enable", coBool);
    def->label = L("Enable hollowing");
    def->category = OptionCategory::hollowing;
    def->tooltip = L("Hollow out a model to have an empty interior");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(false));
    
    def = this->add("hollowing_min_thickness", coFloat);
    def->label = L("Wall thickness");
    def->category = OptionCategory::hollowing;
    def->tooltip  = L("Minimum wall thickness of a hollowed model.");
    def->sidetext = L("mm");
    def->min = 1;
    def->max = 10;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloat(3.));
    
    def = this->add("hollowing_quality", coFloat);
    def->label = L("Accuracy");
    def->category = OptionCategory::hollowing;
    def->tooltip  = L("Performance vs accuracy of calculation. Lower values may produce unwanted artifacts.");
    def->min = 0;
    def->max = 1;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0.5));
    
    def = this->add("hollowing_closing_distance", coFloat);
    def->label = L("Closing distance");
    def->category = OptionCategory::hollowing;
    def->tooltip  = L(
        "Hollowing is done in two steps: first, an imaginary interior is "
        "calculated deeper (offset plus the closing distance) in the object and "
        "then it's inflated back to the specified offset. A greater closing "
        "distance makes the interior more rounded. At zero, the interior will "
        "resemble the exterior the most.");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 10;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(2.0));
}

void PrintConfigDef::handle_legacy(t_config_option_key &opt_key, std::string &value)
{
    // handle legacy options
    if (opt_key == "extrusion_width_ratio" || opt_key == "bottom_layer_speed_ratio"
        || opt_key == "first_layer_height_ratio") {
        boost::replace_first(opt_key, "_ratio", "");
        if (opt_key == "bottom_layer_speed") opt_key = "first_layer_speed";
        try {
            float v = boost::lexical_cast<float>(value);
            if (v != 0)
                value = boost::lexical_cast<std::string>(v*100) + "%";
        } catch (boost::bad_lexical_cast &) {
            value = "0";
        }
    } else if (opt_key == "gcode_flavor" && value == "makerbot") {
        value = "makerware";
    } else if (opt_key == "fill_density" && value.find("%") == std::string::npos) {
        try {
            // fill_density was turned into a percent value
            float v = boost::lexical_cast<float>(value);
            value = boost::lexical_cast<std::string>(v*100) + "%";
        } catch (boost::bad_lexical_cast &) {}
    } else if (opt_key == "randomize_start" && value == "1") {
        opt_key = "seam_position";
        value = "random";
    } else if (opt_key == "bed_size" && !value.empty()) {
        opt_key = "bed_shape";
        ConfigOptionPoint p;
        p.deserialize(value);
        std::ostringstream oss;
        oss << "0x0," << p.value(0) << "x0," << p.value(0) << "x" << p.value(1) << ",0x" << p.value(1);
        value = oss.str();
    } else if ((opt_key == "perimeter_acceleration" && value == "25")
        || (opt_key == "infill_acceleration" && value == "50")) {
        /*  For historical reasons, the world's full of configs having these very low values;
            to avoid unexpected behavior we need to ignore them. Banning these two hard-coded
            values is a dirty hack and will need to be removed sometime in the future, but it
            will avoid lots of complaints for now. */
        value = "0";
    } else if (opt_key == "support_material_pattern" && value == "pillars") {
        // Slic3r PE does not support the pillars. They never worked well.
        value = "rectilinear";
    } else if (opt_key == "skirt_height" && value == "-1") {
    	// PrusaSlicer no more accepts skirt_height == -1 to print a draft shield to the top of the highest object.
    	// A new "draft_shield" boolean config value is used instead.
    	opt_key = "draft_shield";
    	value = "1";
    } else if (opt_key == "octoprint_host") {
        opt_key = "print_host";
    } else if (opt_key == "octoprint_cafile") {
        opt_key = "printhost_cafile";
    } else if (opt_key == "octoprint_apikey") {
        opt_key = "printhost_apikey";
    } else if (opt_key == "elefant_foot_compensation") {
        opt_key = "first_layer_size_compensation";
        float v = boost::lexical_cast<float>(value);
        if (v > 0)
            value = boost::lexical_cast<std::string>(-v);
    } else if (opt_key == "thumbnails") {
        if (value.empty())
            value = "0x0,0x0";
    } else if (opt_key == "z_steps_per_mm") {
        opt_key = "z_step";
        float v = boost::lexical_cast<float>(value);
        value = boost::lexical_cast<std::string>(1/v);
    } else if (opt_key == "infill_not_connected") {
        opt_key = "infill_connection";
        if (value == "1")
            value = "notconnected";
        else
            value = "connected";
    } else if (opt_key == "seam_travel") {
        if (value == "1") {
            opt_key = "seam_travel_cost";
            value = "200%";
        } else {
            opt_key = "";
        }
    } else if (opt_key == "seam_position") {
        if (value == "hidden") {
            opt_key = "seam_travel_cost";
            value = "20%";
        }
    } else if (opt_key == "overhangs") {
        opt_key = "overhangs_width_speed";
        if (value == "1")
            value = "50%";
        else
            value = "0";
    } else if (opt_key == "print_machine_envelope") {
        opt_key = "machine_limits_usage";
        if (value == "1")
            value = "emit_to_gcode";
        else
            value = "time_estimate_only";
    } else if (opt_key == "retract_lift_not_last_layer") {
        opt_key = "retract_lift_top";
        if (value == "1")
            value = "Not on top";
        else
            value = "All surfaces";
    }

    // Ignore the following obsolete configuration keys:
    static std::set<std::string> ignore = {
        "duplicate_x", "duplicate_y", "gcode_arcs", "multiply_x", "multiply_y",
        "support_material_tool", "acceleration", "adjust_overhang_flow",
        "standby_temperature", "scale", "rotate", "duplicate", "duplicate_grid",
        "start_perimeters_at_concave_points", "start_perimeters_at_non_overhang", "randomize_start",
        "seal_position", "vibration_limit", "bed_size",
        "print_center", "g0", "threads", "pressure_advance", "wipe_tower_per_color_wipe",
#ifndef HAS_PRESSURE_EQUALIZER
        "max_volumetric_extrusion_rate_slope_positive", "max_volumetric_extrusion_rate_slope_negative",
#endif /* HAS_PRESSURE_EQUALIZER */
        "cooling", "serial_port", "serial_speed"
    };

    // In PrusaSlicer 2.3.0-alpha0 the "monotonic" infill was introduced, which was later renamed to "monotonous".
    if (value == "monotonous" && (opt_key == "top_fill_pattern" || opt_key == "bottom_fill_pattern" || opt_key == "fill_pattern" || opt_key == "solid_fill_pattern" || opt_key == "support_material_interface_pattern"))
        value = "monotonic";

    if (ignore.find(opt_key) != ignore.end()) {
        opt_key = "";
        return;
    }

    if (! print_config_def.has(opt_key)) {
        opt_key = "";
        return;
    }
}

void PrintConfigDef::to_prusa(t_config_option_key& opt_key, std::string& value, const DynamicConfig& all_conf) {

    std::unordered_set<std::string> to_remove_keys = {
"thumbnails_color",
"thumbnails_custom_color",
"thumbnails_with_bed",
"thumbnails_with_support",
"allow_empty_layers",
"avoid_crossing_not_first_layer",
"top_fan_speed",
"over_bridge_flow_ratio",
"bridge_overlap",
"bridge_speed_internal",
"brim_inside_holes",
"brim_width_interior",
"brim_ears",
"brim_ears_detection_length",
"brim_ears_max_angle",
"brim_ears_pattern",
"brim_offset",
"chamber_temperature",
"complete_objects_one_skirt",
"complete_objects_sort",
"top_fill_pattern",
"solid_fill_pattern",
"enforce_full_fill_volume",
"external_infill_margin",
"bridged_infill_margin",
"external_perimeter_cut_corners",
"external_perimeter_fan_speed",
"external_perimeter_overlap",
"perimeter_overlap",
"perimeter_bonding",
"external_perimeters_vase",
"external_perimeters_nothole",
"external_perimeters_hole",
"perimeter_loop",
"perimeter_loop_seam",
"extra_perimeters_overhangs",
"extra_perimeters_odd_layers",
"only_one_perimeter_top",
"extruder_temperature_offset",
"extruder_fan_offset",
"print_extrusion_multiplier",
"filament_max_speed",
"filament_max_wipe_tower_speed",
"filament_enable_toolchange_temp",
"filament_use_fast_skinnydip",
"filament_enable_toolchange_part_fan",
"filament_toolchange_part_fan_speed",
"filament_use_skinnydip",
"filament_melt_zone_pause",
"filament_cooling_zone_pause",
"filament_dip_insertion_speed",
"filament_dip_extraction_speed",
"filament_toolchange_temp",
"filament_skinnydip_distance",
"filament_shrink",
"fill_angle_increment",
"fill_top_flow_ratio",
"fill_top_flow_ratio",
"first_layer_flow_ratio",
"fill_smooth_width",
"fill_smooth_distribution",
"first_layer_infill_speed",
"gap_fill",
"gap_fill_min_area",
"gap_fill_overlap",
"infill_dense",
"infill_connection",
"infill_dense_algo",
"feature_gcode",
"exact_last_layer_height",
"fan_speedup_time",
"fan_speedup_overhangs",
"fan_kickstart",
"machine_max_acceleration_travel",
"max_speed_reduction",
"min_length",
"min_width_top_surface",
"printhost_apikey",
"printhost_cafile",
"print_host",
"overhangs_speed",
"overhangs_width_speed",
"overhangs_reverse",
"overhangs_reverse_threshold",
"no_perimeter_unsupported_algo",
"support_material_solid_first_layer",
"print_retract_length",
"retract_lift_first_layer",
"retract_lift_top",
"seam_angle_cost",
"seam_travel_cost",
"skirt_extrusion_width",
"small_perimeter_min_length",
"small_perimeter_max_length",
"curve_smoothing_angle_convex",
"curve_smoothing_angle_concave",
"curve_smoothing_precision",
"curve_smoothing_cutoff_dist",
"model_precision",
"support_material_contact_distance_type",
"support_material_contact_distance_bottom",
"support_material_interface_pattern",
"print_temperature",
"print_retract_lift",
"thin_perimeters",
"thin_perimeters_all",
"thin_walls_min_width",
"thin_walls_overlap",
"thin_walls_merge",
"thin_walls_speed",
"time_estimation_compensation",
"tool_name",
"wipe_advanced",
"wipe_advanced_nozzle_melted_volume",
"filament_wipe_advanced_pigment",
"wipe_advanced_multiplier",
"wipe_advanced_algo",
"wipe_tower_brim",
"wipe_extra_perimeter",
"xy_inner_size_compensation",
"hole_size_compensation",
"hole_size_threshold",
"hole_to_polyhole",
"z_step",
"milling_cutter",
"milling_diameter",
"milling_offset",
"milling_z_offset",
"milling_z_lift",
"milling_toolchange_start_gcode",
"milling_toolchange_end_gcode",
"milling_post_process",
"milling_extra_size",
"milling_after_z",
"milling_speed"

    };
    //looks if it's to be removed, or have to be transformed
    if (to_remove_keys.find(opt_key) != to_remove_keys.end()) {
        opt_key = "";
        value = "";
    } else if (opt_key.find("_pattern") != std::string::npos) {
        if ("smooth" == value || "smoothtriple" == value || "smoothhilbert" == value || "rectiwithperimeter" == value || "scatteredrectilinear" == value || "rectilineargapfill" == value || "sawtooth" == value) {
            value = "rectilinear";
        } else if ( "concentricgapfill" == value) {
            value = "concentric";
        }
    } else if ("seam_position" == opt_key) {
        if ("seam_travel_cost" == value || "near" == value || "hidden" == value) {
            value = "nearest";
        }
    } else if ("first_layer_size_compensation" == opt_key) {
        opt_key = "elefant_foot_compensation";
        if (!value.empty()) {
            if (value[0] == '-') {
                value = value.substr(1);
            } else {
                value = "0";
            }
        }
    } else if ("elefant_foot_min_width" == opt_key) {
        opt_key = "elephant_foot_min_width";
    } else if("first_layer_acceleration" == opt_key || "infill_acceleration" == opt_key || "bridge_acceleration" == opt_key || "default_acceleration" == opt_key || "overhangs_speed" == opt_key || "perimeter_acceleration" == opt_key){
        if (value.find("%") != std::string::npos)
            value = "0";
    } else if ("gap_fill_speed" == opt_key && all_conf.has("gap_fill") && !all_conf.option<ConfigOptionBool>("gap_fill")->value) {
        value = "0";
    } else if ("bridge_flow_ratio" == opt_key && all_conf.has("bridge_flow_ratio")) {
        value = boost::lexical_cast<std::string>(all_conf.option<ConfigOptionPercent>("bridge_flow_ratio")->get_abs_value(1));
    } else if ("overhangs_width" == opt_key) {
        opt_key = "overhangs";
        if (value != "0")
            value = "1";
    } else if ("support_material_contact_distance_top" == opt_key) {
        opt_key = "support_material_contact_distance";
        //default : get the top value or 0.2 if a %
        if (value.find("%") != std::string::npos)
            value = "0.2";
        try { //avoid most useless cheks and multiple corners cases with this try catch
            SupportZDistanceType dist_type = all_conf.option<ConfigOptionEnum<SupportZDistanceType>>("support_material_contact_distance_type")->value;
            if (SupportZDistanceType::zdNone == dist_type) {
                value = "0";
            } else {
                double val = all_conf.option<ConfigOptionFloatOrPercent>("support_material_contact_distance_top")->get_abs_value(all_conf.option<ConfigOptionFloats>("nozzle_diameter")->values.front());
                if (SupportZDistanceType::zdFilament == dist_type) { // not exact but good enough effort
                    val += all_conf.option<ConfigOptionFloats>("nozzle_diameter")->values.front();
                    val -= all_conf.get_abs_value("layer_height");
                }
                value = boost::lexical_cast<std::string>(val);
            }
        }
        catch (...) {
        }
    } else if ("gcode_flavor" == opt_key) {
        if ("reprap" == value)
            value = "reprapfirmware";
        else if ("sprinter" == value)
            value = "reprap";
        else if ("lerdge" == value)
            value = "marlin";
        else if ("klipper" == value)
            value = "reprap";
    }

}

const PrintConfigDef print_config_def;

DynamicPrintConfig DynamicPrintConfig::full_print_config()
{
	return DynamicPrintConfig((const PrintRegionConfig&)FullPrintConfig::defaults());
}

DynamicPrintConfig::DynamicPrintConfig(const StaticPrintConfig& rhs) : DynamicConfig(rhs, rhs.keys_ref())
{
}

DynamicPrintConfig* DynamicPrintConfig::new_from_defaults_keys(const std::vector<std::string> &keys)
{
    auto *out = new DynamicPrintConfig();
    out->apply_only(FullPrintConfig::defaults(), keys);
    return out;
}

/*
double min_object_distance(const ConfigBase &cfg)
{   
    const ConfigOptionEnum<PrinterTechnology> *opt_printer_technology = cfg.option<ConfigOptionEnum<PrinterTechnology>>("printer_technology");
    PrinterTechnology printer_technology = opt_printer_technology ? opt_printer_technology->value : ptUnknown;

    double ret = 0.;

    if (printer_technology == ptSLA)
        ret = 6.;
    else {
        auto ecr_opt = cfg.option<ConfigOptionFloat>("extruder_clearance_radius");
        auto dd_opt  = cfg.option<ConfigOptionFloat>("duplicate_distance");
        auto co_opt  = cfg.option<ConfigOptionBool>("complete_objects");

        if (!ecr_opt || !dd_opt || !co_opt) 
            ret = 0.;
        else {
            // min object distance is max(duplicate_distance, clearance_radius)
            ret = (co_opt->value && ecr_opt->value > dd_opt->value) ?
                      ecr_opt->value : dd_opt->value;
        }
    }

    return ret;
}*/


double PrintConfig::min_object_distance() const
{
    return PrintConfig::min_object_distance(static_cast<const ConfigBase*>(this));
}

double PrintConfig::min_object_distance(const ConfigBase *config, double ref_height /* = 0*/)
{
    const ConfigOptionEnum<PrinterTechnology> *opt_printer_technology = config->option<ConfigOptionEnum<PrinterTechnology>>("printer_technology");
    PrinterTechnology printer_technology = opt_printer_technology ? opt_printer_technology->value : ptUnknown;
    if (printer_technology == ptSLA) return 6.;
    
    const ConfigOptionFloat* dd_opt = config->option<ConfigOptionFloat>("duplicate_distance");
    //test if called from usaslicer::l240 where it's called on an empty config...
    if (dd_opt == nullptr) return 0;

    // /2 becasue we only count the grawing for the current object
    const double duplicate_distance = dd_opt->value / 2;
    double base_dist = duplicate_distance;
    //std::cout << "START min_object_distance =>" << base_dist << "\n";
    const ConfigOptionBool* co_opt = config->option<ConfigOptionBool>("complete_objects");
    if (co_opt && co_opt->value) {
        double brim_dist = 0;
        double skirt_dist = 0;
        try {
            std::vector<double> vals = dynamic_cast<const ConfigOptionFloats*>(config->option("nozzle_diameter"))->values;
            double max_nozzle_diam = 0;
            for (double val : vals) max_nozzle_diam = std::fmax(max_nozzle_diam, val);

            // min object distance is max(duplicate_distance, clearance_radius)
            // /2 becasue we only count the grawing for the current object
		    //add 1 as safety offset.
            double extruder_clearance_radius = config->option("extruder_clearance_radius")->getFloat() / 2 + 1;
            if (extruder_clearance_radius > base_dist) {
                base_dist = extruder_clearance_radius;
            }

            //std::cout << "  min_object_distance add extruder_clearance_radius ("<< extruder_clearance_radius <<") =>" << base_dist << "\n";
            //FIXME: now brim can be per-object, so you ahve to get a different min_object_distance per object
            //add brim width
            const double first_layer_height = config->get_abs_value("first_layer_height");
            if (ref_height <= first_layer_height && ref_height != 0) {
                //FIXME: does not take into account object-defined brim !!! you can crash yoursefl with it
                if (config->option("brim_width")->getFloat() > 0) {
                    brim_dist += config->option("brim_width")->getFloat();
                    //std::cout << "  Set  brim=" << config->option("brim_width")->getFloat() << " => " << brim_dist << "\n";
                }
            } else if (config->option("brim_width")->getFloat() + 1 > base_dist) {
                base_dist = config->option("brim_width")->getFloat();
            }
            //add the skirt
            if (config->option("skirts")->getInt() > 0 && config->option("skirt_height")->getInt() == 1 && ref_height == 0) {
                skirt_dist = config->option("skirt_distance")->getFloat();
                const double first_layer_width = config->get_abs_value("first_layer_extrusion_width");
                Flow flow(first_layer_width, first_layer_height, max_nozzle_diam);
                skirt_dist += first_layer_width + (flow.spacing() * ((double)config->option("skirts")->getInt() - 1));
                base_dist = std::max(base_dist, skirt_dist + 1);
                skirt_dist = 0;
            }else if (config->option("skirts")->getInt() > 0 && config->option("skirt_height")->getInt() > 0 && !config->option("complete_objects_one_skirt")->getBool()) {
                double skirt_height = ((double)config->option("skirt_height")->getInt() - 1) * config->get_abs_value("layer_height") + first_layer_height;
                if (ref_height <= skirt_height) {
                    skirt_dist = config->option("skirt_distance")->getFloat();
                    const double first_layer_width = config->get_abs_value("first_layer_extrusion_width");
                    Flow flow(first_layer_width, first_layer_height, max_nozzle_diam);
                    skirt_dist += first_layer_width + (flow.spacing() * ((double)config->option("skirts")->getInt() - 1));
                    //std::cout << "  Set  skirt_dist=" << config->option("skirt_distance")->getFloat() << " => " << skirt_dist << "\n";
                }
            }
        }
        catch (const std::exception & ex) {
            boost::nowide::cerr << ex.what() << std::endl;
        }
        //std::cout << "END  min_object_distance =>" << (base_dist + std::max(skirt_dist, brim_dist)) << "\n";
        return base_dist + std::max(skirt_dist, brim_dist);
    }
    return base_dist;
}

void DynamicPrintConfig::normalize_fdm()
{
    if (this->has("extruder")) {
        int extruder = this->option("extruder")->getInt();
        this->erase("extruder");
        if (extruder != 0) {
            if (!this->has("infill_extruder"))
                this->option("infill_extruder", true)->setInt(extruder);
            if (!this->has("perimeter_extruder"))
                this->option("perimeter_extruder", true)->setInt(extruder);
            // Don't propagate the current extruder to support.
            // For non-soluble supports, the default "0" extruder means to use the active extruder,
            // for soluble supports one certainly does not want to set the extruder to non-soluble.
            // if (!this->has("support_material_extruder"))
            //     this->option("support_material_extruder", true)->setInt(extruder);
            // if (!this->has("support_material_interface_extruder"))
            //     this->option("support_material_interface_extruder", true)->setInt(extruder);
        }
    }

    if (!this->has("solid_infill_extruder") && this->has("infill_extruder"))
        this->option("solid_infill_extruder", true)->setInt(this->option("infill_extruder")->getInt());

    if (this->has("spiral_vase") && this->opt<ConfigOptionBool>("spiral_vase", true)->value) {
        {
            // this should be actually done only on the spiral layers instead of all
            auto* opt = this->opt<ConfigOptionBools>("retract_layer_change", true);
            opt->values.assign(opt->values.size(), false);  // set all values to false
            // Disable retract on layer change also for filament overrides.
            auto* opt_n = this->opt<ConfigOptionBoolsNullable>("filament_retract_layer_change", true);
            opt_n->values.assign(opt_n->values.size(), false);  // Set all values to false.
        }
        {
            this->opt<ConfigOptionInt>("perimeters", true)->value = 1;
            this->opt<ConfigOptionInt>("top_solid_layers", true)->value = 0;
            this->opt<ConfigOptionPercent>("fill_density", true)->value = 0;
            this->opt<ConfigOptionBool>("support_material", true)->value = false;
            this->opt<ConfigOptionInt>("support_material_enforce_layers")->value = 0;
            this->opt<ConfigOptionBool>("exact_last_layer_height", true)->value = false;
            this->opt<ConfigOptionBool>("ensure_vertical_shell_thickness", true)->value = false;
            this->opt<ConfigOptionBool>("infill_dense", true)->value = false;
            this->opt<ConfigOptionBool>("extra_perimeters", true)->value = false;
        }
    }
}

void DynamicPrintConfig::set_num_extruders(unsigned int num_extruders)
{
    const auto& defaults = FullPrintConfig::defaults();
    for (const std::string& key : print_config_def.extruder_option_keys()) {
        if (key == "default_filament_profile")
            // Don't resize this field, as it is presented to the user at the "Dependencies" page of the Printer profile and we don't want to present
            // empty fields there, if not defined by the system profile.
            continue;
        auto* opt = this->option(key, false);
        assert(opt != nullptr);
        assert(opt->is_vector());
        if (opt != nullptr && opt->is_vector())
            static_cast<ConfigOptionVectorBase*>(opt)->resize(num_extruders, defaults.option(key));
    }
}

void DynamicPrintConfig::set_num_milling(unsigned int num_milling)
{
    const auto& defaults = FullPrintConfig::defaults();
    for (const std::string& key : print_config_def.milling_option_keys()) {
        auto* opt = this->option(key, false);
        assert(opt != nullptr);
        assert(opt->is_vector());
        if (opt != nullptr && opt->is_vector())
            static_cast<ConfigOptionVectorBase*>(opt)->resize(num_milling, defaults.option(key));
    }
}

std::string DynamicPrintConfig::validate()
{
    // Full print config is initialized from the defaults.
    const ConfigOption *opt = this->option("printer_technology", false);
    auto printer_technology = (opt == nullptr) ? ptFFF : static_cast<PrinterTechnology>(dynamic_cast<const ConfigOptionEnumGeneric*>(opt)->value);
    switch (printer_technology) {
    case ptFFF:
    {
        FullPrintConfig fpc;
        fpc.apply(*this, true);
        // Verify this print options through the FullPrintConfig.
        return fpc.validate();
    }
    default:
        //FIXME no validation on SLA data?
        return std::string();
    }
}

//FIXME localize this function.
std::string FullPrintConfig::validate()
{
    // --layer-height
    if (this->get_abs_value("layer_height") <= 0)
        return "Invalid value for --layer-height";
    if (fabs(fmod(this->get_abs_value("layer_height"), SCALING_FACTOR)) > 1e-4)
        return "--layer-height must be a multiple of print resolution";

    // --first-layer-height
    if (this->get_abs_value("first_layer_height") <= 0)
        return "Invalid value for --first-layer-height";

    // --filament-diameter
    for (double fd : this->filament_diameter.values)
        if (fd < 1)
            return "Invalid value for --filament-diameter";

    // --nozzle-diameter
    for (double nd : this->nozzle_diameter.values)
        if (nd < 0.005)
            return "Invalid value for --nozzle-diameter";

    // --perimeters
    if (this->perimeters.value < 0)
        return "Invalid value for --perimeters";

    // --solid-layers
    if (this->top_solid_layers < 0)
        return "Invalid value for --top-solid-layers";
    if (this->bottom_solid_layers < 0)
        return "Invalid value for --bottom-solid-layers";

    if (this->use_firmware_retraction.value &&
        this->gcode_flavor.value != gcfSmoothie &&
        this->gcode_flavor.value != gcfSprinter &&
        this->gcode_flavor.value != gcfRepRap &&
        this->gcode_flavor.value != gcfMarlin &&
        this->gcode_flavor.value != gcfMachinekit &&
        this->gcode_flavor.value != gcfRepetier &&
        this->gcode_flavor.value != gcfKlipper &&
        this->gcode_flavor.value != gcfLerdge)
        return "--use-firmware-retraction is only supported by Marlin, Smoothie, Repetier, Machinekit, Klipper and Lerdge firmware";

    if (this->use_firmware_retraction.value)
        for (unsigned char wipe : this->wipe.values)
             if (wipe)
                return "--use-firmware-retraction is not compatible with --wipe";

    // --gcode-flavor
    if (! print_config_def.get("gcode_flavor")->has_enum_value(this->gcode_flavor.serialize()))
        return "Invalid value for --gcode-flavor";

    // --fill-pattern
    if (! print_config_def.get("fill_pattern")->has_enum_value(this->fill_pattern.serialize()))
        return "Invalid value for --fill-pattern";

    // --top-fill-pattern
    if (!print_config_def.get("top_fill_pattern")->has_enum_value(this->top_fill_pattern.serialize()))
        return "Invalid value for --top-fill-pattern";

    // --bottom-fill-pattern
    if (! print_config_def.get("bottom_fill_pattern")->has_enum_value(this->bottom_fill_pattern.serialize()))
        return "Invalid value for --bottom-fill-pattern";

    // --solid-fill-pattern
    if (!print_config_def.get("solid_fill_pattern")->has_enum_value(this->solid_fill_pattern.serialize()))
        return "Invalid value for --solid-fill-pattern";

    // --brim-ears-pattern
    if (!print_config_def.get("brim_ears_pattern")->has_enum_value(this->brim_ears_pattern.serialize()))
        return "Invalid value for --brim-ears-pattern";

    // --fill-density
    if (fabs(this->fill_density.value - 100.) < EPSILON &&
        (! print_config_def.get("top_fill_pattern")->has_enum_value(this->fill_pattern.serialize())
        || ! print_config_def.get("bottom_fill_pattern")->has_enum_value(this->fill_pattern.serialize())
        ))
        return "The selected fill pattern is not supposed to work at 100% density";

    // --infill-every-layers
    if (this->infill_every_layers < 1)
        return "Invalid value for --infill-every-layers";

    // --skirt-height
    if (this->skirt_height < 0)
        return "Invalid value for --skirt-height";
    
    // extruder clearance
    if (this->extruder_clearance_radius <= 0)
        return "Invalid value for --extruder-clearance-radius";
    if (this->extruder_clearance_height <= 0)
        return "Invalid value for --extruder-clearance-height";

    // --extrusion-multiplier
    for (double em : this->extrusion_multiplier.values)
        if (em <= 0)
            return "Invalid value for --extrusion-multiplier";

    // --default-acceleration
    if ((this->perimeter_acceleration != 0. || this->infill_acceleration != 0. || this->bridge_acceleration != 0. || this->first_layer_acceleration != 0.) &&
        this->default_acceleration == 0.)
        return "Invalid zero value for --default-acceleration when using other acceleration settings";

    // --spiral-vase
    if (this->spiral_vase) {
        // Note that we might want to have more than one perimeter on the bottom
        // solid layers.
        if (this->perimeters > 1)
            return "Can't make more than one perimeter when spiral vase mode is enabled";
        else if (this->perimeters < 1)
            return "Can't make less than one perimeter when spiral vase mode is enabled";
        if (this->fill_density > 0)
            return "Spiral vase mode can only print hollow objects, so you need to set Fill density to 0";
        if (this->top_solid_layers > 0)
            return "Spiral vase mode is not compatible with top solid layers";
        if (this->support_material || this->support_material_enforce_layers > 0)
            return "Spiral vase mode is not compatible with support material";
        if (this->infill_dense)
            return "Spiral vase mode can only print hollow objects and have no top surface, so you don't need any dense infill";
        if (this->extra_perimeters)
            return "Can't make more than one perimeter when spiral vase mode is enabled";
    }

    // extrusion widths
    {
        double max_nozzle_diameter = 0.;
        for (double dmr : this->nozzle_diameter.values)
            max_nozzle_diameter = std::max(max_nozzle_diameter, dmr);
        const char *widths[] = { "external_perimeter", "perimeter", "infill", "solid_infill", "top_infill", "support_material", "first_layer" };
        for (size_t i = 0; i < sizeof(widths) / sizeof(widths[i]); ++ i) {
            std::string key(widths[i]);
            key += "_extrusion_width";
            if (this->get_abs_value(key, max_nozzle_diameter) > 10. * max_nozzle_diameter)
                return std::string("Invalid extrusion width (too large): ") + key;
        }
    }

    // Out of range validation of numeric values.
    for (const std::string &opt_key : this->keys()) {
        const ConfigOption      *opt    = this->optptr(opt_key);
        assert(opt != nullptr);
        const ConfigOptionDef   *optdef = print_config_def.get(opt_key);
        assert(optdef != nullptr);
        bool out_of_range = false;
        switch (opt->type()) {
        case coFloat:
        case coPercent:
        {
            auto *fopt = static_cast<const ConfigOptionFloat*>(opt);
            out_of_range = fopt->value < optdef->min || fopt->value > optdef->max;
            break;
        }
        case coFloatOrPercent:
        {
            auto *fopt = static_cast<const ConfigOptionFloatOrPercent*>(opt);
            out_of_range = fopt->get_abs_value(1) < optdef->min || fopt->get_abs_value(1) > optdef->max;
            break;
        }
        case coPercents:
        case coFloats:
            for (double v : static_cast<const ConfigOptionVector<double>*>(opt)->values)
                if (v < optdef->min || v > optdef->max) {
                    out_of_range = true;
                    break;
                }
            break;
        case coInt:
        {
            auto *iopt = static_cast<const ConfigOptionInt*>(opt);
            out_of_range = iopt->value < optdef->min || iopt->value > optdef->max;
            break;
        }
        case coInts:
            for (int v : static_cast<const ConfigOptionVector<int>*>(opt)->values)
                if (v < optdef->min || v > optdef->max) {
                    out_of_range = true;
                    break;
                }
            break;
        default:;
        }
        if (out_of_range)
            return std::string("Value out of range: " + opt_key);
    }

    // The configuration is valid.
    return "";
}

// Declare the static caches for each StaticPrintConfig derived class.
StaticPrintConfig::StaticCache<class Slic3r::PrintObjectConfig> PrintObjectConfig::s_cache_PrintObjectConfig;
StaticPrintConfig::StaticCache<class Slic3r::PrintRegionConfig> PrintRegionConfig::s_cache_PrintRegionConfig;
StaticPrintConfig::StaticCache<class Slic3r::MachineEnvelopeConfig> MachineEnvelopeConfig::s_cache_MachineEnvelopeConfig;
StaticPrintConfig::StaticCache<class Slic3r::GCodeConfig>       GCodeConfig::s_cache_GCodeConfig;
StaticPrintConfig::StaticCache<class Slic3r::PrintConfig>       PrintConfig::s_cache_PrintConfig;
StaticPrintConfig::StaticCache<class Slic3r::FullPrintConfig>   FullPrintConfig::s_cache_FullPrintConfig;

StaticPrintConfig::StaticCache<class Slic3r::SLAMaterialConfig>     SLAMaterialConfig::s_cache_SLAMaterialConfig;
StaticPrintConfig::StaticCache<class Slic3r::SLAPrintConfig>        SLAPrintConfig::s_cache_SLAPrintConfig;
StaticPrintConfig::StaticCache<class Slic3r::SLAPrintObjectConfig>  SLAPrintObjectConfig::s_cache_SLAPrintObjectConfig;
StaticPrintConfig::StaticCache<class Slic3r::SLAPrinterConfig>      SLAPrinterConfig::s_cache_SLAPrinterConfig;
StaticPrintConfig::StaticCache<class Slic3r::SLAFullPrintConfig>    SLAFullPrintConfig::s_cache_SLAFullPrintConfig;

CLIActionsConfigDef::CLIActionsConfigDef()
{
    ConfigOptionDef* def;

    // Actions:
    def = this->add("export_obj", coBool);
    def->label = L("Export OBJ");
    def->tooltip = L("Export the model(s) as OBJ.");
    def->set_default_value(new ConfigOptionBool(false));

/*
    def = this->add("export_svg", coBool);
    def->label = L("Export SVG");
    def->tooltip = L("Slice the model and export solid slices as SVG.");
    def->set_default_value(new ConfigOptionBool(false);
    def->set_default_value(new ConfigOptionBool(false));
*/

    def = this->add("export_sla", coBool);
    def->label = L("Export SLA");
    def->tooltip = L("Slice the model and export SLA printing layers as PNG.");
    def->cli = "export-sla|sla";
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("export_3mf", coBool);
    def->label = L("Export 3MF");
    def->tooltip = L("Export the model(s) as 3MF.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("export_amf", coBool);
    def->label = L("Export AMF");
    def->tooltip = L("Export the model(s) as AMF.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("export_stl", coBool);
    def->label = L("Export STL");
    def->tooltip = L("Export the model(s) as STL.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("export_gcode", coBool);
    def->label = L("Export G-code");
    def->tooltip = L("Slice the model and export toolpaths as G-code.");
    def->cli = "export-gcode|gcode|g";
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("gcodeviewer", coBool);
    def->label = L("G-code viewer");
    def->tooltip = L("Visualize an already sliced and saved G-code");
    def->cli = "gcodeviewer";
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("slice", coBool);
    def->label = L("Slice");
    def->tooltip = L("Slice the model as FFF or SLA based on the printer_technology configuration value.");
    def->cli = "slice|s";
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("help", coBool);
    def->label = L("Help");
    def->tooltip = L("Show this help.");
    def->cli = "help|h";
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("help_fff", coBool);
    def->label = L("Help (FFF options)");
    def->tooltip = L("Show the full list of print/G-code configuration options.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("help_sla", coBool);
    def->label = L("Help (SLA options)");
    def->tooltip = L("Show the full list of SLA print configuration options.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("info", coBool);
    def->label = L("Output Model Info");
    def->tooltip = L("Write information about the model to the console.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("save", coString);
    def->label = L("Save config file");
    def->tooltip = L("Save configuration to the specified file.");
    def->set_default_value(new ConfigOptionString());
}

CLITransformConfigDef::CLITransformConfigDef()
{
    ConfigOptionDef* def;

    // Transform options:
    def = this->add("align_xy", coPoint);
    def->label = L("Align XY");
    def->tooltip = L("Align the model to the given point.");
    def->set_default_value(new ConfigOptionPoint(Vec2d(100,100)));

    def = this->add("cut", coFloat);
    def->label = L("Cut");
    def->tooltip = L("Cut model at the given Z.");
    def->set_default_value(new ConfigOptionFloat(0));

/*
    def = this->add("cut_grid", coFloat);
    def->label = L("Cut");
    def->tooltip = L("Cut model in the XY plane into tiles of the specified max size.");
    def->set_default_value(new ConfigOptionPoint();
    def->set_default_value(new ConfigOptionPoint());

    def = this->add("cut_x", coFloat);
    def->label = L("Cut");
    def->tooltip = L("Cut model at the given X.");
    def->set_default_value(new ConfigOptionFloat(0);
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("cut_y", coFloat);
    def->label = L("Cut");
    def->tooltip = L("Cut model at the given Y.");
    def->set_default_value(new ConfigOptionFloat(0);
    def->set_default_value(new ConfigOptionFloat(0));
*/

    def = this->add("center", coPoint);
    def->label = L("Center");
    def->tooltip = L("Center the print around the given center.");
    def->set_default_value(new ConfigOptionPoint(Vec2d(100,100)));

    def = this->add("dont_arrange", coBool);
    def->label = L("Don't arrange");
    def->tooltip = L("Do not rearrange the given models before merging and keep their original XY coordinates.");

    def = this->add("duplicate", coInt);
    def->label = L("Duplicate");
    def->tooltip =L("Multiply copies by this factor.");
    def->min = 1;

    def = this->add("duplicate_grid", coPoint);
    def->label = L("Duplicate by grid");
    def->tooltip = L("Multiply copies by creating a grid.");

    def = this->add("merge", coBool);
    def->label = L("Merge");
    def->tooltip = L("Arrange the supplied models in a plate and merge them in a single model in order to perform actions once.");
    def->cli = "merge|m";

    def = this->add("repair", coBool);
    def->label = L("Repair");
    def->tooltip = L("Try to repair any non-manifold meshes (this option is implicitly added whenever we need to slice the model to perform the requested action).");

    def = this->add("rotate", coFloat);
    def->label = L("Rotate");
    def->tooltip = L("Rotation angle around the Z axis in degrees.");
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("rotate_x", coFloat);
    def->label = L("Rotate around X");
    def->tooltip = L("Rotation angle around the X axis in degrees.");
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("rotate_y", coFloat);
    def->label = L("Rotate around Y");
    def->tooltip = L("Rotation angle around the Y axis in degrees.");
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("scale", coFloatOrPercent);
    def->label = L("Scale");
    def->tooltip = L("Scaling factor or percentage.");
    def->set_default_value(new ConfigOptionFloatOrPercent(1, false));

    def = this->add("split", coBool);
    def->label = L("Split");
    def->tooltip = L("Detect unconnected parts in the given model(s) and split them into separate objects.");

    def = this->add("scale_to_fit", coPoint3);
    def->label = L("Scale to Fit");
    def->tooltip = L("Scale to fit the given volume.");
    def->set_default_value(new ConfigOptionPoint3(Vec3d(0,0,0)));
}

CLIMiscConfigDef::CLIMiscConfigDef()
{
    ConfigOptionDef* def;

    def = this->add("ignore_nonexistent_config", coBool);
    def->label = L("Ignore non-existent config files");
    def->tooltip = L("Do not fail if a file supplied to --load does not exist.");

    def = this->add("load", coStrings);
    def->label = L("Load config file");
    def->tooltip = L("Load configuration from the specified file. It can be used more than once to load options from multiple files.");

    def = this->add("output", coString);
    def->label = L("Output File");
    def->tooltip = L("The file where the output will be written (if not specified, it will be based on the input file).");
    def->cli = "output|o";

    def = this->add("single_instance", coBool);
    def->label = L("Single instance mode");
    def->tooltip = L("If enabled, the command line arguments are sent to an existing instance of GUI PrusaSlicer, "
                     "or an existing PrusaSlicer window is activated. "
                     "Overrides the \"single_instance\" configuration value from application preferences.");

/*
    def = this->add("autosave", coString);
    def->label = L("Autosave");
    def->tooltip = L("Automatically export current configuration to the specified file.");
*/

    def = this->add("datadir", coString);
    def->label = L("Data directory");
    def->tooltip = L("Load and store settings at the given directory. This is useful for maintaining different profiles or including configurations from a network storage.");

    def = this->add("loglevel", coInt);
    def->label = L("Logging level");
    def->tooltip = L("Sets logging sensitivity. 0:fatal, 1:error, 2:warning, 3:info, 4:debug, 5:trace\n"
                     "For example. loglevel=2 logs fatal, error and warning level messages.");
    def->min = 0;

#if (defined(_MSC_VER) || defined(__MINGW32__)) && defined(SLIC3R_GUI)
    def = this->add("sw_renderer", coBool);
    def->label = L("Render with a software renderer");
    def->tooltip = L("Render with a software renderer. The bundled MESA software renderer is loaded instead of the default OpenGL driver.");
    def->min = 0;
#endif /* _MSC_VER */
}

const CLIActionsConfigDef    cli_actions_config_def;
const CLITransformConfigDef  cli_transform_config_def;
const CLIMiscConfigDef       cli_misc_config_def;

DynamicPrintAndCLIConfig::PrintAndCLIConfigDef DynamicPrintAndCLIConfig::s_def;

void DynamicPrintAndCLIConfig::handle_legacy(t_config_option_key &opt_key, std::string &value) const
{
    if (cli_actions_config_def  .options.find(opt_key) == cli_actions_config_def  .options.end() &&
        cli_transform_config_def.options.find(opt_key) == cli_transform_config_def.options.end() &&
        cli_misc_config_def     .options.find(opt_key) == cli_misc_config_def     .options.end()) {
        PrintConfigDef::handle_legacy(opt_key, value);
    }
}

uint64_t ModelConfig::s_last_timestamp = 1;

static Points to_points(const std::vector<Vec2d> &dpts)
{
    Points pts; pts.reserve(dpts.size());
    for (auto &v : dpts)
        pts.emplace_back( coord_t(scale_(v.x())), coord_t(scale_(v.y())) );
    return pts;    
}

Points get_bed_shape(const DynamicPrintConfig &config)
{
    const auto *bed_shape_opt = config.opt<ConfigOptionPoints>("bed_shape");
    if (!bed_shape_opt) {
        
        // Here, it is certain that the bed shape is missing, so an infinite one
        // has to be used, but still, the center of bed can be queried
        if (auto center_opt = config.opt<ConfigOptionPoint>("center"))
            return { scaled(center_opt->value) };
        
        return {};
    }
    
    return to_points(bed_shape_opt->values);
}

Points get_bed_shape(const PrintConfig &cfg)
{
    return to_points(cfg.bed_shape.values);
}

Points get_bed_shape(const SLAPrinterConfig &cfg) { return to_points(cfg.bed_shape.values); }

} // namespace Slic3r

#include <cereal/types/polymorphic.hpp>
CEREAL_REGISTER_TYPE(Slic3r::DynamicPrintConfig)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::DynamicConfig, Slic3r::DynamicPrintConfig)
