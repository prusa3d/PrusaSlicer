#include "libslic3r/GCode/GCodeWriterConfig.hpp"
#include "Slic3r/Domain/GCodeFlavor.hpp"
#include "Slic3r/Domain/ConfigDefsFDM.hpp"

namespace Slic3r::Biz::Slicing {

using Domain::GCodeFlavor;
using Domain::MachineLimitsUsage;

static std::string get_extrusion_axis(const PrintConfigView& cfg)
{
    using Domain::GCodeFlavor;
    return ((cfg.get<GCodeFlavor>("gcode_flavor") == GCodeFlavor::gcfMach3)
            || (cfg.get<GCodeFlavor>("gcode_flavor") == GCodeFlavor::gcfMachinekit)) ?
        "A" :
        (cfg.get<GCodeFlavor>("gcode_flavor") == GCodeFlavor::gcfNoExtrusion) ? "" :
                                                                                "E";
}

GCodeWriterConfig::GCodeWriterConfig(const PrintConfigView& print_config)
{
    extrusion_axis                 = get_extrusion_axis(print_config);
    single_extruder_multi_material = print_config.get<bool>("single_extruder_multi_material");

    gcode_flavor               = print_config.get<GCodeFlavor>("gcode_flavor");
    const bool use_mach_limits = gcode_flavor == GCodeFlavor::gcfMarlinLegacy
        || gcode_flavor == GCodeFlavor::gcfMarlinFirmware
        || gcode_flavor == GCodeFlavor::gcfPrusaFirmwareBuddy
        || gcode_flavor == GCodeFlavor::gcfRepRapFirmware;
    const bool emit_limits = use_mach_limits
        && print_config.get<MachineLimitsUsage>("machine_limits_usage")
            == MachineLimitsUsage::EmitToGCode;

    supports_separate_travel_acceleration =
        (gcode_flavor == GCodeFlavor::gcfRepetier
         || gcode_flavor == GCodeFlavor::gcfMarlinFirmware
         || gcode_flavor == GCodeFlavor::gcfPrusaFirmwareBuddy
         || gcode_flavor == GCodeFlavor::gcfRepRapFirmware);

    max_acceleration        = static_cast<unsigned int>(std::round(
        emit_limits ?
            print_config.get<std::vector<double>>("machine_max_acceleration_extruding").front() :
            0
    ));
    max_travel_acceleration = static_cast<unsigned int>(std::round(
        (emit_limits && supports_separate_travel_acceleration) ?
            print_config.get<std::vector<double>>("machine_max_acceleration_travel").front() :
            0
    ));
    max_junction_deviation  = emit_limits ?
         print_config.get<std::vector<double>>("machine_max_junction_deviation").front() :
         0.;

    gcode_comments           = print_config.get<bool>("gcode_comments");
    use_volumetric_e         = print_config.get<bool>("use_volumetric_e");
    use_relative_e_distances = print_config.get<bool>("use_relative_e_distances");
    use_firmware_retraction = print_config.get<bool>("use_firmware_retraction");
    travel_speed = print_config.get<double>("travel_speed");
    travel_speed_z = print_config.get<double>("travel_speed_z");
    filament_diameter        = print_config.get<std::vector<double>>("filament_diameter");
    filament_density         = print_config.get<std::vector<double>>("filament_density");
    filament_cost            = print_config.get<std::vector<double>>("filament_cost");
    extrusion_multiplier     = print_config.get<std::vector<double>>("extrusion_multiplier");
    retract_before_wipe = print_config.get<std::vector<Domain::Percentage>>("retract_before_wipe");
    retract_length      = print_config.get<std::vector<double>>("retract_length");
    retract_speed       = print_config.get<std::vector<double>>("retract_speed");
    deretract_speed     = print_config.get<std::vector<double>>("deretract_speed");
    retract_restart_extra     = print_config.get<std::vector<double>>("retract_restart_extra");
    retract_length_toolchange = print_config.get<std::vector<double>>("retract_length_toolchange");
    retract_restart_extra_toolchange =
        print_config.get<std::vector<double>>("retract_restart_extra_toolchange");
}
} // namespace Slic3r::Biz::Slicing
