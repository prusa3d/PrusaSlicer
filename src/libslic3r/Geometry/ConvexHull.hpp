///|/ Copyright (c) Prusa Research 2021 - 2023 Tomáš Mészáros @tamasmeszaros, Pavel Mikuš @Godrak, Vojtěch Bubník @bubnikv, Lukáš Hejl @hejllukas, Filip Sykala @Jony01
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_Geometry_ConvexHull_hpp_
#define slic3r_Geometry_ConvexHull_hpp_

#include <vector>
#include <utility>

#include "../ExPolygon.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Polyline.hpp"

namespace Slic3r {

using ExPolygons = std::vector<ExPolygon>;

namespace Geometry {

Pointf3s convex_hull(Pointf3s points);
Polygon convex_hull(Points points);
Polygon convex_hull(const Polygons &polygons);
Polygon convex_hull(const ExPolygons &expolygons);
Polygon convex_hull(const Polylines &polylines);
inline Polygon convex_hull(const Polygon &poly) { return convex_hull(poly.points); }
inline Polygon convex_hull(const ExPolygon &poly) { return convex_hull(poly.contour.points); }

// Returns true if the intersection of the two convex polygons A and B
// is not an empty set.
bool convex_polygons_intersect(const Polygon &A, const Polygon &B);

// Decompose source convex hull points into top / bottom chains with monotonically increasing x,
// creating an implicit trapezoidal decomposition of the source convex polygon.
// The source convex polygon has to be CCW oriented. O(n) time complexity.
std::pair<std::vector<Vec2d>, std::vector<Vec2d>> decompose_convex_polygon_top_bottom(const std::vector<Vec2d> &src);

// Convex polygon check using a top / bottom chain decomposition with O(log n) time complexity.
bool inside_convex_polygon(const std::pair<std::vector<Vec2d>, std::vector<Vec2d>> &top_bottom_decomposition, const Vec2d &pt);

} } // namespace Slicer::Geometry

#endif
