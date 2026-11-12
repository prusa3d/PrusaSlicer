#ifndef slic3r_SLA_SuppotstIslands_PolygonUtils_hpp_
#define slic3r_SLA_SuppotstIslands_PolygonUtils_hpp_

#include "Slic3r/Domain/Polygon.hpp"

namespace Slic3r::Biz::Algorithms {
/// <summary>
/// Class which contain collection of static function
/// for work with Polygon.
/// </summary>
class PolygonUtils
{
public:
    PolygonUtils() = delete;

    /// <summary>
    /// Create regular polygon with N points
    /// </summary>
    /// <param name="count_points">Count points of regular polygon</param>
    /// <param name="radius">Radius around center</param>
    /// <param name="center">Center point</param>
    /// <returns>Regular Polygon with CCW points</returns>
    static Domain::Polygon create_regular(size_t count_points, double radius = 10., const Domain::Point& center = Domain::Point(0,0));

    /// <summary>
    /// Create circle with N points
    /// alias for create regular
    /// </summary>
    /// <param name="radius">Radius of circle</param>
    /// <param name="count_points">Count points of circle</param>
    /// <param name="center">Center point</param>
    /// <returns>Regular Polygon with CCW points</returns>
    static Domain::Polygon create_circle(double radius, size_t count_points = 10, const Domain::Point& center = Domain::Point(0,0)){
        return create_regular(count_points, radius, center);
    }

    /**
     * @brief Create an ellipse polygon with the given radii.
     * @param radius_x Radius along the X axis (in scaled coordinates).
     * @param radius_y Radius along the Y axis (in scaled coordinates).
     * @param count_points Number of vertices.
     * @param center Center point.
     * @return Polygon with CCW points.
     */
    static Domain::Polygon create_ellipse(
        double radius_x,
        double radius_y,
        size_t count_points         = 10,
        const Domain::Point& center = Domain::Point(0, 0)
    );

    /// <summary>
    /// Create triangle with same length for all sides
    /// </summary>
    /// <param name="edge_size">triangel edge size</param>
    /// <returns>Equilateral triangle</returns>
    static Domain::Polygon create_equilateral_triangle(double edge_size);

    /// <summary>
    /// Create triangle with two side with same size
    /// </summary>
    /// <param name="side">Size of unique side</param>
    /// <param name="height">triangle height</param>
    /// <returns>Isosceles Triangle </returns>
    static Domain::Polygon create_isosceles_triangle(double side, double height);

    /// <summary>
    /// Create squar with center in [0,0]
    /// </summary>
    /// <param name="size"></param>
    /// <returns>Square</returns>
    static Domain::Polygon create_square(double size);

    /// <summary>
    /// Create rect with center in [0,0]
    /// </summary>
    /// <param name="width">width</param>
    /// <param name="height">height</param>
    /// <returns>Rectangle</returns>
    static Domain::Polygon create_rect(double width, double height);

    /// <summary>
    /// check if all pairs on polygon create with center ccw triangle
    /// </summary>
    /// <param name="polygon">input polygon to check</param>
    /// <param name="center">center point inside polygon</param>
    /// <returns>True when all points in polygon are CCW with center</returns>
    static bool is_ccw(const Domain::Polygon &polygon, const Domain::Point &center);

    /// <summary>
    /// ! Only for polygon around point, like Voronoi diagram cell
    /// </summary>
    /// <param name="polygon">Polygon to check</param>
    /// <param name="center">Center inside polygon, points create circle around center</param>
    /// <returns>True when valid without self intersection otherwise FALSE</returns>
    static bool is_not_self_intersect(const Domain::Polygon &polygon, const Domain::Point &center);
};
} // namespace Slic3r::sla
#endif // slic3r_SLA_SuppotstIslands_PolygonUtils_hpp_
