#ifndef libslic3r_MultipleBeds_hpp_
#define libslic3r_MultipleBeds_hpp_

#include "libslic3r/Model.hpp"
#include "libslic3r/ObjectID.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/BoundingBox.hpp"

#include <map>

namespace Slic3r {

class Model;
class BuildVolume;
class PrintBase;
class Print;

extern bool s_reload_preview_after_switching_beds;
extern bool s_beds_just_switched;
extern bool s_beds_switched_since_last_gcode_load;

namespace BedsGrid {
using GridCoords = Vec2crd;
using Index = int;
Index grid_coords2index(const GridCoords &coords);
GridCoords index2grid_coords(Index index);
}

inline std::vector<unsigned> s_bed_selector_thumbnail_texture_ids;
inline std::array<bool, MAX_NUMBER_OF_BEDS> s_bed_selector_thumbnail_changed;
inline bool bed_selector_updated{false};

enum class PrintStatus {
    idle,
    running,
    finished,
    outside,
    invalid,
    empty,
    toolpath_outside
};

bool is_sliceable(const PrintStatus status);

inline std::array<PrintStatus, MAX_NUMBER_OF_BEDS> s_print_statuses;

class MultipleBeds {
public:
	MultipleBeds() = default;

	static constexpr int get_max_beds() { return MAX_NUMBER_OF_BEDS; };
	Vec3d get_bed_translation(int id) const;

	void   clear_inst_map();
	void   set_instance_bed(ObjectID id, bool printable, int bed_idx);
	void   inst_map_updated();
    const std::map<ObjectID, int> &get_inst_map() const { return m_inst_to_bed; }
	bool   is_bed_occupied(int bed_idx) const;

	int    get_number_of_beds() const   { return m_number_of_beds; }
	bool   should_show_next_bed() const { return m_show_next_bed; }

	void   request_next_bed(bool show);
	int    get_active_bed() const       { return m_active_bed; }

	void   set_active_bed(int i);

    void   remove_instances_outside_outside_bed(Model& model, const int bed) const;
    void   set_instances_outside_outside_bed_unprintable(Model& model, const int bed_index) const;

    // Sets !printable to all instances outside the active bed.
    void   move_from_bed_to_first_bed(Model& model, const int bed) const;

	void   set_thumbnail_bed_idx(int bed_idx) { m_bed_for_thumbnails_generation = bed_idx; }
	int    get_thumbnail_bed_idx() const { return m_bed_for_thumbnails_generation; }
	bool   is_glvolume_on_thumbnail_bed(const Model& model, int obj_idx, int instance_idx) const;

	void   set_last_hovered_bed(int i)  { m_last_hovered_bed = i; }
	int    get_last_hovered_bed() const { return m_last_hovered_bed; }

    void   update_shown_beds(Model& model, const BuildVolume& build_volume, bool only_remove = false);
	bool   rearrange_after_load(Model& model, const BuildVolume& build_volume);
	void   set_loading_project_flag(bool project) { m_loading_project = project; }
	bool   get_loading_project_flag() const { return m_loading_project; }

	void   update_build_volume(const BoundingBoxf& build_volume_bb) {
        m_build_volume_bb = build_volume_bb;
    }
    Vec2d   bed_gap() const;
	Vec2crd get_bed_gap() const;
	void   ensure_wipe_towers_on_beds(Model& model, const std::vector<std::unique_ptr<Print>>& prints);

	void   start_autoslice(std::function<void(int,bool)>);
	bool   stop_autoslice(bool restore_original);
	bool   is_autoslicing() const { return m_autoslicing; }
	void   autoslice_next_bed();

private:
	bool   is_instance_on_bed(const ObjectID id, const int bed_index) const;

	int m_number_of_beds = 1;
	int m_active_bed     = 0;
	int m_bed_for_thumbnails_generation = -1;
	bool m_show_next_bed = false;
	std::map<ObjectID, int> m_inst_to_bed;
	std::map<PrintBase*, size_t> m_printbase_to_texture;
	std::array<int, MAX_NUMBER_OF_BEDS> m_occupied_beds_cache;
	int m_last_hovered_bed = -1;
	BoundingBoxf m_build_volume_bb;
	bool m_legacy_layout = false;
	bool m_loading_project = false;

	bool m_autoslicing = false;
	int  m_autoslicing_original_bed = 0;
	std::function<void(int, bool)> m_select_bed_fn;
};

extern MultipleBeds s_multiple_beds;

namespace MultipleBedsUtils {

using InstanceOffsets = std::vector<Vec3d>;
// The bool is true if the instance is printable.
// The order is from 'for o in objects; for i in o.instances.
InstanceOffsets get_instance_offsets(Model& model);

using ObjectInstances = std::vector<std::pair<ModelObject*, ModelInstancePtrs>>;
ObjectInstances get_object_instances(const Model& model);
void restore_instance_offsets(Model& model, const InstanceOffsets &offsets);
void restore_object_instances(Model& model, const ObjectInstances &object_instances);

void with_single_bed_model_fff(Model &model, const int bed_index, const std::function<void()> &callable);
void with_single_bed_model_sla(Model &model, const int bed_index, const std::function<void()> &callable);
}

} // namespace Slic3r

#endif // libslic3r_MultipleBeds_hpp_
