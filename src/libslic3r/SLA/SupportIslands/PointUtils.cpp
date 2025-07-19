#include "PointUtils.hpp"

using namespace Slic3r::sla;

bool PointUtils::is_equal(const Point &p1, const Point &p2) {
    return p1.x() == p2.x() && p1.y() == p2.y();
}

bool PointUtils::is_majorit_x(const Point &point)
{
    return abs(point.x()) > abs(point.y());
}

bool PointUtils::is_majorit_x(const Vec2d &point)
{
    return fabs(point.x()) > fabs(point.y());
}

Slic3r::Point PointUtils::perp(const Point &vector)
{
    return Point(-vector.y(), vector.x());
}

bool PointUtils::is_same_direction(const Point &dir1, const Point &dir2)
{
    // (is_majorit_x(dir1)) ? (dir1.x() > 0) == (dir2.x() > 0) :
    //                        (dir1.y() > 0) == (dir2.y() > 0);
    // Cant use majorit direction:
    // [2] 750000 544907
    // [2] 463525 -1426583
    // !! bad idea
    
    // Cant use dot product for int value ==> dir1.dot(dir2)
    // diferent int result for input
    //[2] - 128707028 93448506 
    //[2] 10475487 1662574
    // may be overflow ??
    
    return dir1.cast<float>().dot(dir2.cast<float>()) > 0;
}
