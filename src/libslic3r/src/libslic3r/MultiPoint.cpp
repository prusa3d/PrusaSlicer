#include "MultiPoint.hpp"

#include <cmath>
#include <limits>
#include <queue>
#include <cstdlib>

#include "libslic3r/Point.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r {

BoundingBox get_extents_rotated(const Points &points, double angle)
{ 
    BoundingBox bbox;
    if (! points.empty()) {
        double s = sin(angle);
        double c = cos(angle);
        Points::const_iterator it = points.begin();
        double cur_x = (double)(*it)(0);
        double cur_y = (double)(*it)(1);
        bbox.min(0) = bbox.max(0) = (coord_t)round(c * cur_x - s * cur_y);
        bbox.min(1) = bbox.max(1) = (coord_t)round(c * cur_y + s * cur_x);
        for (++it; it != points.end(); ++it) {
            double cur_x = (double)(*it)(0);
            double cur_y = (double)(*it)(1);
            coord_t x = (coord_t)round(c * cur_x - s * cur_y);
            coord_t y = (coord_t)round(c * cur_y + s * cur_x);
            bbox.min(0) = std::min(x, bbox.min(0));
            bbox.min(1) = std::min(y, bbox.min(1));
            bbox.max(0) = std::max(x, bbox.max(0));
            bbox.max(1) = std::max(y, bbox.max(1));
        }
        bbox.defined = true;
    }
    return bbox;
}

}
