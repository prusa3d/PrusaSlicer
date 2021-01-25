#include <assert.h>
#include <stdio.h>
#include <memory>

#include "../ClipperUtils.hpp"
#include "../Geometry.hpp"
#include "../Layer.hpp"
#include "../Print.hpp"
#include "../PrintConfig.hpp"
#include "../Surface.hpp"

#include "FillBase.hpp"
#include "FillRectilinear.hpp"

namespace Slic3r {

struct SurfaceFillParams : FillParams
{
    // Zero based extruder ID.
    unsigned int    extruder = 0;
    // Infill pattern, adjusted for the density etc.
    InfillPattern   pattern = InfillPattern(0);

    // FillBase
    // in unscaled coordinates
    coordf_t        spacing = 0.;
    // infill / perimeter overlap, in unscaled coordinates
    coordf_t        overlap = 0.;
    // Angle as provided by the region config, in radians.
    float           angle = 0.f;
    // Non-negative for a bridge.
    float           bridge_angle = 0.f;

    // Various print settings?

    // Index of this entry in a linear vector.
    size_t          idx = 0;


    bool operator<(const SurfaceFillParams &rhs) const {
#define RETURN_COMPARE_NON_EQUAL(KEY) if (this->KEY < rhs.KEY) return true; if (this->KEY > rhs.KEY) return false;
#define RETURN_COMPARE_NON_EQUAL_TYPED(TYPE, KEY) if (TYPE(this->KEY) < TYPE(rhs.KEY)) return true; if (TYPE(this->KEY) > TYPE(rhs.KEY)) return false;

        // Sort first by decreasing bridging angle, so that the bridges are processed with priority when trimming one layer by the other.
        if (this->bridge_angle > rhs.bridge_angle) return true; 
        if (this->bridge_angle < rhs.bridge_angle) return false;

        RETURN_COMPARE_NON_EQUAL(extruder);
        RETURN_COMPARE_NON_EQUAL_TYPED(unsigned, pattern);
        RETURN_COMPARE_NON_EQUAL(spacing);
        RETURN_COMPARE_NON_EQUAL(overlap);
        RETURN_COMPARE_NON_EQUAL(angle);
        RETURN_COMPARE_NON_EQUAL(density);
        RETURN_COMPARE_NON_EQUAL(monotonic);
        RETURN_COMPARE_NON_EQUAL_TYPED(unsigned, connection);
        RETURN_COMPARE_NON_EQUAL_TYPED(unsigned, dont_adjust);

        RETURN_COMPARE_NON_EQUAL(anchor_length);        RETURN_COMPARE_NON_EQUAL(fill_exactly);
        RETURN_COMPARE_NON_EQUAL(flow.width);
        RETURN_COMPARE_NON_EQUAL(flow.height);
        RETURN_COMPARE_NON_EQUAL(flow.nozzle_diameter);
        RETURN_COMPARE_NON_EQUAL_TYPED(unsigned, flow.bridge);
        RETURN_COMPARE_NON_EQUAL_TYPED(unsigned, role);
        return false;
    }

    bool operator==(const SurfaceFillParams &rhs) const {
        return  this->extruder              == rhs.extruder         &&
                this->pattern               == rhs.pattern          &&
                this->pattern               == rhs.pattern          &&
                this->spacing               == rhs.spacing          &&
                this->overlap               == rhs.overlap          &&
                this->angle                 == rhs.angle            &&
                this->density               == rhs.density          &&
                this->monotonic             == rhs.monotonic        &&
                this->connection            == rhs.connection       &&
                this->dont_adjust           == rhs.dont_adjust      &&
                this->anchor_length         == rhs.anchor_length    &&
                this->anchor_length_max     == rhs.anchor_length_max&&
                this->fill_exactly          == rhs.fill_exactly     &&
                this->flow                  == rhs.flow             &&
                this->role                  == rhs.role;
    }
};

struct SurfaceFill {
    SurfaceFill(const SurfaceFillParams& params) : region_id(size_t(-1)), surface(stNone, ExPolygon()), params(params) {}

