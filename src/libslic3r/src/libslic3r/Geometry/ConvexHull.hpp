#ifndef slic3r_Geometry_ConvexHull_hpp_
#define slic3r_Geometry_ConvexHull_hpp_

#include "Slic3r/Biz/Algorithms/Geometry/ConvexHull.hpp"

namespace Slic3r::Geometry {

using Slic3r::Biz::Algorithms::Geometry::convex_hull;
using Slic3r::Biz::Algorithms::Geometry::convex_polygons_intersect;
using Slic3r::Biz::Algorithms::Geometry::decompose_convex_polygon_top_bottom;
using Slic3r::Biz::Algorithms::Geometry::inside_convex_polygon;

} // namespace Slic3r::Geometry

#endif
