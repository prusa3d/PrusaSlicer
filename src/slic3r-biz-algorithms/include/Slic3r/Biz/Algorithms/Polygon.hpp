#pragma once

#include "Slic3r/Domain/BoundingBox.hpp"
#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/Point.hpp"
#include "Slic3r/Domain/Line.hpp"
#include "Slic3r/Domain/Polygon.hpp"
#include "Slic3r/Domain/Polyline.hpp"

#include <optional>

namespace Slic3r::Biz::Algorithms::Polygon {

size_t count_points(const Domain::Polygons& polygons);

void reverse(Domain::Polygons& polygons);

void rotate(Domain::Polygons& polygons, double angle);

void append(Domain::Polygons& dst, const Domain::ExPolygon& expolygon);
void append(Domain::Polygons& dst, const Domain::ExPolygons& expolygons);

void append(Domain::Polygons& dst, Domain::ExPolygon&& expolygon);
void append(Domain::Polygons& dst, Domain::ExPolygons&& expolygons);

/**
 * Checks if the Polygon contains consecutive duplicate points.
 *
 * @param polygon Polygon to search within.
 * @return true If at least one pair of consecutive duplicate points is found.
 * @return false Otherwise.
 */
bool has_consecutive_duplicate_points(const Domain::Polygon& polygon);

/**
 * Removes consecutive duplicate points from the Polygon.
 *
 * @param polygon Reference to Polygon to process and modify in-place.
 * @param check_first_and_last Indicate whether to check for a duplicate between the first and last point.
 * @return true If at least one duplicate point was removed.
 * @return false If no duplicates were found.
 */
bool remove_consecutive_duplicate_points(Domain::Polygon& polygon, bool check_first_and_last = true);

/**
 * Removes consecutive duplicate points from all Polygons.
 *
 * @param polygons Reference to Polygons to process and modify in-place.
 * @param check_first_and_last Indicate whether to check for a duplicate between the first and last point.
 * @return true If at least one duplicate point was removed from any Polygon.
 * @return false If no duplicates were found.
 */
bool remove_consecutive_duplicate_points(Domain::Polygons& polygons, bool check_first_and_last = true);

/**
 * Removes degenerate Polygons (those that are empty or have fewer than three points).
 *
 * @param polygon Reference to Polygon to process and modify in-place.
 * @return true If at least one degenerate Polygon was removed.
 * @return false If no degenerate Polygon were found.
 */
bool remove_degenerate(Domain::Polygons& polygons);

/**
 * Removes Polygons with area smaller than min_area.
 *
 * @param polygon Reference to Polygon to process and modify in-place.
 * @param min_area Minimum absolute area threshold.
 * @return true If at least one Polygon was removed.
 * @return false If no Polygon were removed.
 */
bool remove_small(Domain::Polygons& polygons, double min_area);

/**
 * Finds the index of the closest point in the Polygon to the given point.
 *
 * @param polygon Polygon to search within.
 * @param point The point with which to compare distances.
 * @return Index of the closest point in the list, or -1 if the list is empty.
 */
int closest_point_index(const Domain::Polygon& polygon, const Domain::Point& point);

Domain::Polygon scaled(const std::vector<Domain::Vec2d>& points);

Domain::Polygon translated(const Domain::Polygon& polygon, const Domain::Point& offset);

bool is_counter_clockwise(const Domain::Polygon& polygon);
bool is_clockwise(const Domain::Polygon& polygon);

// Polygon must be valid (at least three points), collinear points and duplicate points removed.
bool is_convex(const Domain::Polygon& polygon);

bool make_counter_clockwise(Domain::Polygon& polygon);
bool make_clockwise(Domain::Polygon& polygon);

Domain::Polyline split_at_vertex(const Domain::Polygon& polygon, const Domain::Point& point);

/**
 * Split a closed polygon into an open polyline, with the split point duplicated at both ends.
 */
Domain::Polyline split_at_index(const Domain::Polygon& polygon, size_t index);

Domain::Polyline split_at_first_point(const Domain::Polygon& polygon);

std::optional<Domain::Point> intersection(const Domain::Polygon& polygon, const Domain::Line& line);

/**
 * @note Works on CCW polygons only, CW contour will be reoriented to CCW by Clipper's simplify_polygons()!
 */
Domain::Polygons simplify(const Domain::Polygon& polygon, double tolerance);

/*
 * Returns true if inside. Returns border_result if on boundary.
 */
bool contains(const Domain::Polygon& polygon, const Domain::Point& point, bool border_result = true);

/*
 * Returns true if inside. Returns border_result if on boundary.
 */
bool contains(const Domain::Polygons& polygons, const Domain::Point& point, bool border_result = true);

double total_length(const Domain::Polygons& polygons);

double area(const Domain::Points& polygon_pts);
double area(const Domain::Polygon& polygon);
double area(const Domain::Polygons& polygons);

Domain::Points to_points(const Domain::Polygon& polygon);
Domain::Points to_points(const Domain::Polygons& polygons);

Domain::Lines to_lines(const Domain::Polygon& polygon);
Domain::Lines to_lines(const Domain::Polygons& polygons);

Domain::Polygons to_polygons(const Domain::VecOfPoints& paths);
Domain::Polygons to_polygons(Domain::VecOfPoints&& paths);

Domain::BoundingBox2crd get_extents(const Domain::Polygon& poly);

Domain::BoundingBox2crd get_extents(const Domain::Polygons& polygons);

Domain::ExPolygons to_expolygons(const Domain::Polygons& polygons);
Domain::ExPolygons to_expolygons(Domain::Polygons&& polygons);

/**
 * Create a closed Polyline from a Polygon.
 *
 * @param polygon Input Polygon to convert.
 * @return Closed Polyline representing the same shape.
 */
Domain::Polyline to_polyline(const Domain::Polygon& polygon);

/**
 * Create closed Polylines from Polygons.
 *
 * @param polygons Input Polygons to convert.
 * @return Closed Polylines representing the same shape.
 */
Domain::Polylines to_polylines(const Domain::Polygons& polygons);

/**
 * Create closed Polylines from Polygons.
 *
 * @param polygons Input Polygons to convert.
 * @return Closed Polylines representing the same shape.
 */
Domain::Polylines to_polylines(Domain::Polygons&& polygons);

} // namespace Slic3r::Biz::Algorithms::Polygon
