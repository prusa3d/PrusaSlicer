#include <cassert>

#include "libslic3r/Flow.hpp"
#include "libslic3r/Slicing.hpp"
#include "libslic3r/libslic3r.h"

#include "PresetHints.hpp"

#include <wx/intl.h> 

#include "GUI.hpp"
#include "format.hpp"
#include "I18N.hpp"

namespace Slic3r {

#define MIN_BUF_LENGTH  4096
std::string PresetHints::cooling_description(const Preset &preset)
{
    std::string out;
    int     min_fan_speed = preset.config.opt_int("min_fan_speed", 0);
    int     max_fan_speed = preset.config.opt_int("max_fan_speed", 0);
    int     top_fan_speed = preset.config.opt_int("top_fan_speed", 0);
    int     bridge_fan_speed = preset.config.opt_int("bridge_fan_speed", 0);
    int     ext_peri_fan_speed = preset.config.opt_int("external_perimeter_fan_speed", 0);
    int     disable_fan_first_layers = preset.config.opt_int("disable_fan_first_layers", 0);
    int     slowdown_below_layer_time = preset.config.opt_int("slowdown_below_layer_time", 0);
    int     min_print_speed = int(preset.config.opt_float("min_print_speed", 0) + 0.5);
    int     max_speed_reduc = int(preset.config.opt_float("max_speed_reduction", 0));
    int     fan_below_layer_time = preset.config.opt_int("fan_below_layer_time", 0);

    //for the time being, -1 shoudl eb for disabel, but it's 0 from legacy.
    if (top_fan_speed == 0) top_fan_speed = -1;
    if (bridge_fan_speed == 0) bridge_fan_speed = -1;
    if (ext_peri_fan_speed == 0) ext_peri_fan_speed = -1;
    if (top_fan_speed == 1) top_fan_speed = 0;
    if (bridge_fan_speed == 1) bridge_fan_speed = 0;
    if (ext_peri_fan_speed == 1) ext_peri_fan_speed = 0;

    //if (preset.config.opt_bool("cooling", 0)) {
    out = _utf8(L("Fan"));
    if (preset.config.opt_bool("fan_always_on", 0)) {

        out += " " + (boost::format(_utf8(L("will run at %1%%% by default"))) % min_fan_speed).str() ;

        if (ext_peri_fan_speed >= 0 && ext_peri_fan_speed != min_fan_speed) {
            out += ", " + (boost::format(_utf8(L("at %1%%% over external perimeters"))) % ext_peri_fan_speed).str();
        }
        if (top_fan_speed >= 0 && top_fan_speed != min_fan_speed) {
            out += ", " + (boost::format(_utf8(L("at %1%%% over top fill surfaces"))) % top_fan_speed).str();
        }
        if (bridge_fan_speed >= 0 && bridge_fan_speed > min_fan_speed) {
            out += ", " + (boost::format(_utf8(L("at %1%%% over bridges"))) % bridge_fan_speed).str();
        }
        if (disable_fan_first_layers > 1)
            out += ", " + (boost::format(_utf8(L("except for the first %1% layers where the fan is disabled"))) % disable_fan_first_layers).str();
        else if (disable_fan_first_layers == 1)
            out += ", " + _utf8(L("except for the first layer where the fan is disabled"));
        out += ".";
    } else
       out += " " + _utf8(L("will be turned off by default."));


    if (fan_below_layer_time > 0
        && fan_below_layer_time > slowdown_below_layer_time
        && max_fan_speed > min_fan_speed) {
        
        out += (boost::format(_utf8(L("\n\nIf estimated layer time is below ~%1%s, but still greater than ~%2%s, "
            "fan will run at a proportionally increasing speed between %3%%% and %4%%%")))
            % fan_below_layer_time % slowdown_below_layer_time % min_fan_speed % max_fan_speed).str();

        if (ext_peri_fan_speed > max_fan_speed) {
            out += ", " + (boost::format(_utf8(L("at %1%%% over external perimeters"))) % ext_peri_fan_speed).str();
        } else if (ext_peri_fan_speed > min_fan_speed) {
            out += ", " + (boost::format(_utf8(L("at %1%%% over external perimeters"))) % ext_peri_fan_speed).str() + " " + L("if it's above the current computed fan speed value");
        }
        if (top_fan_speed >= 0) {
            out += ", " + (boost::format(_utf8(L("at %1%%% over top fill surfaces"))) % top_fan_speed).str();
        }
        if (bridge_fan_speed > max_fan_speed) {
            out += ", " + (boost::format(_utf8(L("at %1%%% over bridges"))) % bridge_fan_speed).str();
        }else if (bridge_fan_speed > min_fan_speed) {
            out += ", " + (boost::format(_utf8(L("at %1%%% over bridges"))) % bridge_fan_speed).str() + " " + L("if it's above the current computed fan speed value");
        }
        if (disable_fan_first_layers > 1)
            out += " ; " + ((boost::format(_utf8(L("except for the first %1% layers where the fan is disabled"))) % disable_fan_first_layers).str());
        else if (disable_fan_first_layers == 1)
            out += " ; "+ _utf8(L("except for the first layer where the fan is disabled"));
        out += ".";
    }

    if (slowdown_below_layer_time > 0) {

        out += (boost::format(_utf8(L("\n\nIf estimated layer time is below ~%1%s")))
            % slowdown_below_layer_time).str();
        if (max_fan_speed > 0 && max_fan_speed > min_fan_speed) {
            out += " " + (boost::format(_utf8(L("fan will run by default to %1%%%")))
                % max_fan_speed).str();

            if (disable_fan_first_layers > 1)
                out += " (" + (boost::format(_utf8(L("except for the first %1% layers where the fan is disabled"))) % disable_fan_first_layers).str() + ")";
            else if (disable_fan_first_layers == 1)
                out += " (" + _utf8(L("except for the first layer where the fan is disabled")) + ")";

            out += " and";
        }
            
        out += " " + (boost::format(_utf8(L("print speed will be reduced "
            "so that no less than %1%s are spent on that layer"))) % slowdown_below_layer_time).str();
        if(min_print_speed > 0)
            if(max_speed_reduc > 0)
                out += " " + (boost::format(_utf8(L("(however, speed will never be reduced below %1%mm/s or up to %2%%% reduction)")))
                    % min_print_speed % max_speed_reduc).str();
            else
                out += " " + (boost::format(_utf8(L("(however, speed will never be reduced below %1%mm/s)")))
                    % min_print_speed).str();
    }

    //tooltip for Depractaed values
    bridge_fan_speed = preset.config.opt_int("bridge_fan_speed", 0);
    ext_peri_fan_speed = preset.config.opt_int("external_perimeter_fan_speed", 0);
    top_fan_speed = preset.config.opt_int("top_fan_speed", 0);
    if (top_fan_speed == 0)
        out += "\n\n!!! 0 for the Top fan speed is Deprecated, please set it to -1 to disable it !!!";
    if (ext_peri_fan_speed == 0)
        out += "\n\n!!! 0 for the External perimeters fan speed is Deprecated, please set it to -1 to disable it !!!";
    if (bridge_fan_speed == 0)
        out += "\n\n!!! 0 for the Bridge fan speed is Deprecated, please set it to -1 to disable it !!!";

    return out;
}

static const ConfigOptionFloatOrPercent& first_positive(const ConfigOptionFloatOrPercent *v1, const ConfigOptionFloatOrPercent &v2, const ConfigOptionFloatOrPercent &v3)
{
    return (v1 != nullptr && v1->value > 0) ? *v1 : ((v2.value > 0) ? v2 : v3);
}

std::string PresetHints::maximum_volumetric_flow_description(const PresetBundle &preset_bundle)
{
    // Find out, to which nozzle index is the current filament profile assigned.
    int idx_extruder  = 0;
	int num_extruders = (int)preset_bundle.filament_presets.size();
    for (; idx_extruder < num_extruders; ++ idx_extruder)
        if (preset_bundle.filament_presets[idx_extruder] == preset_bundle.filaments.get_selected_preset_name())
            break;
    if (idx_extruder == num_extruders)
        // The current filament preset is not active for any extruder.
        idx_extruder = -1;

    const DynamicPrintConfig &print_config    = preset_bundle.prints   .get_edited_preset().config;
    const DynamicPrintConfig &filament_config = preset_bundle.filaments.get_edited_preset().config;
    const DynamicPrintConfig &printer_config  = preset_bundle.printers .get_edited_preset().config;

    // Current printer values.
    float  nozzle_diameter                  = (float)printer_config.opt_float("nozzle_diameter", idx_extruder);

    // Print config values
    double layer_height                     = print_config.opt_float("layer_height");
    double first_layer_height               = print_config.get_abs_value("first_layer_height", layer_height);
    double support_material_speed           = print_config.opt_float("support_material_speed");
    double support_material_interface_speed = print_config.get_abs_value("support_material_interface_speed", support_material_speed);
    double bridge_speed                     = print_config.opt_float("bridge_speed");
    double bridge_flow_ratio                = print_config.opt_float("bridge_flow_ratio");
    double over_bridge_flow_ratio           = print_config.opt_float("over_bridge_flow_ratio");
    double perimeter_speed                  = print_config.opt_float("perimeter_speed");
    double external_perimeter_speed         = print_config.get_abs_value("external_perimeter_speed", perimeter_speed);
    // double gap_fill_speed                   = print_config.opt_float("gap_fill_speed");
    double infill_speed                     = print_config.opt_float("infill_speed");
    double small_perimeter_speed            = print_config.get_abs_value("small_perimeter_speed", perimeter_speed);
    double solid_infill_speed               = print_config.get_abs_value("solid_infill_speed", infill_speed);
    double top_solid_infill_speed           = print_config.get_abs_value("top_solid_infill_speed", solid_infill_speed);
    // Maximum print speed when auto-speed is enabled by setting any of the above speed values to zero.
    double max_print_speed                  = print_config.opt_float("max_print_speed");
    // Maximum volumetric speed allowed for the print profile.
    double max_volumetric_speed             = print_config.opt_float("max_volumetric_speed");

    const auto &extrusion_width                     = *print_config.option<ConfigOptionFloatOrPercent>("extrusion_width");
    const auto &external_perimeter_extrusion_width  = *print_config.option<ConfigOptionFloatOrPercent>("external_perimeter_extrusion_width");
    const auto &first_layer_extrusion_width         = *print_config.option<ConfigOptionFloatOrPercent>("first_layer_extrusion_width");
    const auto &infill_extrusion_width              = *print_config.option<ConfigOptionFloatOrPercent>("infill_extrusion_width");
    const auto &perimeter_extrusion_width           = *print_config.option<ConfigOptionFloatOrPercent>("perimeter_extrusion_width");
    const auto &solid_infill_extrusion_width        = *print_config.option<ConfigOptionFloatOrPercent>("solid_infill_extrusion_width");
    const auto &support_material_extrusion_width    = *print_config.option<ConfigOptionFloatOrPercent>("support_material_extrusion_width");
    const auto &top_infill_extrusion_width          = *print_config.option<ConfigOptionFloatOrPercent>("top_infill_extrusion_width");
    const auto &first_layer_speed                   = *print_config.option<ConfigOptionFloatOrPercent>("first_layer_speed");
    const auto &first_layer_infill_speed            = *print_config.option<ConfigOptionFloatOrPercent>("first_layer_infill_speed");

    // Index of an extruder assigned to a feature. If set to 0, an active extruder will be used for a multi-material print.
    // If different from idx_extruder, it will not be taken into account for this hint.
    auto feature_extruder_active = [idx_extruder, num_extruders](int i) {
        return i <= 0 || i > num_extruders || idx_extruder == -1 || idx_extruder == i - 1;
    };
    bool perimeter_extruder_active                  = feature_extruder_active(print_config.opt_int("perimeter_extruder"));
    bool infill_extruder_active                     = feature_extruder_active(print_config.opt_int("infill_extruder"));
    bool solid_infill_extruder_active               = feature_extruder_active(print_config.opt_int("solid_infill_extruder"));
    bool support_material_extruder_active           = feature_extruder_active(print_config.opt_int("support_material_extruder"));
    bool support_material_interface_extruder_active = feature_extruder_active(print_config.opt_int("support_material_interface_extruder"));

    // Current filament values
    double filament_diameter                = filament_config.opt_float("filament_diameter", 0);
    double filament_crossection             = M_PI * 0.25 * filament_diameter * filament_diameter;
    // double extrusion_multiplier             = filament_config.opt_float("extrusion_multiplier", 0);
    // The following value will be annotated by this hint, so it does not take part in the calculation.
//    double filament_max_volumetric_speed    = filament_config.opt_float("filament_max_volumetric_speed", 0);

    std::string out;
    for (size_t idx_type = (first_layer_extrusion_width.value == 0) ? 1 : 0; idx_type < 3; ++ idx_type) {
        // First test the maximum volumetric extrusion speed for non-bridging extrusions.
        bool first_layer = idx_type == 0;
        bool bridging    = idx_type == 2;
		const ConfigOptionFloatOrPercent *first_layer_extrusion_width_ptr = (first_layer && first_layer_extrusion_width.value > 0) ?
			&first_layer_extrusion_width : nullptr;
        const float                       lh  = float(first_layer ? first_layer_height : layer_height);
        const float                       bfr = bridging ? bridge_flow_ratio : 0.f;
        double                            max_flow = 0.;
        std::string                       max_flow_extrusion_type;
        auto                              limit_by_first_layer_speed = [&first_layer_speed, first_layer](double speed_normal, double speed_max) {
            if (first_layer && first_layer_speed.value > 0)
                // Apply the first layer limit.
                speed_normal = first_layer_speed.get_abs_value(speed_normal);
            return (speed_normal > 0.) ? speed_normal : speed_max;
        };
        auto                              limit_infill_by_first_layer_speed = [&first_layer_infill_speed, first_layer](double speed_normal, double speed_max) {
            if (first_layer && first_layer_infill_speed.value > 0)
                // Apply the first layer limit.
                speed_normal = first_layer_infill_speed.get_abs_value(speed_normal);
            return (speed_normal > 0.) ? speed_normal : speed_max;
        };
        if (perimeter_extruder_active) {
            double external_perimeter_rate = Flow::new_from_config_width(frExternalPerimeter, 
                first_positive(first_layer_extrusion_width_ptr, external_perimeter_extrusion_width, extrusion_width), 
                nozzle_diameter, lh, bfr).mm3_per_mm() *
                (bridging ? bridge_speed : 
                    limit_by_first_layer_speed(std::max(external_perimeter_speed, small_perimeter_speed), max_print_speed));
            if (max_flow < external_perimeter_rate) {
                max_flow = external_perimeter_rate;
                max_flow_extrusion_type = _utf8(L("external perimeters"));
            }
            double perimeter_rate = Flow::new_from_config_width(frPerimeter, 
                first_positive(first_layer_extrusion_width_ptr, perimeter_extrusion_width, extrusion_width), 
                nozzle_diameter, lh, bfr).mm3_per_mm() *
                (bridging ? bridge_speed :
                    limit_by_first_layer_speed(std::max(perimeter_speed, small_perimeter_speed), max_print_speed));
            if (max_flow < perimeter_rate) {
                max_flow = perimeter_rate;
                max_flow_extrusion_type = _utf8(L("perimeters"));
            }
        }
        if (! bridging && infill_extruder_active) {
            double infill_rate = Flow::new_from_config_width(frInfill, 
                first_positive(first_layer_extrusion_width_ptr, infill_extrusion_width, extrusion_width), 
                nozzle_diameter, lh, bfr).mm3_per_mm() * limit_infill_by_first_layer_speed(infill_speed, max_print_speed);
            if (max_flow < infill_rate) {
                max_flow = infill_rate;
                max_flow_extrusion_type = _utf8(L("infill"));
            }
        }
        if (solid_infill_extruder_active) {
            double solid_infill_rate = Flow::new_from_config_width(frInfill, 
                first_positive(first_layer_extrusion_width_ptr, solid_infill_extrusion_width, extrusion_width), 
                nozzle_diameter, lh, 0).mm3_per_mm() *
                (bridging ? bridge_speed : limit_infill_by_first_layer_speed(solid_infill_speed, max_print_speed));
            if (max_flow < solid_infill_rate) {
                max_flow = solid_infill_rate;
                max_flow_extrusion_type = _utf8(L("solid infill"));
            }
            if (! bridging) {
                double top_solid_infill_rate = Flow::new_from_config_width(frInfill, 
                    first_positive(first_layer_extrusion_width_ptr, top_infill_extrusion_width, extrusion_width), 
                    nozzle_diameter, lh, bfr).mm3_per_mm() * limit_infill_by_first_layer_speed(top_solid_infill_speed, max_print_speed);
                if (max_flow < top_solid_infill_rate) {
                    max_flow = top_solid_infill_rate;
                    max_flow_extrusion_type = _utf8(L("top solid infill"));
                }
            }
        }
        if (support_material_extruder_active) {
            double support_material_rate = Flow::new_from_config_width(frSupportMaterial,
                first_positive(first_layer_extrusion_width_ptr, support_material_extrusion_width, extrusion_width), 
                nozzle_diameter, lh, bfr).mm3_per_mm() *
                (bridging ? bridge_speed : limit_by_first_layer_speed(support_material_speed, max_print_speed));
            if (max_flow < support_material_rate) {
                max_flow = support_material_rate;
                max_flow_extrusion_type = _utf8(L("support"));
            }
        }
        if (support_material_interface_extruder_active) {
            double support_material_interface_rate = Flow::new_from_config_width(frSupportMaterialInterface,
                first_positive(first_layer_extrusion_width_ptr, support_material_extrusion_width, extrusion_width),
                nozzle_diameter, lh, bfr).mm3_per_mm() *
                (bridging ? bridge_speed : limit_by_first_layer_speed(support_material_interface_speed, max_print_speed));
            if (max_flow < support_material_interface_rate) {
                max_flow = support_material_interface_rate;
                max_flow_extrusion_type = _utf8(L("support interface"));
            }
        }
        //FIXME handle gap_fill_speed
        if (! out.empty())
            out += "\n";
        out += (first_layer ? _utf8(L("First layer volumetric")) : (bridging ? _utf8(L("Bridging volumetric")) : _utf8(L("Volumetric"))));
        out += " " + _utf8(L("flow rate is maximized")) + " ";
        bool limited_by_max_volumetric_speed = max_volumetric_speed > 0 && max_volumetric_speed < max_flow;
        out += (limited_by_max_volumetric_speed ? 
            _utf8(L("by the print profile maximum")) :
            (_utf8(L("when printing"))+ " " + max_flow_extrusion_type))
            + " " + _utf8(L("with a volumetric rate"))+ " ";
        if (limited_by_max_volumetric_speed)
            max_flow = max_volumetric_speed;

        out += (boost::format(_utf8(L("%3.2f mm³/s at filament speed %3.2f mm/s."))) % max_flow % (max_flow / filament_crossection)).str();
    }

 	return out;
}

std::string PresetHints::recommended_thin_wall_thickness(const PresetBundle& preset_bundle)
{
    const DynamicPrintConfig& print_config = preset_bundle.prints.get_edited_preset().config;
    const DynamicPrintConfig& printer_config = preset_bundle.printers.get_edited_preset().config;

    float   layer_height = float(print_config.opt_float("layer_height"));
    int     num_perimeters = print_config.opt_int("perimeters");
    bool    thin_walls = print_config.opt_bool("thin_walls");
    float   nozzle_diameter = float(printer_config.opt_float("nozzle_diameter", 0));

    std::string out;
    if (layer_height <= 0.f) {
        out += _utf8(L("Recommended object min thin wall thickness: Not available due to invalid layer height."));
        return out;
    }

    Flow    external_perimeter_flow = Flow::new_from_config_width(
        frExternalPerimeter,
        *print_config.opt<ConfigOptionFloatOrPercent>("external_perimeter_extrusion_width"),
        nozzle_diameter, layer_height, false);
    Flow    perimeter_flow = Flow::new_from_config_width(
        frPerimeter,
        *print_config.opt<ConfigOptionFloatOrPercent>("perimeter_extrusion_width"),
        nozzle_diameter, layer_height, false);

    //set spacing
    external_perimeter_flow.spacing_ratio = print_config.opt<ConfigOptionPercent>("external_perimeter_overlap")->get_abs_value(1);
    perimeter_flow.spacing_ratio = print_config.opt<ConfigOptionPercent>("perimeter_overlap")->get_abs_value(1);

    if (num_perimeters > 0) {
        int num_lines = std::min(num_perimeters, 6);
        out += (boost::format(_utf8(L("Recommended object min (thick) wall thickness for layer height %.2f and"))) % layer_height).str() + " ";
        out += (boost::format(_utf8(L("%d perimeter: %.2f mm"))) % 1 % (external_perimeter_flow.width + external_perimeter_flow.spacing())).str() + " ";
        // Start with the width of two closely spaced 
        try {
            double width = 2 * (external_perimeter_flow.width + external_perimeter_flow.spacing(perimeter_flow));
            for (int i = 2; i <= num_lines; thin_walls ? ++i : i++) {
                out += ", " + (boost::format(_utf8(L("%d perimeter: %.2f mm"))) % i % width).str() + " ";
                width += perimeter_flow.spacing() * 2;
            }
        }
        catch (const FlowErrorNegativeSpacing&) {
            out = _utf8(L("Recommended object thin wall thickness: Not available due to excessively small extrusion width."));
        }
    }
    return out;
}

std::string PresetHints::recommended_extrusion_width(const PresetBundle& preset_bundle)
{
    const DynamicPrintConfig& print_config = preset_bundle.prints.get_edited_preset().config;
    const DynamicPrintConfig& printer_config = preset_bundle.printers.get_edited_preset().config;

    int nb_nozzles = printer_config.option<ConfigOptionFloats>("nozzle_diameter")->values.size();

    double nozzle_diameter = 0;
    for(int i=0; i< nb_nozzles; i++)
        nozzle_diameter = std::max(nozzle_diameter, printer_config.opt_float("nozzle_diameter", i));
    double layer_height = print_config.opt_float("layer_height");
    double first_layer_height = print_config.option<ConfigOptionFloatOrPercent>("first_layer_height")->get_abs_value(nozzle_diameter);

    std::string out;

    Flow first_layer_flow = Flow::new_from_spacing(nozzle_diameter, nozzle_diameter, first_layer_height, false);
    Flow layer_flow = Flow::new_from_spacing(nozzle_diameter, nozzle_diameter, layer_height, false);

    out += _utf8(L("Ideally, the spacing between two extrusions shouldn't be lower than the nozzle diameter. Below are the extrusion widths for a spacing equal to the nozzle diameter.\n"));
    out += (boost::format(_utf8(L("Recommended min extrusion width for the first layer (with a first layer height of %1%) is %2$.3f mm (or %3%%%)\n"))) 
        % first_layer_height % first_layer_flow.width % int(first_layer_flow.width * 100. / nozzle_diameter)).str();
    out += (boost::format(_utf8(L("Recommended min extrusion width for other layers (with a layer height of %1%) is %2$.3f mm (or %3%%%).\n"))) 
        % layer_height % layer_flow.width % int(layer_flow.width * 100. / nozzle_diameter)).str();

    return out;
}


// Produce a textual explanation of the combined effects of the top/bottom_solid_layers
// versus top/bottom_min_shell_thickness. Which of the two values wins depends
// on the active layer height.
std::string PresetHints::top_bottom_shell_thickness_explanation(const PresetBundle &preset_bundle)
{
    const DynamicPrintConfig &print_config    = preset_bundle.prints   .get_edited_preset().config;
    const DynamicPrintConfig &printer_config  = preset_bundle.printers .get_edited_preset().config;

    std::string out;

    int     top_solid_layers                = print_config.opt_int("top_solid_layers");
    int     bottom_solid_layers             = print_config.opt_int("bottom_solid_layers");
    bool    has_top_layers 					= top_solid_layers > 0;
    bool    has_bottom_layers 				= bottom_solid_layers > 0;
    double  top_solid_min_thickness        	= print_config.opt_float("top_solid_min_thickness");
    double  bottom_solid_min_thickness  	= print_config.opt_float("bottom_solid_min_thickness");
    double  layer_height                    = print_config.opt_float("layer_height");
    bool    variable_layer_height			= printer_config.opt_bool("variable_layer_height");
    //FIXME the following line takes into account the 1st extruder only.
    double  min_layer_height				= variable_layer_height ? Slicing::min_layer_height_from_nozzle(printer_config, 1) : layer_height;

	if (layer_height <= 0.f) {
		out += _utf8(L("Top / bottom shell thickness hint: Not available due to invalid layer height."));
		return out;
	}

    if (has_top_layers) {
    	double top_shell_thickness = top_solid_layers * layer_height;
    	if (top_shell_thickness < top_solid_min_thickness) {
    		// top_solid_min_shell_thickness triggers even in case of normal layer height. Round the top_shell_thickness up
    		// to an integer multiply of layer_height.
    		double n = ceil(top_solid_min_thickness / layer_height);
    		top_shell_thickness = n * layer_height;
    	}
    	double top_shell_thickness_minimum = std::max(top_solid_min_thickness, top_solid_layers * min_layer_height);
        out += (boost::format(_utf8(L("Top shell is %1% mm thick for layer height %2% mm."))) % top_shell_thickness % layer_height).str();
        if (variable_layer_height && top_shell_thickness_minimum < top_shell_thickness) {
        	out += " ";
	        out += (boost::format(_utf8(L("Minimum top shell thickness is %1% mm."))) % top_shell_thickness_minimum).str();        	
        }
    } else
        out += _utf8(L("Top is open."));

    out += "\n";

    if (has_bottom_layers) {
    	double bottom_shell_thickness = bottom_solid_layers * layer_height;
    	if (bottom_shell_thickness < bottom_solid_min_thickness) {
    		// bottom_solid_min_shell_thickness triggers even in case of normal layer height. Round the bottom_shell_thickness up
    		// to an integer multiply of layer_height.
    		double n = ceil(bottom_solid_min_thickness / layer_height);
    		bottom_shell_thickness = n * layer_height;
    	}
    	double bottom_shell_thickness_minimum = std::max(bottom_solid_min_thickness, bottom_solid_layers * min_layer_height);
        out += (boost::format(_utf8(L("Bottom shell is %1% mm thick for layer height %2% mm."))) % bottom_shell_thickness % layer_height).str();
        if (variable_layer_height && bottom_shell_thickness_minimum < bottom_shell_thickness) {
        	out += " ";
	        out += (boost::format(_utf8(L("Minimum bottom shell thickness is %1% mm."))) % bottom_shell_thickness_minimum).str();        	
        }
    } else 
        out += _utf8(L("Bottom is open."));

    return out;
}

}; // namespace Slic3r
