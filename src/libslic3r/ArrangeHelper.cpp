#include "ArrangeHelper.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/MultipleBeds.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/BuildVolume.hpp"

#include <string>

#include "boost/regex.hpp"
#include "boost/property_tree/json_parser.hpp"
#include "boost/algorithm/string/replace.hpp"
#include <boost/nowide/fstream.hpp>



namespace Slic3r {

	
static bool can_arrange_selected_bed(const Model& model, int bed_idx)
{
	// When arranging a single bed, all instances of each object present must be on the same bed.
	// Otherwise, the resulting order may not be possible to apply without messing up order
	// on the other beds.
	const auto map = s_multiple_beds.get_inst_map();
	for (const ModelObject* mo : model.objects) {
		std::map<int, bool> used_beds;
		bool mo_on_this_bed = false;
		for (const ModelInstance* mi : mo->instances) {
			int id = -1;
			if (auto it = map.find(mi->id()); it != map.end())
				id = it->second;
			if (id == bed_idx)
				mo_on_this_bed = true;
			used_beds[id] = true;
		}
		if (mo_on_this_bed && used_beds.size() != 1)
			return false;
	}
	return true;
}

static Sequential::PrinterGeometry get_printer_geometry(const ConfigBase& config)
{
	enum ShapeType {
		BOX,
		CONVEX
	};
	struct ExtruderSlice {
		coord_t height;
		ShapeType shape_type;
		std::vector<Polygon> polygons;
	};

	BuildVolume bv(config.opt<ConfigOptionPoints>("bed_shape")->values, 10.);
	const BoundingBox& bb = bv.bounding_box();
	Polygon bed_polygon;
	if (bv.type() == BuildVolume::Type::Circle) {
		// Generate an inscribed octagon.
		double r = bv.bounding_volume2d().size().x() / 2.;
		for (double a = 2*M_PI; a > 0.1; a -= M_PI/4.)
			bed_polygon.points.emplace_back(Point::new_scale(r * std::sin(a), r * std::cos(a)));
	} else {
		// Rectangle of Custom. Just use the bounding box.
		bed_polygon = bb.polygon();
	}

	std::vector<ExtruderSlice> slices;
	const std::string printer_notes = config.opt_string("printer_notes");
	{
		if (! printer_notes.empty()) {
			try {
				boost::nowide::ifstream in(resources_dir() + "/data/printer_gantries/geometries.json");
				boost::property_tree::ptree pt;
				boost::property_tree::read_json(in, pt);
				for (const auto& printer : pt.get_child("printers")) {
					slices = {};
					std::string printer_notes_match = printer.second.get<std::string>("printer_notes_regex");
					boost::regex rgx(printer_notes_match);
					if (! boost::regex_match(printer_notes, rgx))
						continue;

					for (const auto& obj : printer.second.get_child("slices")) {
						ExtruderSlice slice;
						slice.height = scaled(obj.second.get<double>("height"));
						std::string type_str = obj.second.get<std::string>("type");
						slice.shape_type = type_str == "box" ? BOX : CONVEX;
						for (const auto& polygon : obj.second.get_child("polygons")) {
							Polygon pgn;
							std::string pgn_str = polygon.second.data();
							boost::replace_all(pgn_str, ";", " ");
							boost::replace_all(pgn_str, ",", " ");
							std::stringstream ss(pgn_str);
							while (ss) {
								double x = 0.;
								double y = 0.;
								ss >> x >> y;
								if (ss)
									pgn.points.emplace_back(Point::new_scale(x, y));
							}
							if (! pgn.points.empty())
								slice.polygons.emplace_back(std::move(pgn));
						}
						slices.emplace_back(std::move(slice));
					}
					break;
				}
			}
			catch (const boost::property_tree::json_parser_error&) {
				// Failed to parse JSON. slices are empty, fallback will be used.
			}
		}
		if (slices.empty()) {
			// Fallback to primitive model using radius and height.
			coord_t r = scaled(std::max(0.1, config.opt_float("extruder_clearance_radius")));
			coord_t h = scaled(std::max(0.1, config.opt_float("extruder_clearance_height")));
			double bed_x = bv.bounding_volume2d().size().x();
			double bed_y = bv.bounding_volume2d().size().y();
			slices.push_back(ExtruderSlice{ 0, CONVEX, { { {  -5000000,   -5000000 }, {   5000000,   -5000000 }, {   5000000,   5000000 }, {  -5000000,   5000000 } } } });
			slices.push_back(ExtruderSlice{ 1000000, BOX, { { {  -r, -r }, { r, -r }, {   r,   r }, {  -r,  r } } } });
			slices.push_back(ExtruderSlice{ h, BOX, { { { -scaled(bed_x),  -r }, { scaled(bed_x),  -r }, { scaled(bed_x), r }, { -scaled(bed_x), r}}} });
		}
	}

	// Convert the read data so libseqarrange understands them.
	Sequential::PrinterGeometry out;
	out.plate = bed_polygon;
	for (const ExtruderSlice& slice : slices) {
		(slice.shape_type == CONVEX ? out.convex_heights : out.box_heights).emplace(slice.height);
		out.extruder_slices.insert(std::make_pair(slice.height, slice.polygons));
	}
	return out;
}

static Sequential::SolverConfiguration get_solver_config(const Sequential::PrinterGeometry& printer_geometry)
{
	return Sequential::SolverConfiguration(printer_geometry);
}

static std::vector<Sequential::ObjectToPrint> get_objects_to_print(const Model& model, const Sequential::PrinterGeometry& printer_geometry, int selected_bed)
{
	// First extract the heights of interest.
	std::vector<double> heights;
	for (const auto& [height, pgns] : printer_geometry.extruder_slices)
		heights.push_back(unscaled(height));
	Slic3r::sort_remove_duplicates(heights);

	// Now collect all objects and projections of convex hull above respective heights.
	std::vector<std::pair<Sequential::ObjectToPrint, std::vector<Sequential::ObjectToPrint>>> objects; // first = object id, the vector = ids of its instances
	for (const ModelObject* mo : model.objects) {
		const TriangleMesh& raw_mesh = mo->raw_mesh();
		coord_t height = scaled(mo->instance_bounding_box(0).size().z());
		std::vector<Sequential::ObjectToPrint> instances;
		for (const ModelInstance* mi : mo->instances) {
			if (selected_bed != -1) {
				auto it = s_multiple_beds.get_inst_map().find(mi->id());
				if (it == s_multiple_beds.get_inst_map().end() || it->second != selected_bed)
					continue;
			}
			if (mi->printable) {
				instances.emplace_back(Sequential::ObjectToPrint{int(mi->id().id), true, height, {}});

				for (double height : heights) {
					// It seems that zero level in the object instance is mi->get_offset().z(), however need to have bed as zero level,
					// hence substracting mi->get_offset().z() from height seems to be an easy hack
					Polygon pgn = its_convex_hull_2d_above(raw_mesh.its, mi->get_matrix_no_offset().cast<float>(), height - mi->get_offset().z());
					instances.back().pgns_at_height.emplace_back(std::make_pair(scaled(height), pgn));
				}
			}
		}
		
		// Collect all instances of this object to be arranged, unglue it from the next object.
		if (! instances.empty()) {
			objects.emplace_back(instances.front(), instances);
			objects.back().second.erase(objects.back().second.begin()); // pop_front
			if (! objects.back().second.empty())
				objects.back().second.back().glued_to_next = false;
			else
				objects.back().first.glued_to_next = false;
		}
	}

	// Now order the objects so that the are always passed in the order of increasing id.
	// That way, the algorithm will give the same result when called repeatedly.
	// However, there is an exception: instances cannot be separated from their objects.
	std::sort(objects.begin(), objects.end(), [](const auto& a, const auto& b) { return a.first.id < b.first.id; });
	std::vector<Sequential::ObjectToPrint> objects_out;
	for (const auto& o : objects) {
		objects_out.emplace_back(o.first);
		for (const auto& i : o.second)
			objects_out.emplace_back(i);
	}

	return objects_out;
}




void arrange_model_sequential(Model& model, const ConfigBase& config, bool current_bed_only)
{
	SeqArrange seq_arrange(model, config, current_bed_only);
	seq_arrange.process_seq_arrange([](int) {});
	seq_arrange.apply_seq_arrange(model);
}



SeqArrange::SeqArrange(const Model& model, const ConfigBase& config, bool current_bed_only)
{
	m_selected_bed = current_bed_only ? s_multiple_beds.get_active_bed() : -1;
	if (m_selected_bed != -1 && ! can_arrange_selected_bed(model, m_selected_bed))
		throw ExceptionCannotAttemptSeqArrange();

    m_printer_geometry = get_printer_geometry(config);
	m_solver_configuration = get_solver_config(m_printer_geometry);
	m_objects = get_objects_to_print(model, m_printer_geometry, m_selected_bed);
}



void SeqArrange::process_seq_arrange(std::function<void(int)> progress_fn)
{
	m_plates =
		Sequential::schedule_ObjectsForSequentialPrint(
			m_solver_configuration,
			m_printer_geometry,
			m_objects, progress_fn);

	// If this was arrangement of a single bed, check that all instances of a single object
	// ended up on the same bed. Otherwise we cannot apply the result (instances of a single
	// object always follow one another in the object list and therefore the print).
	if (m_selected_bed != -1 && s_multiple_beds.get_number_of_beds() > 1) {
		int expected_plate = -1;
		for (const Sequential::ObjectToPrint& otp : m_objects) {
			auto it = std::find_if(m_plates.begin(), m_plates.end(), [&otp](const auto& plate)
				{ return std::any_of(plate.scheduled_objects.begin(), plate.scheduled_objects.end(),
					[&otp](const auto& obj) { return otp.id == obj.id;
				});
			});
			assert(it != m_plates.end());
			size_t plate_id = it - m_plates.begin();
			if (expected_plate != -1 && expected_plate != plate_id)
				throw ExceptionCannotApplySeqArrange();
			expected_plate = otp.glued_to_next ? plate_id : -1;
		}
	}
}


// Extract the result and move the objects in Model accordingly.
void SeqArrange::apply_seq_arrange(Model& model) const
{
	struct MoveData {
		Sequential::ScheduledObject scheduled_object;
		size_t bed_idx;
		ModelObject* mo;
	};

	// Iterate over the result and move the instances.
	std::vector<MoveData> move_data_all; // Needed for the ordering.
	size_t plate_idx = 0;
	size_t new_number_of_beds = s_multiple_beds.get_number_of_beds();
	std::vector<int> touched_beds;
	for (const Sequential::ScheduledPlate& plate : m_plates) {
		int real_bed = plate_idx;
		if (m_selected_bed != -1) {
			// Only a single bed was arranged. Move "first" bed to its position
			// and everything else to newly created beds.
			real_bed += (plate_idx == 0 ? m_selected_bed : s_multiple_beds.get_number_of_beds() - 1);
		}
		touched_beds.emplace_back(real_bed);
		new_number_of_beds = std::max(new_number_of_beds, size_t(real_bed + 1));
		const Vec3d bed_offset = s_multiple_beds.get_bed_translation(real_bed);

		for (const Sequential::ScheduledObject& object : plate.scheduled_objects)
			for (ModelObject* mo : model.objects)
				for (ModelInstance* mi : mo->instances)
					if (mi->id().id == object.id) {
						move_data_all.push_back({ object, size_t(real_bed), mo });
						mi->set_offset(Vec3d(unscaled(object.x) + bed_offset.x(), unscaled(object.y) + bed_offset.y(), mi->get_offset().z()));
					}
		++plate_idx;
	}

	// Create a copy of ModelObject pointers, zero ones present in move_data_all.
	// The point is to only reorder ModelObject which had actually been passed to the arrange algorithm.
	std::vector<ModelObject*> objects_reordered = model.objects;
	for (size_t i = 0; i < objects_reordered.size(); ++i) {
		ModelObject* mo = objects_reordered[i];
		if (std::any_of(move_data_all.begin(), move_data_all.end(), [&mo](const MoveData& md) { return md.mo == mo; }))
			objects_reordered[i] = nullptr;
	}
	// Fill the gaps with the arranged objects in the correct order.
	for (size_t i = 0; i < objects_reordered.size(); ++i) {
		if (! objects_reordered[i]) {
			objects_reordered[i] = move_data_all[0].mo;
			while (! move_data_all.empty() && move_data_all.front().mo == objects_reordered[i])
				move_data_all.erase(move_data_all.begin());
		}
	}

	// Check that the old and new vectors only differ in order of elements.
	auto a = model.objects;
	auto b = objects_reordered;
	std::sort(a.begin(), a.end());
	std::sort(b.begin(), b.end());
	if (a != b)
		std::terminate(); // A bug in the code above. Better crash now than later.

	// Update objects order in the model.
	std::swap(model.objects, objects_reordered);

	// One last thing. Move unprintable instances to new beds. It would be nicer to
	// arrange them (non-sequentially) on just one bed - maybe one day.
	std::map<int, std::vector<ModelInstance*>> instances_to_move; // bed to move from and list of instances
	for (ModelObject* mo : model.objects)
		for (ModelInstance* mi : mo->instances)
			if (!mi->printable) {
				auto it = s_multiple_beds.get_inst_map().find(mi->id());
				if (it == s_multiple_beds.get_inst_map().end() || (m_selected_bed != -1 && it->second != m_selected_bed))
					continue;
				// Was something placed on this bed during arrange? If not, we should not move anything.
				if (std::find(touched_beds.begin(), touched_beds.end(), it->second) != touched_beds.end())
					instances_to_move[it->second].emplace_back(mi);
			}
	// Now actually move them.
	for (auto& [bed_idx, instances] : instances_to_move) {
		Vec3d old_bed_offset = s_multiple_beds.get_bed_translation(bed_idx);
		Vec3d new_bed_offset = s_multiple_beds.get_bed_translation(new_number_of_beds);
		for (ModelInstance* mi : instances)
			mi->set_offset(mi->get_offset() - old_bed_offset + new_bed_offset);
		++new_number_of_beds;
	}
}



std::optional<std::pair<std::string, std::string> > check_seq_conflict(const Model& model, const ConfigBase& config)
{
	Sequential::PrinterGeometry printer_geometry = get_printer_geometry(config);
	Sequential::SolverConfiguration solver_config = get_solver_config(printer_geometry);
	std::vector<Sequential::ObjectToPrint> objects = get_objects_to_print(model, printer_geometry, -1);

	if (printer_geometry.extruder_slices.empty()) {
		// If there are no data for extruder (such as extruder_clearance_radius set to 0),
		// consider it printable.
	        return {};
	}

	Sequential::ScheduledPlate plate;
	for (const ModelObject* mo : model.objects) {
		int inst_id = -1;
		for (const ModelInstance* mi : mo->instances) {
			++inst_id;

			auto it = s_multiple_beds.get_inst_map().find(mi->id());
			if (it == s_multiple_beds.get_inst_map().end() || it->second != s_multiple_beds.get_active_bed())
				continue;

			// Is this instance in objects to print? It may be unprintable or something.
			auto it2 = std::find_if(objects.begin(), objects.end(), [&mi](const Sequential::ObjectToPrint& otp) { return otp.id == mi->id().id; });
			if (it2 == objects.end())
				continue;

			Vec3d offset = s_multiple_beds.get_bed_translation(s_multiple_beds.get_active_bed());
			plate.scheduled_objects.emplace_back(mi->id().id, scaled(mi->get_offset().x() - offset.x()), scaled(mi->get_offset().y() - offset.y()));
		}
	}

	std::optional<std::pair<int,int>> conflict = Sequential::check_ScheduledObjectsForSequentialConflict(solver_config, printer_geometry, objects, std::vector<Sequential::ScheduledPlate>(1, plate));
	if (conflict) {
		std::pair<std::string, std::string> names;
		for (const ModelObject* mo : model.objects)
			for (const ModelInstance* mi : mo->instances) {
				if (mi->id().id == conflict->first)
					names.first = mo->name;
				if (mi->id().id == conflict->second)
					names.second = mo->name;
			}
		return names;
	}
	return std::nullopt;
}


} // namespace Slic3r
