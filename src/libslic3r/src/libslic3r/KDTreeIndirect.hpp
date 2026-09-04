// Proxy header - to remove.
#ifndef slic3r_KDTreeIndirect_hpp_
#define slic3r_KDTreeIndirect_hpp_

#include <algorithm>
#include <limits>
#include <vector>

#include "libslic3r/Utils.hpp" // for next_highest_power_of_2()
#include "Slic3r/Biz/Algorithms/KDTreeIndirect.hpp"

namespace Slic3r {

using Slic3r::Biz::Algorithms::KDTreeIndirect::KDTreeIndirect;

using Slic3r::Biz::Algorithms::KDTreeIndirect::find_closest_points;
using Slic3r::Biz::Algorithms::KDTreeIndirect::find_closest_point;
using Slic3r::Biz::Algorithms::KDTreeIndirect::find_nearby_points;
} // namespace Slic3r

#endif /* slic3r_KDTreeIndirect_hpp_ */
