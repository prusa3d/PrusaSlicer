#include "Slic3r/Biz/Config/ConfigLegacy.hpp"
#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Log.hpp"

#include "Legacy/PrintConfig.hpp"

#include <boost/algorithm/string.hpp>

namespace Slic3r::Biz {

using Domain::ConfigPack;
using Domain::ConfigPackFDM;
using Domain::ConfigPackSLA;
using Domain::EnumVectorWrapper;
using Domain::EnumWrapper;
using Domain::FloatOrPercentage;
using Domain::Percentage;
using Domain::ConfigLocation;
using Domain::FDMConfigLocation;
using Domain::SLAConfigLocation;
using Domain::ConfigItem;

namespace {
    struct LegacyKeysAndOverrides {
        // Configs were exported flat in PrusaSlicer <= 2.9.2. This is the list of keys we should try to fill in.
        std::vector<std::string> keys;

        // Which keys were overrides of something.
        std::vector<std::string> overrides;

        // Overrides used to be distinguished by a prefix.
        std::string override_prefix;

        // Type of the box where the filament overrides should be put in the new structure
        ConfigLocation override_box_type;

        // Whether this is FDM or SLA.
        std::string printer_technology_str;
    };



    LegacyKeysAndOverrides legacy_fdm_data()
    {
        static LegacyKeysAndOverrides out = {
            {
                "arc_fitting", "autoemit_temperature_commands", "automatic_extrusion_widths", "automatic_infill_combination",
                "automatic_infill_combination_max_layer_height", "avoid_crossing_curled_overhangs", "avoid_crossing_perimeters",
                "avoid_crossing_perimeters_max_detour", "bed_custom_model", "bed_custom_texture", "bed_shape", "bed_temperature",
                "bed_temperature_extruder", "before_layer_gcode", "between_objects_gcode", "binary_gcode", "bottom_fill_pattern",
                "bottom_solid_layers", "bottom_solid_min_thickness", "bridge_acceleration", "bridge_angle", "bridge_fan_speed",
                "bridge_flow_ratio", "bridge_speed", "brim_separation", "brim_type", "brim_width", "chamber_minimal_temperature",
                "chamber_temperature", "color_change_gcode", "colorprint_heights", "compatible_printers_condition_cummulative",
                "complete_objects", "cooling", "cooling_perimeter_transition_distance", "cooling_slowdown_logic",
                "cooling_tube_length", "cooling_tube_retraction", "custom_parameters_print", "custom_parameters_printer", "custom_parameters_filament", "default_acceleration",
                "default_filament_profile", "default_print_profile", "deretract_speed", "disable_fan_first_layers",
                "dont_support_bridges", "draft_shield", "duplicate_distance", "elefant_foot_compensation",
                "enable_dynamic_fan_speeds", "enable_dynamic_overhang_speeds", "enable_pressure_advance_during_ramming", "end_filament_gcode", "end_gcode",
                "ensure_vertical_shell_thickness", "external_perimeter_acceleration", "external_perimeter_extrusion_width",
                "external_perimeter_speed", "external_perimeters_first", "extra_loading_move", "extra_perimeters",
                "extra_perimeters_on_overhangs", "extruder_clearance_height", "extruder_clearance_radius", "extruder_colour",
                "extruder_offset", "extrusion_axis", "extrusion_multiplier", "extrusion_width", "fan_always_on",
                "fan_below_layer_time", "filament_abrasive", "filament_colour", "filament_cooling_final_speed",
                "filament_cooling_initial_speed", "filament_cooling_moves", "filament_cost", "filament_density",
                "filament_deretract_speed", "filament_diameter", "filament_flush_speed", "filament_flush_volume",
                "filament_infill_max_crossing_speed", "filament_infill_max_speed",
                "filament_change_time", "filament_loading_speed", "filament_loading_speed_start", "filament_max_volumetric_speed",
                "filament_minimal_purge_on_wipe_tower", "filament_multitool_ramming", "filament_multitool_ramming_flow",
                "filament_multitool_ramming_volume", "filament_notes", "filament_purge_multiplier", "filament_ramming_parameters",
                "filament_ramming_temperature_delta", "filament_ramming_initial_delay",
                "filament_retract_before_travel", "filament_retract_before_wipe", "filament_retract_layer_change",
                "filament_retract_length", "filament_retract_length_toolchange", "filament_retract_lift",
                "filament_retract_lift_above", "filament_retract_lift_below", "filament_retract_restart_extra",
                "filament_retract_restart_extra_toolchange", "filament_retract_speed", "filament_seam_gap_distance",
                "filament_settings_id", "filament_shrinkage_compensation_xy", "filament_shrinkage_compensation_z",
                "filament_soluble", "filament_spool_weight", "filament_stamping_distance", "filament_stamping_loading_speed",
                "filament_toolchange_delay", "filament_travel_lift_before_obstacle", "filament_travel_max_lift",
                "filament_travel_ramping_lift", "filament_travel_slope", "filament_type",
                "filament_unloading_speed", "filament_unloading_speed_start", "filament_vendor", "filament_wipe", "fill_angle",
                "fill_density", "fill_pattern", "first_layer_acceleration", "first_layer_acceleration_over_raft",
                "first_layer_bed_temperature", "first_layer_extrusion_width", "first_layer_height", "first_layer_infill_speed",
                "first_layer_speed", "first_layer_speed_over_raft", "first_layer_temperature", "full_fan_speed_layer", "fuzzy_skin",
                "fuzzy_skin_point_dist", "fuzzy_skin_thickness", "gap_fill_enabled", "gap_fill_speed", "gcode_comments",
                "gcode_flavor", "gcode_label_objects", "gcode_resolution", "gcode_substitutions", "high_current_on_filament_swap",
                "host_type", "idle_temperature", "infill_acceleration", "infill_anchor", "infill_anchor_max", "infill_every_layers",
                "infill_extruder", "infill_extrusion_width", "infill_first", "infill_overlap", "infill_speed", "interface_shells",
                "interlocking_beam", "interlocking_beam_layer_count", "interlocking_beam_width", "interlocking_boundary_avoidance",
                "interlocking_depth", "interlocking_orientation", "ironing", "ironing_flowrate", "ironing_spacing", "ironing_speed",
                "ironing_type", "layer_gcode", "layer_height", "machine_limits_usage", "machine_max_acceleration_e",
                "machine_max_acceleration_extruding", "machine_max_acceleration_retracting", "machine_max_acceleration_travel",
                "machine_max_acceleration_x", "machine_max_acceleration_y", "machine_max_acceleration_z", "machine_max_feedrate_e",
                "machine_max_feedrate_x", "machine_max_feedrate_y", "machine_max_feedrate_z", "machine_max_jerk_e",
                "machine_max_jerk_x", "machine_max_jerk_y", "machine_max_jerk_z", "machine_max_junction_deviation", "machine_min_extruding_rate",
                "machine_min_travel_rate", "max_fan_speed", "max_layer_height", "max_print_height", "max_print_speed",
                "max_volumetric_extrusion_rate_slope_negative", "max_volumetric_extrusion_rate_slope_positive",
                "max_volumetric_speed", "min_bead_width", "min_fan_speed", "min_feature_size", "min_layer_height", "min_print_speed",
                "min_skirt_length", "mmu_segmented_region_interlocking_depth", "mmu_segmented_region_max_width",
                "multimaterial_purging", "notes", "nozzle_diameter", "nozzle_high_flow", "only_one_perimeter_first_layer",
                "only_retract_when_crossing_perimeters", "ooze_prevention", "output_filename_format", "over_bridge_speed",
                "overhang_fan_speed_0", "overhang_fan_speed_1", "overhang_fan_speed_2", "overhang_fan_speed_3", "overhang_speed_0",
                "overhang_speed_1", "overhang_speed_2", "overhang_speed_3", "overhangs", "parking_pos_retraction",
                "pause_print_gcode", "perimeter_acceleration", "perimeter_extruder", "perimeter_extrusion_width",
                "perimeter_generator", "perimeter_speed", "perimeters", "physical_printer_settings_id", "post_process",
                "prefer_clockwise_movements", "print_settings_id", "printer_model", "printer_notes", "printer_settings_id",
                "printer_technology", "printer_variant", "printer_vendor", "raft_contact_distance", "raft_expansion",
                "raft_first_layer_density", "raft_first_layer_expansion", "raft_layers", "remaining_times", "stuck_filament_detection", "resolution",
                "retract_before_travel", "retract_before_wipe", "retract_layer_change", "retract_length",
                "retract_length_toolchange", "retract_lift", "retract_lift_above", "retract_lift_below", "retract_restart_extra",
                "retract_restart_extra_toolchange", "retract_speed", "scarf_seam_entire_loop", "scarf_seam_length",
                "scarf_seam_max_segment_length", "scarf_seam_on_inner_perimeters", "scarf_seam_only_on_smooth",
                "scarf_seam_placement", "scarf_seam_start_height", "seam_gap_distance", "seam_position", "silent_mode",
                "single_extruder_multi_material", "single_extruder_multi_material_priming", "skirt_distance", "skirt_height",
                "skirts", "slice_closing_radius", "slicing_mode", "slowdown_below_layer_time", "small_perimeter_speed",
                "solid_infill_acceleration", "solid_infill_below_area", "solid_infill_every_layers", "solid_infill_extruder",
                "solid_infill_extrusion_width", "solid_infill_speed", "spiral_vase", "staggered_inner_seams",
                "standby_temperature_delta", "start_filament_gcode", "start_gcode", "support_material", "support_material_angle",
                "support_material_bottom_contact_distance", "support_material_bottom_interface_layers",
                "support_material_buildplate_only", "support_material_closing_radius", "support_material_contact_distance",
                "support_material_enforce_layers", "support_material_extruder", "support_material_extrusion_width",
                "support_material_interface_contact_loops", "support_material_interface_extruder",
                "support_material_interface_layers", "support_material_interface_pattern", "support_material_interface_spacing",
                "support_material_interface_speed", "support_material_pattern", "support_material_spacing", "support_material_speed",
                "support_material_style", "support_material_synchronize_layers", "support_material_threshold",
                "support_material_with_sheath", "support_material_xy_spacing", "support_tree_angle", "support_tree_angle_slow",
                "support_tree_branch_diameter", "support_tree_branch_diameter_angle", "support_tree_branch_diameter_double_wall",
                "support_tree_branch_distance", "support_tree_tip_diameter", "support_tree_top_rate", "temperature",
                "template_custom_gcode", "thick_bridges", "thin_walls", "thumbnails", "thumbnails_format", "toolchange_gcode",
                "toolchange_ordering",
                "top_fill_pattern", "top_infill_extrusion_width", "top_one_perimeter_type", "top_solid_infill_acceleration",
                "top_solid_infill_speed", "top_solid_layers", "top_solid_min_thickness", "travel_acceleration",
                "travel_lift_before_obstacle", "travel_max_lift", "travel_ramping_lift",
                "travel_short_distance_acceleration", "travel_slope", "travel_speed",
                "travel_speed_z", "use_firmware_retraction", "use_relative_e_distances", "use_volumetric_e",
                "variable_layer_height", "wall_distribution_count", "wall_transition_angle", "wall_transition_filter_deviation",
                "wall_transition_length", "wipe", "wipe_into_infill", "wipe_into_objects", "wipe_tower", "wipe_tower_acceleration",
                "wipe_tower_bridging", "wipe_tower_brim_width", "wipe_tower_cone_angle", "wipe_tower_extra_flow",
                "wipe_tower_extra_spacing", "wipe_tower_extruder", "wipe_tower_no_sparse_layers", "wipe_tower_width",
                "wiping_volumes_matrix", "wiping_volumes_use_custom_matrix", "xy_size_compensation", "z_offset"
            },
            {
                // floats
                "filament_retract_length", "filament_retract_lift", "filament_retract_lift_above", "filament_retract_lift_below", "filament_retract_speed",
                "filament_travel_max_lift", "filament_deretract_speed", "filament_retract_restart_extra", "filament_retract_before_travel",
                "filament_retract_length_toolchange", "filament_retract_restart_extra_toolchange",
                // bools
                "filament_retract_layer_change", "filament_wipe", "filament_travel_lift_before_obstacle", "filament_travel_ramping_lift",
                // percents
                "filament_retract_before_wipe", "filament_travel_slope",
                // floatsorpercents
                "filament_seam_gap_distance"
            },
            "filament_",
            FDMConfigLocation::Filament,
            "FFF"
        };
        return out;
    }

