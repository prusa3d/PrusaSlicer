#pragma once

#include "Slic3r/Domain/BoundingBox.hpp"
#include "Slic3r/Domain/Line.hpp"
#include "Slic3r/Domain/Polygon.hpp"
#include "Slic3r/Domain/Polyline.hpp"

namespace Slic3r::Biz::Algorithms::Polyline {

void reverse(Domain::Polyline& polyline);

Domain::Polyline reversed(const Domain::Polyline& polyline);

/**
 * Finds the index of the closest point to the query point within a given epsilon.
 *
 * @param polyline Polyline to search within.
 * @param query_pt The point to find the closest match for.
 * @param scaled_epsilon The maximum allowed epsilon for a match.
 * @return Index of the closest point if found, otherwise -1.
 */
int find_point(const Domain::Polyline& polyline, const Domain::Point& query_pt, double scaled_epsilon);

/**
 * Checks if the Polyline contains consecutive duplicate points.
 *
 * @param polyline Polyline to search within.
 * @return true If at least one pair of consecutive duplicate points is found.
 * @return false Otherwise.
 */
bool has_consecutive_duplicate_points(const Domain::Polyline& polyline);

/**
 * Removes consecutive duplicate points from the Polyline.
 *
 * @param polyline Reference to Polyline to process and modify in-place.
 * @return true If at least one duplicate point was removed.
 * @return false If no duplicates were found.
 */
bool remove_consecutive_duplicate_points(Domain::Polyline& polyline);

/**
 * Removes consecutive duplicate points from all Polylines.
 *
 * @param polylines Reference to Polylines to process and modify in-place.
 * @return true If at least one duplicate point was removed from any Polyline.
 * @return false If no duplicates were found.
 */
bool remove_consecutive_duplicate_points(Domain::Polylines& polylines);

/**
 * Removes degenerate Polylines (those that are empty or have fewer than two points).
 *
 * @return true If at least one degenerate Polyline was removed.
 * @return false If no degenerate Polyline were found.
 */
bool remove_degenerate(Domain::Polylines& polylines);

/**
 * @brief Trims the given distance from the end of the polyline.
 *
 * @param polyline The polyline to clip.
 * @param distance The distance to remove from the end.
 */
void clip_end(Domain::Polyline& polyline, double distance);

/**
 * @brief Trims the given distance from the start of the polyline.
 *
 * @param polyline The polyline to clip.
 * @param distance The distance to remove from the start.
 */
void clip_start(Domain::Polyline& polyline, double distance);

/**
 * @brief Extends the last segment of the polyline by given distance.
 *
 * @param polyline The polyline to extend.
 * @param distance The distance to extend the last segment.
 */
void extend_end(Domain::Polyline& polyline, double distance);

/**
 * @brief Extends the first segment of the polyline by given distance.
 *
 * @param polyline The polyline to extend.
 * @param distance The distance to extend the first segment.
 */
void extend_start(Domain::Polyline& polyline, double distance);

Domain::Polyline scaled(const std::vector<Domain::Vec2d> &points);

Domain::BoundingBox2crd get_bounding_box(const Domain::Polyline& polyline);
Domain::BoundingBox2crd get_bounding_box(const Domain::Polylines& polylines);

Domain::Lines to_lines(const Domain::Polyline& polyline);
Domain::Lines to_lines(const Domain::Polylines& polylines);

double total_length(const Domain::Polylines& polylines);
size_t total_lines_count(const Domain::Polylines& polylines);

bool is_straight(const Domain::Polyline& polyline);

void simplify(Domain::Polyline& polyline, double tolerance);
Domain::Polyline simplified(const Domain::Polyline& polyline, double tolerance);

std::pair<Domain::Polyline, Domain::Polyline> split_at_point(const Domain::Polyline& polyline, const Domain::Point& split_point);

double length(const Domain::Points& polyline_pts);
double length(Domain::Points::const_iterator polyline_pts_begin, Domain::Points::const_iterator polyline_pts_end);

/**
 * Close polyline to polygon (connect first and last point in polyline).
 *
 * @note It doesn't handle closed polylines differently. So, for closed polylines, the last point is duplicate.
 */
Domain::Polygons to_polygons(const Domain::Polylines& polylines);

} // namespace Slic3r::Biz::Algorithms::Polyline
