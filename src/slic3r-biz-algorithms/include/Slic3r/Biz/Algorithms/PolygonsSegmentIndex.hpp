//Copyright (c) 2020 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#ifndef UTILS_POLYGONS_SEGMENT_INDEX_H
#define UTILS_POLYGONS_SEGMENT_INDEX_H

#include <boost/polygon/isotropy.hpp>
#include <boost/polygon/segment_concept.hpp>
#include <vector>

#include "Slic3r/Biz/Algorithms/PolygonsPointIndex.hpp"

namespace Slic3r::Biz::Algorithms
{

/*!
 * A class for iterating over the points in one of the polygons in a \ref Polygons object
 */
class PolygonsSegmentIndex : public PolygonsPointIndex
{
public:
    PolygonsSegmentIndex() : PolygonsPointIndex(){};
    PolygonsSegmentIndex(const Domain::Polygons *polygons, unsigned int poly_idx, unsigned int point_idx) : PolygonsPointIndex(polygons, poly_idx, point_idx){};

    Domain::Point from() const { return PolygonsPointIndex::p(); }

    Domain::Point to() const { return PolygonsSegmentIndex::next().p(); }
};

} // namespace Slic3r::Arachne

namespace boost::polygon {

template<> struct geometry_concept<Slic3r::Biz::Algorithms::PolygonsSegmentIndex>
{
    typedef segment_concept type;
};

template<> struct segment_traits<Slic3r::Biz::Algorithms::PolygonsSegmentIndex>
{
    typedef Slic3r::Domain::coord_t       coordinate_type;
    typedef Slic3r::Domain::Point point_type;

    static inline point_type get(const Slic3r::Biz::Algorithms::PolygonsSegmentIndex &CSegment, direction_1d dir)
    {
        return dir.to_int() ? CSegment.to() : CSegment.from();
    }
};

} // namespace boost::polygon

#endif//UTILS_POLYGONS_SEGMENT_INDEX_H
