#pragma once


#include <stdint.h>
#include <vector>
#include <cinttypes>

#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/Line.hpp"
#include "Slic3r/Domain/Polygon.hpp"

namespace Slic3r::Biz::Algorithms {

struct IntersectionLines {
    uint32_t line_index1;
    uint32_t line_index2;
    Domain::Vec2d intersection;
};
using IntersectionsLines = std::vector<IntersectionLines>;

// collect all intersecting points
IntersectionsLines get_intersections(const Domain::Lines &lines);
IntersectionsLines get_intersections(const Domain::Polygon &polygon);
IntersectionsLines get_intersections(const Domain::Polygons &polygons);
IntersectionsLines get_intersections(const Domain::ExPolygon &expolygon);
IntersectionsLines get_intersections(const Domain::ExPolygons &expolygons);

}
