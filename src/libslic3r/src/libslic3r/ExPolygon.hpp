#ifndef slic3r_ExPolygon_hpp_
#define slic3r_ExPolygon_hpp_

#include <assert.h>
#include <oneapi/tbb/scalable_allocator.h>
#include <stdint.h>
#include <vector>
#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <utility>
#include <cassert>
#include <cinttypes>

#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Biz/Algorithms/ExPolygon.hpp"
#include "Point.hpp"
#include "libslic3r.h"
#include "Polygon.hpp"
#include "Polyline.hpp"
#include "libslic3r/Line.hpp"

namespace Slic3r {

using ExPolygon = Slic3r::Domain::ExPolygon;
using ExPolygons = Slic3r::Domain::ExPolygons;

// Approximate on boundary test.
bool on_boundary(const ExPolygon &expolygon, const Point &point, double eps);
// Projection of a point onto the polygon.
Point point_projection(const ExPolygon &expolygon, const Point &point);

void medial_axis(const ExPolygon& expolygon, double min_width, double max_width, ThickPolylines* polylines);
void medial_axis(const ExPolygon& expolygon, double min_width, double max_width, Polylines* polylines);
Polylines medial_axis(const ExPolygon& expolygon, double min_width, double max_width);

inline Linesf to_unscaled_linesf(const ExPolygons &src)
{
    using namespace Slic3r::Biz;

    Linesf lines;
    lines.reserve(Algorithms::ExPolygon::count_points(src));
    for (ExPolygons::const_iterator it_expoly = src.begin(); it_expoly != src.end(); ++ it_expoly) {
        for (size_t i = 0; i <= it_expoly->holes.size(); ++ i) {
            const Points &points = ((i == 0) ? it_expoly->contour : it_expoly->holes[i - 1]).points;
            Vec2d unscaled_a = unscaled(points.front());
            Vec2d unscaled_b = unscaled_a;
            for (Points::const_iterator it = points.begin()+1; it != points.end(); ++it){
                unscaled_b = unscaled(*(it));
                lines.push_back(Linef(unscaled_a, unscaled_b));
                unscaled_a = unscaled_b;
            }
            lines.push_back(Linef(unscaled_a, unscaled(points.front())));
        }
    }
    return lines;
}

// Do expolygons match? If they match, they must have the same topology,
// however their contours may be rotated.
bool expolygons_match(const ExPolygon &l, const ExPolygon &r);

[[deprecated("Use Biz::Algorithms::ExPolygon::get_extents")]]
BoundingBox get_extents(const ExPolygon &expolygon);

[[deprecated("Use Biz::Algorithms::ExPolygon::get_extents")]]
BoundingBox get_extents(const ExPolygons &expolygons);

BoundingBox get_extents_rotated(const ExPolygon &poly, double angle);
BoundingBox get_extents_rotated(const ExPolygons &polygons, double angle);
std::vector<BoundingBox> get_extents_vector(const ExPolygons &polygons);

bool remove_sticks(ExPolygon &poly);
void keep_largest_contour_only(ExPolygons &polygons);

} // namespace Slic3r

#endif
