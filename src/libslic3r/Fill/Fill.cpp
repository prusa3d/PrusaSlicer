///|/ Copyright (c) Prusa Research 2016 - 2023 Lukáš Matěna @lukasmatena, Vojtěch Bubník @bubnikv, Pavel Mikuš @Godrak, Lukáš Hejl @hejllukas
///|/ Copyright (c) SuperSlicer 2023 Remi Durand @supermerill
///|/ Copyright (c) 2016 Sakari Kapanen @Flannelhead
///|/ Copyright (c) Slic3r 2011 - 2015 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2013 Mark Hindess
///|/ Copyright (c) 2011 Michael Moon
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include <oneapi/tbb/scalable_allocator.h>
#include <boost/container/vector.hpp>
#include <memory>
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <utility>
#include <vector>
#include <cassert>
#include <cinttypes>
#include <cstdlib>

#include "../ClipperUtils.hpp"
#include "../Geometry.hpp"
#include "../Layer.hpp"
#include "../Print.hpp"
#include "../PrintConfig.hpp"
#include "../Surface.hpp"
// for Arachne based infills
#include "../PerimeterGenerator.hpp"
#include "FillBase.hpp"
#include "FillRectilinear.hpp"
#include "FillLightning.hpp"
#include "FillEnsuring.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/ExtrusionRole.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/LayerRegion.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polyline.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/ShortestPath.hpp"

namespace Slic3r {
namespace FillAdaptive {
struct Octree;
}  // namespace FillAdaptive
namespace FillLightning {
class Generator;
}  // namespace FillLightning

//static constexpr const float NarrowInfillAreaThresholdMM = 3.f;

struct SurfaceFillParams
{
	// Zero based extruder ID.
    unsigned int 	extruder = 0;
	// Infill pattern, adjusted for the density etc.
    InfillPattern  	pattern = InfillPattern(0);

    // FillBase
    // in unscaled coordinates
    coordf_t    	spacing = 0.;
    // infill / perimeter overlap, in unscaled coordinates
//    coordf_t    	overlap = 0.;
    // Angle as provided by the region config, in radians.
    float       	angle = 0.f;
    // Is bridging used for this fill? Bridging parameters may be used even if this->flow.bridge() is not set.
    bool 			bridge;
    // Non-negative for a bridge.
    float 			bridge_angle = 0.f;

    // FillParams
    float       	density = 0.f;
    // Don't adjust spacing to fill the space evenly.
//    bool        	dont_adjust = false;
    // Length of the infill anchor along the perimeter line.
    // 1000mm is roughly the maximum length line that fits into a 32bit coord_t.
    float 			anchor_length     = 1000.f;
    float 			anchor_length_max = 1000.f;

    // width, height of extrusion, nozzle diameter, is bridge
    // For the output, for fill generator.
    Flow 			flow;

	// For the output
	ExtrusionRole	extrusion_role{ ExtrusionRole::None };

	// Various print settings?

	// Index of this entry in a linear vector.
    size_t 			idx = 0;


	bool operator<(const SurfaceFillParams &rhs) const {
#define RETURN_COMPARE_NON_EQUAL(KEY) if (this->KEY < rhs.KEY) return true; if (this->KEY > rhs.KEY) return false;
#define RETURN_COMPARE_NON_EQUAL_TYPED(TYPE, KEY) if (TYPE(this->KEY) < TYPE(rhs.KEY)) return true; if (TYPE(this->KEY) > TYPE(rhs.KEY)) return false;

		// Sort first by decreasing bridging angle, so that the bridges are processed with priority when trimming one layer by the other.
		if (this->bridge_angle > rhs.bridge_angle) return true; 
		if (this->bridge_angle < rhs.bridge_angle) return false;

		RETURN_COMPARE_NON_EQUAL(extruder);
		RETURN_COMPARE_NON_EQUAL_TYPED(unsigned, pattern);
		RETURN_COMPARE_NON_EQUAL(spacing);
//		RETURN_COMPARE_NON_EQUAL(overlap);
		RETURN_COMPARE_NON_EQUAL(angle);
		RETURN_COMPARE_NON_EQUAL(density);
//		RETURN_COMPARE_NON_EQUAL_TYPED(unsigned, dont_adjust);
		RETURN_COMPARE_NON_EQUAL(anchor_length);
		RETURN_COMPARE_NON_EQUAL(anchor_length_max);
		RETURN_COMPARE_NON_EQUAL(flow.width());
		RETURN_COMPARE_NON_EQUAL(flow.height());
		RETURN_COMPARE_NON_EQUAL(flow.nozzle_diameter());
		RETURN_COMPARE_NON_EQUAL_TYPED(unsigned, bridge);
		return this->extrusion_role.lower(rhs.extrusion_role);
	}

	bool operator==(const SurfaceFillParams &rhs) const {
		return  this->extruder 			== rhs.extruder 		&&
				this->pattern 			== rhs.pattern 			&&
				this->spacing 			== rhs.spacing 			&&
//				this->overlap 			== rhs.overlap 			&&
				this->angle   			== rhs.angle   			&&
				this->bridge   			== rhs.bridge   		&&
//				this->bridge_angle 		== rhs.bridge_angle		&&
				this->density   		== rhs.density   		&&
//				this->dont_adjust   	== rhs.dont_adjust 		&&
				this->anchor_length  	== rhs.anchor_length    &&
				this->anchor_length_max == rhs.anchor_length_max &&
				this->flow 				== rhs.flow 			&&
				this->extrusion_role	== rhs.extrusion_role;
	}
};

struct SurfaceFill {
	SurfaceFill(const SurfaceFillParams& params) : region_id(size_t(-1)), surface(stCount, ExPolygon()), params(params) {}

