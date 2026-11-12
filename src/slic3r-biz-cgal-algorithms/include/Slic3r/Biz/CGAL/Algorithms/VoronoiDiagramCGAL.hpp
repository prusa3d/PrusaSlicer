#ifndef slic3r_SLA_SuppotstIslands_VoronoiDiagramCGAL_hpp_
#define slic3r_SLA_SuppotstIslands_VoronoiDiagramCGAL_hpp_

#include "Slic3r/Domain/Point.hpp"
#include "Slic3r/Domain/Polygon.hpp"

namespace Slic3r::Biz::CGAL::Algorithms {

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
Domain::Polygons create_voronoi_cells_cgal(const Domain::Points &points, Domain::coord_t max_distance);

}
#endif // slic3r_SLA_SuppotstIslands_VoronoiDiagramCGAL_hpp_
