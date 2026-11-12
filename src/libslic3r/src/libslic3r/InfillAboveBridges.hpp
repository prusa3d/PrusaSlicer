#ifndef slic3r_InfillAboveBridges_hpp_
#define slic3r_InfillAboveBridges_hpp_

#include <iostream>
#include <vector>
#include "libslic3r/SurfaceCollection.hpp"

namespace Slic3r::PrepareInfill {
    using SurfaceCollectionRef = std::reference_wrapper<SurfaceCollection>;
    using SurfaceRefsByRegion = std::vector<SurfaceCollectionRef>;
    using SurfaceRefs = std::vector<SurfaceRefsByRegion>;

    void separate_infill_above_bridges(const SurfaceRefs &surfaces, const double expand_offset);
}

#endif // slic3r_InfillAboveBridges_hpp_
