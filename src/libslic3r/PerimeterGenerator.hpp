///|/ Copyright (c) Prusa Research 2016 - 2023 Vojtěch Bubník @bubnikv, Lukáš Hejl @hejllukas
///|/ Copyright (c) Slic3r 2015 - 2016 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2015 Maksim Derbasov @ntfshard
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_PerimeterGenerator_hpp_
#define slic3r_PerimeterGenerator_hpp_

#include <vector>

#include "libslic3r.h"
#include "ExtrusionEntityCollection.hpp"
#include "Flow.hpp"
#include "Polygon.hpp"
#include "PrintConfig.hpp"
#include "SurfaceCollection.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionRole.hpp"
#include "libslic3r/Point.hpp"

namespace Slic3r {
class ExtrusionEntityCollection;
class LayerRegion;
class Surface;
class PrintRegion;
struct ThickPolyline;

struct PerimeterRegion
{
    const PrintRegion *region;
    ExPolygons         expolygons;
    BoundingBox        bbox;

    explicit PerimeterRegion(const LayerRegion &layer_region);

    // If there is any incompatibility, we don't need to create separate LayerRegions.
    // Because it is enough to split perimeters by PerimeterRegions.
    static bool has_compatible_perimeter_regions(const PrintRegionConfig &config, const PrintRegionConfig &other_config);

    static void merge_compatible_perimeter_regions(std::vector<PerimeterRegion> &perimeter_regions);
};

using PerimeterRegions = std::vector<PerimeterRegion>;

} // namespace Slic3r

namespace Slic3r::PerimeterGenerator {

struct Parameters {    
    Parameters(
        double                      layer_height,
        int                         layer_id,
        Flow                        perimeter_flow,
        Flow                        ext_perimeter_flow,
        Flow                        overhang_flow,
        Flow                        solid_infill_flow,
        const PrintRegionConfig    &config,
        const PrintObjectConfig    &object_config,
        const PrintConfig          &print_config,
        const PerimeterRegions     &perimeter_regions,
        const bool                  spiral_vase) :   
            layer_height(layer_height),
            layer_id(layer_id),
            perimeter_flow(perimeter_flow), 
            ext_perimeter_flow(ext_perimeter_flow),
            overhang_flow(overhang_flow), 
            solid_infill_flow(solid_infill_flow),
            config(config), 
            object_config(object_config), 
            print_config(print_config),
            perimeter_regions(perimeter_regions),
            spiral_vase(spiral_vase),
            scaled_resolution(scaled<double>(print_config.gcode_resolution.value)),
            mm3_per_mm(perimeter_flow.mm3_per_mm()),
            ext_mm3_per_mm(ext_perimeter_flow.mm3_per_mm()), 
            mm3_per_mm_overhang(overhang_flow.mm3_per_mm())
        {
        }

    // Input parameters
    double                       layer_height;
    int                          layer_id;
    Flow                         perimeter_flow;
    Flow                         ext_perimeter_flow;
    Flow                         overhang_flow;
    Flow                         solid_infill_flow;
    const PrintRegionConfig     &config;
    const PrintObjectConfig     &object_config;
    const PrintConfig           &print_config;
    const PerimeterRegions      &perimeter_regions;

    // Derived parameters
    bool                         spiral_vase;
    double                       scaled_resolution;
    double                       ext_mm3_per_mm;
    double                       mm3_per_mm;
    double                       mm3_per_mm_overhang;

private:
    Parameters() = delete;
};

void process_classic(
    // Inputs:
    const Parameters           &params,
    const Surface              &surface,
    const ExPolygons           *lower_slices,
    const ExPolygons           *upper_slices,
    // Cache:
    Polygons                   &lower_slices_polygons_cache,
    // Output:
    // Loops with the external thin walls
    ExtrusionEntityCollection  &out_loops,
    // Gaps without the thin walls
    ExtrusionEntityCollection  &out_gap_fill,
    // Infills without the gap fills
    ExPolygons                 &out_fill_expolygons);

void process_arachne(
    // Inputs:
    const Parameters           &params,
    const Surface              &surface,
    const ExPolygons           *lower_slices,
    const ExPolygons           *upper_slices,
    // Cache:
    Polygons                   &lower_slices_polygons_cache,
    // Output:
    // Loops with the external thin walls
    ExtrusionEntityCollection  &out_loops,
    // Gaps without the thin walls
    ExtrusionEntityCollection  &out_gap_fill,
    // Infills without the gap fills
    ExPolygons                 &out_fill_expolygons);

ExtrusionMultiPath thick_polyline_to_multi_path(const ThickPolyline &thick_polyline, ExtrusionRole role, const Flow &flow, float tolerance, float merge_tolerance);

} // namespace Slic3r::PerimeterGenerator

#endif
