#include <tcbspan/span.hpp>
#include "libslic3r/InfillAboveBridges.hpp"
#include "libslic3r/ClipperUtils.hpp"

namespace Slic3r::PrepareInfill {
    void mark_as_infill_above_bridge(const ExPolygons &marker, const SurfaceRefsByRegion &layer) {
        for (const SurfaceCollectionRef &region : layer) {
            const ExPolygons intersection{intersection_ex(region.get().filter_by_type(stInternalSolid), marker, ApplySafetyOffset::No)};
            if (intersection.empty()) {
                continue;
            }
            const ExPolygons clipped{diff_ex(region.get().filter_by_type(stInternalSolid), marker, ApplySafetyOffset::Yes)};
            region.get().remove_type(stInternalSolid);
            region.get().append(clipped, stInternalSolid);
            region.get().append(intersection, stSolidOverBridge);
        }
    }

    void separate_infill_above_bridges(const SurfaceRefs &surfaces, const double expand_offset) {
        if (surfaces.empty()) {
            return;
        }

        const SurfaceRefsByRegion *previous_layer{&surfaces.front()};
        for (const SurfaceRefsByRegion &layer : tcb::span{surfaces}.subspan(1)) {
            ExPolygons bridges;
            for (const SurfaceCollectionRef &region : *previous_layer) {
                for (const Surface *bridge : region.get().filter_by_type(stBottomBridge)) {
                    bridges.push_back(bridge->expolygon);
                }
            }
            if (expand_offset > 0) {
                bridges = offset_ex(bridges, scale_(expand_offset));
            }
            mark_as_infill_above_bridge(bridges, layer);
            previous_layer = &layer;
        }
    }
}
