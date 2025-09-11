///|/ Copyright (c) Prusa Research 2024 Filip Sykala @Jony01
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_SLA_SuppotstIslands_VoronoiDiagramCGAL_hpp_
#define slic3r_SLA_SuppotstIslands_VoronoiDiagramCGAL_hpp_

#include <libslic3r/Point.hpp>
#include <libslic3r/Polygon.hpp>

namespace Slic3r::sla {

/// <summary>
/// 
/// IMPROVE1: use accessor to point coordinate instead of points
/// IMPROVE2: add filter for create cell polygon only for moveable samples
/// </summary>
/// <param name="points">Input points for voronoi diagram</param>
/// <param name="max_distance">Limit for polygon made by point
/// NOTE: prerequisities input points are in max_distance only outer have infinite cell
/// which are croped to max_distance</param>
/// <returns>Polygon cell for input point</returns>
Polygons create_voronoi_cells_cgal(const Points &points, coord_t max_distance);

}
#endif // slic3r_SLA_SuppotstIslands_VoronoiDiagramCGAL_hpp_