    size_t                 region_id;
    Surface             surface;
    ExPolygons           expolygons;
    SurfaceFillParams    params;
};

std::vector<SurfaceFill> group_fills(const Layer &layer)
{
    std::vector<SurfaceFill> surface_fills;

    // Fill in a map of a region & surface to SurfaceFillParams.
    std::set<SurfaceFillParams>                         set_surface_params;
    std::vector<std::vector<const SurfaceFillParams*>>     region_to_surface_params(layer.regions().size(), std::vector<const SurfaceFillParams*>());
    SurfaceFillParams                                    params;
    bool                                                 has_internal_voids = false;
    for (size_t region_id = 0; region_id < layer.regions().size(); ++ region_id) {
        const LayerRegion  &layerm = *layer.regions()[region_id];
        region_to_surface_params[region_id].assign(layerm.fill_surfaces.size(), nullptr);
        for (const Surface &surface : layerm.fill_surfaces.surfaces)
            if (surface.surface_type == (stPosInternal | stDensVoid)) {
                has_internal_voids = true;
            } else {
                const PrintRegionConfig &region_config = layerm.region()->config();
                FlowRole extrusion_role = surface.has_pos_top() ? frTopSolidInfill : (surface.has_fill_solid() ? frSolidInfill : frInfill);
                bool     is_bridge      = layer.id() > 0 && surface.has_mod_bridge();
                bool     is_denser      = false;
                params.extruder         = layerm.region()->extruder(extrusion_role);
                params.pattern          = region_config.fill_pattern.value;
                params.density          = float(region_config.fill_density) / 100.f;
                params.dont_adjust      = false;
                params.connection       = region_config.infill_connection.value;

                if (surface.has_fill_solid()) {
                    params.density = 1.f;
                    params.pattern = ipRectilinear;
                    params.connection = region_config.infill_connection_solid.value;
                    if (surface.has_pos_top())
                        params.connection = region_config.infill_connection_top.value;
                    if (surface.has_pos_bottom())
                        params.connection = region_config.infill_connection_bottom.value;
                    if (is_bridge)
                        params.connection = InfillConnection::icConnected;
                    if (surface.has_pos_external() && !is_bridge)
                        params.pattern = surface.has_pos_top() ? region_config.top_fill_pattern.value : region_config.bottom_fill_pattern.value;
                    else if (!is_bridge)
                        params.pattern = region_config.solid_fill_pattern.value;
                } else {
                    if (is_bridge)
                        params.connection = InfillConnection::icConnected;
                    if (region_config.infill_dense.getBool()
                        && region_config.fill_density < 40
                        && surface.maxNbSolidLayersOnTop == 1) {
                        params.density = 0.42f;
                        is_denser = true;
                        is_bridge = true;
                        params.pattern = ipRectiWithPerimeter;
                        params.connection = InfillConnection::icConnected;
                    }
                    if (params.density <= 0)
                        continue;
                }
                //adjust spacing/density (to over-extrude when needed)
                if (surface.has_mod_overBridge()) {
                    params.density = float(layerm.region()->config().over_bridge_flow_ratio.get_abs_value(1));
                }

                //note: same as getRoleFromSurfaceType()
                params.role = erInternalInfill;
                if (is_bridge) {
                    if(surface.has_pos_bottom())
                        params.role = erBridgeInfill;
                    else
                        params.role = erInternalBridgeInfill;
                } else if (surface.has_fill_solid()) {
                    if (surface.has_pos_top()) {
                        params.role = erTopSolidInfill;
                    } else {
                        params.role = erSolidInfill;
                    }
                }
                params.fill_exactly = region_config.enforce_full_fill_volume.getBool();
                params.bridge_angle = float(surface.bridge_angle);
                if (is_denser) {
                    params.angle = 0;
                } else {
                    params.angle = float(Geometry::deg2rad(region_config.fill_angle.value));
                    params.angle += float(PI * (region_config.fill_angle_increment.value * layerm.layer()->id()) / 180.f);
                }
		        params.anchor_length = std::min(params.anchor_length, params.anchor_length_max);

                //adjust flow (to over-extrude when needed)
                params.flow_mult = 1;
                if (surface.has_pos_top())
                    params.flow_mult *= float(region_config.fill_top_flow_ratio.get_abs_value(1));

                params.config = &layerm.region()->config();

                // calculate the actual flow we'll be using for this infill
                params.flow = layerm.region()->flow(
                    extrusion_role,
                    (surface.thickness == -1) ? layer.height : surface.thickness,   // extrusion height
                    is_bridge,                                                      // bridge flow?
                    layer.id() == 0,                                                // first layer?
                    -1,                                                             // auto width
                    *layer.object()
                );
                
                // Calculate flow spacing for infill pattern generation.
                if (surface.has_fill_solid() || is_bridge) {
                    params.spacing = params.flow.spacing();
                    // Don't limit anchor length for solid or bridging infill.
                    // use old algo to prevent possible weird stuff from sparse bridging
                    params.anchor_length = is_bridge?0:1000.f;
                } else {
                    // it's internal infill, so we can calculate a generic flow spacing 
                    // for all layers, for avoiding the ugly effect of
                    // misaligned infill on first layer because of different extrusion width and
                    // layer height
                    params.spacing = layerm.region()->flow(
                            frInfill,
                            layer.object()->config().layer_height.value,  // TODO: handle infill_every_layers?
                            false,  // no bridge
                            false,  // no first layer
                            -1,     // auto width
                            *layer.object()
                        ).spacing();
                    // Anchor a sparse infill to inner perimeters with the following anchor length:
                    params.anchor_length = float(region_config.infill_anchor);
                    if (region_config.infill_anchor.percent)
                        params.anchor_length = float(params.anchor_length * 0.01 * params.spacing);
                    params.anchor_length_max = float(region_config.infill_anchor_max);
                    if (region_config.infill_anchor_max.percent)
                        params.anchor_length_max = float(params.anchor_length_max * 0.01 * params.spacing);
                }

                auto it_params = set_surface_params.find(params);
                if (it_params == set_surface_params.end())
                    it_params = set_surface_params.insert(it_params, params);
                region_to_surface_params[region_id][&surface - &layerm.fill_surfaces.surfaces.front()] = &(*it_params);
            }
    }

    surface_fills.reserve(set_surface_params.size());
    for (const SurfaceFillParams &params : set_surface_params) {
        const_cast<SurfaceFillParams&>(params).idx = surface_fills.size();
        surface_fills.emplace_back(params);
    }

    for (size_t region_id = 0; region_id < layer.regions().size(); ++ region_id) {
        const LayerRegion &layerm = *layer.regions()[region_id];
        for (const Surface &surface : layerm.fill_surfaces.surfaces)
            if (surface.surface_type != (stPosInternal | stDensVoid)) {
                const SurfaceFillParams *params = region_to_surface_params[region_id][&surface - &layerm.fill_surfaces.surfaces.front()];
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
                    fill.expolygons = all_polygons.empty() ? union_ex(polys, true) : diff_ex(polys, all_polygons, true);
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
        int         region_solid_infill = -1;
        int         region_some_infill = -1;
        for (SurfaceFill &surface_fill : surface_fills)
            if (! surface_fill.expolygons.empty()) {
                distance_between_surfaces = std::max(distance_between_surfaces, surface_fill.params.flow.scaled_spacing());
                append((surface_fill.surface.surface_type == (stPosInternal | stDensVoid)) ? voids : surfaces_polygons, to_polygons(surface_fill.expolygons));
                if (surface_fill.surface.surface_type == (stPosInternal | stDensSolid))
                    region_internal_infill = (int)surface_fill.region_id;
                if (surface_fill.surface.has_fill_solid())
                    region_solid_infill = (int)surface_fill.region_id;
                if (surface_fill.surface.surface_type != (stPosInternal | stDensVoid))
                    region_some_infill = (int)surface_fill.region_id;
            }
        if (! voids.empty() && ! surfaces_polygons.empty()) {
            // First clip voids by the printing polygons, as the voids were ignored by the loop above during mutual clipping.
            voids = diff(voids, surfaces_polygons);
            // Corners of infill regions, which would not be filled with an extrusion path with a radius of distance_between_surfaces/2
            Polygons collapsed = diff(
                surfaces_polygons,
                offset2(surfaces_polygons, (float)-distance_between_surfaces/2, (float)+distance_between_surfaces/2),
                true);
            //FIXME why the voids are added to collapsed here? First it is expensive, second the result may lead to some unwanted regions being
            // added if two offsetted void regions merge.
            // polygons_append(voids, collapsed);
            ExPolygons extensions = intersection_ex(offset(collapsed, (float)distance_between_surfaces), voids, true);
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
                if (surface_fill.surface.surface_type == (stPosInternal | stDensVoid) && std::abs(layer.height - surface_fill.params.flow.height) < EPSILON) {
                    internal_solid_fill = &surface_fill;
                    break;
                }
            if (internal_solid_fill == nullptr) {
                // Produce another solid fill.
                params.extruder = layerm.region()->extruder(frSolidInfill);
                params.pattern  = layerm.region()->config().solid_fill_pattern.value;
                params.density  = 100.f;
                params.role     = erInternalInfill;
                params.angle    = float(Geometry::deg2rad(layerm.region()->config().fill_angle.value));
                // calculate the actual flow we'll be using for this infill
                params.flow = layerm.region()->flow(
                    frSolidInfill,
                    layer.height,         // extrusion height
                    false,                 // bridge flow?
                    layer.id() == 0,    // first layer?
                    -1,                 // auto width
                    *layer.object()
                );
                params.spacing = params.flow.spacing();            
                surface_fills.emplace_back(params);
                surface_fills.back().surface.surface_type = (stPosInternal | stDensSolid);
                surface_fills.back().surface.thickness = layer.height;
                surface_fills.back().expolygons = std::move(extensions);
            } else {
                append(extensions, std::move(internal_solid_fill->expolygons));
                internal_solid_fill->expolygons = union_ex(extensions);
            }
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

// friend to Layer
void Layer::make_fills(FillAdaptive::Octree* adaptive_fill_octree, FillAdaptive::Octree* support_fill_octree)
{
    for (LayerRegion *layerm : m_regions)
        layerm->fills.clear();


#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
//    this->export_region_fill_surfaces_to_svg_debug("10_fill-initial");
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

    std::vector<SurfaceFill>  surface_fills = group_fills(*this);
    const Slic3r::BoundingBox bbox = this->object()->bounding_box();

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
    {
        static int iRun = 0;
        export_group_fills_to_svg(debug_out_path("Layer-fill_surfaces-10_fill-final-%d.svg", iRun ++).c_str(), surface_fills);
    }
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

    for (SurfaceFill &surface_fill : surface_fills) {
        // Create the filler object.
        std::unique_ptr<Fill> f = std::unique_ptr<Fill>(Fill::new_from_type(surface_fill.params.pattern));
        f->set_bounding_box(bbox);
        f->layer_id = this->id();
        f->z        = this->print_z;
        f->angle    = surface_fill.params.angle;
        f->adapt_fill_octree = (surface_fill.params.pattern == ipSupportCubic) ? support_fill_octree : adaptive_fill_octree;

        // calculate flow spacing for infill pattern generation
        bool using_internal_flow = ! surface_fill.surface.has_fill_solid() && ! surface_fill.params.flow.bridge;
        f->init_spacing(surface_fill.params.spacing, surface_fill.params);
        double link_max_length = 0.;
        if (! surface_fill.params.flow.bridge) {
#if 0
            link_max_length = layerm.region()->config().get_abs_value(surface.is_external() ? "external_fill_link_max_length" : "fill_link_max_length", flow.spacing());
//            printf("flow spacing: %f,  is_external: %d, link_max_length: %lf\n", flow.spacing(), int(surface.is_external()), link_max_length);
#else
            if (surface_fill.params.density > .8) // 80%
                link_max_length = 3. * f->get_spacing();
#endif
        }

        // Maximum length of the perimeter segment linking two infill lines.
        f->link_max_length = (coord_t)scale_(link_max_length);
        // Used by the concentric infill pattern to clip the loops to create extrusion paths.
        f->loop_clipping = coord_t(scale_(surface_fill.params.flow.nozzle_diameter) * LOOP_CLIPPING_LENGTH_OVER_NOZZLE_DIAMETER);

        //give the overlap size to let the infill do his overlap
        //add overlap if at least one perimeter
        const LayerRegion* layerm = this->m_regions[surface_fill.region_id];
        const float perimeter_spacing = layerm->flow(frPerimeter).spacing();

        // apply half spacing using this flow's own spacing and generate infill
        //FillParams params;
        //params.density         = float(0.01 * surface_fill.params.density);
        //params.dont_adjust     = surface_fill.params.dont_adjust; // false
        //params.anchor_length = surface_fill.params.anchor_length;
        //params.anchor_length_max = surface_fill.params.anchor_length_max;

        if (using_internal_flow) {
            // if we used the internal flow we're not doing a solid infill
            // so we can safely ignore the slight variation that might have
            // been applied to $f->flow_spacing
        } else {
            surface_fill.params.flow = Flow::new_from_spacing((float)f->get_spacing(), surface_fill.params.flow.nozzle_diameter, (float)surface_fill.params.flow.height, surface_fill.params.flow.bridge);
        }

        //apply bridge_overlap if needed
        if (surface_fill.params.flow.bridge && surface_fill.params.density > 0.99 && layerm->region()->config().bridge_overlap.get_abs_value(1) != 1) {
            surface_fill.params.density *= float(layerm->region()->config().bridge_overlap.get_abs_value(1));
        }

        for (ExPolygon &expoly : surface_fill.expolygons) {
            //set overlap polygons
            if (layerm->region()->config().perimeters > 0) {
                f->overlap = layerm->region()->config().get_abs_value("infill_overlap", (perimeter_spacing + (f->get_spacing())) / 2);
                if (f->overlap != 0) {
                    f->no_overlap_expolygons = intersection_ex(layerm->fill_no_overlap_expolygons, ExPolygons() = { expoly });
                } else {
                    f->no_overlap_expolygons.push_back(expoly);
                }
            } else {
                f->overlap = 0;
                f->no_overlap_expolygons.push_back(expoly);
            }

            //init the surface with the current polygon
            surface_fill.surface.expolygon = std::move(expoly);
            //make fill
            f->fill_surface_extrusion(&surface_fill.surface, surface_fill.params, m_regions[surface_fill.region_id]->fills.entities);
        }
    }

    // add thin fill regions
    // Unpacks the collection, creates multiple collections per path.
    // The path type could be ExtrusionPath, ExtrusionLoop or ExtrusionEntityCollection.
    // Why the paths are unpacked?
    for (LayerRegion *layerm : m_regions)
        for (const ExtrusionEntity *thin_fill : layerm->thin_fills.entities) {
            ExtrusionEntityCollection &collection = *(new ExtrusionEntityCollection());
            layerm->fills.entities.push_back(&collection);
            collection.entities.push_back(thin_fill->clone());
        }

#ifndef NDEBUG
    for (LayerRegion *layerm : m_regions)
        for (size_t i = 0; i < layerm->fills.entities.size(); ++ i)
            assert(dynamic_cast<ExtrusionEntityCollection*>(layerm->fills.entities[i]) != nullptr);
#endif
}

// Create ironing extrusions over top surfaces.
void Layer::make_ironing()
{
    // LayerRegion::slices contains surfaces marked with SurfaceType.
    // Here we want to collect top surfaces extruded with the same extruder.
    // A surface will be ironed with the same extruder to not contaminate the print with another material leaking from the nozzle.

    // First classify regions based on the extruder used.
    struct IroningParams {
        int         extruder     = -1;
        bool         just_infill = false;
        // Spacing of the ironing lines, also to calculate the extrusion flow from.
        double         line_spacing;
        // Height of the extrusion, to calculate the extrusion flow from.
        double         height;
        double         speed;
        double         angle;
        IroningType    type;

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
                this->angle == rhs.angle &&
                this->type == rhs.type;
        }

        LayerRegion *layerm        = nullptr;

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
    bool   extruder_dont_care   = this->object()->config().wipe_into_objects;
    double default_layer_height = this->object()->config().layer_height;

    for (LayerRegion *layerm : m_regions)
        if (! layerm->slices().empty()) {
            IroningParams ironing_params;
            const PrintRegionConfig &config = layerm->region()->config();
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
                ironing_params.type              = config.ironing_type;
                ironing_params.just_infill     = false;
                ironing_params.line_spacing = config.ironing_spacing;
                ironing_params.height         = default_layer_height * 0.01 * config.ironing_flowrate;
                ironing_params.speed         = config.ironing_speed;
                ironing_params.angle         = config.fill_angle * M_PI / 180.;
                ironing_params.layerm         = layerm;
                by_extruder.emplace_back(ironing_params);
            }
        }
    std::sort(by_extruder.begin(), by_extruder.end());

    FillRectilinear 	fill;
    FillParams             fill_params;
    fill.set_bounding_box(this->object()->bounding_box());
    fill.layer_id           = this->id();
    fill.z                  = this->print_z;
    fill.overlap            = 0;
    fill_params.density     = 1.;
    fill_params.connection  = InfillConnection::icConnected;
    fill_params.monotonic  = true;

    for (size_t i = 0; i < by_extruder.size(); ++ i) {
        // Find span of regions equivalent to the ironing operation.
        IroningParams &ironing_params = by_extruder[i];
        size_t j = i;
        for (++ j; j < by_extruder.size() && ironing_params == by_extruder[j]; ++ j) ;

        // Create the ironing extrusions for regions <i, j)
        ExPolygons ironing_areas;
        double nozzle_dmr = this->object()->print()->config().nozzle_diameter.values[ironing_params.extruder - 1];
        if (ironing_params.just_infill) {
            // Just infill.
        } else {
            // Infill and perimeter.
            if (ironing_params.type == IroningType::AllSolid) {
                // Merge top surfaces with the same ironing parameters.
                Polygons polys;
                for (size_t k = i; k < j; ++k)
                    for (const Surface& surface : by_extruder[k].layerm->slices().surfaces)
                        if (surface.has_fill_solid())
                            polygons_append(polys, surface.expolygon);
                // Trim the top surfaces with half the nozzle diameter.
                ironing_areas = intersection_ex(polys, offset(this->lslices, -float(scale_(0.5 * nozzle_dmr))));
            } else {
                // Merge top surfaces with the same ironing parameters.
                Polygons polys;
                for (size_t k = i; k < j; ++k)
                    for (const Surface& surface : by_extruder[k].layerm->slices().surfaces)
                        if (surface.has_pos_top())
                            polygons_append(polys, surface.expolygon);
                // Trim the top surfaces with half the nozzle diameter.
                ironing_areas = intersection_ex(polys, offset(this->lslices, -float(scale_(0.5 * nozzle_dmr))));
            }
        }

        // Create the filler object.
        fill.init_spacing(ironing_params.line_spacing, fill_params);
        fill.angle = float(ironing_params.angle + 0.25 * M_PI);
        fill.link_max_length = (coord_t)scale_(3. * fill.get_spacing());
        double height = ironing_params.height * fill.get_spacing() / nozzle_dmr;
        Flow flow = Flow::new_from_spacing(float(nozzle_dmr), 0., float(height), false);
        double flow_mm3_per_mm = flow.mm3_per_mm();
        Surface surface_fill((stPosTop | stDensSolid), ExPolygon());
        for (ExPolygon &expoly : ironing_areas) {
            surface_fill.expolygon = std::move(expoly);
            Polylines polylines;
            try {
                polylines = fill.fill_surface(&surface_fill, fill_params);
            } catch (InfillFailedException &) {
            }
            if (! polylines.empty()) {
                // Save into layer.
                ExtrusionEntityCollection *eec = new ExtrusionEntityCollection();
                ironing_params.layerm->ironings.entities.push_back(eec);
                // Don't sort the ironing infill lines as they are monotonicly ordered.
                eec->no_sort = true;
                extrusion_entities_append_paths(
                    eec->entities, std::move(polylines),
                    erIroning,
                    flow_mm3_per_mm, float(flow.width), float(height));
            }
        }
    }
}

} // namespace Slic3r
