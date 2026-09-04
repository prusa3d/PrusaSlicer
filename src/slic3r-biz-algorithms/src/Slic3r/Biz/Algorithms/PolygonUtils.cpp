#include "Slic3r/Biz/Algorithms/PolygonUtils.hpp"
#include "Slic3r/Biz/Algorithms/Geometry/Geometry.hpp"

namespace Slic3r::Biz::Algorithms {

using Domain::Polygon;
using Domain::Point;
using Domain::Points;
using Domain::coord_t;

inline bool is_in_coord_limits(const double& value) {
    return (value < std::numeric_limits<coord_t>::max()) &&
           (value > std::numeric_limits<coord_t>::min());
}

Polygon PolygonUtils::create_regular(size_t       count_points,
                                             double       radius,
                                             const Point &center)
{
    assert(radius >= 1.);
    assert(count_points >= 3);
    Points points;
    points.reserve(count_points);
    double increase_angle = 2 * M_PI / count_points;
    for (size_t i = 0; i < count_points; ++i) {
        double angle = i * increase_angle;
        double x = cos(angle) * radius + center.x();
        assert(is_in_coord_limits(x));
        double y = sin(angle) * radius + center.y();
        assert(is_in_coord_limits(y));
        points.emplace_back(x, y);
    }
    return Polygon(points);
}

Polygon PolygonUtils::create_ellipse(
    const double radius_x,
    const double radius_y,
    const size_t count_points,
    const Point& center
)
{
    assert(radius_x >= 1.);
    assert(radius_y >= 1.);
    assert(count_points >= 3);

    Points points;
    points.reserve(count_points);

    const double increase_angle = 2 * std::numbers::pi / static_cast<double>(count_points);
    for (size_t i = 0; i < count_points; ++i) {
        const double angle = static_cast<double>(i) * increase_angle;
        const double x     = std::cos(angle) * radius_x + center.x();
        const double y     = std::sin(angle) * radius_y + center.y();

        assert(is_in_coord_limits(x));
        assert(is_in_coord_limits(y));

        points.emplace_back(x, y);
    }

    return Polygon(points);
}

Polygon PolygonUtils::create_equilateral_triangle(double edge_size)
{
    coord_t x = edge_size / 2;
    coord_t y = sqrt(edge_size * edge_size - edge_size * edge_size / 4) / 2;
    return {{-x, -y}, {x, -y}, {0, y}};
}

Polygon PolygonUtils::create_isosceles_triangle(double side, double height)
{
    const auto side_2{static_cast<coord_t>(std::round(side / 2))};
    const auto height_coord{static_cast<coord_t>(std::round(height))};
    return {{-side_2, 0}, {side_2, 0}, {0, height_coord}};
}

Polygon PolygonUtils::create_square(double size)
{
    const auto size_2{static_cast<coord_t>(std::round(size / 2))};
    return {{-size_2, size_2},
            {-size_2, -size_2},
            {size_2, -size_2},
            {size_2, size_2}};
}

Polygon PolygonUtils::create_rect(double width, double height)
{
    const auto x_2{static_cast<coord_t>(std::round(width / 2))};
    const auto y_2{static_cast<coord_t>(std::round(height / 2))};
    return {{-x_2, y_2}, {-x_2, -y_2}, {x_2, -y_2}, {x_2, y_2}};
}

bool PolygonUtils::is_ccw(const Polygon &polygon, const Point &center) {
    const Point *prev = &polygon.points.back();
    for (const Point &point : polygon.points) { 
        Geometry::Orientation o = Geometry::orient(center, *prev, point);
        if (o != Geometry::Orientation::ORIENTATION_CCW) return false;
        prev = &point;
    }
    return true;
}

bool PolygonUtils::is_not_self_intersect(const Polygon &polygon,
                                         const Point &  center)
{
    auto get_angle = [&center](const Point &point) {
        Point diff_point = point - center;
        return atan2(diff_point.y(), diff_point.x());
    };
    bool         found_circle_end = false; // only one can be on polygon
    double prev_angle = get_angle(polygon.points.back());
    for (const Point &point : polygon.points) {
        double angle = get_angle(point);
        if (angle < prev_angle) { 
            if (found_circle_end) return false;
            found_circle_end = true;
        }
        prev_angle = angle;
    }
    return true;
}
}
