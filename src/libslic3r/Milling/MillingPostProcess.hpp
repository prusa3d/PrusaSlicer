#ifndef slic3r_MillingPostProcess_hpp_
#define slic3r_MillingPostProcess_hpp_

#include "../libslic3r.h"
#include "../ExPolygonCollection.hpp"
#include "../Polygon.hpp"
#include "../Layer.hpp"
#include "../PrintConfig.hpp"
#include "../SurfaceCollection.hpp"

namespace Slic3r {


class MillingPostProcess {
public:
    // Inputs:
    const SurfaceCollection     *slices;
    const ExPolygons            *lower_slices;
    const PrintRegionConfig     *config;
    const PrintObjectConfig     *object_config;
    const PrintConfig           *print_config;
    
    MillingPostProcess(
        // Input:
        const SurfaceCollection*    slices,
        const ExPolygons*           lower_slices,
        const PrintRegionConfig*    config,
        const PrintObjectConfig*    object_config,
        const PrintConfig*          print_config)
        : slices(slices), lower_slices(lower_slices),
            config(config), object_config(object_config), print_config(print_config)
        {};

    /// get mill path
    ExtrusionEntityCollection  process(const Layer *layer);
    /// get the areas that shouldn't be grown because the mill can't go here.
    ExPolygons  get_unmillable_areas(const Layer* layer);
    bool        can_be_milled(const Layer* layer);

private:
    Polygons    _lower_slices_p;

    void getExtrusionLoop(const Layer* layer, Polygon& poly, Polylines& entrypoints, ExtrusionEntityCollection&out_coll);
};

}

#endif
