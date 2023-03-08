#include "ExPolygon.hpp"
#include "Layer.hpp"
#include "BridgeDetector.hpp"
#include "ClipperUtils.hpp"
#include "Geometry.hpp"
#include "PerimeterGenerator.hpp"
#include "Print.hpp"
#include "Surface.hpp"
#include "BoundingBox.hpp"
#include "SVG.hpp"
#include "Algorithm/RegionExpansion.hpp"

#include <string>
#include <map>

#include <boost/log/trivial.hpp>

namespace Slic3r {

Flow LayerRegion::flow(FlowRole role) const
{
    return this->flow(role, m_layer->height);
}

Flow LayerRegion::flow(FlowRole role, double layer_height) const
{
    return m_region->flow(*m_layer->object(), role, layer_height, m_layer->id() == 0);
}

Flow LayerRegion::bridging_flow(FlowRole role, bool force_thick_bridges) const
{
    const PrintRegion       &region         = this->region();
    const PrintRegionConfig &region_config  = region.config();
    const PrintObject       &print_object   = *this->layer()->object();
    if (print_object.config().thick_bridges || force_thick_bridges) {
        // The old Slic3r way (different from all other slicers): Use rounded extrusions.
        // Get the configured nozzle_diameter for the extruder associated to the flow role requested.
        // Here this->extruder(role) - 1 may underflow to MAX_INT, but then the get_at() will follback to zero'th element, so everything is all right.
        auto nozzle_diameter = float(print_object.print()->config().nozzle_diameter.get_at(region.extruder(role) - 1));
        // Applies default bridge spacing.
        return Flow::bridging_flow(float(sqrt(region_config.bridge_flow_ratio)) * nozzle_diameter, nozzle_diameter);
    } else {
        // The same way as other slicers: Use normal extrusions. Apply bridge_flow_ratio while maintaining the original spacing.
        return this->flow(role).with_flow_ratio(region_config.bridge_flow_ratio);
    }
}

// Fill in layerm->fill_surfaces by trimming the layerm->slices by layerm->fill_expolygons.
void LayerRegion::slices_to_fill_surfaces_clipped()
{
    // Collect polygons per surface type.
    std::array<std::vector<const Surface*>, size_t(stCount)> by_surface;
    for (const Surface &surface : this->slices())
        by_surface[size_t(surface.surface_type)].emplace_back(&surface);
    // Trim surfaces by the fill_boundaries.
    m_fill_surfaces.surfaces.clear();
    for (size_t surface_type = 0; surface_type < size_t(stCount); ++ surface_type) {
        const std::vector<const Surface*> &this_surfaces = by_surface[surface_type];
        if (! this_surfaces.empty())
            m_fill_surfaces.append(intersection_ex(this_surfaces, this->fill_expolygons()), SurfaceType(surface_type));
    }
}

// Produce perimeter extrusions, gap fill extrusions and fill polygons for input slices.
void LayerRegion::make_perimeters(
    // Input slices for which the perimeters, gap fills and fill expolygons are to be generated.
    const SurfaceCollection                                &slices,
    // Ranges of perimeter extrusions and gap fill extrusions per suface, referencing
    // newly created extrusions stored at this LayerRegion.
    std::vector<std::pair<ExtrusionRange, ExtrusionRange>> &perimeter_and_gapfill_ranges,
    // All fill areas produced for all input slices above.
    ExPolygons                                             &fill_expolygons,
    // Ranges of fill areas above per input slice.
    std::vector<ExPolygonRange>                            &fill_expolygons_ranges)
{
    m_perimeters.clear();
    m_thin_fills.clear();

    perimeter_and_gapfill_ranges.reserve(perimeter_and_gapfill_ranges.size() + slices.size());
    // There may be more expolygons produced per slice, thus this reserve is conservative.
    fill_expolygons.reserve(fill_expolygons.size() + slices.size());
    fill_expolygons_ranges.reserve(fill_expolygons_ranges.size() + slices.size());

    const PrintConfig       &print_config  = this->layer()->object()->print()->config();
    const PrintRegionConfig &region_config = this->region().config();
    // This needs to be in sync with PrintObject::_slice() slicing_mode_normal_below_layer!
    bool spiral_vase = print_config.spiral_vase &&
        //FIXME account for raft layers.
        (this->layer()->id() >= size_t(region_config.bottom_solid_layers.value) &&
         this->layer()->print_z >= region_config.bottom_solid_min_thickness - EPSILON);

    PerimeterGenerator::Parameters params(
        this->layer()->height,
        int(this->layer()->id()),
        this->flow(frPerimeter),
        this->flow(frExternalPerimeter),
        this->bridging_flow(frPerimeter),
        this->flow(frSolidInfill),
        region_config,
        this->layer()->object()->config(),
        print_config,
        spiral_vase
    );

    // Cummulative sum of polygons over all the regions.
    const ExPolygons *lower_slices = this->layer()->lower_layer ? &this->layer()->lower_layer->lslices : nullptr;
    // Cache for offsetted lower_slices
    Polygons          lower_layer_polygons_cache;

    for (const Surface &surface : slices) {
        auto perimeters_begin      = uint32_t(m_perimeters.size());
        auto gap_fills_begin       = uint32_t(m_thin_fills.size());
        auto fill_expolygons_begin = uint32_t(fill_expolygons.size());
        if (this->layer()->object()->config().perimeter_generator.value == PerimeterGeneratorType::Arachne && !spiral_vase)
            PerimeterGenerator::process_arachne(
                // input:
                params,
                surface,
                lower_slices,
                lower_layer_polygons_cache,
                // output:
                m_perimeters,
                m_thin_fills,
                fill_expolygons);
        else
            PerimeterGenerator::process_classic(
                // input:
                params,
                surface,
                lower_slices,
                lower_layer_polygons_cache,
                // output:
                m_perimeters,
                m_thin_fills,
                fill_expolygons);
        perimeter_and_gapfill_ranges.emplace_back(
            ExtrusionRange{ perimeters_begin, uint32_t(m_perimeters.size()) }, 
            ExtrusionRange{ gap_fills_begin,  uint32_t(m_thin_fills.size()) });
        fill_expolygons_ranges.emplace_back(ExtrusionRange{ fill_expolygons_begin, uint32_t(fill_expolygons.size()) });
    }
}

#if 1

// Extract surfaces of given type from surfaces, extract fill (layer) thickness of one of the surfaces.
static ExPolygons fill_surfaces_extract_expolygons(Surfaces &surfaces, SurfaceType surface_type, double &thickness)
{
    size_t cnt = 0;
    for (const Surface &surface : surfaces)
        if (surface.surface_type == surface_type) {
            ++ cnt;
            thickness = surface.thickness;
        }
    if (cnt == 0)
        return {};

    ExPolygons out;
    out.reserve(cnt);
    for (Surface &surface : surfaces)
        if (surface.surface_type == surface_type)
            out.emplace_back(std::move(surface.expolygon));
    return out;
}

// Extract bridging surfaces from "surfaces", expand them into "shells" using expansion_params,
// detect bridges.
// Trim "shells" by the expanded bridges.
Surfaces expand_bridges_detect_orientations(
    Surfaces                                    &surfaces,
    ExPolygons                                  &shells,
    const Algorithm::RegionExpansionParameters  &expansion_params)
{
    using namespace Slic3r::Algorithm;

    double thickness;
    ExPolygons bridges_ex = fill_surfaces_extract_expolygons(surfaces, stBottomBridge, thickness);
    if (bridges_ex.empty())
        return {};

    // Calculate bridge anchors and their expansions in their respective shell region.
    WaveSeeds                       bridge_anchors    = wave_seeds(bridges_ex, shells, expansion_params.tiny_expansion, true);
    std::vector<RegionExpansionEx>  bridge_expansions = propagate_waves_ex(bridge_anchors, shells, expansion_params);

    // Cache for detecting bridge orientation and merging regions with overlapping expansions.
    struct Bridge {
        ExPolygon                                       expolygon;
        uint32_t                                        group_id;
        std::vector<RegionExpansionEx>::const_iterator  bridge_expansion_begin;
        double                                          angle = -1;
    };
    std::vector<Bridge> bridges;
    {
        bridges.reserve(bridges_ex.size());
        uint32_t group_id = 0;
        for (ExPolygon &ex : bridges_ex)
            bridges.push_back({ std::move(ex), group_id ++, bridge_expansions.end() });
        bridges_ex.clear();
    }

    // Group the bridge surfaces by overlaps.
    auto group_id = [&bridges](uint32_t src_id) {
        uint32_t group_id = bridges[src_id].group_id;
        while (group_id != src_id) {
            src_id = group_id;
            group_id = bridges[src_id].group_id;
        }
        bridges[src_id].group_id = group_id;
        return group_id;
    };

    {
        // Cache of bboxes per expansion boundary.
        std::vector<BoundingBox> bboxes;
        // Detect overlaps of bridge anchors inside their respective shell regions.
        // bridge_expansions are sorted by boundary id and source id.
        for (auto it = bridge_expansions.begin(); it != bridge_expansions.end();) {
            // For each boundary region:
            auto it_begin = it;
            auto it_end   = std::next(it_begin);
            for (; it_end != bridge_expansions.end() && it_end->boundary_id == it_begin->boundary_id; ++ it_end) ;
            bboxes.clear();
            bboxes.reserve(it_end - it_begin);
            for (auto it2 = it_begin; it2 != it_end; ++ it2)
                bboxes.emplace_back(get_extents(it2->expolygon.contour));
            // For each bridge anchor of the current source:
            for (; it != it_end; ++ it) {
                // A grup id for this bridge.
                for (auto it2 = std::next(it); it2 != it_end; ++ it2)
                    if (it->src_id != it2->src_id &&
                        bboxes[it - it_begin].overlap(bboxes[it2 - it_begin]) &&
                        // One may ignore holes, they are irrelevant for intersection test.
                        ! intersection(it->expolygon.contour, it2->expolygon.contour).empty()) {
                        // The two bridge regions intersect. Give them the same (lower) group id.
                        uint32_t id  = group_id(it->src_id);
                        uint32_t id2 = group_id(it2->src_id);
                        if (id < id2)
                            bridges[id2].group_id = id;
                        else
                            bridges[id].group_id = id2;
                    }
            }
        }
    }

    // Detect bridge directions.
    {
        std::sort(bridge_anchors.begin(), bridge_anchors.end(), Algorithm::lower_by_src_and_boundary);
        auto it_bridge_anchor = bridge_anchors.begin();
        Lines lines;
        Polygons anchor_areas;
        for (uint32_t bridge_id = 0; bridge_id < uint32_t(bridges.size()); ++ bridge_id) {
            Bridge &bridge = bridges[bridge_id];
//            lines.clear();
            anchor_areas.clear();
            int32_t last_anchor_id = -1;
            for (; it_bridge_anchor != bridge_anchors.end() && it_bridge_anchor->src == bridge_id; ++ it_bridge_anchor) {
                if (last_anchor_id != int(it_bridge_anchor->boundary)) {
                    last_anchor_id = int(it_bridge_anchor->boundary);
                    append(anchor_areas, to_polygons(shells[last_anchor_id]));
                }
//                if (Points &polyline = it_bridge_anchor->path; polyline.size() >= 2) {
//                    reserve_more_power_of_2(lines, polyline.size() - 1);
//                    for (size_t i = 1; i < polyline.size(); ++ i)
//                        lines.push_back({ polyline[i - 1], polyline[1] });
//                }
            }
            lines = to_lines(diff_pl(to_polylines(bridge.expolygon), expand(anchor_areas, float(SCALED_EPSILON))));
            auto [bridging_dir, unsupported_dist] = detect_bridging_direction(lines, to_polygons(bridge.expolygon));
            bridge.angle = M_PI + std::atan2(bridging_dir.y(), bridging_dir.x());
            // #if 1
            //     coordf_t    stroke_width = scale_(0.06);
            //     BoundingBox bbox         = get_extents(initial);
            //     bbox.offset(scale_(1.));
            //     ::Slic3r::SVG
            //     svg(debug_out_path(("bridge"+std::to_string(bridges[idx_last].bridge_angle)+"_"+std::to_string(this->layer()->bottom_z())).c_str()),
            //     bbox);

            //     svg.draw(initial, "cyan");
            //     svg.draw(to_lines(lower_layer->lslices), "green", stroke_width);
            // #endif
        }
    }

    // Merge the groups with the same group id, produce surfaces by merging source overhangs with their newly expanded anchors.
    Surfaces out;
    {
        Polygons acc;
        Surface templ{ stBottomBridge, {} };
        std::sort(bridge_expansions.begin(), bridge_expansions.end(), [](auto &l, auto &r) {
            return l.src_id < r.src_id || (l.src_id == r.src_id && l.boundary_id < r.boundary_id);
        });
        for (auto it = bridge_expansions.begin(); it != bridge_expansions.end(); ) {
            bridges[it->src_id].bridge_expansion_begin = it;
            uint32_t src_id = it->src_id;
            for (++ it; it != bridge_expansions.end() && it->src_id == src_id; ++ it) ;
        }
        for (uint32_t bridge_id = 0; bridge_id < uint32_t(bridges.size()); ++ bridge_id)
            if (group_id(bridge_id) == bridge_id) {
                // Head of the group.
                acc.clear();
                for (uint32_t bridge_id2 = bridge_id; bridge_id2 < uint32_t(bridges.size()); ++ bridge_id2)
                    if (group_id(bridge_id2) == bridge_id) {
                        append(acc, to_polygons(std::move(bridges[bridge_id2].expolygon)));
                        auto it_bridge_expansion = bridges[bridge_id2].bridge_expansion_begin;
                        assert(it_bridge_expansion == bridge_expansions.end() || it_bridge_expansion->src_id == bridge_id2);
                        for (; it_bridge_expansion != bridge_expansions.end() && it_bridge_expansion->src_id == bridge_id2; ++ it_bridge_expansion)
                            append(acc, to_polygons(std::move(it_bridge_expansion->expolygon)));
                    }
                //FIXME try to be smart and pick the best bridging angle for all?
                templ.bridge_angle = bridges[bridge_id].angle;
                // without safety offset, artifacts are generated (GH #2494)
                for (ExPolygon &ex : union_safety_offset_ex(acc))
                    out.emplace_back(templ, std::move(ex));
            }
    }

    // Clip the shells by the expanded bridges.
    shells = diff_ex(shells, out);
    return out;
}

// Extract bridging surfaces from "surfaces", expand them into "shells" using expansion_params.
// Trim "shells" by the expanded bridges.
static Surfaces expand_merge_surfaces(
    Surfaces                                   &surfaces,
    SurfaceType                                 surface_type,
    ExPolygons                                 &shells,
    const Algorithm::RegionExpansionParameters &params,
    const double                                bridge_angle = -1.)
{
    double thickness;
    ExPolygons src = fill_surfaces_extract_expolygons(surfaces, surface_type, thickness);
    if (src.empty())
        return {};

    std::vector<ExPolygon> expanded = expand_merge_expolygons(std::move(src), shells, params);
    // Trim the shells by the expanded expolygons.
    shells = diff_ex(shells, expanded);

    Surface templ{ surface_type, {} };
    templ.bridge_angle = bridge_angle;
    Surfaces out;
    out.reserve(expanded.size());
    for (auto &expoly : expanded)
        out.emplace_back(templ, std::move(expoly));
    return out;
}

void LayerRegion::process_external_surfaces(const Layer *lower_layer, const Polygons *lower_layer_covered)
{
#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
    export_region_fill_surfaces_to_svg_debug("4_process_external_surfaces-initial");
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

    // Width of the perimeters.
    float shell_width = 0;
    if (int num_perimeters = this->region().config().perimeters; num_perimeters > 0) {
        Flow external_perimeter_flow = this->flow(frExternalPerimeter);
        Flow perimeter_flow          = this->flow(frPerimeter);
        shell_width += 0.5f * external_perimeter_flow.scaled_width() + external_perimeter_flow.scaled_spacing();
        shell_width += perimeter_flow.scaled_spacing() * (num_perimeters - 1);
    } else {
        // TODO: Maybe there is better solution when printing with zero perimeters, but this works reasonably well, given the situation
        shell_width = float(SCALED_EPSILON);
    }

    // Scaled expansions of the respective external surfaces.
    float                           expansion_top           = shell_width * sqrt(2.);
    float                           expansion_bottom        = expansion_top;
    float                           expansion_bottom_bridge = expansion_top;
    // Expand by waves of expansion_step size (expansion_step is scaled), but with no more steps than max_nr_expansion_steps.
    static constexpr const float    expansion_step          = scaled<float>(0.1);
    // Don't take more than max_nr_steps for small expansion_step.
    static constexpr const size_t   max_nr_expansion_steps  = 5;

    // Expand the top / bottom / bridge surfaces into the shell thickness solid infills.
    double     layer_thickness;
    ExPolygons shells = union_ex(fill_surfaces_extract_expolygons(m_fill_surfaces.surfaces, stInternalSolid, layer_thickness));

    SurfaceCollection bridges;
    {
        BOOST_LOG_TRIVIAL(trace) << "Processing external surface, detecting bridges. layer" << this->layer()->print_z;
        const double custom_angle = this->region().config().bridge_angle.value;
        const auto   params = Algorithm::RegionExpansionParameters::build(expansion_bottom_bridge, expansion_step, max_nr_expansion_steps);
        bridges.surfaces = custom_angle > 0 ?
            expand_merge_surfaces(m_fill_surfaces.surfaces, stBottomBridge, shells, params, Geometry::deg2rad(custom_angle)) :
            expand_bridges_detect_orientations(m_fill_surfaces.surfaces, shells, params);
        BOOST_LOG_TRIVIAL(trace) << "Processing external surface, detecting bridges - done";
#if 0
        {
            static int iRun = 0;
            bridges.export_to_svg(debug_out_path("bridges-after-grouping-%d.svg", iRun++), true);
        }
#endif
    }

    Surfaces    bottoms = expand_merge_surfaces(m_fill_surfaces.surfaces, stBottom, shells,
        Algorithm::RegionExpansionParameters::build(expansion_bottom, expansion_step, max_nr_expansion_steps));
    Surfaces    tops    = expand_merge_surfaces(m_fill_surfaces.surfaces, stTop, shells,
        Algorithm::RegionExpansionParameters::build(expansion_top, expansion_step, max_nr_expansion_steps));

    m_fill_surfaces.remove_types({ stBottomBridge, stBottom, stTop, stInternalSolid });
    reserve_more(m_fill_surfaces.surfaces, shells.size() + bridges.size() + bottoms.size() + tops.size());
    Surface solid_templ(stInternalSolid, {});
    solid_templ.thickness = layer_thickness;
    m_fill_surfaces.append(std::move(shells), solid_templ);
    m_fill_surfaces.append(std::move(bridges.surfaces));
    m_fill_surfaces.append(std::move(bottoms));
    m_fill_surfaces.append(std::move(tops));

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
    export_region_fill_surfaces_to_svg_debug("4_process_external_surfaces-final");
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
}
#else

//#define EXTERNAL_SURFACES_OFFSET_PARAMETERS ClipperLib::jtMiter, 3.
//#define EXTERNAL_SURFACES_OFFSET_PARAMETERS ClipperLib::jtMiter, 1.5
#define EXTERNAL_SURFACES_OFFSET_PARAMETERS ClipperLib::jtSquare, 0.

void LayerRegion::process_external_surfaces(const Layer *lower_layer, const Polygons *lower_layer_covered)
{
    const bool      has_infill = this->region().config().fill_density.value > 0.;
//    const float		margin     = scaled<float>(0.1); // float(scale_(EXTERNAL_INFILL_MARGIN));
    const float     margin     = float(scale_(EXTERNAL_INFILL_MARGIN));

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
    export_region_fill_surfaces_to_svg_debug("4_process_external_surfaces-initial");
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

    // 1) Collect bottom and bridge surfaces, each of them grown by a fixed 3mm offset
    // for better anchoring.
    // Bottom surfaces, grown.
    Surfaces                    bottom;
    // Bridge surfaces, initialy not grown.
    Surfaces                    bridges;
    // Top surfaces, grown.
    Surfaces                    top;
    // Internal surfaces, not grown.
    Surfaces                    internal;
    // Areas, where an infill of various types (top, bottom, bottom bride, sparse, void) could be placed.
    Polygons                    fill_boundaries = to_polygons(this->fill_expolygons());

    // Collect top surfaces and internal surfaces.
    // Collect fill_boundaries: If we're slicing with no infill, we can't extend external surfaces over non-existent infill.
    // This loop destroys the surfaces (aliasing this->fill_surfaces.surfaces) by moving into top/internal/fill_boundaries!

    {
        // Voids are sparse infills if infill rate is zero.
        Polygons voids;
        for (const Surface &surface : this->fill_surfaces()) {
            assert(! surface.empty());
            if (! surface.empty()) {
                if (surface.is_top()) {
                    // Collect the top surfaces, inflate them and trim them by the bottom surfaces.
                    // This gives the priority to bottom surfaces.
                    surfaces_append(top, offset_ex(surface.expolygon, margin, EXTERNAL_SURFACES_OFFSET_PARAMETERS), surface);
                } else if (surface.surface_type == stBottom || (surface.surface_type == stBottomBridge && lower_layer == nullptr)) {
                    // Grown by 3mm.
                    surfaces_append(bottom, offset_ex(surface.expolygon, margin, EXTERNAL_SURFACES_OFFSET_PARAMETERS), surface);
                } else if (surface.surface_type == stBottomBridge) {
                    bridges.emplace_back(surface);
                } else {
                    assert(surface.is_internal());
                	assert(surface.surface_type == stInternal || surface.surface_type == stInternalSolid);
                	if (! has_infill && lower_layer != nullptr)
                		polygons_append(voids, surface.expolygon);
                	internal.emplace_back(std::move(surface));
                }
            }
        }
        if (! voids.empty()) {
            // There are some voids (empty infill regions) on this layer. Usually one does not want to expand
            // any infill into these voids, with the exception the expanded infills are supported by layers below
            // with nonzero inill.
            assert(! has_infill && lower_layer != nullptr);
        	// Remove voids from fill_boundaries, that are not supported by the layer below.
            Polygons lower_layer_covered_tmp;
            if (lower_layer_covered == nullptr) {
            	lower_layer_covered = &lower_layer_covered_tmp;
            	lower_layer_covered_tmp = to_polygons(lower_layer->lslices);
            }
            if (! lower_layer_covered->empty())
                // Allow the top / bottom surfaces to expand into the voids of this layer if supported by the layer below.
            	voids = diff(voids, *lower_layer_covered);
            if (! voids.empty())
                fill_boundaries = diff(fill_boundaries, voids);
        }
    }

#if 0
    {
        static int iRun = 0;
        bridges.export_to_svg(debug_out_path("bridges-before-grouping-%d.svg", iRun ++), true);
    }
#endif

    if (bridges.empty())
    {
        fill_boundaries = union_safety_offset(fill_boundaries);
    } else
    {
        // 1) Calculate the inflated bridge regions, each constrained to its island.
        ExPolygons               fill_boundaries_ex = union_safety_offset_ex(fill_boundaries);
        std::vector<Polygons>    bridges_grown;
        std::vector<BoundingBox> bridge_bboxes;

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
        {
            static int iRun = 0;
            SVG svg(debug_out_path("4_process_external_surfaces-fill_regions-%d.svg", iRun ++).c_str(), get_extents(fill_boundaries_ex));
            svg.draw(fill_boundaries_ex);
            svg.draw_outline(fill_boundaries_ex, "black", "blue", scale_(0.05)); 
            svg.Close();
        }
//        export_region_fill_surfaces_to_svg_debug("4_process_external_surfaces-initial");
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
 
        {
            // Bridge expolygons, grown, to be tested for intersection with other bridge regions.
            std::vector<BoundingBox> fill_boundaries_ex_bboxes = get_extents_vector(fill_boundaries_ex);
            bridges_grown.reserve(bridges.size());
            bridge_bboxes.reserve(bridges.size());
            for (size_t i = 0; i < bridges.size(); ++ i) {
                // Find the island of this bridge.
                const Point pt = bridges[i].expolygon.contour.points.front();
                int idx_island = -1;
                for (int j = 0; j < int(fill_boundaries_ex.size()); ++ j)
                    if (fill_boundaries_ex_bboxes[j].contains(pt) && 
                        fill_boundaries_ex[j].contains(pt)) {
                        idx_island = j;
                        break;
                    }
                // Grown by 3mm.
                Polygons polys = offset(bridges[i].expolygon, margin, EXTERNAL_SURFACES_OFFSET_PARAMETERS);
                if (idx_island == -1) {
				    BOOST_LOG_TRIVIAL(trace) << "Bridge did not fall into the source region!";
                } else {
                    // Found an island, to which this bridge region belongs. Trim the expanded bridging region
                    // with its source region, so it does not overflow into a neighbor region.
                    polys = intersection(polys, fill_boundaries_ex[idx_island]);
                }
                bridge_bboxes.push_back(get_extents(polys));
                bridges_grown.push_back(std::move(polys));
            }
        }

        // 2) Group the bridge surfaces by overlaps.
        std::vector<size_t> bridge_group(bridges.size(), (size_t)-1);
        size_t n_groups = 0; 
        for (size_t i = 0; i < bridges.size(); ++ i) {
            // A grup id for this bridge.
            size_t group_id = (bridge_group[i] == size_t(-1)) ? (n_groups ++) : bridge_group[i];
            bridge_group[i] = group_id;
            // For all possibly overlaping bridges:
            for (size_t j = i + 1; j < bridges.size(); ++ j) {
                if (! bridge_bboxes[i].overlap(bridge_bboxes[j]))
                    continue;
                if (intersection(bridges_grown[i], bridges_grown[j]).empty())
                    continue;
                // The two bridge regions intersect. Give them the same group id.
                if (bridge_group[j] != size_t(-1)) {
                    // The j'th bridge has been merged with some other bridge before.
                    size_t group_id_new = bridge_group[j];
                    for (size_t k = 0; k < j; ++ k)
                        if (bridge_group[k] == group_id)
                            bridge_group[k] = group_id_new;
                    group_id = group_id_new;
                }
                bridge_group[j] = group_id;
            }
        }

        // 3) Merge the groups with the same group id, detect bridges.
        {
			BOOST_LOG_TRIVIAL(trace) << "Processing external surface, detecting bridges. layer" << this->layer()->print_z << ", bridge groups: " << n_groups;
            for (size_t group_id = 0; group_id < n_groups; ++ group_id) {
                size_t n_bridges_merged = 0;
                size_t idx_last = (size_t)-1;
                for (size_t i = 0; i < bridges.size(); ++ i) {
                    if (bridge_group[i] == group_id) {
                        ++ n_bridges_merged;
                        idx_last = i;
                    }
                }
                if (n_bridges_merged == 0)
                    // This group has no regions assigned as these were moved into another group.
                    continue;
                // Collect the initial ungrown regions and the grown polygons.
                ExPolygons  initial;
                Polygons    grown;
                for (size_t i = 0; i < bridges.size(); ++ i) {
                    if (bridge_group[i] != group_id)
                        continue;
                    initial.push_back(std::move(bridges[i].expolygon));
                    polygons_append(grown, bridges_grown[i]);
                }
                // detect bridge direction before merging grown surfaces otherwise adjacent bridges
                // would get merged into a single one while they need different directions
                // also, supply the original expolygon instead of the grown one, because in case
                // of very thin (but still working) anchors, the grown expolygon would go beyond them
                double custom_angle = Geometry::deg2rad(this->region().config().bridge_angle.value);
                if (custom_angle > 0.0) {
                    bridges[idx_last].bridge_angle = custom_angle;
                } else {
                    auto [bridging_dir, unsupported_dist] = detect_bridging_direction(to_polygons(initial), to_polygons(lower_layer->lslices));
                    bridges[idx_last].bridge_angle = PI + std::atan2(bridging_dir.y(), bridging_dir.x());

                    // #if 1
                    //     coordf_t    stroke_width = scale_(0.06);
                    //     BoundingBox bbox         = get_extents(initial);
                    //     bbox.offset(scale_(1.));
                    //     ::Slic3r::SVG
                    //     svg(debug_out_path(("bridge"+std::to_string(bridges[idx_last].bridge_angle)+"_"+std::to_string(this->layer()->bottom_z())).c_str()),
                    //     bbox);

                    //     svg.draw(initial, "cyan");
                    //     svg.draw(to_lines(lower_layer->lslices), "green", stroke_width);
                    // #endif
                }

                /*
                BridgeDetector bd(initial, lower_layer->lslices, this->bridging_flow(frInfill).scaled_width());
                #ifdef SLIC3R_DEBUG
                printf("Processing bridge at layer %zu:\n", this->layer()->id());
                #endif
				double custom_angle = Geometry::deg2rad(this->region().config().bridge_angle.value);
				if (bd.detect_angle(custom_angle)) {
                    bridges[idx_last].bridge_angle = bd.angle;
                    if (this->layer()->object()->has_support()) {
//                        polygons_append(this->bridged, bd.coverage());
                        append(m_unsupported_bridge_edges, bd.unsupported_edges());
                    }
				} else if (custom_angle > 0) {
					// Bridge was not detected (likely it is only supported at one side). Still it is a surface filled in
					// using a bridging flow, therefore it makes sense to respect the custom bridging direction.
					bridges[idx_last].bridge_angle = custom_angle;
				}
                */
                // without safety offset, artifacts are generated (GH #2494)
                surfaces_append(bottom, union_safety_offset_ex(grown), bridges[idx_last]);
            }

            fill_boundaries = to_polygons(fill_boundaries_ex);
			BOOST_LOG_TRIVIAL(trace) << "Processing external surface, detecting bridges - done";
		}

    #if 0
        {
            static int iRun = 0;
            bridges.export_to_svg(debug_out_path("bridges-after-grouping-%d.svg", iRun ++), true);
        }
    #endif
    }

    Surfaces new_surfaces;
    {
        // Intersect the grown surfaces with the actual fill boundaries.
        Polygons bottom_polygons = to_polygons(bottom);
        // Merge top and bottom in a single collection.
        surfaces_append(top, std::move(bottom));
        for (size_t i = 0; i < top.size(); ++ i) {
            Surface &s1 = top[i];
            if (s1.empty())
                continue;
            Polygons polys;
            polygons_append(polys, to_polygons(std::move(s1)));
            for (size_t j = i + 1; j < top.size(); ++ j) {
                Surface &s2 = top[j];
                if (! s2.empty() && surfaces_could_merge(s1, s2)) {
                    polygons_append(polys, to_polygons(std::move(s2)));
                    s2.clear();
                }
            }
            if (s1.is_top())
                // Trim the top surfaces by the bottom surfaces. This gives the priority to the bottom surfaces.
                polys = diff(polys, bottom_polygons);
            surfaces_append(
                new_surfaces,
                // Don't use a safety offset as fill_boundaries were already united using the safety offset.
                intersection_ex(polys, fill_boundaries),
                s1);
        }
    }
    
    // Subtract the new top surfaces from the other non-top surfaces and re-add them.
    Polygons new_polygons = to_polygons(new_surfaces);
    for (size_t i = 0; i < internal.size(); ++ i) {
        Surface &s1 = internal[i];
        if (s1.empty())
            continue;
        Polygons polys;
        polygons_append(polys, to_polygons(std::move(s1)));
        for (size_t j = i + 1; j < internal.size(); ++ j) {
            Surface &s2 = internal[j];
            if (! s2.empty() && surfaces_could_merge(s1, s2)) {
                polygons_append(polys, to_polygons(std::move(s2)));
                s2.clear();
            }
        }
        ExPolygons new_expolys = diff_ex(polys, new_polygons);
        polygons_append(new_polygons, to_polygons(new_expolys));
        surfaces_append(new_surfaces, std::move(new_expolys), s1);
    }
    
    m_fill_surfaces.surfaces = std::move(new_surfaces);

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
    export_region_fill_surfaces_to_svg_debug("4_process_external_surfaces-final");
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
}
#endif

void LayerRegion::prepare_fill_surfaces()
{
#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
    export_region_slices_to_svg_debug("2_prepare_fill_surfaces-initial");
    export_region_fill_surfaces_to_svg_debug("2_prepare_fill_surfaces-initial");
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */ 

    /*  Note: in order to make the psPrepareInfill step idempotent, we should never
        alter fill_surfaces boundaries on which our idempotency relies since that's
        the only meaningful information returned by psPerimeters. */
    
    bool spiral_vase = this->layer()->object()->print()->config().spiral_vase;

    // if no solid layers are requested, turn top/bottom surfaces to internal
    // For Lightning infill, infill_only_where_needed is ignored because both
    // do a similar thing, and their combination doesn't make much sense.
    if (! spiral_vase && this->region().config().top_solid_layers == 0) {
        for (Surface &surface : m_fill_surfaces)
            if (surface.is_top())
                surface.surface_type = /*this->layer()->object()->config().infill_only_where_needed && this->region().config().fill_pattern != ipLightning ? stInternalVoid :*/ stInternal;
    }
    if (this->region().config().bottom_solid_layers == 0) {
        for (Surface &surface : m_fill_surfaces)
            if (surface.is_bottom()) // (surface.surface_type == stBottom)
                surface.surface_type = stInternal;
    }

    // turn too small internal regions into solid regions according to the user setting
    if (! spiral_vase && this->region().config().fill_density.value > 0) {
        // scaling an area requires two calls!
        double min_area = scale_(scale_(this->region().config().solid_infill_below_area.value));
        for (Surface &surface : m_fill_surfaces)
            if (surface.surface_type == stInternal && surface.area() <= min_area)
                surface.surface_type = stInternalSolid;
    }

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
    export_region_slices_to_svg_debug("2_prepare_fill_surfaces-final");
    export_region_fill_surfaces_to_svg_debug("2_prepare_fill_surfaces-final");
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
}

double LayerRegion::infill_area_threshold() const
{
    double ss = this->flow(frSolidInfill).scaled_spacing();
    return ss*ss;
}

void LayerRegion::trim_surfaces(const Polygons &trimming_polygons)
{
#ifndef NDEBUG
    for (const Surface &surface : this->slices())
        assert(surface.surface_type == stInternal);
#endif /* NDEBUG */
	m_slices.set(intersection_ex(this->slices().surfaces, trimming_polygons), stInternal);
}

void LayerRegion::elephant_foot_compensation_step(const float elephant_foot_compensation_perimeter_step, const Polygons &trimming_polygons)
{
#ifndef NDEBUG
    for (const Surface &surface : this->slices())
        assert(surface.surface_type == stInternal);
#endif /* NDEBUG */
    Polygons tmp = intersection(this->slices().surfaces, trimming_polygons);
    append(tmp, diff(this->slices().surfaces, opening(this->slices().surfaces, elephant_foot_compensation_perimeter_step)));
    m_slices.set(union_ex(tmp), stInternal);
}

void LayerRegion::export_region_slices_to_svg(const char *path) const
{
    BoundingBox bbox;
    for (const Surface &surface : this->slices())
        bbox.merge(get_extents(surface.expolygon));
    Point legend_size = export_surface_type_legend_to_svg_box_size();
    Point legend_pos(bbox.min(0), bbox.max(1));
    bbox.merge(Point(std::max(bbox.min(0) + legend_size(0), bbox.max(0)), bbox.max(1) + legend_size(1)));

    SVG svg(path, bbox);
    const float transparency = 0.5f;
    for (const Surface &surface : this->slices())
        svg.draw(surface.expolygon, surface_type_to_color_name(surface.surface_type), transparency);
    for (const Surface &surface : this->fill_surfaces())
        svg.draw(surface.expolygon.lines(), surface_type_to_color_name(surface.surface_type));
    export_surface_type_legend_to_svg(svg, legend_pos);
    svg.Close();
}

// Export to "out/LayerRegion-name-%d.svg" with an increasing index with every export.
void LayerRegion::export_region_slices_to_svg_debug(const char *name) const
{
    static std::map<std::string, size_t> idx_map;
    size_t &idx = idx_map[name];
    this->export_region_slices_to_svg(debug_out_path("LayerRegion-slices-%s-%d.svg", name, idx ++).c_str());
}

void LayerRegion::export_region_fill_surfaces_to_svg(const char *path) const
{
    BoundingBox bbox;
    for (const Surface &surface : this->fill_surfaces())
        bbox.merge(get_extents(surface.expolygon));
    Point legend_size = export_surface_type_legend_to_svg_box_size();
    Point legend_pos(bbox.min(0), bbox.max(1));
    bbox.merge(Point(std::max(bbox.min(0) + legend_size(0), bbox.max(0)), bbox.max(1) + legend_size(1)));

    SVG svg(path, bbox);
    const float transparency = 0.5f;
    for (const Surface &surface : this->fill_surfaces()) {
        svg.draw(surface.expolygon, surface_type_to_color_name(surface.surface_type), transparency);
        svg.draw_outline(surface.expolygon, "black", "blue", scale_(0.05)); 
    }
    export_surface_type_legend_to_svg(svg, legend_pos);
    svg.Close();
}

// Export to "out/LayerRegion-name-%d.svg" with an increasing index with every export.
void LayerRegion::export_region_fill_surfaces_to_svg_debug(const char *name) const
{
    static std::map<std::string, size_t> idx_map;
    size_t &idx = idx_map[name];
    this->export_region_fill_surfaces_to_svg(debug_out_path("LayerRegion-fill_surfaces-%s-%d.svg", name, idx ++).c_str());
}


bool LayerRegion::needs_bridge_over_infill() const
{
    // Minimum width of voids in sparse infill for the layer above to be treated as a bridge.
    // Scaled based on layer height, with a 3mm void at 0.2mm layer height as the baseline.
    const float min_void_width = bridging_flow(frSolidInfill, true).scaled_width() * 3.f * m_layer->height / 0.2;

    const float fill_density = region().config().fill_density.value / 100.0;
    if (fill_density > 0.0) {
        // Estimate the infill void width from infill density and extrusion width.
        const float infill_extrusion_width = flow(frInfill).scaled_width();
        if (min_void_width > (infill_extrusion_width / fill_density - infill_extrusion_width))
            return false;
    }

    return true;
}

}
 
