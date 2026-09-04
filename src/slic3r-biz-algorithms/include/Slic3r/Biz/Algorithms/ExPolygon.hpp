#pragma once

#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/Line.hpp"
#include "Slic3r/Domain/Polygon.hpp"
#include "Slic3r/Domain/Polyline.hpp"
#include "Slic3r/Domain/BoundingBox.hpp"

namespace Slic3r::Biz::Algorithms::ExPolygon {

bool is_valid(const Domain::ExPolygon& expolygon);

size_t count_points(const Domain::ExPolygon& expolygon);
size_t count_points(const Domain::ExPolygons& expolygons);

size_t count_polygons(const Domain::ExPolygons& expolygons);

void rotate(Domain::ExPolygons& expolygons, double angle);

void translate(Domain::ExPolygons& expolygons, const Domain::Point& vector);

bool contains(const Domain::ExPolygon& expolygon, const Domain::Point& point, bool border_result = true);
bool contains(const Domain::ExPolygons& expolygons, const Domain::Point& point, bool border_result = true);

bool contains(const Domain::ExPolygon& expolygon, const Domain::Line& line);
bool contains(const Domain::ExPolygon& expolygon, const Domain::Polyline& polyline);
bool contains(const Domain::ExPolygon& expolygon, const Domain::Polylines& polylines);

/**
 * Removes consecutive duplicate points from all ExPolygons.
 *
 * @param expolygons Reference to ExPolygons to process and modify in-place.
 * @return true If at least one duplicate point was removed from any ExPolygon.
 * @return false If no duplicates were found.
 */
bool remove_consecutive_duplicate_points(Domain::ExPolygons& expolygons);

/**
 * Removes ExPolygons with area smaller than min_area and also removes holes with area smaller than min_area.
 *
 * @param expolygons Reference to ExPolygons to process and modify in-place.
 * @param min_area Minimum absolute area threshold.
 * @return true If at least one ExPolygon or hole was removed.
 * @return false If no ExPolygon or hole were removed.
 */
bool remove_small_expolygons_and_holes(Domain::ExPolygons& expolygons, double min_area);

// Does this expolygon overlap another expolygon?
// Either the ExPolygons intersect, or one is fully inside the other,
// and it is not inside a hole of the other expolygon.
// The test may not be commutative if the two expolygons touch by a boundary only,
// see unit test SCENARIO("Clipper diff with polyline", "[Clipper]").
// Namely expolygons touching at a vertical boundary are considered overlapping, while expolygons touching
// at a horizontal boundary are NOT considered overlapping.
bool overlaps(const Domain::ExPolygon& expolygon, const Domain::ExPolygon &other_expolygon);

Domain::Lines to_lines(const Domain::ExPolygon& expolygon);
Domain::Lines to_lines(const Domain::ExPolygons& expolygons);

// Line is from point index(see to_points) to next point.
// Next point of last point in polygon is first polygon point.
Domain::Line2ds to_linesf(const Domain::ExPolygons& src, uint32_t count_lines = 0);

Domain::ExPolygons simplify(const Domain::ExPolygon& expolygon, double tolerance);
Domain::ExPolygons simplify(const Domain::ExPolygons& expolygons, double tolerance);
Domain::Polygons simplify_to_polygons(const Domain::ExPolygon& expolygon, double tolerance);

double area(const Domain::ExPolygon& expolygon);
double area(const Domain::ExPolygons& expolygons);

Domain::Polygons to_polygons(const Domain::ExPolygon& expolygon);
Domain::Polygons to_polygons(const Domain::ExPolygons& expolygons);

Domain::Polygons to_polygons(Domain::ExPolygon&& expolygon);
Domain::Polygons to_polygons(Domain::ExPolygons&& expolygons);

Domain::BoundingBox2crd get_extents(const Domain::ExPolygon& expolygon);
Domain::BoundingBox2crd get_extents(const Domain::ExPolygons& expolygons);

Domain::Points to_points(const Domain::ExPolygon& expolygon);
Domain::Points to_points(const Domain::ExPolygons& expolygons);

Domain::Polylines to_polylines(const Domain::ExPolygon& expolygon);
Domain::Polylines to_polylines(const Domain::ExPolygons& expolygons);

Domain::Polylines to_polylines(Domain::ExPolygon&& expolygon);
Domain::Polylines to_polylines(Domain::ExPolygons&& expolygons);

} // namespace Slic3r::Biz::Algorithms::ExPolygon
