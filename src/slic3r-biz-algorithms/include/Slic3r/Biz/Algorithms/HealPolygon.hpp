#pragma once

#include "Slic3r/Domain/EmbossShape.hpp" // HealedExPolygons

namespace Slic3r::Biz::Algorithms::HealPolygon {

/// <summary>
/// Fix duplicit points and self intersections in polygons.
/// Also try to reduce amount of points and remove useless polygon parts
/// </summary>
/// <param name="is_non_zero">Fill type ClipperLib::pftNonZero for overlapping otherwise </param>
/// <param name="max_iteration">Look at heal_expolygon()::max_iteration</param>
/// <returns>Healed shapes with flag is fully healed</returns>
Domain::HealedExPolygons heal_polygons(
    const Domain::Polygons& shape,
    bool is_non_zero       = true,
    unsigned max_iteration = 10
);

/// <summary>
/// NOTE: call Slic3r::union_ex before this call
///
/// Heal (read: Fix) issues in expolygons:
/// - self intersections
/// - duplicit points
/// - points close to line segments
/// </summary>
/// <param name="shape">In/Out shape to heal</param>
/// <param name="max_iteration">Heal could create another issue,
/// After healing it is checked again until shape is good or maximal count of iteration</param>
/// <returns>True when shapes is good otherwise False</returns>
bool heal_expolygons(Domain::ExPolygons& shape, unsigned max_iteration = 10);

/// <summary>
/// Divide line segments in place near to point
/// (which could lead to self intersection due to preccision)
/// Remove same neighbors
/// Note: Possible part of heal shape
/// </summary>
/// <param name="expolygons">Expolygon to edit</param>
/// <param name="distance">(epsilon)Euclidean distance from point to line which divide line</param>
/// <returns>True when some division was made otherwise false</returns>
bool divide_segments_for_close_point(Domain::ExPolygons& expolygons, double distance);

} // namespace Slic3r::Biz::Algorithms::HealPolygon
