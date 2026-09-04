#include "Slic3r/Biz/Algorithms/IntersectionPoints.hpp"

#include "Slic3r/Biz/Algorithms/ExPolygon.hpp"
#include "Slic3r/Biz/Algorithms/Polygon.hpp"
#include "Slic3r/Biz/Algorithms/AABBTreeLines.hpp"

#include "Slic3r/Biz/Algorithms/AABBTreeIndirect.hpp"
#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Exception.hpp"

//NOTE: using CGAL SweepLines is slower !!! (example in git history)

namespace Slic3r::Biz::Algorithms {

namespace {

IntersectionsLines compute_intersections(const Domain::Lines &lines)
{
    if (lines.size() < 3)
        return {};    

    auto tree = AABBTreeLines::build_aabb_tree_over_indexed_lines(lines);
    IntersectionsLines result;
    for (uint32_t li = 0; li < lines.size()-1; ++li) {
        const Domain::Line &l = lines[li];
        auto intersections = AABBTreeLines::get_intersections_with_line<false, Domain::Point, Domain::Line>(lines, tree, l);
        for (const auto &[p, node_index] : intersections) {
            if (node_index - 1 <= li)
                continue;
            if (const Domain::Line &l_ = lines[node_index];
                l_.a == l.a ||
                l_.a == l.b ||
                l_.b == l.a ||
                l_.b == l.b )
                // it is duplicit point not intersection
                continue; 

            // NOTE: fix AABBTree to compute intersection with double preccission!!
            Domain::Vec2d intersection_point = p.cast<double>();

            result.push_back(IntersectionLines{li, static_cast<uint32_t>(node_index), intersection_point});
        }
    }
    return result;
}
} // namespace

IntersectionsLines get_intersections(const Domain::Lines& lines)
{
    return compute_intersections(lines);
}

IntersectionsLines get_intersections(const Domain::Polygon& polygon)
{
    return compute_intersections(Polygon::to_lines(polygon));
}

IntersectionsLines get_intersections(const Domain::Polygons& polygons)
{
    return compute_intersections(Polygon::to_lines(polygons));
}

IntersectionsLines get_intersections(const Domain::ExPolygon& expolygon)
{
    return compute_intersections(ExPolygon::to_lines(expolygon));
}

IntersectionsLines get_intersections(const Domain::ExPolygons& expolygons)
{
    return compute_intersections(ExPolygon::to_lines(expolygons));
}
}
