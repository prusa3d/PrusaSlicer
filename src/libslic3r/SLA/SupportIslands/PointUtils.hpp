#ifndef slic3r_SLA_SuppotstIslands_PointUtils_hpp_
#define slic3r_SLA_SuppotstIslands_PointUtils_hpp_

#include <libslic3r/Point.hpp>

namespace Slic3r::sla {

/// <summary>
/// Class which contain collection of static function
/// for work with Point and Points.
/// QUESTION: Is it only for SLA?
/// </summary>
class PointUtils
{
public:
    PointUtils() = delete;
    /// <summary>
    /// Is equal p1 == p2
    /// </summary>
    /// <param name="p1">first point</param>
    /// <param name="p2">second point</param>
    /// <returns>True when equal otherwise FALSE</returns>
    static bool is_equal(const Point &p1, const Point &p2);

    /// <summary>
    /// check if absolut value of x is grater than absolut value of y
    /// </summary>
    /// <param name="point">input direction</param>
    /// <returns>True when mayorit vaule is X other wise FALSE(mayorit value is y)</returns>
    static bool is_majorit_x(const Point &point);
    static bool is_majorit_x(const Vec2d &point);

    /// <summary>
    /// Create perpendicular vector[-y,x] 
    /// </summary>
    /// <param name="vector">input vector from zero to point coordinate</param>
    /// <returns>Perpendicular[-vector.y, vector.x]</returns>
    static Point perp(const Point &vector);

    /// <summary>
    /// Check if both direction are on same half of the circle
    /// alpha = atan2 of direction1 
    /// beta = atan2 of direction2
    /// beta is in range from(alpha - Pi/2) to (alpha + Pi/2)
    /// </summary>
    /// <param name="dir1">first direction</param>
    /// <param name="dir2">second direction</param>
    /// <returns>True when on same half otherwise false</returns>
    static bool is_same_direction(const Point &dir1, const Point &dir2);
};

} // namespace Slic3r::sla
#endif // slic3r_SLA_SuppotstIslands_PointUtils_hpp_
