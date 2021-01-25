#include "clipper/clipper_z.hpp"

#include "Exception.hpp"
#include "Print.hpp"
#include "BoundingBox.hpp"
#include "ClipperUtils.hpp"
#include "Extruder.hpp"
#include "Flow.hpp"
#include "Fill/FillBase.hpp"
#include "Geometry.hpp"
#include "I18N.hpp"
#include "ShortestPath.hpp"
#include "SupportMaterial.hpp"
#include "Thread.hpp"
#include "GCode.hpp"
#include "GCode/WipeTower.hpp"
#include "Utils.hpp"

//#include "PrintExport.hpp"

#include <float.h>

#include <algorithm>
#include <limits>
#include <unordered_set>
#include <boost/filesystem/path.hpp>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

// Mark string for localization and translate.
#define L(s) Slic3r::I18N::translate(s)

namespace Slic3r {

template class PrintState<PrintStep, psCount>;
template class PrintState<PrintObjectStep, posCount>;

void Print::clear() 
{
	tbb::mutex::scoped_lock lock(this->state_mutex());
    // The following call should stop background processing if it is running.
    this->invalidate_all_steps();
	for (PrintObject *object : m_objects)
		delete object;
	m_objects.clear();
    for (PrintRegion *region : m_regions)
        delete region;
    m_regions.clear();
    m_model.clear_objects();
}

//PrintRegion* Print::add_region()
//{
//    m_regions.emplace_back(new PrintRegion(this));
//    return m_regions.back();
//}

PrintRegion* Print::add_region(const PrintRegionConfig &config)
{
    m_regions.emplace_back(new PrintRegion(this, config));
    return m_regions.back();
}

// Called by Print::apply().
// This method only accepts PrintConfig option keys.
bool Print::invalidate_state_by_config_options(const std::vector<t_config_option_key> &opt_keys)
{
    if (opt_keys.empty())
        return false;

    // Cache the plenty of parameters, which influence the G-code generator only,
    // or they are only notes not influencing the generated G-code.
    static std::unordered_set<std::string> steps_gcode = {
        "avoid_crossing_perimeters",
        "avoid_crossing_perimeters_max_detour",
        "avoid_crossing_not_first_layer",
        "bed_shape",
        "bed_temperature",
        "chamber_temperature",
        "before_layer_gcode",
        "between_objects_gcode",
        "bridge_acceleration",
        "bridge_fan_speed",
        "colorprint_heights",
        "complete_objects_sort",
        "cooling",
        "default_acceleration",
        "deretract_speed",
        "disable_fan_first_layers",
        "duplicate_distance",
        "end_gcode",
        "end_filament_gcode",
        "external_perimeter_cut_corners",
        "external_perimeter_fan_speed",
        "extrusion_axis",
        "extruder_clearance_height",
        "extruder_clearance_radius",
        "extruder_colour",
        "extruder_offset",
        "extruder_fan_offset"
        "extruder_temperature_offset",
        "extrusion_multiplier",
        "fan_always_on",
        "fan_below_layer_time",
        "fan_kickstart",
        "fan_speedup_overhangs",
        "fan_speedup_time",
        "filament_colour",
        "filament_diameter",
        "filament_density",
        "filament_notes",
        "filament_cost",
        "filament_spool_weight",
        "first_layer_acceleration",
        "first_layer_bed_temperature",
        "first_layer_flow_ratio",
        "first_layer_speed",
        "first_layer_infill_speed",
        "full_fan_speed_layer",
        "gap_fill_speed",
        "gcode_comments",
        "gcode_label_objects",
        "infill_acceleration",
        "layer_gcode",
        "min_fan_speed",
        "max_fan_speed",
        "max_print_height",
        "min_print_speed",
        "max_print_speed",
        "max_volumetric_speed",
        "milling_toolchange_end_gcode",
        "milling_toolchange_start_gcode",
        "milling_offset",
        "milling_z_offset",
        "milling_z_lift",
#ifdef HAS_PRESSURE_EQUALIZER
        "max_volumetric_extrusion_rate_slope_positive",
        "max_volumetric_extrusion_rate_slope_negative",
#endif /* HAS_PRESSURE_EQUALIZER */
        "notes",
        "only_retract_when_crossing_perimeters",
        "output_filename_format",
        "perimeter_acceleration",
        "post_process",
        "printer_notes",
        "retract_before_travel",
        "retract_before_wipe",
        "retract_layer_change",
        "retract_length",
        "retract_length_toolchange",
        "retract_lift",
        "retract_lift_above",
        "retract_lift_below",
        "retract_lift_first_layer",
        "retract_lift_top",
        "retract_restart_extra",
        "retract_restart_extra_toolchange",
        "retract_speed",
        "single_extruder_multi_material_priming",
        "slowdown_below_layer_time",
        "standby_temperature_delta",
        "start_gcode",
        "start_filament_gcode",
        "thin_walls_speed",
        "time_estimation_compensation",
        "tool_name",
        "toolchange_gcode",
        "top_fan_speed",
        "threads",
        "travel_speed",
        "use_firmware_retraction",
        "use_relative_e_distances",
        "use_volumetric_e",
        "variable_layer_height",
        "wipe",
        "wipe_extra_perimeter"
    };

    static std::unordered_set<std::string> steps_ignore;

    std::vector<PrintStep> steps;
    std::vector<PrintObjectStep> osteps;
    bool invalidated = false;

    for (const t_config_option_key &opt_key : opt_keys) {
        if (steps_gcode.find(opt_key) != steps_gcode.end()) {
            // These options only affect G-code export or they are just notes without influence on the generated G-code,
            // so there is nothing to invalidate.
            steps.emplace_back(psGCodeExport);
        } else if (steps_ignore.find(opt_key) != steps_ignore.end()) {
            // These steps have no influence on the G-code whatsoever. Just ignore them.
        } else if (
               opt_key == "skirts"
            || opt_key == "skirt_height"
            || opt_key == "draft_shield"
            || opt_key == "skirt_distance"
            || opt_key == "min_skirt_length"
            || opt_key == "complete_objects_one_skirt"
            || opt_key == "ooze_prevention"
            || opt_key == "wipe_tower_x"
            || opt_key == "wipe_tower_y"
            || opt_key == "wipe_tower_rotation_angle") {
            steps.emplace_back(psSkirt);
        } else if (
            opt_key == "complete_objects") {
            steps.emplace_back(psBrim);
            steps.emplace_back(psSkirt);
            steps.emplace_back(psWipeTower);
        } else if (
            opt_key == "brim_inside_holes"
            || opt_key == "brim_width"
            || opt_key == "brim_width_interior"
            || opt_key == "brim_offset"
            || opt_key == "brim_ears"
            || opt_key == "brim_ears_detection_length"
            || opt_key == "brim_ears_max_angle"
            || opt_key == "brim_ears_pattern") {
            steps.emplace_back(psBrim);
            steps.emplace_back(psSkirt);
        } else if (
               opt_key == "nozzle_diameter"
            || opt_key == "resolution"
            || opt_key == "filament_shrink"
            // Spiral Vase forces different kind of slicing than the normal model:
            // In Spiral Vase mode, holes are closed and only the largest area contour is kept at each layer.
            // Therefore toggling the Spiral Vase on / off requires complete reslicing.
            || opt_key == "spiral_vase") {
            osteps.emplace_back(posSlice);
        } else if (
               opt_key == "filament_type"
            || opt_key == "filament_soluble"
            || opt_key == "first_layer_temperature"
            || opt_key == "filament_loading_speed"
            || opt_key == "filament_loading_speed_start"
            || opt_key == "filament_unloading_speed"
            || opt_key == "filament_unloading_speed_start"
            || opt_key == "filament_toolchange_delay"
            || opt_key == "filament_cooling_moves"
            || opt_key == "filament_minimal_purge_on_wipe_tower"
            || opt_key == "filament_cooling_initial_speed"
            || opt_key == "filament_cooling_final_speed"
            || opt_key == "filament_ramming_parameters"
            || opt_key == "filament_max_speed"
            || opt_key == "filament_max_volumetric_speed"
            || opt_key == "filament_use_skinnydip"        // skinnydip params start
            || opt_key == "filament_use_fast_skinnydip"
            || opt_key == "filament_skinnydip_distance"
            || opt_key == "filament_melt_zone_pause"
            || opt_key == "filament_cooling_zone_pause"
            || opt_key == "filament_toolchange_temp"
            || opt_key == "filament_enable_toolchange_temp"
            || opt_key == "filament_enable_toolchange_part_fan"
            || opt_key == "filament_toolchange_part_fan_speed"
            || opt_key == "filament_dip_insertion_speed"
            || opt_key == "filament_dip_extraction_speed"    //skinnydip params end	
            || opt_key == "gcode_flavor"
            || opt_key == "high_current_on_filament_swap"
            || opt_key == "infill_first"
            || opt_key == "single_extruder_multi_material"
            || opt_key == "temperature"
            || opt_key == "wipe_tower"
            || opt_key == "wipe_tower_width"
            || opt_key == "wipe_tower_bridging"
            || opt_key == "wipe_tower_no_sparse_layers"
            || opt_key == "wiping_volumes_matrix"
            || opt_key == "parking_pos_retraction"
            || opt_key == "cooling_tube_retraction"
            || opt_key == "cooling_tube_length"
            || opt_key == "extra_loading_move"
            || opt_key == "z_offset"
            || opt_key == "wipe_tower_brim") {
            steps.emplace_back(psWipeTower);
            steps.emplace_back(psSkirt);
        }
        else if (
            opt_key == "first_layer_extrusion_width"
            || opt_key == "min_layer_height"
            || opt_key == "max_layer_height") {
            osteps.emplace_back(posPerimeters);
            osteps.emplace_back(posInfill);
            osteps.emplace_back(posSupportMaterial);
            steps.emplace_back(psSkirt);
            steps.emplace_back(psBrim);
        }
        else if (opt_key == "posSlice")
            osteps.emplace_back(posSlice);
        else if (opt_key == "posPerimeters")
            osteps.emplace_back(posPerimeters);
        else if (opt_key == "posPrepareInfill")
            osteps.emplace_back(posPrepareInfill);
        else if (opt_key == "posInfill")
            osteps.emplace_back(posInfill);
        else if (opt_key == "posSupportMaterial")
            osteps.emplace_back(posSupportMaterial);
        else if (opt_key == "posCount")
            osteps.emplace_back(posCount);
        else {
            // for legacy, if we can't handle this option let's invalidate all steps
            //FIXME invalidate all steps of all objects as well?
            invalidated |= this->invalidate_all_steps();
            // Continue with the other opt_keys to possibly invalidate any object specific steps.
        }
    }

    sort_remove_duplicates(steps);
    for (PrintStep step : steps)
        invalidated |= this->invalidate_step(step);
    sort_remove_duplicates(osteps);
    for (PrintObjectStep ostep : osteps)
        for (PrintObject *object : m_objects)
            invalidated |= object->invalidate_step(ostep);
    return invalidated;
}

bool Print::invalidate_step(PrintStep step)
{
	bool invalidated = Inherited::invalidate_step(step);
    // Propagate to dependent steps.
    if (step == psSkirt)
		invalidated |= Inherited::invalidate_step(psBrim);
    if (step != psGCodeExport)
        invalidated |= Inherited::invalidate_step(psGCodeExport);
    return invalidated;
}

// returns true if an object step is done on all objects
// and there's at least one object
bool Print::is_step_done(PrintObjectStep step) const
{
    if (m_objects.empty())
        return false;
    tbb::mutex::scoped_lock lock(this->state_mutex());
    for (const PrintObject *object : m_objects)
        if (! object->is_step_done_unguarded(step))
            return false;
    return true;
}

// returns 0-based indices of used extruders
std::vector<uint16_t> Print::object_extruders(const PrintObjectPtrs &objects) const
{
    std::vector<uint16_t> extruders;
    extruders.reserve(m_regions.size() * 3);
    std::vector<unsigned char> region_used(m_regions.size(), false);
    for (const PrintObject *object : objects)
		for (const std::vector<std::pair<t_layer_height_range, int>> &volumes_per_region : object->region_volumes)
        	if (! volumes_per_region.empty())
        		region_used[&volumes_per_region - &object->region_volumes.front()] = true;
    for (size_t idx_region = 0; idx_region < m_regions.size(); ++ idx_region)
    	if (region_used[idx_region])
        	m_regions[idx_region]->collect_object_printing_extruders(extruders);
    sort_remove_duplicates(extruders);
    return extruders;
}

// returns 0-based indices of used extruders
std::vector<uint16_t> Print::support_material_extruders() const
{
    std::vector<uint16_t> extruders;
    bool support_uses_current_extruder = false;
    auto num_extruders = (uint16_t)m_config.nozzle_diameter.size();

    for (PrintObject *object : m_objects) {
        if (object->has_support_material()) {
        	assert(object->config().support_material_extruder >= 0);
            if (object->config().support_material_extruder == 0)
                support_uses_current_extruder = true;
            else {
                uint16_t i = (uint16_t)object->config().support_material_extruder - 1;
                extruders.emplace_back((i >= num_extruders) ? 0 : i);
            }
        	assert(object->config().support_material_interface_extruder >= 0);
            if (object->config().support_material_interface_extruder == 0)
                support_uses_current_extruder = true;
            else {
                uint16_t i = (uint16_t)object->config().support_material_interface_extruder - 1;
                extruders.emplace_back((i >= num_extruders) ? 0 : i);
        }
    }
    }

    if (support_uses_current_extruder)
        // Add all object extruders to the support extruders as it is not know which one will be used to print supports.
        append(extruders, this->object_extruders(m_objects));
    
    sort_remove_duplicates(extruders);
    return extruders;
}

// returns 0-based indices of used extruders
std::vector<uint16_t> Print::extruders() const
{
    std::vector<uint16_t> extruders = this->object_extruders(m_objects);
    append(extruders, this->support_material_extruders());
    sort_remove_duplicates(extruders);
    return extruders;
}

uint16_t Print::num_object_instances() const
{
    uint16_t instances = 0;
    for (const PrintObject *print_object : m_objects)
        instances += (uint16_t)print_object->instances().size();
    return instances;
}

double Print::max_allowed_layer_height() const
{
    double nozzle_diameter_max = 0.;
    for (unsigned int extruder_id : this->extruders())
        nozzle_diameter_max = std::max(nozzle_diameter_max, m_config.nozzle_diameter.get_at(extruder_id));
    return nozzle_diameter_max;
}

// Add or remove support modifier ModelVolumes from model_object_dst to match the ModelVolumes of model_object_new
// in the exact order and with the same IDs.
// It is expected, that the model_object_dst already contains the non-support volumes of model_object_new in the correct order.
void Print::model_volume_list_update_supports_seams(ModelObject &model_object_dst, const ModelObject &model_object_new)
{
	typedef std::pair<const ModelVolume*, bool> ModelVolumeWithStatus;
	std::vector<ModelVolumeWithStatus> old_volumes;
    old_volumes.reserve(model_object_dst.volumes.size());
	for (const ModelVolume *model_volume : model_object_dst.volumes)
		old_volumes.emplace_back(ModelVolumeWithStatus(model_volume, false));
	auto model_volume_lower = [](const ModelVolumeWithStatus &mv1, const ModelVolumeWithStatus &mv2){ return mv1.first->id() <  mv2.first->id(); };
	auto model_volume_equal = [](const ModelVolumeWithStatus &mv1, const ModelVolumeWithStatus &mv2){ return mv1.first->id() == mv2.first->id(); };
    std::sort(old_volumes.begin(), old_volumes.end(), model_volume_lower);
    model_object_dst.volumes.clear();
    model_object_dst.volumes.reserve(model_object_new.volumes.size());
    for (const ModelVolume *model_volume_src : model_object_new.volumes) {
		ModelVolumeWithStatus key(model_volume_src, false);
		auto it = std::lower_bound(old_volumes.begin(), old_volumes.end(), key, model_volume_lower);
		if (it != old_volumes.end() && model_volume_equal(*it, key)) {
            // The volume was found in the old list. Just copy it.
            assert(! it->second); // not consumed yet
            it->second = true;
            ModelVolume *model_volume_dst = const_cast<ModelVolume*>(it->first);
			// For support modifiers, the type may have been switched from blocker to enforcer and vice versa.
			assert((model_volume_dst->is_support_modifier() && model_volume_src->is_support_modifier()) || model_volume_dst->type() == model_volume_src->type());
            model_object_dst.volumes.emplace_back(model_volume_dst);
			if (model_volume_dst->is_support_modifier() || model_volume_dst->is_seam_position()) {
				// For support modifiers, the type may have been switched from blocker to enforcer and vice versa.
				model_volume_dst->set_type(model_volume_src->type());
				model_volume_dst->set_transformation(model_volume_src->get_transformation());
			}
            assert(model_volume_dst->get_matrix().isApprox(model_volume_src->get_matrix()));
        } else {
            // The volume was not found in the old list. Create a new copy.
            assert(model_volume_src->is_support_modifier() || model_volume_src->is_seam_position());
            model_object_dst.volumes.emplace_back(new ModelVolume(*model_volume_src));
            model_object_dst.volumes.back()->set_model_object(&model_object_dst);
        }
    }
    // Release the non-consumed old volumes (those were deleted from the new list).
	for (ModelVolumeWithStatus &mv_with_status : old_volumes)
        if (! mv_with_status.second)
            delete mv_with_status.first;
}

static inline void model_volume_list_copy_configs(ModelObject &model_object_dst, const ModelObject &model_object_src, const ModelVolumeType type)
{
    size_t i_src, i_dst;
    for (i_src = 0, i_dst = 0; i_src < model_object_src.volumes.size() && i_dst < model_object_dst.volumes.size();) {
        const ModelVolume &mv_src = *model_object_src.volumes[i_src];
        ModelVolume       &mv_dst = *model_object_dst.volumes[i_dst];
        if (mv_src.type() != type) {
            ++ i_src;
            continue;
        }
        if (mv_dst.type() != type) {
            ++ i_dst;
            continue;
        }
        assert(mv_src.id() == mv_dst.id());
        // Copy the ModelVolume data.
        mv_dst.name   = mv_src.name;
        mv_dst.config.assign_config(mv_src.config);
        assert(mv_dst.supported_facets.id() == mv_src.supported_facets.id());
        mv_dst.supported_facets.assign(mv_src.supported_facets);
        assert(mv_dst.seam_facets.id() == mv_src.seam_facets.id());
        mv_dst.seam_facets.assign(mv_src.seam_facets);
        //FIXME what to do with the materials?
        // mv_dst.m_material_id = mv_src.m_material_id;
        ++ i_src;
        ++ i_dst;
    }
}

static inline void layer_height_ranges_copy_configs(t_layer_config_ranges &lr_dst, const t_layer_config_ranges &lr_src)
{
    assert(lr_dst.size() == lr_src.size());
    auto it_src = lr_src.cbegin();
    for (auto &kvp_dst : lr_dst) {
        const auto &kvp_src = *it_src ++;
        assert(std::abs(kvp_dst.first.first  - kvp_src.first.first ) <= EPSILON);
        assert(std::abs(kvp_dst.first.second - kvp_src.first.second) <= EPSILON);
        // Layer heights are allowed do differ in case the layer height table is being overriden by the smooth profile.
        // assert(std::abs(kvp_dst.second.option("layer_height")->getFloat() - kvp_src.second.option("layer_height")->getFloat()) <= EPSILON);
        kvp_dst.second = kvp_src.second;
    }
}

static inline bool transform3d_lower(const Transform3d &lhs, const Transform3d &rhs) 
{
    typedef Transform3d::Scalar T;
    const T *lv = lhs.data();
    const T *rv = rhs.data();
    for (size_t i = 0; i < 16; ++ i, ++ lv, ++ rv) {
        if (*lv < *rv)
            return true;
        else if (*lv > *rv)
            return false;
    }
    return false;
}

static inline bool transform3d_equal(const Transform3d &lhs, const Transform3d &rhs) 
{
    typedef Transform3d::Scalar T;
    const T *lv = lhs.data();
    const T *rv = rhs.data();
    for (size_t i = 0; i < 16; ++ i, ++ lv, ++ rv)
        if (*lv != *rv)
            return false;
    return true;
}

struct PrintObjectTrafoAndInstances
{
    Transform3d    	trafo;
    PrintInstances	instances;
    bool operator<(const PrintObjectTrafoAndInstances &rhs) const { return transform3d_lower(this->trafo, rhs.trafo); }
};

// Generate a list of trafos and XY offsets for instances of a ModelObject
static std::vector<PrintObjectTrafoAndInstances> print_objects_from_model_object(const ModelObject &model_object)
{
    std::set<PrintObjectTrafoAndInstances> trafos;
    PrintObjectTrafoAndInstances           trafo;
    for (ModelInstance *model_instance : model_object.instances)
        if (model_instance->is_printable()) {
            trafo.trafo = model_instance->get_matrix();
            auto shift = Point::new_scale(trafo.trafo.data()[12], trafo.trafo.data()[13]);
            // Reset the XY axes of the transformation.
            trafo.trafo.data()[12] = 0;
            trafo.trafo.data()[13] = 0;
            // Search or insert a trafo.
            auto it = trafos.emplace(trafo).first;
            const_cast<PrintObjectTrafoAndInstances&>(*it).instances.emplace_back(PrintInstance{ nullptr, model_instance, shift });
        }
    return std::vector<PrintObjectTrafoAndInstances>(trafos.begin(), trafos.end());
}

// Compare just the layer ranges and their layer heights, not the associated configs.
// Ignore the layer heights if check_layer_heights is false.
static bool layer_height_ranges_equal(const t_layer_config_ranges &lr1, const t_layer_config_ranges &lr2, bool check_layer_height)
{
    if (lr1.size() != lr2.size())
        return false;
    auto it2 = lr2.begin();
    for (const auto &kvp1 : lr1) {
        const auto &kvp2 = *it2 ++;
        if (std::abs(kvp1.first.first  - kvp2.first.first ) > EPSILON ||
            std::abs(kvp1.first.second - kvp2.first.second) > EPSILON ||
            (check_layer_height && std::abs(kvp1.second.option("layer_height")->getFloat() - kvp2.second.option("layer_height")->getFloat()) > EPSILON))
            return false;
    }
    return true;
}

// Returns true if va == vb when all CustomGCode items that are not ToolChangeCode are ignored.
static bool custom_per_printz_gcodes_tool_changes_differ(const std::vector<CustomGCode::Item> &va, const std::vector<CustomGCode::Item> &vb)
{
	auto it_a = va.begin();
	auto it_b = vb.begin();
	while (it_a != va.end() || it_b != vb.end()) {
		if (it_a != va.end() && it_a->type != CustomGCode::ToolChange) {
			// Skip any CustomGCode items, which are not tool changes.
			++ it_a;
			continue;
		}
		if (it_b != vb.end() && it_b->type != CustomGCode::ToolChange) {
			// Skip any CustomGCode items, which are not tool changes.
			++ it_b;
			continue;
		}
		if (it_a == va.end() || it_b == vb.end())
			// va or vb contains more Tool Changes than the other.
			return true;
		assert(it_a->type == CustomGCode::ToolChange);
		assert(it_b->type == CustomGCode::ToolChange);
		if (*it_a != *it_b)
			// The two Tool Changes differ.
			return true;
		++ it_a;
		++ it_b;
	}
	// There is no change in custom Tool Changes.
	return false;
}

// Collect diffs of configuration values at various containers,
// resolve the filament rectract overrides of extruder retract values.
void Print::config_diffs(
	const DynamicPrintConfig &new_full_config, 
	t_config_option_keys &print_diff, t_config_option_keys &object_diff, t_config_option_keys &region_diff, 
	t_config_option_keys &full_config_diff, 
	DynamicPrintConfig &filament_overrides) const
{
    // Collect changes to print config, account for overrides of extruder retract values by filament presets.
    {
	    const std::vector<std::string> &extruder_retract_keys = print_config_def.extruder_retract_keys();
	    const std::string               filament_prefix       = "filament_";
	    for (const t_config_option_key &opt_key : m_config.keys()) {
	        const ConfigOption *opt_old = m_config.option(opt_key);
	        assert(opt_old != nullptr);
	        const ConfigOption *opt_new = new_full_config.option(opt_key);
			// assert(opt_new != nullptr);
			if (opt_new == nullptr)
				//FIXME This may happen when executing some test cases.
				continue;
	        const ConfigOption *opt_new_filament = std::binary_search(extruder_retract_keys.begin(), extruder_retract_keys.end(), opt_key) ? new_full_config.option(filament_prefix + opt_key) : nullptr;
	        if (opt_new_filament != nullptr && ! opt_new_filament->is_nil()) {
	        	// An extruder retract override is available at some of the filament presets.
	        	if (*opt_old != *opt_new || opt_new->overriden_by(opt_new_filament)) {
	        		auto opt_copy = opt_new->clone();
	        		opt_copy->apply_override(opt_new_filament);
	        		if (*opt_old == *opt_copy)
	        			delete opt_copy;
	        		else {
	        			filament_overrides.set_key_value(opt_key, opt_copy);
	        			print_diff.emplace_back(opt_key);
	        		}
	        	}
	        } else if (*opt_new != *opt_old)
	            print_diff.emplace_back(opt_key);
	    }
	}
	// Collect changes to object and region configs.
    object_diff = m_default_object_config.diff(new_full_config);
    region_diff = m_default_region_config.diff(new_full_config);
    // Prepare for storing of the full print config into new_full_config to be exported into the G-code and to be used by the PlaceholderParser.
    for (const t_config_option_key &opt_key : new_full_config.keys()) {
        const ConfigOption *opt_old = m_full_print_config.option(opt_key);
        const ConfigOption *opt_new = new_full_config.option(opt_key);
        if (opt_old == nullptr || *opt_new != *opt_old)
            full_config_diff.emplace_back(opt_key);
    }
}

std::vector<ObjectID> Print::print_object_ids() const 
{ 
    std::vector<ObjectID> out; 
    // Reserve one more for the caller to append the ID of the Print itself.
    out.reserve(m_objects.size() + 1);
    for (const PrintObject *print_object : m_objects)
        out.emplace_back(print_object->id());
    return out;
}

Print::ApplyStatus Print::apply(const Model &model, DynamicPrintConfig new_full_config)
{
#ifdef _DEBUG
    check_model_ids_validity(model);
#endif /* _DEBUG */

    // Normalize the config.
	new_full_config.option("print_settings_id",    true);
	new_full_config.option("filament_settings_id", true);
	new_full_config.option("printer_settings_id",  true);
    new_full_config.option("physical_printer_settings_id", true);
    new_full_config.normalize_fdm();

    // Find modified keys of the various configs. Resolve overrides extruder retract values by filament profiles.
	t_config_option_keys print_diff, object_diff, region_diff, full_config_diff;
	DynamicPrintConfig filament_overrides;
	this->config_diffs(new_full_config, print_diff, object_diff, region_diff, full_config_diff, filament_overrides);

    // Do not use the ApplyStatus as we will use the max function when updating apply_status.
    unsigned int apply_status = APPLY_STATUS_UNCHANGED;
    auto update_apply_status = [&apply_status](bool invalidated)
        { apply_status = std::max<unsigned int>(apply_status, invalidated ? APPLY_STATUS_INVALIDATED : APPLY_STATUS_CHANGED); };
    if (! (print_diff.empty() && object_diff.empty() && region_diff.empty()))
        update_apply_status(false);

    // Grab the lock for the Print / PrintObject milestones.
	tbb::mutex::scoped_lock lock(this->state_mutex());

    // The following call may stop the background processing.
    if (! print_diff.empty())
        update_apply_status(this->invalidate_state_by_config_options(print_diff));

    // Apply variables to placeholder parser. The placeholder parser is used by G-code export,
    // which should be stopped if print_diff is not empty.
    size_t num_extruders = m_config.nozzle_diameter.size();
    bool   num_extruders_changed = false;
    if (! full_config_diff.empty()) {
        update_apply_status(this->invalidate_step(psGCodeExport));
        // Set the profile aliases for the PrintBase::output_filename()
		m_placeholder_parser.set("print_preset",    new_full_config.option("print_settings_id")->clone());
		m_placeholder_parser.set("filament_preset", new_full_config.option("filament_settings_id")->clone());
		m_placeholder_parser.set("printer_preset",  new_full_config.option("printer_settings_id")->clone());
        m_placeholder_parser.set("physical_printer_preset",   new_full_config.option("physical_printer_settings_id")->clone());
		// We want the filament overrides to be applied over their respective extruder parameters by the PlaceholderParser.
		// see "Placeholders do not respect filament overrides." GH issue #3649
		m_placeholder_parser.apply_config(filament_overrides);
	    // It is also safe to change m_config now after this->invalidate_state_by_config_options() call.
	    m_config.apply_only(new_full_config, print_diff, true);
	    //FIXME use move semantics once ConfigBase supports it.
	    m_config.apply(filament_overrides);
	    // Handle changes to object config defaults
	    m_default_object_config.apply_only(new_full_config, object_diff, true);
	    // Handle changes to regions config defaults
	    m_default_region_config.apply_only(new_full_config, region_diff, true);
        m_full_print_config = std::move(new_full_config);
        if (num_extruders != m_config.nozzle_diameter.size()) {
        	num_extruders = m_config.nozzle_diameter.size();
        	num_extruders_changed = true;
    }
    }
    
    class LayerRanges
    {
    public:
        LayerRanges() {}
        // Convert input config ranges into continuous non-overlapping sorted vector of intervals and their configs.
        void assign(const t_layer_config_ranges &in) {
            m_ranges.clear();
            m_ranges.reserve(in.size());
            // Input ranges are sorted lexicographically. First range trims the other ranges.
            coordf_t last_z = 0;
            for (const std::pair<const t_layer_height_range, ModelConfig> &range : in)
				if (range.first.second > last_z) {
                    coordf_t min_z = std::max(range.first.first, 0.);
                    if (min_z > last_z + EPSILON) {
                        m_ranges.emplace_back(t_layer_height_range(last_z, min_z), nullptr);
                        last_z = min_z;
                    }
                    if (range.first.second > last_z + EPSILON) {
						const DynamicPrintConfig *cfg = &range.second.get();
                        m_ranges.emplace_back(t_layer_height_range(last_z, range.first.second), cfg);
                        last_z = range.first.second;
                    }
                }
            if (m_ranges.empty())
                m_ranges.emplace_back(t_layer_height_range(0, DBL_MAX), nullptr);
            else if (m_ranges.back().second == nullptr)
                m_ranges.back().first.second = DBL_MAX;
            else
                m_ranges.emplace_back(t_layer_height_range(m_ranges.back().first.second, DBL_MAX), nullptr);
        }

        const DynamicPrintConfig* config(const t_layer_height_range &range) const {
            auto it = std::lower_bound(m_ranges.begin(), m_ranges.end(), std::make_pair< t_layer_height_range, const DynamicPrintConfig*>(t_layer_height_range(range.first - EPSILON, range.second - EPSILON), nullptr));
            // #ys_FIXME_COLOR
            // assert(it != m_ranges.end());
            // assert(it == m_ranges.end() || std::abs(it->first.first  - range.first ) < EPSILON);
            // assert(it == m_ranges.end() || std::abs(it->first.second - range.second) < EPSILON);
            if (it == m_ranges.end() ||
                std::abs(it->first.first - range.first) > EPSILON ||
                std::abs(it->first.second - range.second) > EPSILON )
                return nullptr; // desired range doesn't found
            return (it == m_ranges.end()) ? nullptr : it->second;
        }
        std::vector<std::pair<t_layer_height_range, const DynamicPrintConfig*>>::const_iterator begin() const { return m_ranges.cbegin(); }
        std::vector<std::pair<t_layer_height_range, const DynamicPrintConfig*>>::const_iterator end() const { return m_ranges.cend(); }
    private:
        std::vector<std::pair<t_layer_height_range, const DynamicPrintConfig*>> m_ranges;
    };
    struct ModelObjectStatus {
        enum Status {
            Unknown,
            Old,
            New,
            Moved,
            Deleted,
        };
        ModelObjectStatus(ObjectID id, Status status = Unknown) : id(id), status(status) {}
		ObjectID     id;
        Status       status;
        LayerRanges  layer_ranges;
        // Search by id.
        bool operator<(const ModelObjectStatus &rhs) const { return id < rhs.id; }
    };
    std::set<ModelObjectStatus> model_object_status;

    // 1) Synchronize model objects.
    if (model.id() != m_model.id()) {
        // Kill everything, initialize from scratch.
        // Stop background processing.
        this->call_cancel_callback();
        update_apply_status(this->invalidate_all_steps());
        for (PrintObject *object : m_objects) {
            model_object_status.emplace(object->model_object()->id(), ModelObjectStatus::Deleted);
			update_apply_status(object->invalidate_all_steps());
			delete object;
        }
        m_objects.clear();
        for (PrintRegion *region : m_regions)
            delete region;
        m_regions.clear();
        m_model.assign_copy(model);
		for (const ModelObject *model_object : m_model.objects)
			model_object_status.emplace(model_object->id(), ModelObjectStatus::New);
    } else {
        if (m_model.custom_gcode_per_print_z != model.custom_gcode_per_print_z) {
            update_apply_status(num_extruders_changed || 
            	// Tool change G-codes are applied as color changes for a single extruder printer, no need to invalidate tool ordering.
            	//FIXME The tool ordering may be invalidated unnecessarily if the custom_gcode_per_print_z.mode is not applicable
            	// to the active print / model state, and then it is reset, so it is being applicable, but empty, thus the effect is the same.
            	(num_extruders > 1 && custom_per_printz_gcodes_tool_changes_differ(m_model.custom_gcode_per_print_z.gcodes, model.custom_gcode_per_print_z.gcodes)) ?
            	// The Tool Ordering and the Wipe Tower are no more valid.
            	this->invalidate_steps({ psWipeTower, psGCodeExport }) :
            	// There is no change in Tool Changes stored in custom_gcode_per_print_z, therefore there is no need to update Tool Ordering.
            	this->invalidate_step(psGCodeExport));
            m_model.custom_gcode_per_print_z = model.custom_gcode_per_print_z;
        }
        if (model_object_list_equal(m_model, model)) {
            // The object list did not change.
			for (const ModelObject *model_object : m_model.objects)
				model_object_status.emplace(model_object->id(), ModelObjectStatus::Old);
        } else if (model_object_list_extended(m_model, model)) {
            // Add new objects. Their volumes and configs will be synchronized later.
            update_apply_status(this->invalidate_step(psGCodeExport));
            for (const ModelObject *model_object : m_model.objects)
                model_object_status.emplace(model_object->id(), ModelObjectStatus::Old);
            for (size_t i = m_model.objects.size(); i < model.objects.size(); ++ i) {
                model_object_status.emplace(model.objects[i]->id(), ModelObjectStatus::New);
                m_model.objects.emplace_back(ModelObject::new_copy(*model.objects[i]));
				m_model.objects.back()->set_model(&m_model);
            }
        } else {
            // Reorder the objects, add new objects.
            // First stop background processing before shuffling or deleting the PrintObjects in the object list.
            this->call_cancel_callback();
            update_apply_status(this->invalidate_step(psGCodeExport));
            // Second create a new list of objects.
            std::vector<ModelObject*> model_objects_old(std::move(m_model.objects));
            m_model.objects.clear();
            m_model.objects.reserve(model.objects.size());
            auto by_id_lower = [](const ModelObject *lhs, const ModelObject *rhs){ return lhs->id() < rhs->id(); };
            std::sort(model_objects_old.begin(), model_objects_old.end(), by_id_lower);
            for (const ModelObject *mobj : model.objects) {
                auto it = std::lower_bound(model_objects_old.begin(), model_objects_old.end(), mobj, by_id_lower);
                if (it == model_objects_old.end() || (*it)->id() != mobj->id()) {
                    // New ModelObject added.
					m_model.objects.emplace_back(ModelObject::new_copy(*mobj));
					m_model.objects.back()->set_model(&m_model);
                    model_object_status.emplace(mobj->id(), ModelObjectStatus::New);
                } else {
                    // Existing ModelObject re-added (possibly moved in the list).
                    m_model.objects.emplace_back(*it);
                    model_object_status.emplace(mobj->id(), ModelObjectStatus::Moved);
                }
            }
            bool deleted_any = false;
			for (ModelObject *&model_object : model_objects_old) {
                if (model_object_status.find(ModelObjectStatus(model_object->id())) == model_object_status.end()) {
                    model_object_status.emplace(model_object->id(), ModelObjectStatus::Deleted);
                    deleted_any = true;
                } else
                    // Do not delete this ModelObject instance.
                    model_object = nullptr;
            }
            if (deleted_any) {
                // Delete PrintObjects of the deleted ModelObjects.
                std::vector<PrintObject*> print_objects_old = std::move(m_objects);
                m_objects.clear();
                m_objects.reserve(print_objects_old.size());
                for (PrintObject *print_object : print_objects_old) {
                    auto it_status = model_object_status.find(ModelObjectStatus(print_object->model_object()->id()));
                    assert(it_status != model_object_status.end());
                    if (it_status->status == ModelObjectStatus::Deleted) {
                        update_apply_status(print_object->invalidate_all_steps());
                        delete print_object;
                    } else
                        m_objects.emplace_back(print_object);
                }
                for (ModelObject *model_object : model_objects_old)
                    delete model_object;
            }
        }
    }

    // 2) Map print objects including their transformation matrices.
    struct PrintObjectStatus {
        enum Status {
            Unknown,
            Deleted,
            Reused,
            New
        };
        PrintObjectStatus(PrintObject *print_object, Status status = Unknown) : 
            id(print_object->model_object()->id()),
            print_object(print_object),
            trafo(print_object->trafo()),
            status(status) {}
        PrintObjectStatus(ObjectID id) : id(id), print_object(nullptr), trafo(Transform3d::Identity()), status(Unknown) {}
        // ID of the ModelObject & PrintObject
        ObjectID          id;
        // Pointer to the old PrintObject
        PrintObject     *print_object;
        // Trafo generated with model_object->world_matrix(true) 
        Transform3d      trafo;
        Status           status;
        // Search by id.
        bool operator<(const PrintObjectStatus &rhs) const { return id < rhs.id; }
    };
    std::multiset<PrintObjectStatus> print_object_status;
    for (PrintObject *print_object : m_objects)
        print_object_status.emplace(PrintObjectStatus(print_object));

    // 3) Synchronize ModelObjects & PrintObjects.
    for (size_t idx_model_object = 0; idx_model_object < model.objects.size(); ++ idx_model_object) {
        ModelObject &model_object = *m_model.objects[idx_model_object];
        auto it_status = model_object_status.find(ModelObjectStatus(model_object.id()));
        assert(it_status != model_object_status.end());
        assert(it_status->status != ModelObjectStatus::Deleted);
		const ModelObject& model_object_new = *model.objects[idx_model_object];
		const_cast<ModelObjectStatus&>(*it_status).layer_ranges.assign(model_object_new.layer_config_ranges);
        if (it_status->status == ModelObjectStatus::New)
            // PrintObject instances will be added in the next loop.
            continue;
        // Update the ModelObject instance, possibly invalidate the linked PrintObjects.
        assert(it_status->status == ModelObjectStatus::Old || it_status->status == ModelObjectStatus::Moved);
        // Check whether a model part volume was added or removed, their transformations or order changed.
        // Only volume IDs, volume types, transformation matrices and their order are checked, configuration and other parameters are NOT checked.
        bool model_parts_differ         = model_volume_list_changed(model_object, model_object_new, ModelVolumeType::MODEL_PART);
        bool modifiers_differ           = model_volume_list_changed(model_object, model_object_new, ModelVolumeType::PARAMETER_MODIFIER);
        bool supports_differ            = model_volume_list_changed(model_object, model_object_new, ModelVolumeType::SUPPORT_BLOCKER) ||
                                          model_volume_list_changed(model_object, model_object_new, ModelVolumeType::SUPPORT_ENFORCER);
        bool seam_position_differ       = model_volume_list_changed(model_object, model_object_new, ModelVolumeType::SEAM_POSITION);
        if (model_parts_differ || modifiers_differ ||
            model_object.origin_translation         != model_object_new.origin_translation   ||
            ! model_object.layer_height_profile.timestamp_matches(model_object_new.layer_height_profile) ||
            ! layer_height_ranges_equal(model_object.layer_config_ranges, model_object_new.layer_config_ranges, model_object_new.layer_height_profile.empty())) {
            // The very first step (the slicing step) is invalidated. One may freely remove all associated PrintObjects.
            auto range = print_object_status.equal_range(PrintObjectStatus(model_object.id()));
            for (auto it = range.first; it != range.second; ++ it) {
                update_apply_status(it->print_object->invalidate_all_steps());
                const_cast<PrintObjectStatus&>(*it).status = PrintObjectStatus::Deleted;
            }
            // Copy content of the ModelObject including its ID, do not change the parent.
            model_object.assign_copy(model_object_new);
        } else if (supports_differ || seam_position_differ || model_custom_supports_data_changed(model_object, model_object_new)) {
            // First stop background processing before shuffling or deleting the ModelVolumes in the ModelObject's list.
            if (supports_differ) {
                this->call_cancel_callback();
                update_apply_status(false);
            }
            // Invalidate just the supports step.
            auto range = print_object_status.equal_range(PrintObjectStatus(model_object.id()));
            for (auto it = range.first; it != range.second; ++ it)
                update_apply_status(it->print_object->invalidate_step(posSupportMaterial));
            if (supports_differ) {
                // Copy just the support volumes.
                model_volume_list_update_supports_seams(model_object, model_object_new);
            }else if (seam_position_differ) {
                // First stop background processing before shuffling or deleting the ModelVolumes in the ModelObject's list.
                this->call_cancel_callback();
                update_apply_status(false);
                // Invalidate just the gcode step.
                invalidate_step(psGCodeExport);
                // Copy just the seam volumes.
                model_volume_list_update_supports_seams(model_object, model_object_new);
            }
        } else if (model_custom_seam_data_changed(model_object, model_object_new)) {
            update_apply_status(this->invalidate_step(psGCodeExport));
        }
        if (! model_parts_differ && ! modifiers_differ) {
            // Synchronize Object's config.
            bool object_config_changed = ! model_object.config.timestamp_matches(model_object_new.config);
			if (object_config_changed)
				model_object.config.assign_config(model_object_new.config);
            if (! object_diff.empty() || object_config_changed || num_extruders_changed) {
                PrintObjectConfig new_config = PrintObject::object_config_from_model_object(m_default_object_config, model_object, num_extruders);
                auto range = print_object_status.equal_range(PrintObjectStatus(model_object.id()));
                for (auto it = range.first; it != range.second; ++ it) {
                    t_config_option_keys diff = it->print_object->config().diff(new_config);
                    if (! diff.empty()) {
                        update_apply_status(it->print_object->invalidate_state_by_config_options(diff));
                        it->print_object->config_apply_only(new_config, diff, true);
                    }
                }
            }
            // Synchronize (just copy) the remaining data of ModelVolumes (name, config, custom supports data).
            //FIXME What to do with m_material_id?
			model_volume_list_copy_configs(model_object /* dst */, model_object_new /* src */, ModelVolumeType::MODEL_PART);
			model_volume_list_copy_configs(model_object /* dst */, model_object_new /* src */, ModelVolumeType::PARAMETER_MODIFIER);
            layer_height_ranges_copy_configs(model_object.layer_config_ranges /* dst */, model_object_new.layer_config_ranges /* src */);
            // Copy the ModelObject name, input_file and instances. The instances will be compared against PrintObject instances in the next step.
            model_object.name       = model_object_new.name;
            model_object.input_file = model_object_new.input_file;
            // Only refresh ModelInstances if there is any change.
            if (model_object.instances.size() != model_object_new.instances.size() || 
            	! std::equal(model_object.instances.begin(), model_object.instances.end(), model_object_new.instances.begin(), [](auto l, auto r){ return l->id() == r->id(); })) {
            	// G-code generator accesses model_object.instances to generate sequential print ordering matching the Plater object list.
            	update_apply_status(this->invalidate_step(psGCodeExport));
	            model_object.clear_instances();
	            model_object.instances.reserve(model_object_new.instances.size());
	            for (const ModelInstance *model_instance : model_object_new.instances) {
	                model_object.instances.emplace_back(new ModelInstance(*model_instance));
	                model_object.instances.back()->set_model_object(&model_object);
	            }
	        } else if (! std::equal(model_object.instances.begin(), model_object.instances.end(), model_object_new.instances.begin(), 
	        		[](auto l, auto r){ return l->print_volume_state == r->print_volume_state && l->printable == r->printable && 
	        						           l->get_transformation().get_matrix().isApprox(r->get_transformation().get_matrix()); })) {
	        	// If some of the instances changed, the bounding box of the updated ModelObject is likely no more valid.
	        	// This is safe as the ModelObject's bounding box is only accessed from this function, which is called from the main thread only.
	 			model_object.invalidate_bounding_box();
	        	// Synchronize the content of instances.
	        	auto new_instance = model_object_new.instances.begin();
				for (auto old_instance = model_object.instances.begin(); old_instance != model_object.instances.end(); ++ old_instance, ++ new_instance) {
					(*old_instance)->set_transformation((*new_instance)->get_transformation());
                    (*old_instance)->print_volume_state = (*new_instance)->print_volume_state;
                    (*old_instance)->printable 		    = (*new_instance)->printable;
  				}
	        }
        }
    }

    // 4) Generate PrintObjects from ModelObjects and their instances.
    {
        std::vector<PrintObject*> print_objects_new;
        print_objects_new.reserve(std::max(m_objects.size(), m_model.objects.size()));
        bool new_objects = false;
        // Walk over all new model objects and check, whether there are matching PrintObjects.
        for (ModelObject *model_object : m_model.objects) {
            auto range = print_object_status.equal_range(PrintObjectStatus(model_object->id()));
            std::vector<const PrintObjectStatus*> old;
            if (range.first != range.second) {
                old.reserve(print_object_status.count(PrintObjectStatus(model_object->id())));
                for (auto it = range.first; it != range.second; ++ it)
                    if (it->status != PrintObjectStatus::Deleted)
                        old.emplace_back(&(*it));
            }
            // Generate a list of trafos and XY offsets for instances of a ModelObject
            // Producing the config for PrintObject on demand, caching it at print_object_last.
            const PrintObject *print_object_last = nullptr;
            auto print_object_apply_config = [this, &print_object_last, model_object, num_extruders](PrintObject* print_object) {
                print_object->config_apply(print_object_last ?
                    print_object_last->config() :
                    PrintObject::object_config_from_model_object(m_default_object_config, *model_object, num_extruders));
                print_object_last = print_object;
            };
            std::vector<PrintObjectTrafoAndInstances> new_print_instances = print_objects_from_model_object(*model_object);
            if (old.empty()) {
                // Simple case, just generate new instances.
                for (PrintObjectTrafoAndInstances &print_instances : new_print_instances) {
                    PrintObject *print_object = new PrintObject(this, model_object, print_instances.trafo, std::move(print_instances.instances));
                    print_object_apply_config(print_object);
                    print_objects_new.emplace_back(print_object);
                    // print_object_status.emplace(PrintObjectStatus(print_object, PrintObjectStatus::New));
                    new_objects = true;
                }
                continue;
            }
            // Complex case, try to merge the two lists.
            // Sort the old lexicographically by their trafos.
            std::sort(old.begin(), old.end(), [](const PrintObjectStatus *lhs, const PrintObjectStatus *rhs){ return transform3d_lower(lhs->trafo, rhs->trafo); });
            // Merge the old / new lists.
            auto it_old = old.begin();
            for (PrintObjectTrafoAndInstances &new_instances : new_print_instances) {
				for (; it_old != old.end() && transform3d_lower((*it_old)->trafo, new_instances.trafo); ++ it_old);
				if (it_old == old.end() || ! transform3d_equal((*it_old)->trafo, new_instances.trafo)) {
                    // This is a new instance (or a set of instances with the same trafo). Just add it.
                    PrintObject *print_object = new PrintObject(this, model_object, new_instances.trafo, std::move(new_instances.instances));
                    print_object_apply_config(print_object);
                    print_objects_new.emplace_back(print_object);
                    // print_object_status.emplace(PrintObjectStatus(print_object, PrintObjectStatus::New));
                    new_objects = true;
                    if (it_old != old.end())
                        const_cast<PrintObjectStatus*>(*it_old)->status = PrintObjectStatus::Deleted;
                } else {
                    // The PrintObject already exists and the copies differ.
					PrintBase::ApplyStatus status = (*it_old)->print_object->set_instances(std::move(new_instances.instances));
                    if (status != PrintBase::APPLY_STATUS_UNCHANGED)
						update_apply_status(status == PrintBase::APPLY_STATUS_INVALIDATED);
					print_objects_new.emplace_back((*it_old)->print_object);
					const_cast<PrintObjectStatus*>(*it_old)->status = PrintObjectStatus::Reused;
				}
            }
        }
        if (m_objects != print_objects_new) {
            this->call_cancel_callback();
			update_apply_status(this->invalidate_all_steps());
            m_objects = print_objects_new;
            // Delete the PrintObjects marked as Unknown or Deleted.
            bool deleted_objects = false;
            for (auto &pos : print_object_status)
                if (pos.status == PrintObjectStatus::Unknown || pos.status == PrintObjectStatus::Deleted) {
                    update_apply_status(pos.print_object->invalidate_all_steps());
                    delete pos.print_object;
					deleted_objects = true;
                }
			if (new_objects || deleted_objects)
				update_apply_status(this->invalidate_steps({ psSkirt, psBrim, psWipeTower, psGCodeExport }));
			if (new_objects)
	            update_apply_status(false);
        }
        print_object_status.clear();
    }

    // 5) Synchronize configs of ModelVolumes, synchronize AMF / 3MF materials (and their configs), refresh PrintRegions.
    // Update reference counts of regions from the remaining PrintObjects and their volumes.
    // Regions with zero references could and should be reused.
    for (PrintRegion *region : m_regions)
        region->m_refcnt = 0;
    for (PrintObject *print_object : m_objects) {
        for (int idx_region = 0; idx_region < print_object->region_volumes.size(); ++idx_region) {
            if (!print_object->region_volumes[idx_region].empty())
                ++ m_regions[idx_region]->m_refcnt;
        }
    }

    // All regions now have distinct settings.
    // Check whether applying the new region config defaults we'd get different regions.
    for (size_t region_id = 0; region_id < m_regions.size(); ++ region_id) {
        PrintRegion       &region = *m_regions[region_id];
        PrintRegionConfig  this_region_config;
        bool               this_region_config_set = false;
        for (PrintObject *print_object : m_objects) {
            const LayerRanges *layer_ranges;
            {
                auto it_status = model_object_status.find(ModelObjectStatus(print_object->model_object()->id()));
                assert(it_status != model_object_status.end());
                assert(it_status->status != ModelObjectStatus::Deleted);
                layer_ranges = &it_status->layer_ranges;
            }
            if (region_id < print_object->region_volumes.size()) {
                for (const std::pair<t_layer_height_range, int> &volume_and_range : print_object->region_volumes[region_id]) {
                    const ModelVolume        &volume             = *print_object->model_object()->volumes[volume_and_range.second];
                    const DynamicPrintConfig *layer_range_config = layer_ranges->config(volume_and_range.first);
                    if (this_region_config_set) {
                        // If the new config for this volume differs from the other
                        // volume configs currently associated to this region, it means
                        // the region subdivision does not make sense anymore.
                        if (! this_region_config.equals(PrintObject::region_config_from_model_volume(m_default_region_config, layer_range_config, volume, num_extruders)))
                            // Regions were split. Reset this print_object.
                            goto print_object_end;
                    } else {
                        this_region_config = PrintObject::region_config_from_model_volume(m_default_region_config, layer_range_config, volume, num_extruders);
						for (size_t i = 0; i < region_id; ++ i) {
							const PrintRegion &region_other = *m_regions[i];
							if (region_other.m_refcnt != 0 && region_other.config().equals(this_region_config))
								// Regions were merged. Reset this print_object.
								goto print_object_end;
						}
                        this_region_config_set = true;
                    }
                }
            }
            continue;
        print_object_end:
            update_apply_status(print_object->invalidate_all_steps());
            // Decrease the references to regions from this volume.
            int ireg = 0;
            for (const std::vector<std::pair<t_layer_height_range, int>> &volumes : print_object->region_volumes) {
                if (! volumes.empty())
                    -- m_regions[ireg]->m_refcnt;
                ++ ireg;
            }
            print_object->region_volumes.clear();
        }
        if (this_region_config_set) {
            t_config_option_keys diff = region.config().diff(this_region_config);
            if (! diff.empty()) {
                region.config_apply_only(this_region_config, diff, false);
                for (PrintObject *print_object : m_objects)
                    if (region_id < print_object->region_volumes.size() && ! print_object->region_volumes[region_id].empty())
                        update_apply_status(print_object->invalidate_state_by_config_options(diff));
            }
        }
    }

    // Possibly add new regions for the newly added or resetted PrintObjects.
    for (size_t idx_print_object = 0; idx_print_object < m_objects.size(); ++ idx_print_object) {
        PrintObject        &print_object0 = *m_objects[idx_print_object];
        const ModelObject  &model_object  = *print_object0.model_object();
        const LayerRanges *layer_ranges;
        {
            auto it_status = model_object_status.find(ModelObjectStatus(model_object.id()));
            assert(it_status != model_object_status.end());
            assert(it_status->status != ModelObjectStatus::Deleted);
            layer_ranges = &it_status->layer_ranges;
        }
        std::vector<int>   regions_in_object;
        regions_in_object.reserve(64);
        for (size_t i = idx_print_object; i < m_objects.size() && m_objects[i]->model_object() == &model_object; ++ i) {
            PrintObject &print_object = *m_objects[i];
			bool         fresh = print_object.region_volumes.empty();
            unsigned int volume_id = 0;
            unsigned int idx_region_in_object = 0;
            for (const ModelVolume *volume : model_object.volumes) {
                if (! volume->is_model_part() && ! volume->is_modifier()) {
					++ volume_id;
					continue;
				}
                // Filter the layer ranges, so they do not overlap and they contain at least a single layer.
                // Now insert a volume with a layer range to its own region.
                for (auto it_range = layer_ranges->begin(); it_range != layer_ranges->end(); ++ it_range) {
                    int region_id = -1;
                    if (&print_object == &print_object0) {
                        // Get the config applied to this volume.
                        PrintRegionConfig config = PrintObject::region_config_from_model_volume(m_default_region_config, it_range->second, *volume, num_extruders);
                        // Find an existing print region with the same config.
    					int idx_empty_slot = -1;
    					for (int i = 0; i < (int)m_regions.size(); ++ i) {
    						if (m_regions[i]->m_refcnt == 0) {
                                if (idx_empty_slot == -1)
                                    idx_empty_slot = i;
                            } else if (config.equals(m_regions[i]->config())) {
                                region_id = i;
                                break;
                            }
    					}
                        // If no region exists with the same config, create a new one.
    					if (region_id == -1) {
    						if (idx_empty_slot == -1) {
    							region_id = (int)m_regions.size();
    							this->add_region(config);
    						} else {
    							region_id = idx_empty_slot;
                                m_regions[region_id]->set_config(std::move(config));
    						}
                        }
                        regions_in_object.emplace_back(region_id);
                    } else
                        region_id = regions_in_object[idx_region_in_object ++];
                    // Assign volume to a region.
    				if (fresh) {
    					if ((size_t)region_id >= print_object.region_volumes.size() || print_object.region_volumes[region_id].empty())
    						++ m_regions[region_id]->m_refcnt;
    					print_object.add_region_volume(region_id, volume_id, it_range->first);
    				}
                }
				++ volume_id;
			}
        }
    }

    // Update SlicingParameters for each object where the SlicingParameters is not valid.
    // If it is not valid, then it is ensured that PrintObject.m_slicing_params is not in use
    // (posSlicing and posSupportMaterial was invalidated).
    for (PrintObject *object : m_objects)
        object->update_slicing_parameters();

#ifdef _DEBUG
    check_model_ids_equal(m_model, model);
#endif /* _DEBUG */

	return static_cast<ApplyStatus>(apply_status);
}

bool Print::has_infinite_skirt() const
{
    return (m_config.draft_shield && m_config.skirts > 0) || (m_config.ooze_prevention && this->extruders().size() > 1);
}

bool Print::has_skirt() const
{
    return (m_config.skirt_height > 0 && m_config.skirts > 0) || this->has_infinite_skirt();
}

static inline bool sequential_print_horizontal_clearance_valid(const Print &print)
{
	Polygons convex_hulls_other;
	std::map<ObjectID, Polygon> map_model_object_to_convex_hull;
	for (const PrintObject *print_object : print.objects()) {
        double dist_grow = PrintConfig::min_object_distance(&print_object->config());
	    assert(! print_object->model_object()->instances.empty());
	    assert(! print_object->instances().empty());
	    ObjectID model_object_id = print_object->model_object()->id();
	    auto it_convex_hull = map_model_object_to_convex_hull.find(model_object_id);
        // Get convex hull of all printable volumes assigned to this print object.
        ModelInstance *model_instance0 = print_object->model_object()->instances.front();
	    if (it_convex_hull == map_model_object_to_convex_hull.end()) {
	        // Calculate the convex hull of a printable object. 
	        // Grow convex hull with the clearance margin.
	        // FIXME: Arrangement has different parameters for offsetting (jtMiter, limit 2)
	        // which causes that the warning will be showed after arrangement with the
	        // appropriate object distance. Even if I set this to jtMiter the warning still shows up.
	        it_convex_hull = map_model_object_to_convex_hull.emplace_hint(it_convex_hull, model_object_id, 
                offset(print_object->model_object()->convex_hull_2d(
	                        Geometry::assemble_transform(Vec3d::Zero(), model_instance0->get_rotation(), model_instance0->get_scaling_factor(), model_instance0->get_mirror())),
                	// Shrink the extruder_clearance_radius a tiny bit, so that if the object arrangement algorithm placed the objects
	                // exactly by satisfying the extruder_clearance_radius, this test will not trigger collision.
	                float(scale_(0.5 * dist_grow - EPSILON)),
	                jtRound, float(scale_(0.1))).front());
	    }
	    // Make a copy, so it may be rotated for instances.
        //FIXME seems like the rotation isn't taken into account
	    Polygon convex_hull0 = it_convex_hull->second;
        //this can create bugs in macos, for reasons.
		double z_diff = Geometry::rotation_diff_z(model_instance0->get_rotation(), print_object->instances().front().model_instance->get_rotation());
		if (std::abs(z_diff) > EPSILON)
			convex_hull0.rotate(z_diff);
        // Now we check that no instance of convex_hull intersects any of the previously checked object instances.
        for (const PrintInstance& instance : print_object->instances()) {
            Polygon convex_hull = convex_hull0;
            // instance.shift is a position of a centered object, while model object may not be centered.
            // Conver the shift from the PrintObject's coordinates into ModelObject's coordinates by removing the centering offset.
            convex_hull.translate(instance.shift - print_object->center_offset());
	        if (! intersection(convex_hulls_other, (Polygons)convex_hull).empty())
                return false;
	        convex_hulls_other.emplace_back(std::move(convex_hull));
        }

        /*
        'old' superslicer sequential_print_horizontal_clearance_valid, that is better at skirts, but need some works, as the arrange has changed.
	    // Now we check that no instance of convex_hull intersects any of the previously checked object instances.
	    for (const PrintInstance &instance : print_object->instances()) {
            Polygons convex_hull = print_object->model_object()->convex_hull_2d(
                Geometry::assemble_transform(Vec3d::Zero(),
                    instance.model_instance->get_rotation(), instance.model_instance->get_scaling_factor(), instance.model_instance->get_mirror()));
                // Shrink the extruder_clearance_radius a tiny bit, so that if the object arrangement algorithm placed the objects
                // exactly by satisfying the extruder_clearance_radius, this test will not trigger collision.
                //float(scale_(0.5 * print.config().extruder_clearance_radius.value - EPSILON)),
                //jtRound, float(scale_(0.1)));
            if (convex_hull.empty())
                continue;
	        // instance.shift is a position of a centered object, while model object may not be centered.
	        // Conver the shift from the PrintObject's coordinates into ModelObject's coordinates by removing the centering offset.
            for(Polygon &poly : convex_hull)
	            poly.translate(instance.shift - print_object->center_offset());
	        if (! intersection(
                    convex_hulls_other, 
                    offset(convex_hull[0], double(scale_(PrintConfig::min_object_distance(&instance.print_object->config(),0.)) - SCALED_EPSILON), jtRound, scale_(0.1))).empty())
	            return false;
            double extra_grow = PrintConfig::min_object_distance(&instance.print_object->config(), 1.);
            if (extra_grow > 0)
                convex_hull = offset(convex_hull, scale_(extra_grow));
	        polygons_append(convex_hulls_other, convex_hull);
	    }
        */
	}
	return true;
}

static inline bool sequential_print_vertical_clearance_valid(const Print &print)
{
	std::vector<const PrintInstance*> print_instances_ordered = sort_object_instances_by_model_order(print);
	// Ignore the last instance printed.
	print_instances_ordered.pop_back();
	// Find the other highest instance.
	auto it = std::max_element(print_instances_ordered.begin(), print_instances_ordered.end(), [](auto l, auto r) {
		return l->print_object->height() < r->print_object->height();
	});
    return it == print_instances_ordered.end() || (*it)->print_object->height() <= scale_(print.config().extruder_clearance_height.value);
}

// Precondition: Print::validate() requires the Print::apply() to be called its invocation.
std::pair<PrintBase::PrintValidationError, std::string> Print::validate() const
{
    if (m_objects.empty())
        return { PrintBase::PrintValidationError::pveWrongPosition, L("All objects are outside of the print volume.") };

    if (extruders().empty())
        return { PrintBase::PrintValidationError::pveNoPrint, L("The supplied settings will cause an empty print.") };

    if (m_config.complete_objects) {
    	if (! sequential_print_horizontal_clearance_valid(*this))
            return { PrintBase::PrintValidationError::pveWrongPosition, L("Some objects are too close; your extruder will collide with them.") };
        if (! sequential_print_vertical_clearance_valid(*this))
            return { PrintBase::PrintValidationError::pveWrongPosition,L("Some objects are too tall and cannot be printed without extruder collisions.") };
    }

    if (m_config.spiral_vase) {
        size_t total_copies_count = 0;
        for (const PrintObject *object : m_objects)
            total_copies_count += object->instances().size();
        // #4043
        if (total_copies_count > 1 && ! m_config.complete_objects.value)
            return { PrintBase::PrintValidationError::pveWrongSettings,L("Only a single object may be printed at a time in Spiral Vase mode. "
                     "Either remove all but the last object, or enable sequential mode by \"complete_objects\".") };
        assert(m_objects.size() == 1 || config().complete_objects.value);
        size_t num_regions = 0;
        for (const std::vector<std::pair<t_layer_height_range, int>> &volumes_per_region : m_objects.front()->region_volumes)
        	if (! volumes_per_region.empty())
        		++ num_regions;
        if (num_regions > 1)
            return { PrintBase::PrintValidationError::pveWrongSettings,L("The Spiral Vase option can only be used when printing single material objects.") };
    }

    if (this->has_wipe_tower() && ! m_objects.empty()) {
        // Make sure all extruders use same diameter filament and have the same nozzle diameter
        // EPSILON comparison is used for nozzles and 10 % tolerance is used for filaments
        double first_nozzle_diam = m_config.nozzle_diameter.get_at(extruders().front());
        double first_filament_diam = m_config.filament_diameter.get_at(extruders().front());
        for (const auto& extruder_idx : extruders()) {
            double nozzle_diam = m_config.nozzle_diameter.get_at(extruder_idx);
            double filament_diam = m_config.filament_diameter.get_at(extruder_idx);
            if (nozzle_diam - EPSILON > first_nozzle_diam || nozzle_diam + EPSILON < first_nozzle_diam
             || std::abs((filament_diam-first_filament_diam)/first_filament_diam) > 0.1)
                return { PrintBase::PrintValidationError::pveWrongSettings,L("The wipe tower is only supported if all extruders have the same nozzle diameter "
                         "and use filaments of the same diameter.") };
        }

        if (m_config.gcode_flavor != gcfRepRap && m_config.gcode_flavor != gcfSprinter && m_config.gcode_flavor != gcfRepetier && m_config.gcode_flavor != gcfMarlin
            && m_config.gcode_flavor != gcfKlipper)
            return { PrintBase::PrintValidationError::pveWrongSettings,L("The Wipe Tower is currently only supported for the Marlin, RepRap/Sprinter and Repetier G-code flavors.") };
        if (! m_config.use_relative_e_distances)
            return { PrintBase::PrintValidationError::pveWrongSettings,L("The Wipe Tower is currently only supported with the relative extruder addressing (use_relative_e_distances=1).") };
        if (m_config.ooze_prevention)
            return { PrintBase::PrintValidationError::pveWrongSettings,L("Ooze prevention is currently not supported with the wipe tower enabled.") };
        if (m_config.use_volumetric_e)
            return { PrintBase::PrintValidationError::pveWrongSettings,L("The Wipe Tower currently does not support volumetric E (use_volumetric_e=0).") };
        if (m_config.complete_objects && extruders().size() > 1)
            return { PrintBase::PrintValidationError::pveWrongSettings,L("The Wipe Tower is currently not supported for multimaterial sequential prints.") };
        
        if (m_objects.size() > 1) {
            bool                                has_custom_layering = false;
            std::vector<std::vector<coordf_t>>  layer_height_profiles;
            for (const PrintObject *object : m_objects) {
                has_custom_layering = ! object->model_object()->layer_config_ranges.empty() || ! object->model_object()->layer_height_profile.empty();
                if (has_custom_layering) {
                    layer_height_profiles.assign(m_objects.size(), std::vector<coordf_t>());
                    break;
                }
            }
            const SlicingParameters &slicing_params0 = m_objects.front()->slicing_parameters();
            size_t            tallest_object_idx = 0;
            if (has_custom_layering)
                PrintObject::update_layer_height_profile(*m_objects.front()->model_object(), slicing_params0, layer_height_profiles.front());
            for (size_t i = 1; i < m_objects.size(); ++ i) {
                const PrintObject       *object         = m_objects[i];
                const SlicingParameters &slicing_params = object->slicing_parameters();
                if (std::abs(slicing_params.first_print_layer_height - slicing_params0.first_print_layer_height) > EPSILON ||
                    std::abs(slicing_params.layer_height             - slicing_params0.layer_height            ) > EPSILON)
                    return { PrintBase::PrintValidationError::pveWrongSettings,L("The Wipe Tower is only supported for multiple objects if they have equal layer heights") };
                if (slicing_params.raft_layers() != slicing_params0.raft_layers())
                    return { PrintBase::PrintValidationError::pveWrongSettings,L("The Wipe Tower is only supported for multiple objects if they are printed over an equal number of raft layers") };
                if (object->config().support_material_contact_distance_type != m_objects.front()->config().support_material_contact_distance_type
                    || object->config().support_material_contact_distance_top != m_objects.front()->config().support_material_contact_distance_top
                    || object->config().support_material_contact_distance_bottom != m_objects.front()->config().support_material_contact_distance_bottom)
                    return { PrintBase::PrintValidationError::pveWrongSettings,L("The Wipe Tower is only supported for multiple objects if they are printed with the same support_material_contact_distance") };
                if (! equal_layering(slicing_params, slicing_params0))
                    return { PrintBase::PrintValidationError::pveWrongSettings,L("The Wipe Tower is only supported for multiple objects if they are sliced equally.") };
                if (has_custom_layering) {
                    PrintObject::update_layer_height_profile(*object->model_object(), slicing_params, layer_height_profiles[i]);
                    if (*(layer_height_profiles[i].end()-2) > *(layer_height_profiles[tallest_object_idx].end()-2))
                        tallest_object_idx = i;
                }
            }

            if (has_custom_layering) {
                const std::vector<coordf_t> &layer_height_profile_tallest = layer_height_profiles[tallest_object_idx];
                for (size_t idx_object = 0; idx_object < m_objects.size(); ++ idx_object) {
                    if (idx_object == tallest_object_idx)
                        continue;
                    const std::vector<coordf_t> &layer_height_profile = layer_height_profiles[idx_object];

                    // The comparison of the profiles is not just about element-wise equality, some layers may not be
                    // explicitely included. Always remember z and height of last reference layer that in the vector
                    // and compare to that. In case some layers are in the vectors multiple times, only the last entry is
                    // taken into account and compared.
                    size_t i = 0; // index into tested profile
                    size_t j = 0; // index into reference profile
                    coordf_t ref_z = -1.;
                    coordf_t next_ref_z = layer_height_profile_tallest[0];
                    coordf_t ref_height = -1.;
                    while (i < layer_height_profile.size()) {
                        coordf_t this_z = layer_height_profile[i];
                        // find the last entry with this z
                        while (i+2 < layer_height_profile.size() && layer_height_profile[i+2] == this_z)
                            i += 2;

                        coordf_t this_height = layer_height_profile[i+1];
                        if (ref_height < -1. || next_ref_z < this_z + EPSILON) {
                            ref_z = next_ref_z;
                            do { // one layer can be in the vector several times
                                ref_height = layer_height_profile_tallest[j+1];
                                if (j+2 >= layer_height_profile_tallest.size())
                                    break;
                                j += 2;
                                next_ref_z = layer_height_profile_tallest[j];
                            } while (ref_z == next_ref_z);
                        }
                        if (std::abs(this_height - ref_height) > EPSILON)
                            return { PrintBase::PrintValidationError::pveWrongSettings,L("The Wipe tower is only supported if all objects have the same variable layer height") };
                        i += 2;
                    }
                }
            }
    }
    }
    
	{
		std::vector<uint16_t> extruders = this->extruders();

		// Find the smallest used nozzle diameter and the number of unique nozzle diameters.
		double min_nozzle_diameter = std::numeric_limits<double>::max();
		double max_nozzle_diameter = 0;
		for (uint16_t extruder_id : extruders) {
			double dmr = m_config.nozzle_diameter.get_at(extruder_id);
			min_nozzle_diameter = std::min(min_nozzle_diameter, dmr);
			max_nozzle_diameter = std::max(max_nozzle_diameter, dmr);
		}

#if 0
        // We currently allow one to assign extruders with a higher index than the number
        // of physical extruders the machine is equipped with, as the Printer::apply() clamps them.
        unsigned int total_extruders_count = m_config.nozzle_diameter.size();
        for (const auto& extruder_idx : extruders)
            if ( extruder_idx >= total_extruders_count )
                return L("One or more object were assigned an extruder that the printer does not have.");
#endif

        auto validate_extrusion_width = [min_nozzle_diameter, max_nozzle_diameter](const ConfigBase &config, const char *opt_key, double layer_height, std::string &err_msg) -> bool {
            double extrusion_width_min = config.get_abs_value(opt_key, min_nozzle_diameter);
            double extrusion_width_max = config.get_abs_value(opt_key, max_nozzle_diameter);
        	if (extrusion_width_min == 0) {
        		// Default "auto-generated" extrusion width is always valid.
        	} else if (extrusion_width_min <= layer_height) {
        		err_msg = (boost::format(L("%1%=%2% mm is too low to be printable at a layer height %3% mm")) % opt_key % extrusion_width_min % layer_height).str();
				return false;
			} else if (extrusion_width_max >= max_nozzle_diameter * 4.) {
				err_msg = (boost::format(L("Excessive %1%=%2% mm to be printable with a nozzle diameter %3% mm")) % opt_key % extrusion_width_max % max_nozzle_diameter).str();
				return false;
			}
			return true;
		};
        for (PrintObject *object : m_objects) {
            if (object->config().raft_layers > 0 || object->config().support_material.value) {
                if ((object->config().support_material_extruder == 0 || object->config().support_material_interface_extruder == 0) && max_nozzle_diameter - min_nozzle_diameter > EPSILON) {
                    // The object has some form of support and either support_material_extruder or support_material_interface_extruder
                    // will be printed with the current tool without a forced tool change. Play safe, assert that all object nozzles
                    // are of the same diameter.
                    return { PrintBase::PrintValidationError::pveWrongSettings,L("Printing with multiple extruders of differing nozzle diameters. "
                           "If support is to be printed with the current extruder (support_material_extruder == 0 or support_material_interface_extruder == 0), "
                           "all nozzles have to be of the same diameter.") };
                }
                if (this->has_wipe_tower()) {
                    if (object->config().support_material_contact_distance_type == zdNone) {
                        // Soluble interface
                        if (! object->config().support_material_synchronize_layers)
                            return { PrintBase::PrintValidationError::pveWrongSettings,L("For the Wipe Tower to work with the soluble supports, the support layers need to be synchronized with the object layers.") };
                    } else {
                        // Non-soluble interface
                        if (object->config().support_material_extruder != 0 || object->config().support_material_interface_extruder != 0)
                            return { PrintBase::PrintValidationError::pveWrongSettings,L("The Wipe Tower currently supports the non-soluble supports only if they are printed with the current extruder without triggering a tool change. "
                                     "(both support_material_extruder and support_material_interface_extruder need to be set to 0).") };
                    }
                }
            }
            
            // validate first_layer_height
            double first_layer_height = object->config().get_abs_value("first_layer_height", this->m_config.nozzle_diameter.get_at(0));
            double first_layer_min_nozzle_diameter;
            if (object->config().raft_layers > 0) {
                // if we have raft layers, only support material extruder is used on first layer
                size_t first_layer_extruder = object->config().raft_layers == 1
                    ? object->config().support_material_interface_extruder-1
                    : object->config().support_material_extruder-1;
                first_layer_min_nozzle_diameter = (first_layer_extruder == size_t(-1)) ? 
                    min_nozzle_diameter : 
                    m_config.nozzle_diameter.get_at(first_layer_extruder);
            } else {
                // if we don't have raft layers, any nozzle diameter is potentially used in first layer
                first_layer_min_nozzle_diameter = min_nozzle_diameter;
            }
            if (first_layer_height > first_layer_min_nozzle_diameter)
                return { PrintBase::PrintValidationError::pveWrongSettings,L("First layer height can't be greater than nozzle diameter") };
            
            // validate layer_height
            double layer_height = object->config().layer_height.value;
            if (layer_height > min_nozzle_diameter)
                return { PrintBase::PrintValidationError::pveWrongSettings,L("Layer height can't be greater than nozzle diameter") };

            // Validate extrusion widths.
            std::string err_msg;
            if (! validate_extrusion_width(object->config(), "extrusion_width", layer_height, err_msg))
                return { PrintBase::PrintValidationError::pveWrongSettings,err_msg };
            if ((object->config().support_material || object->config().raft_layers > 0) && ! validate_extrusion_width(object->config(), "support_material_extrusion_width", layer_height, err_msg))
                return { PrintBase::PrintValidationError::pveWrongSettings,err_msg };
            for (const char *opt_key : { "perimeter_extrusion_width", "external_perimeter_extrusion_width", "infill_extrusion_width", "solid_infill_extrusion_width", "top_infill_extrusion_width" })
                for (size_t i = 0; i < object->region_volumes.size(); ++ i)
                    if (! object->region_volumes[i].empty() && ! validate_extrusion_width(this->get_region(i)->config(), opt_key, layer_height, err_msg))
                        return { PrintBase::PrintValidationError::pveWrongSettings, err_msg };
        }
    }

    return { PrintValidationError::pveNone, std::string() };
}

#if 0
// the bounding box of objects placed in copies position
// (without taking skirt/brim/support material into account)
BoundingBox Print::bounding_box() const
{
    BoundingBox bb;
    for (const PrintObject *object : m_objects)
        for (const PrintInstance &instance : object->instances()) {
            BoundingBox bb2(object->bounding_box());
            bb.merge(bb2.min + instance.shift);
            bb.merge(bb2.max + instance.shift);
        }
    return bb;
}

// the total bounding box of extrusions, including skirt/brim/support material
// this methods needs to be called even when no steps were processed, so it should
// only use configuration values
BoundingBox Print::total_bounding_box() const
{
    // get objects bounding box
    BoundingBox bb = this->bounding_box();
    
    // we need to offset the objects bounding box by at least half the perimeters extrusion width
    Flow perimeter_flow = m_objects.front()->get_layer(0)->get_region(0)->flow(frPerimeter);
    double extra = perimeter_flow.width/2;
    
    // consider support material
    if (this->has_support_material()) {
        extra = std::max(extra, SUPPORT_MATERIAL_MARGIN);
    }
    
    // consider brim and skirt
    if (m_config.brim_width.value > 0) {
        Flow brim_flow = this->brim_flow();
        extra = std::max(extra, m_config.brim_width.value + brim_flow.width/2);
    }
    if (this->has_skirt()) {
        int skirts = m_config.skirts.value;
        if (skirts == 0 && this->has_infinite_skirt()) skirts = 1;
        Flow skirt_flow = this->skirt_flow();
        extra = std::max(
            extra,
            m_config.brim_width.value
                + m_config.skirt_distance.value
                + skirts * skirt_flow.spacing()
                + skirt_flow.width/2
        );
    }
    
    if (extra > 0)
        bb.offset(scale_(extra));
    
    return bb;
}
#endif

double Print::skirt_first_layer_height() const
{
    if (m_objects.empty()) 
        throw Slic3r::InvalidArgument("skirt_first_layer_height() can't be called without PrintObjects");
    return m_objects.front()->config().get_abs_value("first_layer_height");
}

Flow Print::brim_flow(size_t extruder_id) const
{
    ConfigOptionFloatOrPercent width = m_config.first_layer_extrusion_width;
    if (width.value <= 0) 
        width = m_regions.front()->config().perimeter_extrusion_width;
    if (width.value <= 0) 
        width = m_objects.front()->config().extrusion_width;
    
    /* We currently use a random region's perimeter extruder.
       While this works for most cases, we should probably consider all of the perimeter
       extruders and take the one with, say, the smallest index.
       The same logic should be applied to the code that selects the extruder during G-code
       generation as well. */
    return Flow::new_from_config_width(
        frPerimeter,
		width,
        (float)m_config.nozzle_diameter.get_at(extruder_id),
		(float)this->skirt_first_layer_height()
    );
}

Flow Print::skirt_flow(size_t extruder_id) const
{
    ConfigOptionFloatOrPercent width = m_config.skirt_extrusion_width;
    if (width.value <= 0 && m_config.first_layer_extrusion_width.value > 0)
        width = m_config.first_layer_extrusion_width;
    if (width.value <= 0) 
        width = m_regions.front()->config().perimeter_extrusion_width;
    if (width.value <= 0)
        width = m_objects.front()->config().extrusion_width;
    
    /* We currently use a random object's support material extruder.
       While this works for most cases, we should probably consider all of the support material
       extruders and take the one with, say, the smallest index;
       The same logic should be applied to the code that selects the extruder during G-code
       generation as well. */
    /* or select the used extruder with the highest nozzle diameter, to be on the safe side.*/
    return Flow::new_from_config_width(
        frPerimeter,
		width,
		(float)m_config.nozzle_diameter.get_at(extruder_id),
		(float)this->skirt_first_layer_height()
    );
}

bool Print::has_support_material() const
{
    for (const PrintObject *object : m_objects)
        if (object->has_support_material()) 
            return true;
    return false;
}

/*  This method assigns extruders to the volumes having a material
    but not having extruders set in the volume config. */
void Print::auto_assign_extruders(ModelObject* model_object) const
{
    // only assign extruders if object has more than one volume
    if (model_object->volumes.size() < 2)
        return;
    
//    size_t extruders = m_config.nozzle_diameter.values.size();
    for (size_t volume_id = 0; volume_id < model_object->volumes.size(); ++ volume_id) {
        ModelVolume *volume = model_object->volumes[volume_id];
        //FIXME Vojtech: This assigns an extruder ID even to a modifier volume, if it has a material assigned.
        if ((volume->is_model_part() || volume->is_modifier()) && ! volume->material_id().empty() && ! volume->config.has("extruder"))
            volume->config.set("extruder", int(volume_id + 1));
    }
}

// Slicing process, running at a background thread.
void Print::process()
{
    name_tbb_thread_pool_threads();

    BOOST_LOG_TRIVIAL(info) << "Starting the slicing process." << log_memory_info();
    for (PrintObject *obj : m_objects)
        obj->make_perimeters();
    this->set_status(70, L("Infilling layers"));
    for (PrintObject *obj : m_objects)
        obj->infill();
    for (PrintObject *obj : m_objects)
        obj->ironing();
    for (PrintObject *obj : m_objects)
        obj->generate_support_material();
    if (this->set_started(psWipeTower)) {
        m_wipe_tower_data.clear();
        m_tool_ordering.clear();
        if (this->has_wipe_tower()) {
            //this->set_status(95, L("Generating wipe tower"));
            this->_make_wipe_tower();
        } else if (! this->config().complete_objects.value) {
            // Initialize the tool ordering, so it could be used by the G-code preview slider for planning tool changes and filament switches.
            m_tool_ordering = ToolOrdering(*this, -1, false);
            if (m_tool_ordering.empty() || m_tool_ordering.last_extruder() == unsigned(-1))
                throw Slic3r::SlicingError("The print is empty. The model is not printable with current print settings.");
        }
        this->set_done(psWipeTower);
    }
    if (this->set_started(psSkirt)) {
        m_skirt.clear();

        m_skirt_convex_hull.clear();
        m_first_layer_convex_hull.points.clear();
        for (PrintObject *obj : m_objects) {
            obj->m_skirt.clear();
        }
        if (this->has_skirt()) {
            this->set_status(88, L("Generating skirt"));
            if (config().complete_objects && !config().complete_objects_one_skirt){
                for (PrintObject *obj : m_objects){
                    //create a skirt "pattern" (one per object)
                    const std::vector<PrintInstance> copies{ obj->instances() };
                    obj->m_instances.clear();
                    obj->m_instances.emplace_back();
                    this->_make_skirt({ obj }, obj->m_skirt);
                    obj->m_instances = copies;
                }
            } else {
                this->_make_skirt(m_objects, m_skirt);
            }
        }
        this->set_done(psSkirt);
    }
	if (this->set_started(psBrim)) {
        m_brim.clear();
        //group object per brim settings
        m_first_layer_convex_hull.points.clear();
        std::vector<std::vector<PrintObject*>> obj_groups;
        for (PrintObject *obj : m_objects) {
            obj->m_brim.clear();
            bool added = false;
            for (std::vector<PrintObject*> &obj_group : obj_groups) {
                if (obj_group.front()->config().brim_ears.value == obj->config().brim_ears.value
                    && obj_group.front()->config().brim_ears_max_angle.value == obj->config().brim_ears_max_angle.value
                    && obj_group.front()->config().brim_ears_pattern.value == obj->config().brim_ears_pattern.value
                    && obj_group.front()->config().brim_inside_holes.value == obj->config().brim_inside_holes.value
                    && obj_group.front()->config().brim_offset.value == obj->config().brim_offset.value
                    && obj_group.front()->config().brim_width.value == obj->config().brim_width.value
                    && obj_group.front()->config().brim_width_interior.value == obj->config().brim_width_interior.value) {
                    added = true;
                    obj_group.push_back(obj);
                }
            }
            if (!added) {
                obj_groups.emplace_back();
                obj_groups.back().push_back(obj);
            }
        }
        ExPolygons brim_area;
        if (obj_groups.size() > 1) {
            for (std::vector<PrintObject*> &obj_group : obj_groups)
                for (const PrintObject *object : obj_group)
                    if (!object->m_layers.empty())
                        for (const PrintInstance &pt : object->m_instances) {
                            int first_idx = brim_area.size();
                            brim_area.insert(brim_area.end(), object->m_layers.front()->lslices.begin(), object->m_layers.front()->lslices.end());
                            for (int i = first_idx; i < brim_area.size(); i++) {
                                brim_area[i].translate(pt.shift.x(), pt.shift.y());
                            }
                        }
        }
        for (std::vector<PrintObject*> &obj_group : obj_groups) {
            const PrintObjectConfig &brim_config = obj_group.front()->config();
            if (brim_config.brim_width > 0 || brim_config.brim_width_interior > 0) {
                this->set_status(88, L("Generating brim"));
                if (config().complete_objects) {
                    for (PrintObject *obj : obj_group) {
                        //get flow
                        std::vector<uint16_t> set_extruders = this->object_extruders({ obj });
                        append(set_extruders, this->support_material_extruders());
                        sort_remove_duplicates(set_extruders);
                        Flow        flow = this->brim_flow(set_extruders.empty() ? m_regions.front()->config().perimeter_extruder - 1 : set_extruders.front());
                        //don't consider other objects/instances. It's not possible because it's duplicated by some code afterward... i think.
                        brim_area.clear();
                        //create a brim "pattern" (one per object)
                        const std::vector<PrintInstance> copies{ obj->instances() };
                        obj->m_instances.clear();
                        obj->m_instances.emplace_back();
                        if (brim_config.brim_width > 0) {
                            if (brim_config.brim_ears)
                                this->_make_brim_ears(flow, { obj }, brim_area, obj->m_brim);
                            else
                                this->_make_brim(flow, { obj }, brim_area, obj->m_brim);
                        }
                        if (brim_config.brim_width_interior > 0) {
                            _make_brim_interior(flow, { obj }, brim_area, obj->m_brim);
                        }
                        obj->m_instances = copies;
                    }
                } else {
                    if (obj_groups.size() > 1)
                        brim_area = union_ex(brim_area);
                    //get the first extruder in the list for these objects... replicating gcode generation
                    std::vector<uint16_t> set_extruders = this->object_extruders(m_objects);
                    append(set_extruders, this->support_material_extruders());
                    sort_remove_duplicates(set_extruders);
                    Flow        flow = this->brim_flow(set_extruders.empty() ? m_regions.front()->config().perimeter_extruder - 1 : set_extruders.front());
                    if (brim_config.brim_ears)
                        this->_make_brim_ears(flow, obj_group, brim_area, m_brim);
                    else
                        this->_make_brim(flow, obj_group, brim_area, m_brim);
                    if (brim_config.brim_width_interior > 0)
                        _make_brim_interior(flow, obj_group, brim_area, m_brim);
                }
            }
        }
        // Brim depends on skirt (brim lines are trimmed by the skirt lines), therefore if
        // the skirt gets invalidated, brim gets invalidated as well and the following line is called.
        this->finalize_first_layer_convex_hull();
       this->set_done(psBrim);
    }
    BOOST_LOG_TRIVIAL(info) << "Slicing process finished." << log_memory_info();
}

// G-code export process, running at a background thread.
// The export_gcode may die for various reasons (fails to process output_filename_format,
// write error into the G-code, cannot execute post-processing scripts).
// It is up to the caller to show an error message.
std::string Print::export_gcode(const std::string& path_template, GCodeProcessor::Result* result, ThumbnailsGeneratorCallback thumbnail_cb)
{
    // output everything to a G-code file
    // The following call may die if the output_filename_format template substitution fails.
    std::string path = this->output_filepath(path_template);
    std::string message;
    if (!path.empty() && result == nullptr) {
        // Only show the path if preview_data is not set -> running from command line.
        message = L("Exporting G-code");
        message += " to ";
        message += path;
    } else
        message = L("Generating G-code");
    this->set_status(90, message);

    // The following line may die for multiple reasons.
    GCode gcode;
    gcode.do_export(this, path.c_str(), result, thumbnail_cb);
    return path.c_str();
}

void Print::_make_skirt(const PrintObjectPtrs &objects, ExtrusionEntityCollection &out)
{
    // First off we need to decide how tall the skirt must be.
    // The skirt_height option from config is expressed in layers, but our
    // object might have different layer heights, so we need to find the print_z
    // of the highest layer involved.
    // Note that unless has_infinite_skirt() == true
    // the actual skirt might not reach this $skirt_height_z value since the print
    // order of objects on each layer is not guaranteed and will not generally
    // include the thickest object first. It is just guaranteed that a skirt is
    // prepended to the first 'n' layers (with 'n' = skirt_height).
    // $skirt_height_z in this case is the highest possible skirt height for safety.
    coordf_t skirt_height_z = 0.;
    for (const PrintObject *object : objects) {
        size_t skirt_layers = this->has_infinite_skirt() ?
            object->layer_count() : 
            std::min(size_t(m_config.skirt_height.value), object->layer_count());
        skirt_height_z = std::max(skirt_height_z, object->m_layers[skirt_layers-1]->print_z);
    }
    
    // Collect points from all layers contained in skirt height.
    Points points;
    for (const PrintObject *object : objects) {
        Points object_points;
        // Get object layers up to skirt_height_z.
        for (const Layer *layer : object->m_layers) {
            if (layer->print_z > skirt_height_z)
                break;
            for (const ExPolygon &expoly : layer->lslices)
                // Collect the outer contour points only, ignore holes for the calculation of the convex hull.
                append(object_points, expoly.contour.points);
        }
        // Get support layers up to skirt_height_z.
        for (const SupportLayer *layer : object->support_layers()) {
            if (layer->print_z > skirt_height_z)
                break;
            for (const ExtrusionEntity *extrusion_entity : layer->support_fills.entities) {
                Polylines poly;
                extrusion_entity->collect_polylines(poly);
                for (const Polyline &polyline : poly)
                    append(object_points, polyline.points);
            }
        }
        // Repeat points for each object copy.
        for (const PrintInstance &instance : object->instances()) {
            Points copy_points = object_points;
            for (Point &pt : copy_points)
                pt += instance.shift;
            append(points, copy_points);
        }
    }

    // Include the wipe tower.
    append(points, this->first_layer_wipe_tower_corners());

    if (points.size() < 3)
        // At least three points required for a convex hull.
        return;
    
    this->throw_if_canceled();
    Polygon convex_hull = Slic3r::Geometry::convex_hull(points);
    
    // Skirt may be printed on several layers, having distinct layer heights,
    // but loops must be aligned so can't vary width/spacing
    // TODO: use each extruder's own flow
    double first_layer_height = this->skirt_first_layer_height();
    
    std::vector<size_t> extruders;
    std::vector<double> extruders_e_per_mm;
    {
        std::vector<uint16_t> set_extruders = this->object_extruders(objects);
        append(set_extruders, this->support_material_extruders());
        sort_remove_duplicates(set_extruders);
        extruders.reserve(set_extruders.size());
        extruders_e_per_mm.reserve(set_extruders.size());
        for (unsigned int extruder_id : set_extruders) {
            Flow   flow = this->skirt_flow(extruder_id);
            float  spacing = flow.spacing();
            double mm3_per_mm = flow.mm3_per_mm();
            extruders.push_back(extruder_id);
            extruders_e_per_mm.push_back(Extruder((unsigned int)extruder_id, &m_config).e_per_mm(mm3_per_mm));
        }
    }

    // Number of skirt loops per skirt layer.
    size_t n_skirts = m_config.skirts.value;
    if (this->has_infinite_skirt() && n_skirts == 0)
        n_skirts = 1;

    // Initial offset of the brim inner edge from the object (possible with a support & raft).
    // The skirt will touch the brim if the brim is extruded.
    auto   distance = float(scale_(m_config.skirt_distance.value) - this->skirt_flow(extruders[extruders.size()-1]).spacing()/2.);

    size_t lines_per_extruder = (n_skirts + extruders.size() - 1) / extruders.size();
    size_t current_lines_per_extruder = n_skirts - lines_per_extruder * (extruders.size() - 1);

    // Draw outlines from outside to inside.
    // Loop while we have less skirts than required or any extruder hasn't reached the min length if any.
    std::vector<coordf_t> extruded_length(extruders.size(), 0.);
    for (size_t i = n_skirts, extruder_idx = 0, nb_skirts = 1; i > 0; -- i) {
        Flow   flow = this->skirt_flow(extruders[extruders.size() - (1+ extruder_idx)]);
        float  spacing = flow.spacing();
        double mm3_per_mm = flow.mm3_per_mm();
        this->throw_if_canceled();
        // Offset the skirt outside.
        distance += float(scale_(spacing/2));
        // Generate the skirt centerline.
        Polygon loop;
        {
            Polygons loops = offset(convex_hull, distance, ClipperLib::jtRound, float(scale_(0.1)));
            Geometry::simplify_polygons(loops, scale_(0.05), &loops);
			if (loops.empty())
				break;
			loop = loops.front();
        }
        distance += float(scale_(spacing / 2));
        // Extrude the skirt loop.
        ExtrusionLoop eloop(elrSkirt);
        eloop.paths.emplace_back(ExtrusionPath(
            ExtrusionPath(
                erSkirt,
                (float)mm3_per_mm,         // this will be overridden at G-code export time
                flow.width,
				(float)first_layer_height  // this will be overridden at G-code export time
            )));
        eloop.paths.back().polyline = loop.split_at_first_point();
        //we make it clowkwise, but as it will be reversed, it will be ccw
        eloop.make_clockwise();
        out.append(eloop);
        if (m_config.min_skirt_length.value > 0) {
            // The skirt length is limited. Sum the total amount of filament length extruded, in mm.
            extruded_length[extruder_idx] += unscale<double>(loop.length()) * extruders_e_per_mm[extruder_idx];
            if (extruded_length[extruder_idx] < m_config.min_skirt_length.value) {
                // Not extruded enough yet with the current extruder. Add another loop.
                if (i == 1)
                    ++ i;
            } else {
                assert(extruded_length[extruder_idx] >= m_config.min_skirt_length.value);
                // Enough extruded with the current extruder. Extrude with the next one,
                // until the prescribed number of skirt loops is extruded.
                if (extruder_idx + 1 < extruders.size())
                    if (nb_skirts < current_lines_per_extruder) {
                        nb_skirts++;
                    } else {
                        current_lines_per_extruder = lines_per_extruder;
                        nb_skirts = 1;
                        ++extruder_idx;
                    }
            }
        } else {
            // The skirt lenght is not limited, extrude the skirt with the 1st extruder only.
        }
    }
    // Brims were generated inside out, reverse to print the outmost contour first.
    out.reverse();

    // Remember the outer edge of the last skirt line extruded as m_skirt_convex_hull.
    for (Polygon &poly : offset(convex_hull, distance + 0.5f * float(this->skirt_flow(extruders[extruders.size() - 1]).scaled_spacing()), ClipperLib::jtRound, float(scale_(0.1))))
        append(m_skirt_convex_hull, std::move(poly.points));
}

void Print::_extrude_brim_from_tree(std::vector<std::vector< BrimLoop>>& loops, const Polygons& frontiers, const Flow& flow, ExtrusionEntityCollection& out, bool reversed/*= false*/) {

    // nest contour loops (same as in perimetergenerator)
    for (int d = loops.size() - 1; d >= 1; --d) {
        std::vector<BrimLoop>& contours_d = loops[d];
        // loop through all contours having depth == d
        for (int i = 0; i < (int)contours_d.size(); ++i) {
            const BrimLoop& loop = contours_d[i];
            // find the contour loop that contains it
            for (int t = d - 1; t >= 0; --t) {
                for (size_t j = 0; j < loops[t].size(); ++j) {
                    BrimLoop& candidate_parent = loops[t][j];
                    bool test = reversed
                        ? loop.polygon().contains(candidate_parent.line.first_point())
                        : candidate_parent.polygon().contains(loop.line.first_point());
                    if (test) {
                        candidate_parent.children.push_back(loop);
                        contours_d.erase(contours_d.begin() + i);
                        --i;
                        goto NEXT_CONTOUR;
                    }
                }
            }
            //didn't find a contour: add it as a root loop
            loops[0].push_back(loop);
            contours_d.erase(contours_d.begin() + i);
            --i;
        NEXT_CONTOUR:;
        }
    }

    //def
    //cut loops if they go inside a forbidden region
    std::function<void(BrimLoop&)> cut_loop = [&frontiers](BrimLoop& to_cut) {
        Polylines result;
        if (to_cut.is_loop)
            result = intersection_pl(Polygons{ to_cut.polygon() }, frontiers, true);
        else
            result = intersection_pl(Polylines{ to_cut.line }, frontiers, true);
        if (result.empty())
            to_cut.line.points.clear();
        else {
            if (to_cut.line.points != result[0].points) {
                to_cut.line.points = result[0].points;
                to_cut.is_loop = false;
            }
            for (int i = 1; i < result.size(); i++)
                to_cut.children.insert(to_cut.children.begin() + i - 1, BrimLoop(std::move(result[i])));
        }

    };
    //calls
    for (std::vector<BrimLoop>& loops : loops)
        for (BrimLoop& loop : loops)
            cut_loop(loop);

    this->throw_if_canceled();


    //def: push into extrusions, in the right order
    float mm3_per_mm = float(flow.mm3_per_mm());
    float width = float(flow.width);
    float height = float(this->skirt_first_layer_height());
    int nextIdx = 0;
    std::function<void(BrimLoop&, ExtrusionEntityCollection*)>* extrude_ptr;
    std::function<void(BrimLoop&, ExtrusionEntityCollection*) > extrude = [&mm3_per_mm, &width, &height, &extrude_ptr, &nextIdx](BrimLoop& to_cut, ExtrusionEntityCollection* parent) {
        int idx = nextIdx++;
        bool i_have_line = !to_cut.line.points.empty() && to_cut.line.is_valid();
        if (!i_have_line && to_cut.children.empty()) {
            //nothing
        } else if (i_have_line && to_cut.children.empty()) {
            if (to_cut.line.points.back() == to_cut.line.points.front()) {
                ExtrusionPath path(erSkirt, mm3_per_mm, width, height);
                path.polyline.points = to_cut.line.points;
                parent->entities.emplace_back(new ExtrusionLoop(std::move(path), elrSkirt));
            } else {
                ExtrusionPath* extrusion_path = new ExtrusionPath(erSkirt, mm3_per_mm, width, height);
                parent->entities.push_back(extrusion_path);
                extrusion_path->polyline = to_cut.line;
            }
        } else if (!i_have_line && !to_cut.children.empty()) {
            if (to_cut.children.size() == 1) {
                (*extrude_ptr)(to_cut.children[0], parent);
            } else {
                ExtrusionEntityCollection* mycoll = new ExtrusionEntityCollection();
                parent->entities.push_back(mycoll);
                for (BrimLoop& child : to_cut.children)
                    (*extrude_ptr)(child, mycoll);
            }
        } else {
            ExtrusionEntityCollection* print_me_first = new ExtrusionEntityCollection();
            parent->entities.push_back(print_me_first);
            print_me_first->no_sort = true;
            if (to_cut.line.points.back() == to_cut.line.points.front()) {
                ExtrusionPath path(erSkirt, mm3_per_mm, width, height);
                path.polyline.points = to_cut.line.points;
                print_me_first->entities.emplace_back(new ExtrusionLoop(std::move(path), elrSkirt));
            } else {
                ExtrusionPath* extrusion_path = new ExtrusionPath(erSkirt, mm3_per_mm, width, height);
                print_me_first->entities.push_back(extrusion_path);
                extrusion_path->polyline = to_cut.line;
            }
            if (to_cut.children.size() == 1) {
                (*extrude_ptr)(to_cut.children[0], print_me_first);
            } else {
                ExtrusionEntityCollection* children = new ExtrusionEntityCollection();
                print_me_first->entities.push_back(children);
                for (BrimLoop& child : to_cut.children)
                    (*extrude_ptr)(child, children);
            }
        }
    };
    extrude_ptr = &extrude;

    if (loops.empty()) {
        BOOST_LOG_TRIVIAL(error) << "Failed to extrude brim: no loops to extrude, are you sure your settings are ok?";
        return;
    }

    //launch extrude
    for (BrimLoop& loop : loops[0]) {
        extrude(loop, &out);
    }
}

//TODO: test if no regression vs old _make_brim.
// this new one can extrude brim for an object inside an other object.
void Print::_make_brim(const Flow &flow, const PrintObjectPtrs &objects, ExPolygons &unbrimmable, ExtrusionEntityCollection &out) {
    const PrintObjectConfig &brim_config = objects.front()->config();
    coord_t brim_offset = scale_(brim_config.brim_offset.value);
    ExPolygons    islands;
    for (PrintObject *object : objects) {
        ExPolygons object_islands;
        for (ExPolygon &expoly : object->m_layers.front()->lslices)
            if(brim_config.brim_inside_holes || brim_config.brim_width_interior > 0)
                object_islands.push_back(brim_offset == 0 ? expoly : offset_ex(expoly, brim_offset)[0]);
            else
                object_islands.emplace_back(brim_offset == 0 ? to_expolygon(expoly.contour) : offset_ex(to_expolygon(expoly.contour), brim_offset)[0]);
        if (!object->support_layers().empty()) {
            Polygons polys = object->support_layers().front()->support_fills.polygons_covered_by_spacing(float(SCALED_EPSILON));
            for (Polygon poly : polys) {
                for (ExPolygon& expoly2 : union_ex(Polygons{ poly })) {
                    if (brim_config.brim_inside_holes || brim_config.brim_width_interior > 0) {
                        object_islands.emplace_back(brim_offset == 0 ? expoly2 : offset_ex(expoly2, brim_offset)[0]);
                    } else {
                        object_islands.emplace_back(brim_offset == 0 ? to_expolygon(expoly2.contour) : offset_ex(to_expolygon(expoly2.contour), brim_offset)[0]);
                    }
                }
            }
        }
        islands.reserve(islands.size() + object_islands.size() * object->m_instances.size());
        for (const PrintInstance &pt : object->m_instances) {
            for (ExPolygon &poly : object_islands) {
                islands.push_back(poly);
                islands.back().translate(pt.shift.x(), pt.shift.y());
            }
        }
    }

    this->throw_if_canceled();

    //simplify & merge
    ExPolygons unbrimmable_areas;
    for (ExPolygon &expoly : islands)
        for (ExPolygon &expoly : expoly.simplify(SCALED_RESOLUTION))
            unbrimmable_areas.emplace_back(std::move(expoly));
    islands = union_ex(unbrimmable_areas, true);
    unbrimmable_areas = islands;

    //get the brimmable area
    const size_t num_loops = size_t(floor(std::max(0.,(brim_config.brim_width.value - brim_config.brim_offset.value)) / flow.spacing()));
    ExPolygons brimmable_areas;
    for (ExPolygon &expoly : islands) {
        for (Polygon poly : offset(expoly.contour, num_loops * flow.scaled_spacing(), jtSquare)) {
            brimmable_areas.emplace_back();
            brimmable_areas.back().contour = poly;
            brimmable_areas.back().contour.make_counter_clockwise();
            brimmable_areas.back().holes.push_back(expoly.contour);
            brimmable_areas.back().holes.back().make_clockwise();
        }
    }
    brimmable_areas = union_ex(brimmable_areas);
    this->throw_if_canceled();

    //don't collide with objects
    brimmable_areas = diff_ex(brimmable_areas, unbrimmable_areas, true);
    brimmable_areas = diff_ex(brimmable_areas, unbrimmable, true);

    this->throw_if_canceled();

    //now get all holes, use them to create loops
    std::vector<std::vector<BrimLoop>> loops;
    ExPolygons bigger_islands;
    //grow a half of spacing, to go to the first extrusion polyline.
    Polygons unbrimmable_polygons;
    for (ExPolygon &expoly : islands) {
        unbrimmable_polygons.push_back(expoly.contour);
        //do it separately because we don't want to union them
        for (ExPolygon &big_expoly : offset_ex(expoly, double(flow.scaled_spacing()) * 0.5, jtSquare)) {
            bigger_islands.emplace_back(big_expoly);
            unbrimmable_polygons.insert(unbrimmable_polygons.end(), big_expoly.holes.begin(), big_expoly.holes.end());
        }
    }
    islands = bigger_islands;
    for (size_t i = 0; i < num_loops; ++i) {
        loops.emplace_back();
        this->throw_if_canceled();
        // only grow the contour, not holes
        bigger_islands.clear();
        if (i > 0) {
            for (ExPolygon &expoly : islands) {
                for (Polygon &big_contour : offset(expoly.contour, double(flow.scaled_spacing()) * i, jtSquare)) {
                    bigger_islands.emplace_back(expoly);
                    bigger_islands.back().contour = big_contour;
                }
            }
        } else bigger_islands = islands;
        bigger_islands = union_ex(bigger_islands);
        for (ExPolygon &expoly : bigger_islands) {
            loops[i].emplace_back(expoly.contour);
            //also add hole, in case of it's merged with a contour. <= HOW? if there's an island inside a hole! (in the same object)
            for (Polygon &hole : expoly.holes)
                //but remove the points that are inside the holes of islands
                for (Polyline& pl : diff_pl(Polygons{ hole }, unbrimmable_polygons, true))
                    loops[i].emplace_back(pl);
        }
    }

    std::reverse(loops.begin(), loops.end());

    //intersection
    Polygons frontiers;
    //use contour from brimmable_areas (external frontier)
    for (ExPolygon &expoly : brimmable_areas) {
        frontiers.push_back(expoly.contour);
        frontiers.back().make_counter_clockwise();
    }
    // add internal frontier
    frontiers.insert(frontiers.begin(), unbrimmable_polygons.begin(), unbrimmable_polygons.end());

    _extrude_brim_from_tree(loops, frontiers, flow, out);
    
    unbrimmable.insert(unbrimmable.end(), brimmable_areas.begin(), brimmable_areas.end());
}

void Print::_make_brim_ears(const Flow &flow, const PrintObjectPtrs &objects, ExPolygons &unbrimmable, ExtrusionEntityCollection &out) {
    const PrintObjectConfig &brim_config = objects.front()->config();
    Points pt_ears;
    coord_t brim_offset = scale_(brim_config.brim_offset.value);
    ExPolygons islands;
    ExPolygons unbrimmable_with_support = unbrimmable;
    for (PrintObject *object : objects) {
        ExPolygons object_islands;
        for (const ExPolygon &expoly : object->m_layers.front()->lslices)
            if (brim_config.brim_inside_holes || brim_config.brim_width_interior > 0)
                object_islands.push_back(brim_offset==0?expoly:offset_ex(expoly, brim_offset)[0]);
            else
                object_islands.emplace_back(brim_offset == 0 ? to_expolygon(expoly.contour) : offset_ex(to_expolygon(expoly.contour), brim_offset)[0]);

        if (!object->support_layers().empty()) {
            Polygons polys = object->support_layers().front()->support_fills.polygons_covered_by_spacing(float(SCALED_EPSILON));
            for (Polygon poly : polys)
                for (ExPolygon& expoly2 : union_ex(Polygons{ poly }))
                    //don't put ears over supports unless it's 100% fill
                    if (object->config().support_material_solid_first_layer) {
                        if (brim_config.brim_inside_holes || brim_config.brim_width_interior > 0)
                            object_islands.push_back(brim_offset == 0 ? expoly2 : offset_ex(expoly2, brim_offset)[0]);
                        else
                            object_islands.emplace_back(brim_offset == 0 ? to_expolygon(expoly2.contour) : offset_ex(to_expolygon(expoly2.contour), brim_offset)[0]);
                    } else {
                        unbrimmable_with_support.push_back(expoly2);
                    }
        }
        islands.reserve(islands.size() + object_islands.size() * object->m_instances.size());
        for (const PrintInstance &copy_pt : object->m_instances)
            for (const ExPolygon &poly : object_islands) {
                islands.push_back(poly);
                islands.back().translate(copy_pt.shift.x(), copy_pt.shift.y());
                Polygon decimated_polygon = poly.contour;
                // brim_ears_detection_length codepath
                if (object->config().brim_ears_detection_length.value > 0) {
                    //copy
                    decimated_polygon = poly.contour;
                    //decimate polygon
                    Points points = poly.contour.points;
                    points.push_back(points.front());
                    decimated_polygon = Polygon(MultiPoint::_douglas_peucker(points, scale_(object->config().brim_ears_detection_length.value)));
                }
                for (const Point &p : decimated_polygon.convex_points(brim_config.brim_ears_max_angle.value * PI / 180.0)) {
                    pt_ears.push_back(p);
                    pt_ears.back() += (copy_pt.shift);
                }
            }
    }

    islands = union_ex(islands, true);

    //get the brimmable area (for the return value only)
    const size_t num_loops = size_t(floor((brim_config.brim_width.value - brim_config.brim_offset.value) / flow.spacing()));
    ExPolygons brimmable_areas;
    Polygons contours;
    Polygons holes;
    for (ExPolygon &expoly : islands) {
        for (Polygon poly : offset(expoly.contour, num_loops * flow.scaled_width(), jtSquare)) {
            contours.push_back(poly);
        }
        holes.push_back(expoly.contour);
    }
    brimmable_areas = diff_ex(union_(contours), union_(holes));
    brimmable_areas = diff_ex(brimmable_areas, unbrimmable_with_support, true);

    this->throw_if_canceled();

    if (brim_config.brim_ears_pattern.value == InfillPattern::ipConcentric) {

        //create loops (same as standard brim)
        Polygons loops;
        islands = offset_ex(islands, -0.5f * double(flow.scaled_spacing()));
        for (size_t i = 0; i < num_loops; ++i) {
            this->throw_if_canceled();
            islands = offset_ex(islands, double(flow.scaled_spacing()), jtSquare);
            for (ExPolygon &expoly : islands) {
                Polygon poly = expoly.contour;
                poly.points.push_back(poly.points.front());
                Points p = MultiPoint::_douglas_peucker(poly.points, SCALED_RESOLUTION);
                p.pop_back();
                poly.points = std::move(p);
                loops.push_back(poly);
            }
        }
        //order path with least travel possible
        loops = union_pt_chained_outside_in(loops, false);

        //create ear pattern
        coord_t size_ear = (scale_((brim_config.brim_width.value - brim_config.brim_offset.value)) - flow.scaled_spacing());
        Polygon point_round;
        for (size_t i = 0; i < POLY_SIDES; i++) {
            double angle = (2.0 * PI * i) / POLY_SIDES;
            point_round.points.emplace_back(size_ear * cos(angle), size_ear * sin(angle));
        }

        //create ears
        Polygons mouse_ears;
        ExPolygons mouse_ears_ex;
        for (Point pt : pt_ears) {
            mouse_ears.push_back(point_round);
            mouse_ears.back().translate(pt);
            mouse_ears_ex.emplace_back();
            mouse_ears_ex.back().contour = mouse_ears.back();
        }

        //intersection
        Polylines lines = intersection_pl(loops, mouse_ears);
        this->throw_if_canceled();

        //reorder & extrude them
        Polylines lines_sorted = _reorder_brim_polyline(lines, out, flow);

        //push into extrusions
        extrusion_entities_append_paths(
            out.entities,
            lines_sorted,
            erSkirt,
            float(flow.mm3_per_mm()),
            float(flow.width),
            float(this->skirt_first_layer_height())
        );

        ExPolygons new_brim_area = intersection_ex(brimmable_areas, mouse_ears_ex);
        unbrimmable.insert(unbrimmable.end(), new_brim_area.begin(), new_brim_area.end());
    } else /* brim_config.brim_ears_pattern.value == InfillPattern::ipRectilinear */{
        
        //create ear pattern
        coord_t size_ear = (scale_((brim_config.brim_width.value - brim_config.brim_offset.value)) - flow.scaled_spacing());
        Polygon point_round;
        for (size_t i = 0; i < POLY_SIDES; i++) {
            double angle = (2.0 * PI * i) / POLY_SIDES;
            point_round.points.emplace_back(size_ear * cos(angle), size_ear * sin(angle));
        }

        //create ears
        ExPolygons mouse_ears_ex;
        for (Point pt : pt_ears) {
            mouse_ears_ex.emplace_back();
            mouse_ears_ex.back().contour = point_round;
            mouse_ears_ex.back().contour.translate(pt);
        }

        ExPolygons new_brim_area = intersection_ex(brimmable_areas, mouse_ears_ex);

        std::unique_ptr<Fill> filler = std::unique_ptr<Fill>(Fill::new_from_type(ipRectiWithPerimeter));
        filler->angle = 0;

        FillParams fill_params;
        fill_params.density = 1.f;
        fill_params.fill_exactly = true;
        fill_params.flow = flow;
        fill_params.role = erSkirt;
        filler->init_spacing(flow.spacing(), fill_params);
        for (const ExPolygon &expoly : new_brim_area) {
            Surface surface(stPosInternal | stDensSparse, expoly);
            filler->fill_surface_extrusion(&surface, fill_params, out.entities);
        }

        unbrimmable.insert(unbrimmable.end(), new_brim_area.begin(), new_brim_area.end());
    }

}

void Print::_make_brim_interior(const Flow &flow, const PrintObjectPtrs &objects, ExPolygons &unbrimmable_areas, ExtrusionEntityCollection &out) {
    // Brim is only printed on first layer and uses perimeter extruder.

    const PrintObjectConfig &brim_config = objects.front()->config();
    coord_t brim_offset = scale_(brim_config.brim_offset.value);
    ExPolygons    islands;
    for (PrintObject *object : objects) {
        ExPolygons object_islands;
        for (ExPolygon &expoly : object->m_layers.front()->lslices)
            object_islands.push_back(brim_offset == 0 ? expoly : offset_ex(expoly, brim_offset)[0]);
        if (!object->support_layers().empty()) {
            Polygons polys = object->support_layers().front()->support_fills.polygons_covered_by_spacing(float(SCALED_EPSILON));
            for (Polygon poly : polys)
                for (ExPolygon& expoly2 : union_ex(Polygons{ poly }))
                    object_islands.push_back(brim_offset == 0 ? expoly2 : offset_ex(expoly2, brim_offset)[0]);
        }
        islands.reserve(islands.size() + object_islands.size() * object->instances().size());
        for (const PrintInstance &instance : object->instances())
            for (ExPolygon &poly : object_islands) {
                islands.push_back(poly);
                islands.back().translate(instance.shift.x(), instance.shift.y());
            }
    }

    islands = union_ex(islands);

    //to have the brimmable areas, get all holes, use them as contour , add smaller hole inside and make a diff with unbrimmable
    const size_t num_loops = size_t(floor((brim_config.brim_width_interior.value - brim_config.brim_offset.value) / flow.spacing()));
    ExPolygons brimmable_areas;
    Polygons islands_to_loops;
    for (const ExPolygon &expoly : islands) {
        for (const Polygon &hole : expoly.holes) {
            brimmable_areas.emplace_back();
            brimmable_areas.back().contour = hole;
            brimmable_areas.back().contour.make_counter_clockwise();
            for (Polygon poly : offset(brimmable_areas.back().contour, -flow.scaled_width() * (double)num_loops, jtSquare)) {
                brimmable_areas.back().holes.push_back(poly);
                brimmable_areas.back().holes.back().make_clockwise();
            }
            islands_to_loops.insert(islands_to_loops.begin(), brimmable_areas.back().contour);
        }
    }

    brimmable_areas = diff_ex(brimmable_areas, islands, true);
    brimmable_areas = diff_ex(brimmable_areas, unbrimmable_areas, true);

    //now get all holes, use them to create loops
    std::vector<std::vector<BrimLoop>> loops;
    for (size_t i = 0; i < num_loops; ++i) {
        this->throw_if_canceled();
        loops.emplace_back();
        islands_to_loops = offset(islands_to_loops, double(-flow.scaled_spacing()), jtSquare);
        for (Polygon &poly : islands_to_loops) {
            poly.points.push_back(poly.points.front());
            Points p = MultiPoint::_douglas_peucker(poly.points, SCALED_RESOLUTION);
            p.pop_back();
            poly.points = std::move(p);
        }
        for (Polygon& poly : offset(islands_to_loops, 0.5f * double(flow.scaled_spacing())))
            loops[i].emplace_back(poly);
    }
    // loops = union_pt_chained_outside_in(loops, false);
    std::reverse(loops.begin(), loops.end());

    //intersection
    Polygons frontiers;
    for (ExPolygon &expoly : brimmable_areas) {
        for (Polygon &big_contour : offset(expoly.contour, 0.1f * flow.scaled_width())) {
            frontiers.push_back(big_contour);
            for (Polygon &hole : expoly.holes) {
                frontiers.push_back(hole);
                //don't reverse it! back! or it will be ignored by intersection_pl. 
                //frontiers.back().reverse();
            }
        }
    }

    _extrude_brim_from_tree(loops, frontiers, flow, out, true);

    unbrimmable_areas.insert(unbrimmable_areas.end(), brimmable_areas.begin(), brimmable_areas.end());
}

/// reorder & join polyline if their ending are near enough, then extrude the brim from the polyline into 'out'.
Polylines Print::_reorder_brim_polyline(Polylines lines, ExtrusionEntityCollection &out, const Flow &flow) {
    //reorder them
    std::sort(lines.begin(), lines.end(), [](const Polyline &a, const Polyline &b)->bool { return a.closest_point(Point(0, 0))->y() < b.closest_point(Point(0, 0))->y(); });
    Polylines lines_sorted;
    Polyline* previous = NULL;
    Polyline* best = NULL;
    double best_dist = -1;
    size_t best_idx = 0;
    while (lines.size() > 0) {
        if (previous == NULL) {
            lines_sorted.push_back(lines.back());
            previous = &lines_sorted.back();
            lines.erase(lines.end() - 1);
        } else {
            best = NULL;
            best_dist = -1;
            best_idx = 0;
            for (size_t i = 0; i < lines.size(); ++i) {
                Polyline &viewed_line = lines[i];
                double dist = viewed_line.points.front().distance_to(previous->points.front());
                dist = std::min(dist, viewed_line.points.front().distance_to(previous->points.back()));
                dist = std::min(dist, viewed_line.points.back().distance_to(previous->points.front()));
                dist = std::min(dist, viewed_line.points.back().distance_to(previous->points.back()));
                if (dist < best_dist || best == NULL) {
                    best = &viewed_line;
                    best_dist = dist;
                    best_idx = i;
                }
            }
            if (best != NULL) {
                //copy new line inside the sorted array.
                lines_sorted.push_back(lines[best_idx]);
                lines.erase(lines.begin() + best_idx);

                //connect if near enough
                if (lines_sorted.size() > 1) {
                    size_t idx = lines_sorted.size() - 2;
                    bool connect = false;
                    if (lines_sorted[idx].points.back().distance_to(lines_sorted[idx + 1].points.front()) < flow.scaled_spacing() * 2) {
                        connect = true;
                    } else if (lines_sorted[idx].points.back().distance_to(lines_sorted[idx + 1].points.back()) < flow.scaled_spacing() * 2) {
                        lines_sorted[idx + 1].reverse();
                        connect = true;
                    } else if (lines_sorted[idx].points.front().distance_to(lines_sorted[idx + 1].points.front()) < flow.scaled_spacing() * 2) {
                        lines_sorted[idx].reverse();
                        connect = true;
                    } else if (lines_sorted[idx].points.front().distance_to(lines_sorted[idx + 1].points.back()) < flow.scaled_spacing() * 2) {
                        lines_sorted[idx].reverse();
                        lines_sorted[idx + 1].reverse();
                        connect = true;
                    }

                    if (connect) {
                        //connect them
                        lines_sorted[idx].points.insert(
                            lines_sorted[idx].points.end(),
                            lines_sorted[idx + 1].points.begin(),
                            lines_sorted[idx + 1].points.end());
                        lines_sorted.erase(lines_sorted.begin() + idx + 1);
                        idx--;
                    }
                }

                //update last position
                previous = &lines_sorted.back();
            }
        }
    }

    return lines_sorted;
}

Polygons Print::first_layer_islands() const
{
    Polygons islands;
    for (PrintObject *object : m_objects) {
        Polygons object_islands;
        for (ExPolygon &expoly : object->m_layers.front()->lslices)
            object_islands.push_back(expoly.contour);
        if (! object->support_layers().empty())
            object->support_layers().front()->support_fills.polygons_covered_by_spacing(object_islands, float(SCALED_EPSILON));
        islands.reserve(islands.size() + object_islands.size() * object->instances().size());
        for (const PrintInstance &instance : object->instances())
            for (Polygon &poly : object_islands) {
                islands.push_back(poly);
                islands.back().translate(instance.shift);
            }
    }
    return islands;
}

std::vector<Point> Print::first_layer_wipe_tower_corners() const
{
    std::vector<Point> corners;
    if (has_wipe_tower() && ! m_wipe_tower_data.tool_changes.empty()) {
        double width = m_config.wipe_tower_width + 2*m_wipe_tower_data.brim_width;
        double depth = m_wipe_tower_data.depth + 2*m_wipe_tower_data.brim_width;
        Vec2d pt0(-m_wipe_tower_data.brim_width, -m_wipe_tower_data.brim_width);
        for (Vec2d pt : {
                pt0,
                Vec2d(pt0.x()+width, pt0.y()      ),
                Vec2d(pt0.x()+width, pt0.y()+depth),
                Vec2d(pt0.x(),       pt0.y()+depth)
            }) {
            pt = Eigen::Rotation2Dd(Geometry::deg2rad(m_config.wipe_tower_rotation_angle.value)) * pt;
            pt += Vec2d(m_config.wipe_tower_x.value, m_config.wipe_tower_y.value);
            corners.emplace_back(Point(scale_(pt.x()), scale_(pt.y())));
        }
    }
    return corners;
}

void Print::finalize_first_layer_convex_hull()
{
    append(m_first_layer_convex_hull.points, m_skirt_convex_hull);
    if (m_first_layer_convex_hull.empty()) {
        // Neither skirt nor brim was extruded. Collect points of printed objects from 1st layer.
        for (Polygon &poly : this->first_layer_islands())
            append(m_first_layer_convex_hull.points, std::move(poly.points));
    }
    append(m_first_layer_convex_hull.points, this->first_layer_wipe_tower_corners());
    m_first_layer_convex_hull = Geometry::convex_hull(m_first_layer_convex_hull.points);
}

// Wipe tower support.
bool Print::has_wipe_tower() const
{
    return 
        ! m_config.spiral_vase.value &&
        m_config.wipe_tower.value && 
        m_config.nozzle_diameter.values.size() > 1;
}

const WipeTowerData& Print::wipe_tower_data(size_t extruders_cnt, double first_layer_height, double nozzle_diameter) const
{
    // If the wipe tower wasn't created yet, make sure the depth and brim_width members are set to default.
    if (! is_step_done(psWipeTower) && extruders_cnt !=0) {

        float width = float(m_config.wipe_tower_width);
		float unscaled_brim_width = m_config.wipe_tower_brim.get_abs_value(nozzle_diameter);

        const_cast<Print*>(this)->m_wipe_tower_data.depth = (900.f/width) * float(extruders_cnt - 1);
        const_cast<Print*>(this)->m_wipe_tower_data.brim_width = unscaled_brim_width;
    }

    return m_wipe_tower_data;
}

void Print::_make_wipe_tower()
{
    m_wipe_tower_data.clear();
    if (! this->has_wipe_tower())
        return;

    // Get wiping matrix to get number of extruders and convert vector<double> to vector<float>:
    std::vector<float> wiping_matrix(cast<float>(m_config.wiping_volumes_matrix.values));
    // Extract purging volumes for each extruder pair:
    std::vector<std::vector<float>> wipe_volumes;
    const unsigned int number_of_extruders = (unsigned int)(sqrt(wiping_matrix.size())+EPSILON);
    for (unsigned int i = 0; i<number_of_extruders; ++i)
        wipe_volumes.push_back(std::vector<float>(wiping_matrix.begin()+i*number_of_extruders, wiping_matrix.begin()+(i+1)*number_of_extruders));

    // Let the ToolOrdering class know there will be initial priming extrusions at the start of the print.
    m_wipe_tower_data.tool_ordering = ToolOrdering(*this, (unsigned int)-1, true);

    if (! m_wipe_tower_data.tool_ordering.has_wipe_tower())
        // Don't generate any wipe tower.
        return;

    // Check whether there are any layers in m_tool_ordering, which are marked with has_wipe_tower,
    // they print neither object, nor support. These layers are above the raft and below the object, and they
    // shall be added to the support layers to be printed.
    // see https://github.com/prusa3d/PrusaSlicer/issues/607
    {
        size_t idx_begin = size_t(-1);
        size_t idx_end   = m_wipe_tower_data.tool_ordering.layer_tools().size();
        // Find the first wipe tower layer, which does not have a counterpart in an object or a support layer.
        for (size_t i = 0; i < idx_end; ++ i) {
            const LayerTools &lt = m_wipe_tower_data.tool_ordering.layer_tools()[i];
            if (lt.has_wipe_tower && ! lt.has_object && ! lt.has_support) {
                idx_begin = i;
                break;
            }
        }
        if (idx_begin != size_t(-1)) {
            // Find the position in m_objects.first()->support_layers to insert these new support layers.
            double wipe_tower_new_layer_print_z_first = m_wipe_tower_data.tool_ordering.layer_tools()[idx_begin].print_z;
            SupportLayerPtrs::const_iterator it_layer = m_objects.front()->support_layers().begin();
            SupportLayerPtrs::const_iterator it_end   = m_objects.front()->support_layers().end();
            for (; it_layer != it_end && (*it_layer)->print_z - EPSILON < wipe_tower_new_layer_print_z_first; ++ it_layer);
            // Find the stopper of the sequence of wipe tower layers, which do not have a counterpart in an object or a support layer.
            for (size_t i = idx_begin; i < idx_end; ++ i) {
                LayerTools &lt = const_cast<LayerTools&>(m_wipe_tower_data.tool_ordering.layer_tools()[i]);
                if (! (lt.has_wipe_tower && ! lt.has_object && ! lt.has_support))
                    break;
                lt.has_support = true;
                // Insert the new support layer.
                double height    = lt.print_z - (i == 0 ? 0. : m_wipe_tower_data.tool_ordering.layer_tools()[i-1].print_z);
                //FIXME the support layer ID is set to -1, as Vojtech hopes it is not being used anyway.
                it_layer = m_objects.front()->insert_support_layer(it_layer, -1, height, lt.print_z, lt.print_z - 0.5 * height);
                ++ it_layer;
            }
        }
    }
    this->throw_if_canceled();

    // Initialize the wipe tower.
    WipeTower wipe_tower(m_config, wipe_volumes, m_wipe_tower_data.tool_ordering.first_extruder());
    

    //wipe_tower.set_retract();
    //wipe_tower.set_zhop();

    // Set the extruder & material properties at the wipe tower object.
    for (size_t i = 0; i < number_of_extruders; ++i)
        wipe_tower.set_extruder(i);

    m_wipe_tower_data.priming = Slic3r::make_unique<std::vector<WipeTower::ToolChangeResult>>(
        wipe_tower.prime((float)this->skirt_first_layer_height(), m_wipe_tower_data.tool_ordering.all_extruders(), false));

    // Lets go through the wipe tower layers and determine pairs of extruder changes for each
    // to pass to wipe_tower (so that it can use it for planning the layout of the tower)
    {
        unsigned int current_extruder_id = m_wipe_tower_data.tool_ordering.all_extruders().back();
        for (auto &layer_tools : m_wipe_tower_data.tool_ordering.layer_tools()) { // for all layers
            if (!layer_tools.has_wipe_tower) continue;
            bool first_layer = &layer_tools == &m_wipe_tower_data.tool_ordering.front();
            wipe_tower.plan_toolchange((float)layer_tools.print_z, (float)layer_tools.wipe_tower_layer_height, current_extruder_id, current_extruder_id, false);
            for (const auto extruder_id : layer_tools.extruders) {
                if ((first_layer && extruder_id == m_wipe_tower_data.tool_ordering.all_extruders().back()) || extruder_id != current_extruder_id) {
                    double volume_to_wipe = wipe_volumes[current_extruder_id][extruder_id];             // total volume to wipe after this toolchange
                    
                    if (m_config.wipe_advanced) {
                        volume_to_wipe = m_config.wipe_advanced_nozzle_melted_volume;
                        float pigmentBef = m_config.filament_wipe_advanced_pigment.get_at(current_extruder_id);
                        float pigmentAft = m_config.filament_wipe_advanced_pigment.get_at(extruder_id);
                        if (m_config.wipe_advanced_algo.value == waLinear) {
                            volume_to_wipe += m_config.wipe_advanced_multiplier.value * (pigmentBef - pigmentAft);
                            std::cout << "advanced wiping (lin) ";
                            std::cout << current_extruder_id << " -> " << extruder_id << " will use " << volume_to_wipe << " mm3\n";
                            std::cout << " calculus : " << m_config.wipe_advanced_nozzle_melted_volume << " + " << m_config.wipe_advanced_multiplier.value
                                << " * ( " << pigmentBef << " - " << pigmentAft << " )\n";
                            std::cout << "    = " << m_config.wipe_advanced_nozzle_melted_volume << " + " << (m_config.wipe_advanced_multiplier.value* (pigmentBef - pigmentAft)) << "\n";
                        } else if (m_config.wipe_advanced_algo.value == waQuadra) {
                            volume_to_wipe += m_config.wipe_advanced_multiplier.value * (pigmentBef - pigmentAft)
                                + m_config.wipe_advanced_multiplier.value * (pigmentBef - pigmentAft) * (pigmentBef - pigmentAft) * (pigmentBef - pigmentAft);
                            std::cout << "advanced wiping (quadra) ";
                            std::cout << current_extruder_id << " -> " << extruder_id << " will use " << volume_to_wipe << " mm3\n";
                            std::cout << " calculus : " << m_config.wipe_advanced_nozzle_melted_volume << " + " << m_config.wipe_advanced_multiplier.value
                                << " * ( " << pigmentBef << " - " << pigmentAft << " ) + " << m_config.wipe_advanced_multiplier.value
                                << " * ( " << pigmentBef << " - " << pigmentAft << " ) ^3 \n";
                            std::cout << "    = " << m_config.wipe_advanced_nozzle_melted_volume << " + " << (m_config.wipe_advanced_multiplier.value* (pigmentBef - pigmentAft))
                                << " + " << (m_config.wipe_advanced_multiplier.value*(pigmentBef - pigmentAft)*(pigmentBef - pigmentAft)*(pigmentBef - pigmentAft))<<"\n";
                        } else if (m_config.wipe_advanced_algo.value == waHyper) {
                            volume_to_wipe += m_config.wipe_advanced_multiplier.value * (0.5 + pigmentBef) / (0.5 + pigmentAft);
                            std::cout << "advanced wiping (hyper) ";
                            std::cout << current_extruder_id << " -> " << extruder_id << " will use " << volume_to_wipe << " mm3\n";
                            std::cout << " calculus : " << m_config.wipe_advanced_nozzle_melted_volume << " + " << m_config.wipe_advanced_multiplier.value
                                << " * ( 0.5 + " << pigmentBef << " ) / ( 0.5 + " << pigmentAft << " )\n";
                            std::cout << "    = " << m_config.wipe_advanced_nozzle_melted_volume << " + " << (m_config.wipe_advanced_multiplier.value * (0.5 + pigmentBef) / (0.5 + pigmentAft)) << "\n";
                        }
                    }
                    //filament_wipe_advanced_pigment
                    
                    // Not all of that can be used for infill purging:
                    volume_to_wipe -= (float)m_config.filament_minimal_purge_on_wipe_tower.get_at(extruder_id);

                    // try to assign some infills/objects for the wiping:
                    volume_to_wipe = layer_tools.wiping_extrusions().mark_wiping_extrusions(*this, current_extruder_id, extruder_id, volume_to_wipe);

                    // add back the minimal amount toforce on the wipe tower:
                    volume_to_wipe += (float)m_config.filament_minimal_purge_on_wipe_tower.get_at(extruder_id);

                    // request a toolchange at the wipe tower with at least volume_to_wipe purging amount
                    wipe_tower.plan_toolchange((float)layer_tools.print_z, (float)layer_tools.wipe_tower_layer_height, current_extruder_id, extruder_id,
                                               first_layer && extruder_id == m_wipe_tower_data.tool_ordering.all_extruders().back(), volume_to_wipe);
                    current_extruder_id = extruder_id;
                }
            }
            layer_tools.wiping_extrusions().ensure_perimeters_infills_order(*this);
            if (&layer_tools == &m_wipe_tower_data.tool_ordering.back() || (&layer_tools + 1)->wipe_tower_partitions == 0)
                break;
        }
    }

    // Generate the wipe tower layers.
    m_wipe_tower_data.tool_changes.reserve(m_wipe_tower_data.tool_ordering.layer_tools().size());
    wipe_tower.generate(m_wipe_tower_data.tool_changes);
    m_wipe_tower_data.depth = wipe_tower.get_depth();
    m_wipe_tower_data.brim_width = wipe_tower.get_brim_width();

    // Unload the current filament over the purge tower.
    coordf_t layer_height = m_objects.front()->config().layer_height.value;
    if (m_wipe_tower_data.tool_ordering.back().wipe_tower_partitions > 0) {
        // The wipe tower goes up to the last layer of the print.
        if (wipe_tower.layer_finished()) {
            // The wipe tower is printed to the top of the print and it has no space left for the final extruder purge.
            // Lift Z to the next layer.
            wipe_tower.set_layer(float(m_wipe_tower_data.tool_ordering.back().print_z + layer_height), float(layer_height), 0, false, true);
        } else {
            // There is yet enough space at this layer of the wipe tower for the final purge.
        }
    } else {
        // The wipe tower does not reach the last print layer, perform the pruge at the last print layer.
        assert(m_wipe_tower_data.tool_ordering.back().wipe_tower_partitions == 0);
        wipe_tower.set_layer(float(m_wipe_tower_data.tool_ordering.back().print_z), float(layer_height), 0, false, true);
    }
    m_wipe_tower_data.final_purge = Slic3r::make_unique<WipeTower::ToolChangeResult>(
        wipe_tower.tool_change((unsigned int)(-1)));

    m_wipe_tower_data.used_filament = wipe_tower.get_used_filament();
    m_wipe_tower_data.number_of_toolchanges = wipe_tower.get_number_of_toolchanges();
}

// Generate a recommended G-code output file name based on the format template, default extension, and template parameters
// (timestamps, object placeholders derived from the model, current placeholder prameters and print statistics.
// Use the final print statistics if available, or just keep the print statistics placeholders if not available yet (before G-code is finalized).
std::string Print::output_filename(const std::string &filename_base) const 
{ 
    // Set the placeholders for the data know first after the G-code export is finished.
    // These values will be just propagated into the output file name.
    DynamicConfig config = this->finished() ? this->print_statistics().config() : this->print_statistics().placeholders();
    config.set_key_value("num_extruders", new ConfigOptionInt((int)m_config.nozzle_diameter.size()));
    return this->PrintBase::output_filename(m_config.output_filename_format.value, ".gcode", filename_base, &config);
}

DynamicConfig PrintStatistics::config() const
{
    DynamicConfig config;
    std::string normal_print_time = short_time(this->estimated_normal_print_time);
    std::string silent_print_time = short_time(this->estimated_silent_print_time);
    config.set_key_value("print_time",                new ConfigOptionString(normal_print_time));
    config.set_key_value("normal_print_time",         new ConfigOptionString(normal_print_time));
    config.set_key_value("silent_print_time",         new ConfigOptionString(silent_print_time));
    config.set_key_value("used_filament",             new ConfigOptionFloat(this->total_used_filament / 1000.));
    config.set_key_value("extruded_volume",           new ConfigOptionFloat(this->total_extruded_volume));
    config.set_key_value("total_cost",                new ConfigOptionFloat(this->total_cost));
    config.set_key_value("total_toolchanges",         new ConfigOptionInt(this->total_toolchanges));
    config.set_key_value("total_weight",              new ConfigOptionFloat(this->total_weight));
    config.set_key_value("total_wipe_tower_cost",     new ConfigOptionFloat(this->total_wipe_tower_cost));
    config.set_key_value("total_wipe_tower_filament", new ConfigOptionFloat(this->total_wipe_tower_filament));
    return config;
}

DynamicConfig PrintStatistics::placeholders()
{
    DynamicConfig config;
    for (const std::string &key : { 
        "print_time", "normal_print_time", "silent_print_time", 
        "used_filament", "extruded_volume", "total_cost", "total_weight", 
        "total_toolchanges", "total_wipe_tower_cost", "total_wipe_tower_filament"})
        config.set_key_value(key, new ConfigOptionString(std::string("{") + key + "}"));
    return config;
}

std::string PrintStatistics::finalize_output_path(const std::string &path_in) const
{
    std::string final_path;
    try {
        boost::filesystem::path path(path_in);
        DynamicConfig cfg = this->config();
        PlaceholderParser pp;
        std::string new_stem = pp.process(path.stem().string(), 0, &cfg);
        final_path = (path.parent_path() / (new_stem + path.extension().string())).string();
    } catch (const std::exception &ex) {
        BOOST_LOG_TRIVIAL(error) << "Failed to apply the print statistics to the export file name: " << ex.what();
        final_path = path_in;
    }
    return final_path;
}

} // namespace Slic3r
