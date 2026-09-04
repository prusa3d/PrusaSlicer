#ifndef slic3r_Polygon_hpp_
#define slic3r_Polygon_hpp_

#include <assert.h>
#include <math.h>
#include <oneapi/tbb/scalable_allocator.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <utility>
#include <cassert>
#include <cmath>

#include "Slic3r/Domain/MultiPoint.hpp"
#include "Slic3r/Domain/Point.hpp"
#include "Slic3r/Domain/Polygon.hpp"
#include "Slic3r/Biz/Algorithms/Polygon.hpp"
#include "libslic3r.h"
#include "Line.hpp"
#include "Point.hpp"
#include "MultiPoint.hpp"
#include "Polyline.hpp"

namespace Slic3r {

using Polygon = Slic3r::Domain::Polygon;
using Polygons = Slic3r::Domain::Polygons;

class ColorPolygon;

using ColorPolygons = std::vector<ColorPolygon>;

// Projection of a point onto the polygon.
Point point_projection(const Polygon &polygon, const Point &point);

// Approximate on boundary test.
inline bool polygon_on_boundary(const Polygon &polygon, const Point &point, double eps)
{
    return (point_projection(polygon, point) - point).cast<double>().squaredNorm() < eps * eps;
}

[[deprecated("Use Biz::Algorithms::Polygon::get_extents")]]
BoundingBox get_extents(const Polygon &poly);

[[deprecated("Use Biz::Algorithms::Polygon::get_extents")]]
BoundingBox get_extents(const Polygons &polygons);

BoundingBox get_extents_rotated(const Polygon &poly, double angle);
std::vector<BoundingBox> get_extents_vector(const Polygons &polygons);

// Remove sticks (tentacles with zero area) from the polygon.
bool remove_sticks(Polygon &poly);
bool remove_sticks(Polygons &polys);

void remove_collinear(Polygon &poly);

Polygons polygons_simplify(Polygons &&polys, double tolerance, bool strictly_simple = true);
Polygons polygons_simplify(const Polygons &polys, double tolerance, bool strictly_simple = true);

inline Polygons to_polygons(const VecOfPoints &paths)
{
    Polygons out;
    out.reserve(paths.size());
    for (const Points &path : paths)
        out.emplace_back(path);
    return out;
}

inline Polygons to_polygons(VecOfPoints &&paths)
{
    Polygons out;
    out.reserve(paths.size());
    for (Points &path : paths)
        out.emplace_back(std::move(path));
    return out;
}

// Do polygons match? If they match, they must have the same topology,
// however their contours may be rotated.
bool polygons_match(const Polygon &l, const Polygon &r);

Polygon make_circle(double radius, double error);
Polygon make_circle_num_segments(double radius, size_t num_segments);

/**
@brief Define point laying on polygon
keep index of polygon line and point coordinate
*/
struct PolygonPoint
{
    // index of line inside of polygon
    // 0 .. from point polygon[0] to polygon[1]
    size_t index;

    // Point, which lay on line defined by index
    Point point;
};
using PolygonPoints = std::vector<PolygonPoint>;

// To replace reserve_vector where it's used for Polygons
template<class I> IntegerOnly<I, Polygons> reserve_polygons(I cap)
{
    return reserve_vector<Polygon, I, typename Polygons::allocator_type>(cap);
}

class ColorPolygon : public Polygon
{
public:
    using Color  = uint8_t;
    using Colors = std::vector<Color>;

    Colors colors;

    ColorPolygon() = default;
    explicit ColorPolygon(const Points &points, const Colors &colors) : Polygon(points), colors(colors) {}
    ColorPolygon(std::initializer_list<Point> points, std::initializer_list<Color> colors) : Polygon(points), colors(colors) {}
    ColorPolygon(const ColorPolygon &other) : ColorPolygon(other.points, other.colors) {}
    ColorPolygon(ColorPolygon &&other) noexcept : ColorPolygon(std::move(other.points), std::move(other.colors)) {}
    ColorPolygon(Points &&points, Colors &&colors) : Polygon(std::move(points)), colors(std::move(colors)) {}

    void reverse() override {
        Polygon::reverse();
        std::reverse(this->colors.begin(), this->colors.end());
    }

    ColorPolygon &operator=(const ColorPolygon &other) {
        this->points = other.points;
        this->colors = other.colors;
        return *this;
    }

    BoundingBox bounding_box() const;
};

using ColorPolygons = std::vector<ColorPolygon>;

} // namespace Slic3r

namespace Slic3r::Biz::Algorithms::Polygon {

// TODO: Temporary proxy method that will be removed after migration to BoundingBox2crd.
Slic3r::BoundingBox get_bounding_box(const Slic3r::Polygon& poly);

} // namespace Slic3r::Biz::Algorithms::Polygon

#endif
