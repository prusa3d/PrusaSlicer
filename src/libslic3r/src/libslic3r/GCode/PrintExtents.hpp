// Measure extents of the planned extrusions.
// To be used for collision reporting.

#ifndef slic3r_PrintExtents_hpp_
#define slic3r_PrintExtents_hpp_

#include "Slic3r/Domain/BoundingBox.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r {

class Print;
class PrintObject;

// Returns a bounding box of a projection of the brim and skirt.
BoundingBoxf get_print_extrusions_extents(const Print &print);

// Returns a bounding box of a projection of the object extrusions at z <= max_print_z.
BoundingBoxf get_print_object_extrusions_extents(const PrintObject &print_object, const double max_print_z);

// Returns a bounding box of a projection of the wipe tower for the layers <= max_print_z.
// The projection does not contain the priming regions.
BoundingBoxf get_wipe_tower_extrusions_extents(const Print &print, const double max_print_z);

// Returns a bounding box of the wipe tower priming extrusions.
BoundingBoxf get_wipe_tower_priming_extrusions_extents(const Print &print);

};

#endif /* slic3r_PrintExtents_hpp_ */