	size_t 				region_id;
	Surface 			surface;
	ExPolygons       	expolygons;
	SurfaceFillParams	params;
};

static inline bool fill_type_monotonic(InfillPattern pattern)
{
	return pattern == ipMonotonic || pattern == ipMonotonicLines;
}

std::vector<SurfaceFill> group_fills(const Layer &layer)
{
	std::vector<SurfaceFill> surface_fills;

	// Fill in a map of a region & surface to SurfaceFillParams.
	std::set<SurfaceFillParams> 						set_surface_params;
	std::vector<std::vector<const SurfaceFillParams*>> 	region_to_surface_params(layer.regions().size(), std::vector<const SurfaceFillParams*>());
    SurfaceFillParams									params;
    bool 												has_internal_voids = false;
	for (size_t region_id = 0; region_id < layer.regions().size(); ++ region_id) {
		const LayerRegion  &layerm = *layer.regions()[region_id];
		region_to_surface_params[region_id].assign(layerm.fill_surfaces().size(), nullptr);
	    for (const Surface &surface : layerm.fill_surfaces())
	        if (surface.surface_type == stInternalVoid)
	        	has_internal_voids = true;
	        else {
		        const PrintRegionConfig &region_config = layerm.region().config();
		        FlowRole extrusion_role = surface.is_top() ? frTopSolidInfill : (surface.is_solid() ? frSolidInfill : frInfill);
		        bool     is_bridge 	    = layer.id() > 0 && surface.is_bridge();
		        params.extruder 	 = layerm.region().extruder(extrusion_role);
		        params.pattern 		 = region_config.fill_pattern.value;
		        params.density       = float(region_config.fill_density);

		        if (surface.is_solid()) {
		            params.density = 100.f;
					//FIXME for non-thick bridges, shall we allow a bottom surface pattern?
		            params.pattern = (surface.is_external() && ! is_bridge) ? 
						(surface.is_top() ? region_config.top_fill_pattern.value : region_config.bottom_fill_pattern.value) :
		                fill_type_monotonic(region_config.top_fill_pattern) ? ipMonotonic : ipRectilinear;
		        } else if (params.density <= 0)
		            continue;

		        if (is_bridge) {
		            params.extrusion_role = ExtrusionRole::BridgeInfill;
                } else {
                    if (surface.is_solid()) {
                        if (surface.is_top()) {
                            params.extrusion_role = ExtrusionRole::TopSolidInfill;
                        } else if (surface.surface_type == stSolidOverBridge) {
                            params.extrusion_role = ExtrusionRole::InfillOverBridge;
                        } else {
                            params.extrusion_role = ExtrusionRole::SolidInfill;
                        }
                    } else {
                        params.extrusion_role = ExtrusionRole::InternalInfill;
                    }
                }
		        params.bridge_angle = float(surface.bridge_angle);
		        params.angle 		= float(Geometry::deg2rad(region_config.fill_angle.value));

		        // Calculate the actual flow we'll be using for this infill.
		        params.bridge = is_bridge || Fill::use_bridge_flow(params.pattern);
				params.flow   = params.bridge ?
					// Always enable thick bridges for internal bridges.
					layerm.bridging_flow(extrusion_role, surface.is_bridge() && ! surface.is_external()) :
					layerm.flow(extrusion_role, (surface.thickness == -1) ? layer.height : surface.thickness);

				// Calculate flow spacing for infill pattern generation.
		        if (surface.is_solid() || is_bridge) {
		            params.spacing = params.flow.spacing();
		            // Don't limit anchor length for solid or bridging infill.
		            params.anchor_length = 1000.f;
					params.anchor_length_max = 1000.f;
		        } else {
					// Internal infill. Calculating infill line spacing independent of the current layer height and 1st layer status,
					// so that internall infill will be aligned over all layers of the current region.
		            params.spacing = layerm.region().flow(*layer.object(), frInfill, layer.object()->config().layer_height, false).spacing();
		            // Anchor a sparse infill to inner perimeters with the following anchor length:
			        params.anchor_length = float(region_config.infill_anchor);
					if (region_config.infill_anchor.percent)
						params.anchor_length = float(params.anchor_length * 0.01 * params.spacing);
					params.anchor_length_max = float(region_config.infill_anchor_max);
					if (region_config.infill_anchor_max.percent)
						params.anchor_length_max = float(params.anchor_length_max * 0.01 * params.spacing);
					params.anchor_length = std::min(params.anchor_length, params.anchor_length_max);
				}

		        auto it_params = set_surface_params.find(params);
		        if (it_params == set_surface_params.end())
		        	it_params = set_surface_params.insert(it_params, params);
		        region_to_surface_params[region_id][&surface - &layerm.fill_surfaces().surfaces.front()] = &(*it_params);
		    }
	}

	surface_fills.reserve(set_surface_params.size());
	for (const SurfaceFillParams &params : set_surface_params) {
		const_cast<SurfaceFillParams&>(params).idx = surface_fills.size();
		surface_fills.emplace_back(params);
	}

	for (size_t region_id = 0; region_id < layer.regions().size(); ++ region_id) {
		const LayerRegion &layerm = *layer.regions()[region_id];
	    for (const Surface &surface : layerm.fill_surfaces())
	        if (surface.surface_type != stInternalVoid) {
	        	const SurfaceFillParams *params = region_to_surface_params[region_id][&surface - &layerm.fill_surfaces().surfaces.front()];
				if (params != nullptr) {
	        		SurfaceFill &fill = surface_fills[params->idx];
                    if (fill.region_id == size_t(-1)) {
	        			fill.region_id = region_id;
	        			fill.surface = surface;
	        			fill.expolygons.emplace_back(std::move(fill.surface.expolygon));
	        		} else
	        			fill.expolygons.emplace_back(surface.expolygon);
				}
	        }
	}

	{
		Polygons all_polygons;
		for (SurfaceFill &fill : surface_fills)
			if (! fill.expolygons.empty()) {
				if (fill.expolygons.size() > 1 || ! all_polygons.empty()) {
					Polygons polys = to_polygons(std::move(fill.expolygons));
		            // Make a union of polygons, use a safety offset, subtract the preceding polygons.
				    // Bridges are processed first (see SurfaceFill::operator<())
		            fill.expolygons = all_polygons.empty() ? union_safety_offset_ex(polys) : diff_ex(polys, all_polygons, ApplySafetyOffset::Yes);
					append(all_polygons, std::move(polys));
				} else if (&fill != &surface_fills.back())
					append(all_polygons, to_polygons(fill.expolygons));
	        }
	}

    // we need to detect any narrow surfaces that might collapse
    // when adding spacing below
    // such narrow surfaces are often generated in sloping walls
    // by bridge_over_infill() and combine_infill() as a result of the
    // subtraction of the combinable area from the layer infill area,
    // which leaves small areas near the perimeters
    // we are going to grow such regions by overlapping them with the void (if any)
    // TODO: detect and investigate whether there could be narrow regions without
    // any void neighbors
    if (has_internal_voids) {
    	// Internal voids are generated only if "infill_only_where_needed" or "infill_every_layers" are active.
        coord_t  distance_between_surfaces = 0;
        Polygons surfaces_polygons;
        Polygons voids;
		int      region_internal_infill = -1;
		int		 region_solid_infill = -1;
		int		 region_some_infill = -1;
    	for (SurfaceFill &surface_fill : surface_fills)
			if (! surface_fill.expolygons.empty()) {
    			distance_between_surfaces = std::max(distance_between_surfaces, surface_fill.params.flow.scaled_spacing());
				append((surface_fill.surface.surface_type == stInternalVoid) ? voids : surfaces_polygons, to_polygons(surface_fill.expolygons));
				if (surface_fill.surface.surface_type == stInternalSolid)
					region_internal_infill = (int)surface_fill.region_id;
				if (surface_fill.surface.is_solid())
					region_solid_infill = (int)surface_fill.region_id;
				if (surface_fill.surface.surface_type != stInternalVoid)
					region_some_infill = (int)surface_fill.region_id;
			}
    	if (! voids.empty() && ! surfaces_polygons.empty()) {
    		// First clip voids by the printing polygons, as the voids were ignored by the loop above during mutual clipping.
    		voids = diff(voids, surfaces_polygons);
	        // Corners of infill regions, which would not be filled with an extrusion path with a radius of distance_between_surfaces/2
	        Polygons collapsed = diff(
	            surfaces_polygons,
				opening(surfaces_polygons, float(distance_between_surfaces /2), float(distance_between_surfaces / 2 + ClipperSafetyOffset)));
	        //FIXME why the voids are added to collapsed here? First it is expensive, second the result may lead to some unwanted regions being
	        // added if two offsetted void regions merge.
	        // polygons_append(voids, collapsed);
	        ExPolygons extensions = intersection_ex(expand(collapsed, float(distance_between_surfaces)), voids, ApplySafetyOffset::Yes);
	        // Now find an internal infill SurfaceFill to add these extrusions to.
	        SurfaceFill *internal_solid_fill = nullptr;
			unsigned int region_id = 0;
			if (region_internal_infill != -1)
				region_id = region_internal_infill;
			else if (region_solid_infill != -1)
				region_id = region_solid_infill;
			else if (region_some_infill != -1)
				region_id = region_some_infill;
			const LayerRegion& layerm = *layer.regions()[region_id];
	        for (SurfaceFill &surface_fill : surface_fills)
	        	if (surface_fill.surface.surface_type == stInternalSolid && std::abs(layer.height - surface_fill.params.flow.height()) < EPSILON) {
	        		internal_solid_fill = &surface_fill;
	        		break;
	        	}
	        if (internal_solid_fill == nullptr) {
	        	// Produce another solid fill.
		        params.extruder 	 = layerm.region().extruder(frSolidInfill);
	            params.pattern 		 = fill_type_monotonic(layerm.region().config().top_fill_pattern) ? ipMonotonic : ipRectilinear;
	            params.density 		 = 100.f;
		        params.extrusion_role = ExtrusionRole::InternalInfill;
		        params.angle 		= float(Geometry::deg2rad(layerm.region().config().fill_angle.value));
		        // calculate the actual flow we'll be using for this infill
				params.flow = layerm.flow(frSolidInfill);
		        params.spacing = params.flow.spacing();	        
				surface_fills.emplace_back(params);
				surface_fills.back().surface.surface_type = stInternalSolid;
				surface_fills.back().surface.thickness = layer.height;
				surface_fills.back().expolygons = std::move(extensions);
	        } else {
	        	append(extensions, std::move(internal_solid_fill->expolygons));
	        	internal_solid_fill->expolygons = union_ex(extensions);
	        }
		}
    }

    // Use ipEnsuring pattern for all internal Solids.
    {
        for (size_t surface_fill_id = 0; surface_fill_id < surface_fills.size(); ++surface_fill_id)
            if (SurfaceFill &fill = surface_fills[surface_fill_id];
                    fill.surface.surface_type == stInternalSolid
                    || fill.surface.surface_type == stSolidOverBridge) {
                fill.params.pattern = ipEnsuring;
            }
    }

    return surface_fills;
}

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
void export_group_fills_to_svg(const char *path, const std::vector<SurfaceFill> &fills)
{
    BoundingBox bbox;
    for (const auto &fill : fills)
        for (const auto &expoly : fill.expolygons)
            bbox.merge(get_extents(expoly));
    Point legend_size = export_surface_type_legend_to_svg_box_size();
    Point legend_pos(bbox.min(0), bbox.max(1));
    bbox.merge(Point(std::max(bbox.min(0) + legend_size(0), bbox.max(0)), bbox.max(1) + legend_size(1)));

    SVG svg(path, bbox);
    const float transparency = 0.5f;
    for (const auto &fill : fills)
        for (const auto &expoly : fill.expolygons)
            svg.draw(expoly, surface_type_to_color_name(fill.surface.surface_type), transparency);
    export_surface_type_legend_to_svg(svg, legend_pos);
    svg.Close(); 
}
#endif

static void insert_fills_into_islands(Layer &layer, uint32_t fill_region_id, uint32_t fill_begin, uint32_t fill_end)
{
	if (fill_begin < fill_end) {
    	// Sort the extrusion range into its LayerIsland.
	    // Traverse the slices in an increasing order of bounding box size, so that the islands inside another islands are tested first,
	    // so we can just test a point inside ExPolygon::contour and we may skip testing the holes.
	    auto point_inside_surface = [&layer](const size_t lslice_idx, const Point &point) {
	        const BoundingBox &bbox = layer.lslices_ex[lslice_idx].bbox;
	        return point.x() >= bbox.min.x() && point.x() < bbox.max.x() &&
	               point.y() >= bbox.min.y() && point.y() < bbox.max.y() &&
	               layer.lslices[lslice_idx].contour.contains(point);
	    };
	    Point point = layer.get_region(fill_region_id)->fills().entities[fill_begin]->first_point();
	    int lslice_idx = int(layer.lslices_ex.size()) - 1;
	    for (; lslice_idx >= 0; -- lslice_idx)
	        if (point_inside_surface(lslice_idx, point))
	        	break;
	    assert(lslice_idx >= 0);
	    if (lslice_idx >= 0) {
	    	LayerSlice &lslice = layer.lslices_ex[lslice_idx];
	    	// Find an island.
	    	LayerIsland *island = nullptr;
	    	if (lslice.islands.size() == 1) {
	    		// Cool, just save the extrusions in there.
	    		island = &lslice.islands.front();
	    	} else {
	    		// The infill was created for one of the infills.
	    		// In case of ironing, the infill may not fall into any of the infill expolygons either.
	    		// In case of some numerical error, the infill may not fall into any of the infill expolygons either.
	    		// 1) Try an exact test, it should be cheaper than a closest region test.
	    		for (LayerIsland &li : lslice.islands) {
	    			const BoundingBoxes &bboxes     = li.fill_expolygons_composite() ?
	    				layer.get_region(li.perimeters.region())->fill_expolygons_composite_bboxes() :
	    				layer.get_region(li.fill_region_id)->fill_expolygons_bboxes();
	    			const ExPolygons 	&expolygons = li.fill_expolygons_composite() ? 
	    				layer.get_region(li.perimeters.region())->fill_expolygons_composite() :
	    				layer.get_region(li.fill_region_id)->fill_expolygons();
	    			for (uint32_t fill_expolygon_id : li.fill_expolygons)
	    				if (bboxes[fill_expolygon_id].contains(point) && expolygons[fill_expolygon_id].contains(point)) {
	    					island = &li;
	    					goto found;
	    				}
	    		}
	    		// 2) Find closest fill_expolygon, branch and bound by distance to bounding box.
				{
					struct Island {
						uint32_t island_idx;
						uint32_t expolygon_idx;
						double   distance2;
					};
	    			std::vector<Island> islands_sorted;
	    			for (uint32_t island_idx = 0; island_idx < uint32_t(lslice.islands.size()); ++ island_idx) {
	    				const LayerIsland   &li     = lslice.islands[island_idx];
	    				const BoundingBoxes &bboxes = li.fill_expolygons_composite() ?
	    					layer.get_region(li.perimeters.region())->fill_expolygons_composite_bboxes() :
	    					layer.get_region(li.fill_region_id)->fill_expolygons_bboxes();
	    				for (uint32_t fill_expolygon_id : li.fill_expolygons)
							islands_sorted.push_back({ island_idx, fill_expolygon_id, bbox_point_distance_squared(bboxes[fill_expolygon_id], point) });
	    			}
	    			std::sort(islands_sorted.begin(), islands_sorted.end(), [](auto &l, auto &r){ return l.distance2 < r.distance2; });
	    			auto dist_min2 = std::numeric_limits<double>::max();
	    			for (uint32_t sorted_bbox_idx = 0; sorted_bbox_idx < uint32_t(islands_sorted.size()); ++ sorted_bbox_idx) {
	    				const Island &isl = islands_sorted[sorted_bbox_idx];
	    				if (isl.distance2 > dist_min2)
							// Branch & bound condition.
	    					break;
	    				LayerIsland		  &li         = lslice.islands[isl.island_idx];
	    				const ExPolygons  &expolygons = li.fill_expolygons_composite() ?
	    					layer.get_region(li.perimeters.region())->fill_expolygons_composite() :
	    					layer.get_region(li.fill_region_id)->fill_expolygons();
	    				double d2 = (expolygons[isl.expolygon_idx].point_projection(point) - point).cast<double>().squaredNorm();
	    				if (d2 < dist_min2) {
	    					dist_min2 = d2;
	    					island = &li;
	    				}
	    			}
				}
	    	found:;
	    	}
	    	assert(island);
	    	if (island)
	    		island->add_fill_range(LayerExtrusionRange{ fill_region_id, { fill_begin, fill_end }});
	    }
    }
}

void Layer::clear_fills()
{
    for (LayerRegion *layerm : m_regions)
        layerm->m_fills.clear();
    for (LayerSlice &lslice : lslices_ex)
		for (LayerIsland &island : lslice.islands)
			island.fills.clear();
}

void Layer::make_fills(FillAdaptive::Octree* adaptive_fill_octree, FillAdaptive::Octree* support_fill_octree, FillLightning::Generator* lightning_generator)
{
	this->clear_fills();

    std::vector<SurfaceFill>  surface_fills       = group_fills(*this);
    const Slic3r::BoundingBox bbox                = this->object()->bounding_box();
    const auto                resolution          = this->object()->print()->config().gcode_resolution.value;
    const auto                perimeter_generator = this->object()->config().perimeter_generator;

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
	{
		static int iRun = 0;
		export_group_fills_to_svg(debug_out_path("Layer-fill_surfaces-10_fill-final-%d.svg", iRun ++).c_str(), surface_fills);
	}
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

	size_t first_object_layer_id = this->object()->get_layer(0)->id();
    for (SurfaceFill &surface_fill : surface_fills) {
        // Create the filler object.
        std::unique_ptr<Fill> f = std::unique_ptr<Fill>(Fill::new_from_type(surface_fill.params.pattern));
        f->set_bounding_box(bbox);
		// Layer ID is used for orienting the infill in alternating directions.
		// Layer::id() returns layer ID including raft layers, subtract them to make the infill direction independent
		// from raft.
        f->layer_id = this->id() - first_object_layer_id;
        f->z 		= this->print_z;
        f->angle 	= surface_fill.params.angle;
        f->adapt_fill_octree   = (surface_fill.params.pattern == ipSupportCubic) ? support_fill_octree : adaptive_fill_octree;
        f->print_config        = &this->object()->print()->config();
        f->print_object_config = &this->object()->config();

        if (surface_fill.params.pattern == ipLightning)
            dynamic_cast<FillLightning::Filler*>(f.get())->generator = lightning_generator;

        if (surface_fill.params.pattern == ipEnsuring) {
            auto *fill_ensuring = dynamic_cast<FillEnsuring *>(f.get());
            assert(fill_ensuring != nullptr);
            fill_ensuring->print_region_config = &m_regions[surface_fill.region_id]->region().config();
        }

        // calculate flow spacing for infill pattern generation
        bool using_internal_flow = ! surface_fill.surface.is_solid() && ! surface_fill.params.bridge;
        double link_max_length = 0.;
        if (! surface_fill.params.bridge) {
#if 0
            link_max_length = layerm.region()->config().get_abs_value(surface.is_external() ? "external_fill_link_max_length" : "fill_link_max_length", flow.spacing());
//            printf("flow spacing: %f,  is_external: %d, link_max_length: %lf\n", flow.spacing(), int(surface.is_external()), link_max_length);
#else
            if (surface_fill.params.density > 80.) // 80%
                link_max_length = 3. * f->spacing;
#endif
        }

        // Maximum length of the perimeter segment linking two infill lines.
        f->link_max_length = (coord_t)scale_(link_max_length);
        // Used by the concentric infill pattern to clip the loops to create extrusion paths.
        f->loop_clipping = coord_t(scale_(surface_fill.params.flow.nozzle_diameter()) * LOOP_CLIPPING_LENGTH_OVER_NOZZLE_DIAMETER);

        LayerRegion &layerm = *m_regions[surface_fill.region_id];

        // apply half spacing using this flow's own spacing and generate infill
        FillParams params;
        params.density                    = float(0.01 * surface_fill.params.density);
        params.dont_adjust                = false; //  surface_fill.params.dont_adjust;
        params.anchor_length              = surface_fill.params.anchor_length;
        params.anchor_length_max          = surface_fill.params.anchor_length_max;
        params.resolution                 = resolution;
        params.use_arachne                = (perimeter_generator == PerimeterGeneratorType::Arachne && surface_fill.params.pattern == ipConcentric) || surface_fill.params.pattern == ipEnsuring;
        params.layer_height               = layerm.layer()->height;
        params.prefer_clockwise_movements = this->object()->print()->config().prefer_clockwise_movements;

        for (ExPolygon &expoly : surface_fill.expolygons) {
			// Spacing is modified by the filler to indicate adjustments. Reset it for each expolygon.
			f->spacing = surface_fill.params.spacing;
			surface_fill.surface.expolygon = std::move(expoly);
            Polylines      polylines;
            ThickPolylines thick_polylines;
			try {
                if (params.use_arachne)
                    thick_polylines = f->fill_surface_arachne(&surface_fill.surface, params);
                else
				    polylines = f->fill_surface(&surface_fill.surface, params);
			} catch (InfillFailedException &) {
			}
            if (!polylines.empty() || !thick_polylines.empty()) {
                // calculate actual flow from spacing (which might have been adjusted by the infill
		        // pattern generator)
		        double flow_mm3_per_mm = surface_fill.params.flow.mm3_per_mm();
		        double flow_width      = surface_fill.params.flow.width();
		        if (using_internal_flow) {
		            // if we used the internal flow we're not doing a solid infill
		            // so we can safely ignore the slight variation that might have
		            // been applied to f->spacing
		        } else {
		            Flow new_flow   = surface_fill.params.flow.with_spacing(float(f->spacing));
		        	flow_mm3_per_mm = new_flow.mm3_per_mm();
		        	flow_width      = new_flow.width();
		        }
                // Save into layer.
                ExtrusionEntityCollection *eec        = new ExtrusionEntityCollection();
                auto                       fill_begin = uint32_t(layerm.fills().size());
                // Only concentric fills are not sorted.
                eec->no_sort = f->no_sort();
                if (params.use_arachne) {
                    for (const ThickPolyline &thick_polyline : thick_polylines) {
                        Flow new_flow = surface_fill.params.flow.with_spacing(float(f->spacing));

                        ExtrusionMultiPath multi_path = PerimeterGenerator::thick_polyline_to_multi_path(thick_polyline, surface_fill.params.extrusion_role, new_flow, scaled<float>(0.05), float(SCALED_EPSILON));
                        // Append paths to collection.
                        if (!multi_path.empty()) {
                            if (multi_path.paths.front().first_point() == multi_path.paths.back().last_point())
                                eec->entities.emplace_back(new ExtrusionLoop(std::move(multi_path.paths)));
                            else
                                eec->entities.emplace_back(new ExtrusionMultiPath(std::move(multi_path)));
                        }
                    }

                    if (!eec->empty())
                        layerm.m_fills.entities.push_back(eec);
                    else
                        delete eec;

                    thick_polylines.clear();
                } else {
                    // When prefer_clockwise_movements is true, we have to ensure that extrusion paths will not be reversed during path planning.
                    extrusion_entities_append_paths(
                        eec->entities, std::move(polylines),
						ExtrusionAttributes{
                            surface_fill.params.extrusion_role,
							ExtrusionFlow{ flow_mm3_per_mm, float(flow_width), surface_fill.params.flow.height() },
                            f->is_self_crossing()
						}, !params.prefer_clockwise_movements);
                    layerm.m_fills.entities.push_back(eec);
                }
                insert_fills_into_islands(*this, uint32_t(surface_fill.region_id), fill_begin, uint32_t(layerm.fills().size()));
		    }
		}
    }

	for (LayerSlice &lslice : this->lslices_ex)
		for (LayerIsland &island : lslice.islands) {
			if (! island.thin_fills.empty()) {
				// Copy thin fills into fills packed as a collection.
				// Fills are always stored as collections, the rest of the pipeline (wipe into infill, G-code generator) relies on it.
				LayerRegion				  &layerm	  = *this->get_region(island.perimeters.region());
				ExtrusionEntityCollection &collection = *(new ExtrusionEntityCollection());
				layerm.m_fills.entities.push_back(&collection);
				collection.entities.reserve(island.thin_fills.size());
				for (uint32_t fill_id : island.thin_fills)
					collection.entities.push_back(layerm.thin_fills().entities[fill_id]->clone());
	    		island.add_fill_range({ island.perimeters.region(), { uint32_t(layerm.m_fills.entities.size() - 1), uint32_t(layerm.m_fills.entities.size()) } });
			}
			// Sort the fills by region ID.
			std::sort(island.fills.begin(), island.fills.end(), [](auto &l, auto &r){ return l.region() < r.region() || (l.region() == r.region() && *l.begin() < *r.begin()); });
			// Compress continuous fill ranges of the same region.
			{
				size_t k = 0;
				for (size_t i = 0; i < island.fills.size();) {
					uint32_t region_id = island.fills[i].region();
					uint32_t begin     = *island.fills[i].begin();
					uint32_t end       = *island.fills[i].end();
					size_t   j         = i + 1;
					for (; j < island.fills.size() && island.fills[j].region() == region_id && *island.fills[j].begin() == end; ++ j)
						end = *island.fills[j].end();
					island.fills[k ++] = { region_id, { begin, end } };
					i = j;
				}
				island.fills.erase(island.fills.begin() + k, island.fills.end());
			}
		}

#ifndef NDEBUG
	for (LayerRegion *layerm : m_regions)
	    for (const ExtrusionEntity *e : layerm->fills())
    	    assert(dynamic_cast<const ExtrusionEntityCollection*>(e) != nullptr);
#endif
}

Polylines Layer::generate_sparse_infill_polylines_for_anchoring(FillAdaptive::Octree* adaptive_fill_octree, FillAdaptive::Octree* support_fill_octree,  FillLightning::Generator* lightning_generator) const
{
    std::vector<SurfaceFill>  surface_fills = group_fills(*this);
    const Slic3r::BoundingBox bbox          = this->object()->bounding_box();
    const auto                resolution    = this->object()->print()->config().gcode_resolution.value;

    Polylines sparse_infill_polylines{};

    for (SurfaceFill &surface_fill : surface_fills) {
		if (surface_fill.surface.surface_type != stInternal) {
			continue;
		}

        switch (surface_fill.params.pattern) {
        case ipCount: continue; break;
        case ipSupportBase: continue; break;
        case ipEnsuring: continue; break;
        case ipLightning:
		case ipAdaptiveCubic:
        case ipSupportCubic:
        case ipRectilinear:
        case ipMonotonic:
        case ipMonotonicLines:
        case ipAlignedRectilinear:
        case ipGrid:
        case ipTriangles:
        case ipStars:
        case ipCubic:
        case ipLine:
        case ipConcentric:
        case ipHoneycomb:
        case ip3DHoneycomb:
        case ipGyroid:
        case ipHilbertCurve:
        case ipArchimedeanChords:
        case ipOctagramSpiral:
        case ipZigZag: break;
        }

        // Create the filler object.
        std::unique_ptr<Fill> f = std::unique_ptr<Fill>(Fill::new_from_type(surface_fill.params.pattern));
        f->set_bounding_box(bbox);
        f->layer_id = this->id() - this->object()->get_layer(0)->id(); // We need to subtract raft layers.
        f->z        = this->print_z;
        f->angle    = surface_fill.params.angle;
        f->adapt_fill_octree   = (surface_fill.params.pattern == ipSupportCubic) ? support_fill_octree : adaptive_fill_octree;
        f->print_config        = &this->object()->print()->config();
        f->print_object_config = &this->object()->config();

        if (surface_fill.params.pattern == ipLightning)
            dynamic_cast<FillLightning::Filler *>(f.get())->generator = lightning_generator;

        // calculate flow spacing for infill pattern generation
        double link_max_length = 0.;
        if (!surface_fill.params.bridge) {
#if 0
            link_max_length = layerm.region()->config().get_abs_value(surface.is_external() ? "external_fill_link_max_length" : "fill_link_max_length", flow.spacing());
//            printf("flow spacing: %f,  is_external: %d, link_max_length: %lf\n", flow.spacing(), int(surface.is_external()), link_max_length);
#else
            if (surface_fill.params.density > 80.) // 80%
                link_max_length = 3. * f->spacing;
#endif
        }

        // Maximum length of the perimeter segment linking two infill lines.
        f->link_max_length = (coord_t) scale_(link_max_length);
        // Used by the concentric infill pattern to clip the loops to create extrusion paths.
        f->loop_clipping = coord_t(scale_(surface_fill.params.flow.nozzle_diameter()) * LOOP_CLIPPING_LENGTH_OVER_NOZZLE_DIAMETER);

        LayerRegion &layerm = *m_regions[surface_fill.region_id];

        // apply half spacing using this flow's own spacing and generate infill
        FillParams params;
        params.density           = float(0.01 * surface_fill.params.density);
        params.dont_adjust       = false; //  surface_fill.params.dont_adjust;
        params.anchor_length     = surface_fill.params.anchor_length;
        params.anchor_length_max = surface_fill.params.anchor_length_max;
        params.resolution        = resolution;
        params.use_arachne       = false;
        params.layer_height      = layerm.layer()->height;

        for (ExPolygon &expoly : surface_fill.expolygons) {
            // Spacing is modified by the filler to indicate adjustments. Reset it for each expolygon.
            f->spacing                     = surface_fill.params.spacing;
            surface_fill.surface.expolygon = std::move(expoly);
            try {
                Polylines polylines = f->fill_surface(&surface_fill.surface, params);
                sparse_infill_polylines.insert(sparse_infill_polylines.end(), polylines.begin(), polylines.end());
            } catch (InfillFailedException &) {}
        }
    }

    return sparse_infill_polylines;
}

// Create ironing extrusions over top surfaces.
void Layer::make_ironing()
{
	// LayerRegion::slices contains surfaces marked with SurfaceType.
	// Here we want to collect top surfaces extruded with the same extruder.
	// A surface will be ironed with the same extruder to not contaminate the print with another material leaking from the nozzle.

	// First classify regions based on the extruder used.
	struct IroningParams {
		int 		extruder 	= -1;
		bool 		just_infill = false;
		// Spacing of the ironing lines, also to calculate the extrusion flow from.
		double 		line_spacing;
		// Height of the extrusion, to calculate the extrusion flow from.
		double 		height;
		double 		speed;
		double 		angle;

		bool operator<(const IroningParams &rhs) const {
			if (this->extruder < rhs.extruder)
				return true;
			if (this->extruder > rhs.extruder)
				return false;
			if (int(this->just_infill) < int(rhs.just_infill))
				return true;
			if (int(this->just_infill) > int(rhs.just_infill))
				return false;
			if (this->line_spacing < rhs.line_spacing)
				return true;
			if (this->line_spacing > rhs.line_spacing)
				return false;
			if (this->height < rhs.height)
				return true;
			if (this->height > rhs.height)
				return false;
			if (this->speed < rhs.speed)
				return true;
			if (this->speed > rhs.speed)
				return false;
			if (this->angle < rhs.angle)
				return true;
			if (this->angle > rhs.angle)
				return false;
			return false;
		}

		bool operator==(const IroningParams &rhs) const {
			return this->extruder == rhs.extruder && this->just_infill == rhs.just_infill &&
				   this->line_spacing == rhs.line_spacing && this->height == rhs.height && this->speed == rhs.speed &&
				   this->angle == rhs.angle;
		}

		LayerRegion *layerm;
		uint32_t     region_id;

		// IdeaMaker: ironing
		// ironing flowrate (5% percent)
		// ironing speed (10 mm/sec)

		// Kisslicer: 
		// iron off, Sweep, Group
		// ironing speed: 15 mm/sec

		// Cura:
		// Pattern (zig-zag / concentric)
		// line spacing (0.1mm)
		// flow: from normal layer height. 10%
		// speed: 20 mm/sec
	};

	std::vector<IroningParams> by_extruder;
    double default_layer_height = this->object()->config().layer_height;

	for (uint32_t region_id = 0; region_id < uint32_t(this->regions().size()); ++region_id)
		if (LayerRegion *layerm = this->get_region(region_id); ! layerm->slices().empty()) {
			IroningParams ironing_params;
			const PrintRegionConfig &config = layerm->region().config();
			if (config.ironing && 
				(config.ironing_type == IroningType::AllSolid ||
				 	(config.top_solid_layers > 0 && 
						(config.ironing_type == IroningType::TopSurfaces ||
					 	(config.ironing_type == IroningType::TopmostOnly && layerm->layer()->upper_layer == nullptr))))) {
				if (config.perimeter_extruder == config.solid_infill_extruder || config.perimeters == 0) {
					// Iron the whole face.
					ironing_params.extruder = config.solid_infill_extruder;
				} else {
					// Iron just the infill.
					ironing_params.extruder = config.solid_infill_extruder;
				}
			}
			if (ironing_params.extruder != -1) {
				//TODO just_infill is currently not used.
				ironing_params.just_infill 	= false;
				ironing_params.line_spacing = config.ironing_spacing;
				ironing_params.height 		= default_layer_height * 0.01 * config.ironing_flowrate;
				ironing_params.speed 		= config.ironing_speed;
				ironing_params.angle 		= config.fill_angle * M_PI / 180.;
				ironing_params.layerm 		= layerm;
				ironing_params.region_id    = region_id;
				by_extruder.emplace_back(ironing_params);
			}
		}
	std::sort(by_extruder.begin(), by_extruder.end());

    FillRectilinear 	fill;
    FillParams 			fill_params;
	fill.set_bounding_box(this->object()->bounding_box());
	// Layer ID is used for orienting the infill in alternating directions.
	// Layer::id() returns layer ID including raft layers, subtract them to make the infill direction independent
	// from raft.
	//FIXME ironing does not take fill angle into account. Shall it? Does it matter?
	fill.layer_id 			 = this->id() - this->object()->get_layer(0)->id();
    fill.z 					 = this->print_z;
    fill.overlap 			 = 0;
    fill_params.density 	 = 1.;
    fill_params.monotonic    = true;

	for (size_t i = 0; i < by_extruder.size();) {
		// Find span of regions equivalent to the ironing operation.
		IroningParams &ironing_params = by_extruder[i];
		size_t j = i;
		for (++ j; j < by_extruder.size() && ironing_params == by_extruder[j]; ++ j) ;

		// Create the ironing extrusions for regions <i, j)
		ExPolygons ironing_areas;
		double nozzle_dmr = this->object()->print()->config().nozzle_diameter.values[ironing_params.extruder - 1];
		if (ironing_params.just_infill) {
			//TODO just_infill is currently not used.
			// Just infill.
		} else {
			// Infill and perimeter.
			// Merge top surfaces with the same ironing parameters.
			Polygons polys;
			Polygons infills;
			for (size_t k = i; k < j; ++ k) {
				const IroningParams		 &ironing_params  = by_extruder[k];
				const PrintRegionConfig  &region_config   = ironing_params.layerm->region().config();
				bool					  iron_everything = region_config.ironing_type == IroningType::AllSolid;
				bool					  iron_completely = iron_everything;
				if (iron_everything) {
					// Check whether there is any non-solid hole in the regions.
					bool internal_infill_solid = region_config.fill_density.value > 95.;
					for (const Surface &surface : ironing_params.layerm->fill_surfaces())
						if ((! internal_infill_solid && surface.surface_type == stInternal) || surface.surface_type == stInternalBridge || surface.surface_type == stInternalVoid) {
							// Some fill region is not quite solid. Don't iron over the whole surface.
							iron_completely = false;
							break;
						}
				}
				if (iron_completely) {
					// Iron everything. This is likely only good for solid transparent objects.
					for (const Surface &surface : ironing_params.layerm->slices())
						polygons_append(polys, surface.expolygon);
				} else {
					for (const Surface &surface : ironing_params.layerm->slices())
						if (surface.surface_type == stTop || (iron_everything && surface.surface_type == stBottom))
							// stBottomBridge is not being ironed on purpose, as it would likely destroy the bridges.
							polygons_append(polys, surface.expolygon);
				}
				if (iron_everything && ! iron_completely) {
					// Add solid fill surfaces. This may not be ideal, as one will not iron perimeters touching these
					// solid fill surfaces, but it is likely better than nothing.
					for (const Surface &surface : ironing_params.layerm->fill_surfaces())
						if (surface.surface_type == stInternalSolid)
							polygons_append(infills, surface.expolygon);
				}
			}

			if (! infills.empty() || j > i + 1) {
				// Ironing over more than a single region or over solid internal infill.
				if (! infills.empty())
					// For IroningType::AllSolid only:
					// Add solid infill areas for layers, that contain some non-ironable infil (sparse infill, bridge infill).
					append(polys, std::move(infills));
				polys = union_safety_offset(polys);
			}
			// Trim the top surfaces with half the nozzle diameter.
			ironing_areas = intersection_ex(polys, offset(this->lslices, - float(scale_(0.5 * nozzle_dmr))));
		}

        // Create the filler object.
        fill.spacing = ironing_params.line_spacing;
        fill.angle = float(ironing_params.angle + 0.25 * M_PI);
        fill.link_max_length = (coord_t)scale_(3. * fill.spacing);
		double extrusion_height = ironing_params.height * fill.spacing / nozzle_dmr;
		float  extrusion_width  = Flow::rounded_rectangle_extrusion_width_from_spacing(float(nozzle_dmr), float(extrusion_height));
		double flow_mm3_per_mm = nozzle_dmr * extrusion_height;
        Surface surface_fill(stTop, ExPolygon());
        for (ExPolygon &expoly : ironing_areas) {
			surface_fill.expolygon = std::move(expoly);
			Polylines polylines;
			try {
                assert(!fill_params.use_arachne);
				polylines = fill.fill_surface(&surface_fill, fill_params);
			} catch (InfillFailedException &) {
			}
	        if (! polylines.empty()) {
		        // Save into layer.
				auto fill_begin = uint32_t(ironing_params.layerm->fills().size());
				ExtrusionEntityCollection *eec = nullptr;
		        ironing_params.layerm->m_fills.entities.push_back(eec = new ExtrusionEntityCollection());
		        // Don't sort the ironing infill lines as they are monotonicly ordered.
				eec->no_sort = true;
		        extrusion_entities_append_paths(
		            eec->entities, std::move(polylines),
					ExtrusionAttributes{ ExtrusionRole::Ironing,
						ExtrusionFlow{ flow_mm3_per_mm, extrusion_width, float(extrusion_height) }
					});
				insert_fills_into_islands(*this, ironing_params.region_id, fill_begin, uint32_t(ironing_params.layerm->fills().size()));
		    }
		}

		// Regions up to j were processed.
		i = j;
	}
}

} // namespace Slic3r
