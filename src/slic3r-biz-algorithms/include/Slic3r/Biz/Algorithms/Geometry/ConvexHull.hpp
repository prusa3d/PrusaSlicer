///Vojtěch Bubník @bubnikv, Lukáš Hejl @hejllukas, Filip Sykala @Jony01

#pragma once

#include <vector>
#include <utility>

#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/Point.hpp"
#include "Slic3r/Domain/Polygon.hpp"
#include "Slic3r/Domain/Polyline.hpp"

namespace Slic3r::Biz::Algorithms::Geometry {

Domain::Polygon convex_hull(Domain::Points points);
Domain::Polygon convex_hull(const Domain::Polygons& polygons);
Domain::Polygon convex_hull(const Domain::ExPolygons& expolygons);
Domain::Polygon convex_hull(const Domain::Polylines& polylines);
Domain::Polygon convex_hull(const Domain::Polygon& poly);
Domain::Polygon convex_hull(const Domain::ExPolygon& poly);

// Be cautious of this one. It ignores z coordinate.
Domain::Vec3fs convex_hull_2d_xy(Domain::Vec3fs points);


// Returns true if the intersection of the two convex polygons A and B
// is not an empty set.
bool convex_polygons_intersect(const Domain::Polygon& A, const Domain::Polygon& B);

// Decompose source convex hull points into top / bottom chains with monotonically increasing x,
// creating an implicit trapezoidal decomposition of the source convex polygon.
// The source convex polygon has to be CCW oriented. O(n) time complexity.
std::pair<std::vector<Domain::Vec2d>, std::vector<Domain::Vec2d>>
decompose_convex_polygon_top_bottom(const std::vector<Domain::Vec2d>& src);

// Convex polygon check using a top / bottom chain decomposition with O(log n) time complexity.
bool inside_convex_polygon(
    const std::pair<std::vector<Domain::Vec2d>, std::vector<Domain::Vec2d>>& top_bottom_decomposition,
    const Domain::Vec2d& pt
);

} // namespace Slic3r::Biz::Algorithms::Geometry
