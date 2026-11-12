#pragma once

#include <boost/polygon/polygon.hpp>
#include <iterator>

#include "Slic3r/Biz/CGAL/Algorithms/Voronoi.hpp"
#include "Slic3r/Biz/Algorithms/PolygonsSegmentIndex.hpp"

namespace Slic3r::Biz::CGAL::Algorithms {

using VoronoiDiagram = VoronoiDiagram;

class VoronoiUtilsCgal
{
public:
    // Check if the Voronoi diagram is planar using CGAL sweeping edge algorithm for enumerating all intersections between lines.
    static bool is_voronoi_diagram_planar_intersection(const VoronoiDiagram &voronoi_diagram);

    // Check if the Voronoi diagram is planar using verification that all neighboring edges are ordered CCW for each vertex.
    template<typename SegmentIterator>
    static typename boost::polygon::enable_if<
        typename boost::polygon::gtl_if<typename boost::polygon::is_segment_concept<
            typename boost::polygon::geometry_concept<typename std::iterator_traits<SegmentIterator>::value_type>::type>::type>::type,
        bool>::type
    is_voronoi_diagram_planar_angle(const VoronoiDiagram &voronoi_diagram, SegmentIterator segment_begin, SegmentIterator segment_end);
};
}