    LegacyKeysAndOverrides legacy_sla_data()
    {
        static LegacyKeysAndOverrides out = {
            {"absolute_correction","area_fill","bed_custom_model","bed_custom_texture","bed_shape","bottle_cost","bottle_volume","bottle_weight","branchingsupport_base_diameter",
             "branchingsupport_base_height","branchingsupport_base_safety_distance","branchingsupport_buildplate_only","branchingsupport_critical_angle","branchingsupport_head_front_diameter",
             "branchingsupport_head_penetration","branchingsupport_head_width","branchingsupport_max_bridge_length","branchingsupport_max_bridges_on_pillar","branchingsupport_max_pillar_link_distance",
             "branchingsupport_max_weight_on_model","branchingsupport_object_elevation","branchingsupport_pillar_connection_mode","branchingsupport_pillar_diameter",
             "branchingsupport_pillar_widening_factor","branchingsupport_small_pillar_diameter_percent","compatible_printers_condition_cummulative","default_sla_material_profile",
             "default_sla_print_profile","delay_after_exposure","delay_before_exposure","display_height","display_mirror_x","display_mirror_y","display_orientation","display_pixels_x",
             "display_pixels_y","display_width","elefant_foot_compensation","elefant_foot_min_width","exposure_time","faded_layers","fast_tilt_time","gamma_correction","high_viscosity_tilt_time",
             "hollowing_closing_distance","hollowing_enable","hollowing_min_thickness","hollowing_quality","host_type","initial_exposure_time","initial_layer_height","layer_height",
             "material_colour","material_correction","material_correction_x","material_correction_y","material_correction_z","material_density","material_notes","material_ow_absolute_correction",
             "material_ow_branchingsupport_head_front_diameter","material_ow_branchingsupport_head_penetration","material_ow_branchingsupport_head_width","material_ow_branchingsupport_pillar_diameter",
             "material_ow_elefant_foot_compensation","material_ow_support_head_front_diameter","material_ow_support_head_penetration","material_ow_support_head_width",
             "material_ow_support_pillar_diameter","material_ow_support_points_density_relative","material_print_speed","material_type","material_vendor","max_exposure_time",
             "max_initial_exposure_time","max_print_height","min_exposure_time","min_initial_exposure_time","output_filename_format","pad_around_object","pad_around_object_everywhere",
             "pad_brim_size","pad_enable","pad_max_merge_distance","pad_object_connector_penetration","pad_object_connector_stride","pad_object_connector_width","pad_object_gap",
             "pad_wall_height","pad_wall_slope","pad_wall_thickness","physical_printer_settings_id","print_host","printer_model","printer_notes","printer_settings_id","printer_technology",
             "printer_variant","printer_vendor","printhost_apikey","printhost_cafile","relative_correction","relative_correction_x","relative_correction_y","relative_correction_z",
             "sla_archive_format","sla_material_settings_id","sla_output_precision","sla_print_settings_id","slice_closing_radius","slicing_mode","slow_tilt_time","support_base_diameter",
             "support_base_height","support_base_safety_distance","support_buildplate_only","support_critical_angle","support_enforcers_only","support_head_front_diameter",
             "support_head_penetration","support_head_width","support_max_bridge_length","support_max_bridges_on_pillar","support_max_pillar_link_distance","support_max_weight_on_model","support_object_elevation",
             "support_pillar_connection_mode","support_pillar_diameter","support_pillar_widening_factor","support_points_density_relative","support_small_pillar_diameter_percent",
             "support_tree_type","supports_enable","thumbnails","tilt_down_cycles","tilt_down_delay","tilt_down_finish_speed","tilt_down_initial_speed","tilt_down_offset_delay",
             "tilt_down_offset_steps","tilt_up_cycles","tilt_up_delay","tilt_up_finish_speed","tilt_up_initial_speed","tilt_up_offset_delay","tilt_up_offset_steps","tower_hop_height",
             "tower_speed","use_tilt","zcorrection_layers"
            },
            {
                // float
                "material_ow_support_head_front_diameter", "material_ow_branchingsupport_head_front_diameter", 
                "material_ow_support_head_penetration", "material_ow_branchingsupport_head_penetration", 
                "material_ow_support_head_width", "material_ow_branchingsupport_head_width",
                "material_ow_support_pillar_diameter", "material_ow_branchingsupport_pillar_diameter",
                "material_ow_elefant_foot_compensation", "material_ow_absolute_correction",
                // int
                "material_ow_support_points_density_relative"
            },
            "material_ow_",
            SLAConfigLocation::Material,
            "SLA"
        };
        return out;
    }
}




static void convert_enum(const Slic3rLegacy::ConfigOption* co, Domain::ConfigItem& item)
{
    ASSERT(co->type() == Slic3rLegacy::coEnum && item.holds_alternative<EnumWrapper>());
    const std::string old_str = co->serialize();

    EnumWrapper enum_wrapper{item.get<EnumWrapper>()};

    for (const Domain::EnumValueDef& evd : enum_wrapper.def()) {
        if (evd.str_serialized == old_str) {
            enum_wrapper.set_string(old_str);
            item.set(enum_wrapper);
        }
    }
}

static void convert_enums(const Slic3rLegacy::ConfigOption* co, Domain::ConfigItem& item)
{
    ASSERT(co->type() == Slic3rLegacy::coEnums && item.holds_alternative<EnumVectorWrapper>());
    const std::string old_str = co->serialize();
    std::vector<std::string> old_strs;
    boost::split(old_strs, old_str, boost::is_any_of(","));

    EnumVectorWrapper enum_vector_wrapper{item.get<EnumVectorWrapper>()};

    for (const std::string& str : old_strs)
        if (std::none_of(enum_vector_wrapper.def().begin(), enum_vector_wrapper.def().end(),
            [&str](const Domain::EnumValueDef& evd) { return str == evd.str_serialized; }))
            return;

    enum_vector_wrapper.set_strings(old_strs);
    item.set(enum_vector_wrapper);
}



static Slic3rLegacy::DynamicPrintConfig load_legacy_config_from_legacy_file(const std::string& filename)
{
    Slic3rLegacy::DynamicPrintConfig cfg;
    Slic3rLegacy::ForwardCompatibilitySubstitutionRule substitutions_ctxt = Slic3rLegacy::ForwardCompatibilitySubstitutionRule::EnableSilent;
    cfg.load(filename, substitutions_ctxt);
    return cfg;
}



static bool convert_old_to_new(const Slic3rLegacy::ConfigOption* opt, Domain::ConfigItem& item, int filament_id = -1)
{
    if (opt->type() == Slic3rLegacy::coBool && item.holds_alternative<bool>())
        item.set(opt->getBool());
    else if (opt->type() == Slic3rLegacy::coInt && item.holds_alternative<int>())
        item.set(opt->getInt());
    else if (opt->type() == Slic3rLegacy::coFloat && item.holds_alternative<double>())
        item.set(opt->getFloat());
    else if (opt->type() == Slic3rLegacy::coString && item.holds_alternative<std::string>())
        item.set(static_cast<const Slic3rLegacy::ConfigOptionString*>(opt)->value);
    else if (opt->type() == Slic3rLegacy::coEnum && item.holds_alternative<EnumWrapper>())
        convert_enum(opt, item);
    else if (opt->type() == Slic3rLegacy::coFloatOrPercent && item.holds_alternative<FloatOrPercentage>()) {
        bool is_percent = static_cast<const Slic3rLegacy::ConfigOptionFloatOrPercent*>(opt)->percent;
        double value = static_cast<const Slic3rLegacy::ConfigOptionFloatOrPercent*>(opt)->value;
        Domain::FloatOrPercentage fop = is_percent ? Domain::Percentage{value} : Domain::FloatOrPercentage{value};
        item.set(fop);
    }
    else if (opt->type() == Slic3rLegacy::coPercent && item.holds_alternative<Percentage>())
        item.set(Domain::Percentage{static_cast<const Slic3rLegacy::ConfigOptionPercent*>(opt)->value});
    else if (opt->type() == Slic3rLegacy::coBools && item.holds_alternative<std::vector<bool>>()) {
        std::vector<unsigned char> old_vec = static_cast<const Slic3rLegacy::ConfigOptionBools*>(opt)->values;
        std::vector<bool> vec(old_vec.begin(), old_vec.end());
        item.set(vec);
    }
    else if (opt->type() == Slic3rLegacy::coInts && item.holds_alternative<std::vector<int>>())
        item.set(static_cast<const Slic3rLegacy::ConfigOptionInts*>(opt)->values);
    else if (opt->type() == Slic3rLegacy::coFloats && item.holds_alternative<std::vector<double>>())
        item.set(static_cast<const Slic3rLegacy::ConfigOptionFloats*>(opt)->values);
    else if (opt->type() == Slic3rLegacy::coStrings && item.holds_alternative<std::vector<std::string>>())
        item.set(static_cast<const Slic3rLegacy::ConfigOptionStrings*>(opt)->values);
    else if (opt->type() == Slic3rLegacy::coPoints && item.holds_alternative<std::vector<Domain::Vec2d>>()) {
        const std::vector<Slic3rLegacy::Vec2d> old_vec = static_cast<const Slic3rLegacy::ConfigOptionPoints*>(opt)->values;
        std::vector<Domain::Vec2d> vec(old_vec.begin(), old_vec.end());
        item.set(vec);
    }
    else if (opt->type() == Slic3rLegacy::coEnums && item.holds_alternative<EnumVectorWrapper>()) {
        convert_enums(opt, item);
    }
    else if (opt->is_vector() && filament_id != -1) {
        // This vector actually contains scalar values to be assigned to
        // different print / toolprint settings.
        if (opt->type() == Slic3rLegacy::coBools && item.holds_alternative<bool>())
            item.set(static_cast<const Slic3rLegacy::ConfigOptionBools*>(opt)->get_at(filament_id));
        else if (opt->type() == Slic3rLegacy::coInts && item.holds_alternative<int>())
            item.set(static_cast<const Slic3rLegacy::ConfigOptionInts*>(opt)->get_at(filament_id));
        else if (opt->type() == Slic3rLegacy::coInts && item.holds_alternative<std::optional<int>>()) {
            if (static_cast<const Slic3rLegacy::ConfigOptionInts*>(opt)->is_nil(filament_id))
                item.set(std::optional<int>());
            else
                item.set(std::optional<int>(static_cast<const Slic3rLegacy::ConfigOptionInts*>(opt)->get_at(filament_id)));
        }
        else if (opt->type() == Slic3rLegacy::coFloats && item.holds_alternative<double>())
            item.set(static_cast<const Slic3rLegacy::ConfigOptionFloats*>(opt)->get_at(filament_id));
        else if (opt->type() == Slic3rLegacy::coStrings && item.holds_alternative<std::string>())
            item.set(static_cast<const Slic3rLegacy::ConfigOptionStrings*>(opt)->get_at(filament_id));
        else if (opt->type() == Slic3rLegacy::coPercents && item.holds_alternative<Percentage>())
            item.set(Domain::Percentage{static_cast<const Slic3rLegacy::ConfigOptionPercents*>(opt)->get_at(filament_id)});
        else if (opt->type() == Slic3rLegacy::coFloatsOrPercents && item.holds_alternative<FloatOrPercentage>()) {
            bool is_percent = static_cast<const Slic3rLegacy::ConfigOptionFloatsOrPercents*>(opt)->get_at(filament_id).percent;
            double value = static_cast<const Slic3rLegacy::ConfigOptionFloatsOrPercents*>(opt)->get_at(filament_id).value;
            Domain::FloatOrPercentage fop = is_percent ? Domain::Percentage{value} : Domain::FloatOrPercentage{value};
            item.set(fop);
        }
        else if (opt->type() == Slic3rLegacy::coPoints && item.holds_alternative<Domain::Vec2d>()) {
            item.set(Domain::Vec2d(static_cast<const Slic3rLegacy::ConfigOptionPoints*>(opt)->values[filament_id]));
        } else if (opt->type() == Slic3rLegacy::coEnums && item.holds_alternative<EnumWrapper>()) {
            EnumWrapper enum_wrapper{item.get<EnumWrapper>()};
            enum_wrapper.set_index(
                static_cast<const Slic3rLegacy::ConfigOptionInts*>(opt)->values[filament_id]
            );
            item.set(enum_wrapper);
        } else {
            // Old and new types do not match.
            return false;
        }
    }
    else {
        // Old and new types do not match.
        return false;
    }
    return true;
}



static bool convert_new_to_old(const Domain::ConfigItem& item, Slic3rLegacy::ConfigOption* opt, const Slic3rLegacy::ConfigOptionDef& def_old, int filament_id = -1, bool is_nil = false)
{
    using namespace Slic3rLegacy;

    if (opt->type() == Slic3rLegacy::coBool && item.holds_alternative<bool>())
        static_cast<Slic3rLegacy::ConfigOptionBool*>(opt)->value = item.get<bool>();
    else if (opt->type() == Slic3rLegacy::coInt && item.holds_alternative<int>()) {
        static_cast<Slic3rLegacy::ConfigOptionInt*>(opt)->value = is_nil
            ? static_cast<Slic3rLegacy::ConfigOptionInt*>(opt)->nil_value()
            : item.get<int>();
    }
    else if (opt->type() == Slic3rLegacy::coFloat && item.holds_alternative<double>()) {
        static_cast<Slic3rLegacy::ConfigOptionFloat*>(opt)->value = is_nil
            ? static_cast<Slic3rLegacy::ConfigOptionFloat*>(opt)->nil_value()
            : item.get<double>();
    }
    else if (opt->type() == Slic3rLegacy::coString && item.holds_alternative<std::string>())
        static_cast<Slic3rLegacy::ConfigOptionString*>(opt)->value = item.get<std::string>();
    else if (opt->type() == Slic3rLegacy::coEnum && item.holds_alternative<EnumWrapper>()) {
        std::string new_ser = std::string(item.get<EnumWrapper>().get_string());
        if (def_old.has_enum_value(new_ser))
            opt->deserialize(new_ser);
        else {
            // Old enum does not have this value.
            return false;
        }
    }
    else if (opt->type() == Slic3rLegacy::coFloatOrPercent && item.holds_alternative<FloatOrPercentage>()) {
        bool is_percent = item.get<Domain::FloatOrPercentage>().is_percentage();
        static_cast<Slic3rLegacy::ConfigOptionFloatOrPercent*>(opt)->percent = is_percent;
        static_cast<Slic3rLegacy::ConfigOptionFloatOrPercent*>(opt)->value = is_percent
            ? item.get<Domain::FloatOrPercentage>().percentage().value
            : item.get<Domain::FloatOrPercentage>().float_value();
    }            
    else if (opt->type() == Slic3rLegacy::coPercent && item.holds_alternative<Percentage>())
        static_cast<Slic3rLegacy::ConfigOptionPercent*>(opt)->value = item.get<Domain::Percentage>().value;
    else if (opt->type() == Slic3rLegacy::coBools && item.holds_alternative<std::vector<bool>>()) {
        std::vector<bool> new_vec = item.get<std::vector<bool>>();
        std::vector<unsigned char> old_vec(new_vec.begin(), new_vec.end());
        static_cast<Slic3rLegacy::ConfigOptionBools*>(opt)->values = old_vec;
    }
    else if (opt->type() == Slic3rLegacy::coInts && item.holds_alternative<std::vector<int>>())
        static_cast<Slic3rLegacy::ConfigOptionInts*>(opt)->values = item.get<std::vector<int>>();
    else if (opt->type() == Slic3rLegacy::coFloats && item.holds_alternative<std::vector<double>>())
        static_cast<Slic3rLegacy::ConfigOptionFloats*>(opt)->values = item.get<std::vector<double>>();
    else if (opt->type() == Slic3rLegacy::coStrings && item.holds_alternative<std::vector<std::string>>())
        static_cast<Slic3rLegacy::ConfigOptionStrings*>(opt)->values = item.get<std::vector<std::string>>();
    else if (opt->type() == Slic3rLegacy::coPoints && item.holds_alternative<std::vector<Domain::Vec2d>>()) {
        const auto& new_vec = item.get<std::vector<Domain::Vec2d>>();
        std::vector<Slic3rLegacy::Vec2d> old_vec(new_vec.begin(), new_vec.end());
        static_cast<Slic3rLegacy::ConfigOptionPoints*>(opt)->values = old_vec;
    }
    else if (opt->type() == Slic3rLegacy::coEnums && item.holds_alternative<EnumVectorWrapper>()) {
        const auto& strs = item.get<EnumVectorWrapper>().get_strings();
        std::string serialized;
        for (const auto& str_serialized : strs)
            serialized += std::string(str_serialized) + ",";
        ASSERT(! serialized.empty());
        serialized.pop_back();
        static_cast<Slic3rLegacy::ConfigOptionPoints*>(opt)->deserialize(serialized);
    }
    else if (opt->is_vector() && filament_id != -1) {
        if (opt->type() == Slic3rLegacy::coBools && item.holds_alternative<bool>()) {
            static_cast<Slic3rLegacy::ConfigOptionBools*>(opt)->values.resize(filament_id + 1);
            static_cast<Slic3rLegacy::ConfigOptionBools*>(opt)->values[filament_id] = is_nil
                ? static_cast<Slic3rLegacy::ConfigOptionBools*>(opt)->nil_value()
                : item.get<bool>();
        }
        else if (opt->type() == Slic3rLegacy::coInts && item.holds_alternative<int>()) {
            static_cast<Slic3rLegacy::ConfigOptionInts*>(opt)->values.resize(filament_id + 1);
            static_cast<Slic3rLegacy::ConfigOptionInts*>(opt)->values[filament_id] = item.get<int>();
        }
        else if (opt->type() == Slic3rLegacy::coInts && item.holds_alternative<std::optional<int>>()) {
            static_cast<Slic3rLegacy::ConfigOptionInts*>(opt)->values.resize(filament_id + 1);
            const auto& idle = item.get<std::optional<int>>();
            static_cast<Slic3rLegacy::ConfigOptionInts*>(opt)->values[filament_id] = idle
                ? *idle
                : static_cast<Slic3rLegacy::ConfigOptionInts*>(opt)->nil_value();
        }
        else if (opt->type() == Slic3rLegacy::coFloats && item.holds_alternative<double>()) {
            static_cast<Slic3rLegacy::ConfigOptionFloats*>(opt)->values.resize(filament_id + 1);
            static_cast<Slic3rLegacy::ConfigOptionFloats*>(opt)->values[filament_id] = is_nil
                ? static_cast<Slic3rLegacy::ConfigOptionFloats*>(opt)->nil_value()
                : item.get<double>();
        }
        else if (opt->type() == Slic3rLegacy::coStrings && item.holds_alternative<std::string>()) {
            static_cast<Slic3rLegacy::ConfigOptionStrings*>(opt)->values.resize(filament_id + 1);
            static_cast<Slic3rLegacy::ConfigOptionStrings*>(opt)->values[filament_id] = item.get<std::string>();
        }
        else if (opt->type() == Slic3rLegacy::coPercents && item.holds_alternative<Percentage>()) {
            static_cast<Slic3rLegacy::ConfigOptionPercents*>(opt)->values.resize(filament_id + 1);
            static_cast<Slic3rLegacy::ConfigOptionPercents*>(opt)->values[filament_id] = is_nil
                ? static_cast<Slic3rLegacy::ConfigOptionPercents*>(opt)->nil_value()
                : item.get<Domain::Percentage>().value;
        }
        else if (opt->type() == Slic3rLegacy::coFloatsOrPercents && item.holds_alternative<FloatOrPercentage>()) {
            static_cast<Slic3rLegacy::ConfigOptionFloatsOrPercents*>(opt)->values.resize(filament_id + 1);
            if (is_nil) {
                static_cast<Slic3rLegacy::ConfigOptionFloatsOrPercents*>(opt)->values[filament_id] = static_cast<Slic3rLegacy::ConfigOptionFloatsOrPercents*>(opt)->nil_value();
            }
            else {
                Slic3rLegacy::FloatOrPercent fop;
                fop.percent = item.get<Domain::FloatOrPercentage>().is_percentage();
                fop.value = fop.percent
                    ? item.get<Domain::FloatOrPercentage>().percentage().value
                    : item.get<Domain::FloatOrPercentage>().float_value();
                static_cast<Slic3rLegacy::ConfigOptionFloatsOrPercents*>(opt)->values[filament_id] = fop;
            }
        }
        else if (opt->type() == Slic3rLegacy::coPoints && item.holds_alternative<Domain::Vec2d>()) {
            static_cast<Slic3rLegacy::ConfigOptionPoints*>(opt)->values.resize(filament_id + 1);
            static_cast<Slic3rLegacy::ConfigOptionPoints*>(opt)->values[filament_id] = Slic3rLegacy::Vec2d(item.get<Domain::Vec2d>());
        }
        else if (opt->type() == Slic3rLegacy::coEnums && item.holds_alternative<EnumWrapper>()) {
            static_cast<Slic3rLegacy::ConfigOptionInts*>(opt)->values.resize(filament_id + 1);
            static_cast<Slic3rLegacy::ConfigOptionInts*>(opt)->values[filament_id] = item.get<EnumWrapper>().value();
        }
        else {
            // Old and new types do not match.
            return false;
        }
    }
    else {
        // Old and new types do not match.
        return false;
    }
    return true;
}

static void fill_config_box_from_legacy(
    const Slic3rLegacy::DynamicPrintConfig& cfg,
    Domain::ConfigBox& box,
    const LegacyKeysAndOverrides& legacy,
    int filament_id     = -1,
    bool fill_overrides = false
)
{

    for (const std::string& old_key : cfg.keys()) {
        bool has_override = std::ranges::find(legacy.overrides, legacy.override_prefix + old_key) != legacy.overrides.end();
        bool is_filament_override = std::ranges::find(legacy.overrides, old_key) != legacy.overrides.end();
        ASSERT(! is_filament_override || boost::starts_with(old_key, legacy.override_prefix));
        std::string new_key(old_key.begin() + (is_filament_override ? legacy.override_prefix.size() : 0), old_key.end()); // trim prefix

        const auto [item_ptr, new_is_override]{box.find(new_key)};
        if (!item_ptr) {
            continue;
        }
        if (!fill_overrides && new_is_override && !is_filament_override) {
            continue;
        }

        const Slic3rLegacy::ConfigOption* opt = cfg.option(old_key);
        ConfigItem& item{*item_ptr};

        if (is_filament_override) {
            if (box.location != legacy.override_box_type
             || std::ranges::find(item.def().overrides_in, legacy.override_box_type) == item.def().overrides_in.end())
                continue;

            if (legacy.printer_technology_str == "FFF")
                if (filament_id == -1
                    || !opt->is_vector()
                    || static_cast<const Slic3rLegacy::ConfigOptionVectorBase*>(opt)->is_nil(filament_id))
                    continue;
            if (legacy.printer_technology_str == "SLA") {
                ASSERT(filament_id == -1);
                if (static_cast<const Slic3rLegacy::ConfigOption*>(opt)->is_nil())
                    continue;
            }
        }

        if (has_override && box.location == legacy.override_box_type) {
            // This config option has override in filament settings. Only continue
            // if current box is NOT filament settings, otherwise we may overwrite
            // what we already loaded as an override.
            continue;
        }

        // if (filament_id != -1 && ! opt->is_vector()) {
        //     // This box exists once per filament, but the old value is not vector.
        //     continue;
        // }
        if (convert_old_to_new(opt, item, filament_id)) {
            if (!opt->is_nil() && new_is_override) {
                box.overrides.enable(new_key);
            }
        }
    }
}



void fill_config_box_from_legacy(const Slic3rLegacy::DynamicPrintConfig& cfg,
    Domain::ConfigBox& box)
{
    LegacyKeysAndOverrides legacy = {};
    fill_config_box_from_legacy(cfg, box, legacy, -1, true);
}

/**
 * Converts a percentage-based extrusion width option to absolute value.
 *
 * @param config The legacy configuration to modify.
 * @param width_option_name Name of the extrusion width option to convert.
 * @param layer_height The layer height used as reference for percentage calculations (layer height or first layer height).
 */
void convert_percentage_extrusion_width_to_absolute(
    Slic3rLegacy::DynamicPrintConfig& config,
    const std::string& extrusion_width_option_name,
    const double layer_height
)
{
    if (!config.has(extrusion_width_option_name)) {
        return;
    }

    Slic3rLegacy::ConfigOptionFloatOrPercent* extrusion_width_option =
        config.option<Slic3rLegacy::ConfigOptionFloatOrPercent>(extrusion_width_option_name);
    if (extrusion_width_option->percent) {
        const double absolute_extrusion_width = extrusion_width_option->get_abs_value(layer_height);
        extrusion_width_option->percent       = false;
        extrusion_width_option->value         = absolute_extrusion_width;
    }
}

/**
 * Converts all percentage-based extrusion width options to absolute values.
 *
 * @param config The legacy configuration to modify.
 */
void convert_legacy_extrusion_width_options(Slic3rLegacy::DynamicPrintConfig& config)
{
    if (!config.has("layer_height") || !config.has("first_layer_height")) {
        return;
    }

    const double layer_height =
        config.option<Slic3rLegacy::ConfigOptionFloat>("layer_height")->value;
    const double first_layer_height =
        config.option<Slic3rLegacy::ConfigOptionFloatOrPercent>("first_layer_height")
            ->get_abs_value(layer_height);

    // Convert extrusion width options that use layer_height as a reference.
    for (const std::string extrusion_width_option_name :
         {"external_perimeter_extrusion_width",
          "extrusion_width",
          "infill_extrusion_width",
          "perimeter_extrusion_width",
          "solid_infill_extrusion_width",
          "support_material_extrusion_width",
          "top_infill_extrusion_width"})
    {
        convert_percentage_extrusion_width_to_absolute(
            config,
            extrusion_width_option_name,
            layer_height
        );
    }

    // Convert the first layer extrusion width separately as it uses first_layer_height as a reference.
    convert_percentage_extrusion_width_to_absolute(
        config,
        "first_layer_extrusion_width",
        first_layer_height
    );
}

static int get_extruder_num(const Slic3rLegacy::DynamicPrintConfig& cfg)
{
    return cfg.has("nozzle_diameter") ? int(cfg.option<Slic3rLegacy::ConfigOptionFloats>("nozzle_diameter")->size()) : 1;
}

static void split_raft_first_layer_params(Slic3rLegacy::DynamicPrintConfig& cfg)
{
    if (cfg.has("raft_first_layer_density") && !cfg.has("support_material_first_layer_density")) {
        cfg.set_key_value(
            "support_material_first_layer_density",
            cfg.option("raft_first_layer_density")->clone()
        );
    }

    if (cfg.has("raft_first_layer_expansion") && !cfg.has("support_material_first_layer_expansion"))
    {
        cfg.set_key_value(
            "support_material_first_layer_expansion",
            cfg.option("raft_first_layer_expansion")->clone()
        );
    }
}

static void convert_legacy_fdm_options(Slic3rLegacy::DynamicPrintConfig& cfg)
{
    if (!cfg.has("printer_technology")
        || cfg.opt_enum<Slic3rLegacy::PrinterTechnology>("printer_technology")
            != Slic3rLegacy::ptFFF)
    {
        return;
    }

    // Since PrusaSlicer 3.0.0, all extrusion width options are related to nozzle diameter instead of layer height,
    // so we need to convert them into absolute values to preserve backward compatibility.
    convert_legacy_extrusion_width_options(cfg);

    // Since PrusaSlicer 3.0.0, raft_first_layer_density/expansion are split into separate raft and support parameters.
    split_raft_first_layer_params(cfg);
}

ConfigPack convert_dynamic_print_config_to_new(Slic3rLegacy::DynamicPrintConfig& cfg)
{
    convert_legacy_fdm_options(cfg);

    if (cfg.has("printer_technology")) {
        if (auto pt = cfg.opt_enum<Slic3rLegacy::PrinterTechnology>("printer_technology"); pt == Slic3rLegacy::ptFFF) {
            int extruder_num = get_extruder_num(cfg);
            bool mmu = (cfg.has("single_extruder_multi_material") && cfg.opt_bool("single_extruder_multi_material"));

            LegacyKeysAndOverrides legacy_data = legacy_fdm_data();
            ConfigPackFDM out;
            out.tool.resize(extruder_num);
            out.filament.resize(extruder_num);

            if (cfg.has("filament_vendor")) {
                // Filament_vendor was saved as a single string, not a vector. In order to place it into
                // filament settings now, we need to pretend that it was a vector.
                cfg.set_key_value("filament_vendor", new Slic3rLegacy::ConfigOptionStrings(extruder_num, cfg.opt_string("filament_vendor")));
            }

            fill_config_box_from_legacy(cfg, out.printer, legacy_data);
            fill_config_box_from_legacy(cfg, out.print, legacy_data);

            for (int i = 0; i < extruder_num; ++i) {
                fill_config_box_from_legacy(cfg, out.tool[i], legacy_data, i, extruder_num > 1);
                fill_config_box_from_legacy(cfg, out.filament[i], legacy_data, i, extruder_num > 1);
            }

            if (mmu) {
                // Bake tool overrides to the print item values.
                for (Domain::ConfigBox& box : out.tool) {
                    for (const ConfigItem& item : box.overrides.overridden_items()) {
                        out.print.items.opt(item.name()).set(item.value());
                        box.overrides.disable(item.name());
                    }
                }
            }

            fill_config_box_from_legacy(cfg, out.project, legacy_data);
            return out;
        }
        else if (pt == Slic3rLegacy::ptSLA) {
            LegacyKeysAndOverrides legacy_data = legacy_sla_data();
            ConfigPackSLA out;
            fill_config_box_from_legacy(cfg, out.sla_printer_settings, legacy_data);
            fill_config_box_from_legacy(cfg, out.sla_material_settings, legacy_data);
            fill_config_box_from_legacy(cfg, out.sla_print_settings, legacy_data);
            return out;
        }
    }
    throw std::runtime_error("Loading legacy config failed: missing or incorrect printer technology.");
}


LegacyPresetMetadata extract_legacy_preset_metadata(Slic3rLegacy::DynamicPrintConfig& cfg)
{
    LegacyPresetMetadata ret;
    if (!cfg.has("printer_technology"))
        throw std::runtime_error("Loading legacy config failed: missing or incorrect printer technology.");
    auto pt = cfg.opt_enum<Slic3rLegacy::PrinterTechnology>("printer_technology");

    ret.technology = pt == Slic3rLegacy::ptFFF ? Domain::PrinterTechnology::FFF : Domain::PrinterTechnology::SLA;

    auto get_string = [&cfg](const std::string& name, std::string& out)
    {
        if (cfg.has(name)) {
            out = cfg.opt_string(name);
        }
    };

    auto get_strings = [&cfg](const std::string& name, std::vector<std::string>& out)
    {
        if (cfg.has(name)) {
            const auto* opt = cfg.option<Slic3rLegacy::ConfigOptionStrings>(name);
            out = opt->values;
        }
    };

    get_string("printer_model", ret.printer_model);
    get_string("printer_notes", ret.printer_notes);
    get_string("printer_settings_id", ret.printer_settings_id);

    if (ret.technology == Domain::PrinterTechnology::FFF) {
        get_string("print_settings_id", ret.print_settings_id);
        get_strings("filament_settings_id", ret.material_settings_id);

        const int num = get_extruder_num(cfg);
        const auto* nozzle_diameter = cfg.option<Slic3rLegacy::ConfigOptionFloats>("nozzle_diameter");
        const auto* nozzle_high_flow = cfg.option<Slic3rLegacy::ConfigOptionBools>("nozzle_high_flow");
        for (int i = 0; i < num; ++i) {
            LegacyHwToolConfig tool_config;
            if (nozzle_diameter) {
                tool_config.nozzle_diameter = nozzle_diameter->get_at(i);
            }
            if (nozzle_high_flow) {
                tool_config.nozzle_high_flow = nozzle_high_flow->get_at(i);
            }
            ret.tools.emplace_back(tool_config);
        }
    } else if (ret.technology == Domain::PrinterTechnology::SLA) {
        get_string("sla_print_settings_id", ret.print_settings_id);
        std::string mat_id;
        get_string("sla_material_settings_id", mat_id);
        ret.material_settings_id = {mat_id};
    }
    else {
        UNREACHABLE("Unknown printer technology");
    }

    return ret;
}


ConfigPack load_config_from_legacy_file(const std::string& filename)
{
    Slic3rLegacy::DynamicPrintConfig cfg = load_legacy_config_from_legacy_file(filename);
    return convert_dynamic_print_config_to_new(cfg);
}

static std::vector<double> get_nozzle_diameters(const Domain::Preset::HwPrinterConfig& hw_config)
{
    std::vector<double> result;

    if (hw_config.material_slot_count() != hw_config.tool_count) {
        const std::optional<double> nozzle_diameter{
            Domain::Preset::get_feature<double>(hw_config.tools.front().features, "nozzle_diameter")
        };
        ASSERT(nozzle_diameter);
        result.resize(hw_config.material_slot_count(), *nozzle_diameter);
    } else {
        for (const auto& tool : hw_config.tools) {
            const std::optional<double> nozzle_diameter{
                Domain::Preset::get_feature<double>(tool.features, "nozzle_diameter")
            };
            ASSERT(nozzle_diameter);
            result.push_back(*nozzle_diameter);
        }
    }

    return result;
}

static std::vector<unsigned char> get_nozzle_high_flows(
    const Domain::Preset::HwPrinterConfig& hw_config
)
{
    std::vector<unsigned char> result;

    if (hw_config.material_slot_count() != hw_config.tool_count) {
        const std::optional<bool> high_flow{
            Domain::Preset::get_feature<bool>(hw_config.tools.front().features, "nozzle_high_flow")
        };
        if (high_flow.value_or(false)) {
            result.resize(hw_config.material_slot_count(), 1);
        } else {
            result.resize(hw_config.material_slot_count(), 0);
        }
    } else {
        for (const auto& tool : hw_config.tools) {
            const std::optional<bool> high_flow{
                Domain::Preset::get_feature<bool>(tool.features, "nozzle_high_flow")
            };
            if (high_flow.value_or(false)) {
                result.push_back(1);
            } else {
                result.push_back(0);
            }
        }
    }

    return result;
}

std::string serialize_as_legacy_config(
    const ConfigPack& cfgvar,
    const Domain::Preset::HwPrinterConfig& hw_config
)
{
    std::vector<std::pair<const Domain::ConfigBox*, int>> boxes;
    LegacyKeysAndOverrides legacy_data;

    if (std::holds_alternative<ConfigPackFDM>(cfgvar)) {
        legacy_data = legacy_fdm_data();
        const ConfigPackFDM& cfg = std::get<ConfigPackFDM>(cfgvar);
        boxes.emplace_back(&cfg.printer, -1);
        boxes.emplace_back(&cfg.print, -1);
        // TODO: At the moment only single-tool printers with MMU are supported,
        ASSERT(cfg.tool.size() == cfg.filament.size() || cfg.tool.size() == 1);
        const size_t slot_count = cfg.filament.size();
        const size_t tool_count = cfg.tool.size();
        for (int i = 0; i < slot_count; ++i) {
            const int tool_idx = i % tool_count;
            boxes.emplace_back(&cfg.tool[tool_idx], i);
        }
        for (int i = 0; i < cfg.filament.size(); ++i)
            boxes.emplace_back(&cfg.filament[i], i);
        boxes.emplace_back(&cfg.project, -1);
    }
    else {
        legacy_data = legacy_sla_data();
        const ConfigPackSLA& cfg = std::get<ConfigPackSLA>(cfgvar);
        boxes.emplace_back(&cfg.sla_printer_settings, -1);
        boxes.emplace_back(&cfg.sla_material_settings, -1);
        boxes.emplace_back(&cfg.sla_print_settings, -1);
    }

    std::unique_ptr<Slic3rLegacy::DynamicPrintConfig> cfg_old(Slic3rLegacy::DynamicPrintConfig::new_from_defaults_keys(legacy_data.keys));
    for (const std::string& key : cfg_old->keys()) {
        Slic3rLegacy::ConfigOption* opt = cfg_old->option(key);

        for (const auto& [box, filament_id] : boxes) {
            const ConfigItem* item{box->items.find(key)};

            if (!item && box->location != legacy_data.override_box_type) {
                const ConfigItem* override{box->overrides.find(key)};
                if (override && box->overrides.get(key)) {
                    item = override;
                }
            }
            if (item) {
                if (hw_config.material_slot_count() != hw_config.tool_count
                    && item->def().overrides_in.contains(FDMConfigLocation::Tool))
                {
                    for (std::size_t i{}; i < hw_config.material_slot_count(); ++i) {
                        convert_new_to_old(*item, opt, *cfg_old->def()->get(key), i);
                    }
                } else {
                    convert_new_to_old(*item, opt, *cfg_old->def()->get(key), filament_id);
                }
            }

            if (!item && box->location == legacy_data.override_box_type
             && std::ranges::find(legacy_data.overrides, key) != legacy_data.overrides.end()) {
                // This old item is not present in any of the boxes, but it is an override of something.
                // PrusaSlicer 2.9.2 had all overrides at filament/material level. Find the current override.
                ASSERT(boost::starts_with(key, legacy_data.override_prefix));
                std::string new_key(key.begin() + legacy_data.override_prefix.size(), key.end());

                const auto [over, must_be_override]{box->find(new_key)};
                ASSERT(over && must_be_override);
                const bool is_nil{!box->overrides.get(new_key)};
                convert_new_to_old(*over, opt, *cfg_old->def()->get(new_key), filament_id, is_nil);
            }
        }
    }

    if (std::holds_alternative<ConfigPackFDM>(cfgvar)) {
        cfg_old->set_key_value(
            "nozzle_diameter",
            new Slic3rLegacy::ConfigOptionFloats(get_nozzle_diameters(hw_config))
        );
        cfg_old->set_key_value(
            "nozzle_high_flow",
            new Slic3rLegacy::ConfigOptionBools(get_nozzle_high_flows(hw_config))
        );
    }

    std::string out;
    for (const std::string& key : cfg_old->keys()) {
        out += key + " = " + cfg_old->opt_serialize(key) + "\n";
    }
    if (! out.empty())
        out.pop_back();
    return out;
}

Slic3rLegacy::DynamicPrintConfig convert_box_to_dynamic_print_config(const Domain::ConfigBox& box)
{
    std::vector<std::string> acceptable_keys;
    const ConfigLocation fdm_volume{FDMConfigLocation::Volume};
    const ConfigLocation fdm_object{FDMConfigLocation::Object};
    const ConfigLocation sla_object{SLAConfigLocation::Object};

    if (box.location == fdm_volume || box.location == fdm_object)
        acceptable_keys = legacy_fdm_data().keys;
    else if (box.location == sla_object)
        acceptable_keys = legacy_sla_data().keys;
    else
        PANIC();

    acceptable_keys.push_back("extruder");

    std::vector<std::string> override_keys_to_convert;

    for (const auto& item_ref : box.overrides.overridden_items()) {
        const Domain::ConfigItem& item{item_ref.get()};
        const std::string& name{item.def().name};
        if (std::ranges::find(acceptable_keys, name) == acceptable_keys.end())
            continue;
        override_keys_to_convert.emplace_back(name);
    }

    std::vector<std::string> items_keys_to_convert;
    for (const Domain::ConfigItem& item : box.items.all_items()) {
        const std::string& name{item.def().name};
        if (std::ranges::find(acceptable_keys, name) == acceptable_keys.end())
            continue;
        items_keys_to_convert.emplace_back(name);
    }

    std::vector<std::string> keys_to_convert{override_keys_to_convert};
    keys_to_convert.insert(keys_to_convert.end(), items_keys_to_convert.begin(), items_keys_to_convert.end());
    std::unique_ptr<Slic3rLegacy::DynamicPrintConfig> cfg_old(Slic3rLegacy::DynamicPrintConfig::new_from_defaults_keys(keys_to_convert));

    for (const std::string& key : override_keys_to_convert) {
        Slic3rLegacy::ConfigOption* opt = cfg_old->option(key);
        const Domain::ConfigItem item = *ASSERT_VAL(box.overrides.get(key));
        convert_new_to_old(item, opt, *cfg_old->def()->get(key));
    }

    for (const std::string& key : items_keys_to_convert) {
        Slic3rLegacy::ConfigOption* opt = cfg_old->option(key);
        const Domain::ConfigItem& item = box.items.opt(key);
        convert_new_to_old(item, opt, *cfg_old->def()->get(key));
    }
    return *cfg_old;
}



// TODO: New slicer changed enums PrintHostType and AuthorizationType (=PrintHostAuthType).
// We need to convert old options to the new ones properly. BEWARE especially of PrusaConnect and
// PrusaConnectNew. PrusaConnect was removed and PrusaConnectNew was renamed to PrusaConnect.

} // namespace Slic3r::Biz
