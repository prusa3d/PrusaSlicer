// Polygon offsetting using Voronoi diagram prodiced by boost::polygon.

#ifndef slic3r_VoronoiOffset_hpp_
#define slic3r_VoronoiOffset_hpp_

#include "libslic3r.h"
#include "Polygon.hpp"

#define BOOST_VORONOI_USE_GMP 1
#include "boost/polygon/voronoi.hpp"
#include "boost/polygon/segment_data.hpp"
using boost::polygon::voronoi_builder;
using boost::polygon::voronoi_diagram;


namespace Slic3r {


class VoronoiDiagram : public boost::polygon::voronoi_diagram<double> {
public:
	typedef double                                          coord_type;
	typedef boost::polygon::point_data<coordinate_type>     point_type;
	typedef boost::polygon::segment_data<coordinate_type>   segment_type;
	typedef boost::polygon::rectangle_data<coordinate_type> rect_type;
};


// Offset a polygon or a set of polygons possibly with holes by traversing a Voronoi diagram.
// The input polygons are stored in lines and lines are referenced by vd.
// Outer curve will be extracted for a positive offset_distance,
// inner curve will be extracted for a negative offset_distance.
// Circular arches will be discretized to achieve discretization_error.
Polygons voronoi_offset(
	const VoronoiDiagram 	&vd, 
	const Lines 					&lines, 
	double 							 offset_distance, 
	double 							 discretization_error);

} // namespace Slic3r

#endif // slic3r_VoronoiOffset_hpp_
