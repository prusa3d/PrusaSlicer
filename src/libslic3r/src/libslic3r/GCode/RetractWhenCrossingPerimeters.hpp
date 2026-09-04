#ifndef slic3r_RetractWhenCrossingPerimeters_hpp_
#define slic3r_RetractWhenCrossingPerimeters_hpp_

#include <vector>

#include "Slic3r/Biz/Algorithms/AABBTreeIndirect.hpp"
#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/Polyline.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r {

// Forward declarations.
class Layer;

class RetractWhenCrossingPerimeters
{
public:
    bool    travel_inside_internal_regions(const Layer &layer, const Domain::Polyline &travel);

private:
    // Last object layer visited, for which a cache of internal islands was created.
    const Layer                        *m_layer;
    // Internal islands only, referencing data owned by m_layer->regions()->surfaces().
    std::vector<const Domain::ExPolygon*>       m_internal_islands;
    // Search structure over internal islands.
    using AABBTree = Biz::Algorithms::AABBTreeIndirect::Tree<2, coord_t>;
    AABBTree                            m_aabbtree_internal_islands;
};

} // namespace Slic3r

#endif // slic3r_RetractWhenCrossingPerimeters_hpp_
