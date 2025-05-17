/*================================================================*/
/*
 * Author:  Pavel Surynek, 2023 - 2025
 * Company: Prusa Research
 *
 * File:    seq_preprocess.hpp
 *
 * Object preprocessing for the sequential printing SMT model.
 */
/*================================================================*/

#include "seq_defs.hpp"

#include "libslic3r/Geometry.hpp"
#include "libslic3r/ClipperUtils.hpp"

#include "seq_preprocess.hpp"
#include "libseqarrange/seq_interface.hpp"


/*----------------------------------------------------------------*/

using namespace std;
using namespace Slic3r;
//using namespace ClipperLib;


/*----------------------------------------------------------------*/

namespace Sequential
{

        

/*----------------------------------------------------------------*/

// These are only approximate values for M3S, TODO: measure MK3S for true values
const std::vector<Slic3r::Polygon> SEQ_UNREACHABLE_POLYGON_NOZZLE_LEVEL_MK3S =
{
    {
        {-500000, -500000},
        {500000, -500000},
        {500000, 500000},
        {-500000, 500000}
    }
};


// TODO: measure MK3S for true values
const std::vector<Slic3r::Polygon> SEQ_UNREACHABLE_POLYGON_EXTRUDER_LEVEL_MK3S =
{
    {
        {-2000000, -10000000},
        {2000000, -10000000},
        {2000000, 2000000},
        {-2000000, 2000000}
    }	
};


// TODO: measure MK3S for true values
const std::vector<Slic3r::Polygon> SEQ_UNREACHABLE_POLYGON_HOSE_LEVEL_MK3S =
{
    {
        {-1000000, 500000},
        {1000000, 500000},
        {1000000, -250000000},
        {-1000000, -250000000}	
    }
};


// TODO: measure MK3S for true values
const std::vector<Slic3r::Polygon> SEQ_UNREACHABLE_POLYGON_GANTRY_LEVEL_MK3S =
{
    {
        {-250000000, 2000000},
        {250000000, 2000000},	
        {250000000, 2100000},
        {-250000000, 2100000}
    }
};

    
// TODO: measure MK3S for true values
const std::vector<std::vector<Slic3r::Polygon> > SEQ_UNREACHABLE_POLYGON_ALL_LEVELS_MK3S =
{
    SEQ_UNREACHABLE_POLYGON_NOZZLE_LEVEL_MK3S,
    SEQ_UNREACHABLE_POLYGON_EXTRUDER_LEVEL_MK3S,
    SEQ_UNREACHABLE_POLYGON_HOSE_LEVEL_MK3S,
    SEQ_UNREACHABLE_POLYGON_GANTRY_LEVEL_MK3S    
};


const std::vector<std::vector<Slic3r::Polygon> > SEQ_UNREACHABLE_POLYGON_CONVEX_LEVELS_MK3S =
{
    SEQ_UNREACHABLE_POLYGON_NOZZLE_LEVEL_MK3S,
    SEQ_UNREACHABLE_POLYGON_EXTRUDER_LEVEL_MK3S
};


const std::vector<std::vector<Slic3r::Polygon> > SEQ_UNREACHABLE_POLYGON_BOX_LEVELS_MK3S =
{
    SEQ_UNREACHABLE_POLYGON_HOSE_LEVEL_MK3S,
    SEQ_UNREACHABLE_POLYGON_GANTRY_LEVEL_MK3S    
};


/*----------------------------------------------------------------*/

// Nozzle height range: 0.00mm-4.9mm
const std::vector<Slic3r::Polygon> SEQ_UNREACHABLE_POLYGON_NOZZLE_LEVEL_MK4 =
{
    {
	/* hand tailored */
	{ -5000000,  -5000000},
	{ 5000000,  -5000000},
	{ 5000000,  5000000},
	{ -5000000,  5000000}
	
	/* original from decimator
	{ -3 728 158,  -1 611 789},
	{ -468 223,  -4 034 578},
	{ 2 543 938,  -2 732 339},
	{ 3 259 933,  -2 422 789},
	{ 3 728 160,  1 611 785},
	{ 468 227,  4 034 579},
	{ -1 666 062,  3 111 867},
	{ -3 259 931,  2 422 789},
	*/
    }
};

// Extruder height range: 4.9mm-13.0mm
const std::vector<Slic3r::Polygon> SEQ_UNREACHABLE_POLYGON_EXTRUDER_LEVEL_MK4 =
{
    {   /* fan - hand tailored */
	{ -10000000, -21000000},	
	{ 37000000, -21000000},
	{ 37000000,  44000000},
	{ -10000000,  44000000}

	/* fan - original from decimator
	{  87952801,    3665480},	
	{ 103166346,   -1375028},	
	{ 105384145,   -1136906},	
	{ 107137556,     241781},	
	{ 107889619,    2905295},		
	{ 102396166,   55454515},	
	{ 101386126,   58737097},	
	{  93 053 422,   62777197},	
	{  87 447 788,   59999636},	
	{  70 782 970,   28440457},	

	// nozzle
	{ -29 076 068,  18 872 356},	
	{ -29 001 876,  18 872 356},	
	{ -29 001 876,  18 952 646},	
	{ -29076068,  18952646},	

 */
    },
    {   /* body - hand tailored */
	{-40000000, -45000000},
	{ 38000000, -45000000},
	{ 38000000,  20000000},
	{-40000000,  20000000}
	
        /* body - original from decimator
	{ -68105202,  -14269412},	
	{ -62019977,  -20740757},	
	{ -37145411,  -25968391},	
	{ -23949432,  -25968391},	
	{    919905,  -20740757},	
	{   3102334,  -16781961},	
	{   8275483,    3033496},	
	{   -130845,   26409612},	
	{ -20142759,   38793397},	
	{ -62268386,   38793397},	
	{ -67090122,   17070789},	

	// nozzle
	{ -29076068,  18872356},	
	{ -29001876,  18872356},	
	{ -29001876,  18952646},	
	{ -29076068,  18952646},	
	*/
    }
};


// Gantry height range: 13.0mm-15.0mm
const std::vector<Slic3r::Polygon> SEQ_UNREACHABLE_POLYGON_GANTRY_LEVEL_MK4 =
{
    {
	/* hand tailored */
	{ -350000000, -4000000},
        { 350000000, -4000000},
        { 350000000, -14000000},
        { -350000000, -14000000}

	/* original from decimator
        { -206972968,  -12664471},
        { -206470468,  -13167301}
        { 164374531,  -13167301},
        { 164877031,  -12664471},
        { 164877031,  -5630724},
        { 164374531,  -5128674},
        { -206470468,  -5128674},
        { -206972968,  -5630724},

	nozzle
        { -29111351,  18877954},
        { -29022835,  18841825},
        { -28966594,  18940523},
        { -29040014,  18983178},
        */
    }
};

    
// Hose height range: 15.0mm-infinity (the hose is the last)
const std::vector<Slic3r::Polygon> SEQ_UNREACHABLE_POLYGON_HOSE_LEVEL_MK4 =
{
    {
	/* rigid hose - hand tailored */
	{ -12000000, -350000000},
	{   9000000, -350000000},
	{   9000000,  -39000000},
	{ -12000000,  -39000000}
	
	/* original from decimator
	{ -40942228,  -22802359},
        { -38008017,  -64681679},
        { -23603700,  -65215173},
        { -20135995,  -20401563},
        { -28933517,  21680323},

	// nozzle
        { -29111351,  18877954},
        { -29022835,  18841825},
        { -28966594,  18940523},
        { -29040014,  18983178},
	*/
    },
    {
	/* flexible hose - hand tailored */
	{ -12000000, -350000000},
	{ 250000000, -350000000},
	{ 250000000,  -82000000},
	{ -12000000,  -82000000}
    }
};
    

const std::vector<std::vector<Slic3r::Polygon> > SEQ_UNREACHABLE_POLYGON_ALL_LEVELS_MK4 =
{
    SEQ_UNREACHABLE_POLYGON_NOZZLE_LEVEL_MK4,
    SEQ_UNREACHABLE_POLYGON_EXTRUDER_LEVEL_MK4,
    SEQ_UNREACHABLE_POLYGON_GANTRY_LEVEL_MK4,
    SEQ_UNREACHABLE_POLYGON_HOSE_LEVEL_MK4    
};


const std::vector<std::vector<Slic3r::Polygon> > SEQ_UNREACHABLE_POLYGON_CONVEX_LEVELS_MK4 =
{
    SEQ_UNREACHABLE_POLYGON_NOZZLE_LEVEL_MK4,
    SEQ_UNREACHABLE_POLYGON_EXTRUDER_LEVEL_MK4
};


const std::vector<std::vector<Slic3r::Polygon> > SEQ_UNREACHABLE_POLYGON_BOX_LEVELS_MK4 =
{
    SEQ_UNREACHABLE_POLYGON_HOSE_LEVEL_MK4,
    SEQ_UNREACHABLE_POLYGON_GANTRY_LEVEL_MK4    
};
    

/*----------------------------------------------------------------*/

// TODO: Measure XL for true values
const std::vector<Slic3r::Polygon> SEQ_UNREACHABLE_POLYGON_NOZZLE_LEVEL_XL =
{
    {
        {-500000, -500000},
        {500000, -500000},
        {500000, 500000},
        {-500000, 500000}
    }
};


// TODO: Measure XL for true values
const std::vector<Slic3r::Polygon> SEQ_UNREACHABLE_POLYGON_EXTRUDER_LEVEL_XL =
{
    {
        {-2000000, -10000000},
        {2000000, -10000000},
        {2000000, 2000000},
        {-2000000, 2000000}
    }	
};


// TODO: Measure XL for true values
const std::vector<Slic3r::Polygon> SEQ_UNREACHABLE_POLYGON_HOSE_LEVEL_XL =
{
    {
        {-1000000, 500000},
        {1000000, 500000},
        {1000000, -250000000},
        {-1000000, -250000000}	
    }
};


// TODO: Measure XL for true values
const std::vector<Slic3r::Polygon> SEQ_UNREACHABLE_POLYGON_GANTRY_LEVEL_XL =
{
    {
        {-250000000, 2000000},
        {250000000, 2000000},	
        {250000000, 2100000},
        {-250000000, 2100000}
    }
};


const std::vector<std::vector<Slic3r::Polygon> > SEQ_UNREACHABLE_POLYGON_ALL_LEVELS_XL =
{
    SEQ_UNREACHABLE_POLYGON_NOZZLE_LEVEL_XL,
    SEQ_UNREACHABLE_POLYGON_EXTRUDER_LEVEL_XL,
    SEQ_UNREACHABLE_POLYGON_HOSE_LEVEL_XL,
    SEQ_UNREACHABLE_POLYGON_GANTRY_LEVEL_XL    
};


const std::vector<std::vector<Slic3r::Polygon> > SEQ_UNREACHABLE_POLYGON_CONVEX_LEVELS_XL =
{
    SEQ_UNREACHABLE_POLYGON_NOZZLE_LEVEL_XL,
    SEQ_UNREACHABLE_POLYGON_EXTRUDER_LEVEL_XL
};


const std::vector<std::vector<Slic3r::Polygon> > SEQ_UNREACHABLE_POLYGON_BOX_LEVELS_XL =
{
    SEQ_UNREACHABLE_POLYGON_HOSE_LEVEL_XL,
    SEQ_UNREACHABLE_POLYGON_GANTRY_LEVEL_XL    
};
    
    
/*----------------------------------------------------------------*/

Rational scaleDown_CoordinateForSequentialSolver(coord_t x)
{
    Rational scale_down_x(x, SEQ_SLICER_SCALE_FACTOR);
    scale_down_x.normalize();

    return scale_down_x;
}


void scaleDown_PolygonForSequentialSolver(const Slic3r::Polygon &polygon,
					  Slic3r::Polygon       &scale_down_polygon)
{
    scaleDown_PolygonForSequentialSolver(SEQ_SLICER_SCALE_FACTOR, polygon, scale_down_polygon);
}


void scaleDown_PolygonForSequentialSolver(coord_t                scale_factor,
					  const Slic3r::Polygon &polygon,
					  Slic3r::Polygon       &scale_down_polygon)
{
    for (unsigned int i = 0; i < polygon.points.size(); ++i)
    {
	scale_down_polygon.points.insert(scale_down_polygon.points.begin() + i, Point(polygon.points[i].x() / scale_factor, polygon.points[i].y() / scale_factor));
    }
    scale_down_polygon.make_counter_clockwise();    
}


Slic3r::Polygon scaleDown_PolygonForSequentialSolver(coord_t scale_factor, const Slic3r::Polygon &polygon)
{
    Slic3r::Polygon scale_down_polygon;
	
    for (unsigned int i = 0; i < polygon.points.size(); ++i)
    {
	scale_down_polygon.points.insert(scale_down_polygon.points.begin() + i, Point(polygon.points[i].x() / scale_factor, polygon.points[i].y() / scale_factor));
    }
    scale_down_polygon.make_counter_clockwise();

    return scale_down_polygon;
}


void scaleUp_PositionForSlicer(const Rational &position_X,
			       const Rational &position_Y,			       
			       coord_t        &scaled_position_X,
			       coord_t        &scaled_position_Y)
{
    scaleUp_PositionForSlicer(SEQ_SLICER_SCALE_FACTOR, position_X, position_Y, scaled_position_X, scaled_position_Y);
}


void scaleUp_PositionForSlicer(coord_t        scale_factor,
			       const Rational &position_X,
			       const Rational &position_Y,			       
			       coord_t        &scaled_position_X,
			       coord_t        &scaled_position_Y)
{
    scaled_position_X = (position_X.normalize() * scale_factor).as_int64();
    scaled_position_Y = (position_Y.normalize() * scale_factor).as_int64();
}



void scaleUp_PositionForSlicer(double position_X, double position_Y, coord_t &scaled_position_X, coord_t &scaled_position_Y)
{
    scaleUp_PositionForSlicer(SEQ_SLICER_SCALE_FACTOR, position_X, position_Y, scaled_position_X, scaled_position_Y);
}


void scaleUp_PositionForSlicer(coord_t  scale_factor,
			       double   position_X,
			       double   position_Y,
			       coord_t &scaled_position_X,
			       coord_t &scaled_position_Y)
{
    scaled_position_X = scale_factor * position_X;
    scaled_position_Y = scale_factor * position_Y;    
}


Slic3r::Polygon scaleUp_PolygonForSlicer(const Slic3r::Polygon &polygon)
{
    return scaleUp_PolygonForSlicer(SEQ_SLICER_SCALE_FACTOR, polygon);
}


Slic3r::Polygon scaleUp_PolygonForSlicer(coord_t scale_factor, const Slic3r::Polygon &polygon)
{
    Slic3r::Polygon poly = polygon;

    for (unsigned int i = 0; i < poly.points.size(); ++i)
    {
	poly.points[i] = Slic3r::Point(poly.points[i].x() * scale_factor, poly.points[i].y() * scale_factor);
    }

    return poly;
}


Slic3r::Polygon scaleUp_PolygonForSlicer(const Polygon &polygon, double x_pos, double y_pos)
{
    return scaleUp_PolygonForSlicer(SEQ_SLICER_SCALE_FACTOR, polygon, x_pos, y_pos);
}


Slic3r::Polygon scaleUp_PolygonForSlicer(coord_t scale_factor, const Polygon &polygon, double x_pos, double y_pos)
{
    Slic3r::Polygon poly = polygon;

    for (unsigned int i = 0; i < poly.points.size(); ++i)
    {	
	poly.points[i] = Point(poly.points[i].x() * scale_factor + x_pos * scale_factor,
			       poly.points[i].y() * scale_factor + y_pos * scale_factor);
    }

    return poly;
}


Slic3r::Polygon truncate_PolygonAsSeenBySequentialSolver(coord_t scale_factor, const Slic3r::Polygon &polygon)
{
    Slic3r::Polygon scale_down_polygon = scaleDown_PolygonForSequentialSolver(scale_factor, polygon);
    Slic3r::Polygon scale_up_polygon = scaleUp_PolygonForSlicer(scale_factor, scale_down_polygon);

    return scale_up_polygon;
}


void ground_PolygonByBoundingBox(Slic3r::Polygon &polygon)
{    
    BoundingBox polygon_box = get_extents(polygon);

    for (unsigned int i = 0; i < polygon.points.size(); ++i)
    {
	polygon.points[i] -= polygon_box.min;
    }    
}


void ground_PolygonByFirstPoint(Slic3r::Polygon &polygon)
{
    Point first = polygon.points[0];
    for (unsigned int i = 0; i < polygon.points.size(); ++i)
    {
	polygon.points[i] -= first;
    }    
}


void shift_Polygon(Slic3r::Polygon &polygon, coord_t x_offset, coord_t y_offset)
{
    Point offset(x_offset, y_offset);
    
    shift_Polygon(polygon, offset);
}


void shift_Polygon(Slic3r::Polygon &polygon, const Slic3r::Point &offset)
{
    for (unsigned int i = 0; i < polygon.points.size(); ++i)
    {
	polygon.points[i] += offset;
    }           
}


/*----------------------------------------------------------------*/

Polygon transform_UpsideDown(const SolverConfiguration &solver_configuration, const Polygon &polygon)
{
    return transform_UpsideDown(solver_configuration, SEQ_SLICER_SCALE_FACTOR, polygon);
}


Polygon transform_UpsideDown(const SolverConfiguration &solver_configuration, coord_t scale_factor, const Polygon &polygon)
{
    Polygon poly = polygon;

    for (unsigned int i = 0; i < poly.points.size(); ++i)
    {	
	poly.points[i] = Point(poly.points[i].x(),
			       (coord_t)((solver_configuration.plate_bounding_box.max.y() - solver_configuration.plate_bounding_box.min.y())  * scale_factor - poly.points[i].y()));
    }

    return poly;    
}


void transform_UpsideDown(const SolverConfiguration &solver_configuration, const coord_t &scaled_x_pos, const coord_t &scaled_y_pos, coord_t &transformed_x_pos, coord_t &transformed_y_pos)
{
    transform_UpsideDown(solver_configuration, SEQ_SLICER_SCALE_FACTOR, scaled_x_pos, scaled_y_pos, transformed_x_pos, transformed_y_pos);
}


void transform_UpsideDown(const SolverConfiguration &solver_configuration, coord_t scale_factor, const coord_t &scaled_x_pos, const coord_t &scaled_y_pos, coord_t &transformed_x_pos, coord_t &transformed_y_pos)
{
    transformed_x_pos = scaled_x_pos;
    transformed_y_pos = (solver_configuration.plate_bounding_box.max.y() - solver_configuration.plate_bounding_box.min.y()) * scale_factor - scaled_y_pos;
}


/*----------------------------------------------------------------*/

void grow_PolygonForContainedness(coord_t center_x, coord_t center_y, Slic3r::Polygon &polygon)
{
    for (unsigned int i = 0; i < polygon.points.size(); ++i)
    {
	polygon.points[i] *= SEQ_POLYGON_DECIMATION_GROW_FACTOR;
    }
    
    BoundingBox polygon_box = get_extents(polygon);
    
    coord_t shift_x = ((polygon_box.min.x() + polygon_box.max.x()) / 2) - center_x;
    coord_t shift_y = ((polygon_box.min.y() + polygon_box.max.y()) / 2) - center_y;
    
    for (unsigned int i = 0; i < polygon.points.size(); ++i)
    {
	polygon.points[i] -= Point(shift_x, shift_y);
    }    
}


void decimate_PolygonForSequentialSolver(const SolverConfiguration &solver_configuration,
					 const Slic3r::Polygon     &polygon,
					 Slic3r::Polygon           &decimated_polygon,
					 bool                       extra_safety)
{
    double DP_tolerance = SolverConfiguration::convert_DecimationPrecision2Tolerance(solver_configuration.decimation_precision);

    decimate_PolygonForSequentialSolver(DP_tolerance, polygon, decimated_polygon, extra_safety);
}

    
void decimate_PolygonForSequentialSolver(double                 DP_tolerance,
					 const Slic3r::Polygon &polygon,
					 Slic3r::Polygon       &decimated_polygon,
					 bool                   extra_safety)
{
    decimated_polygon = polygon;
    decimated_polygon.make_counter_clockwise();

    decimated_polygon.douglas_peucker(DP_tolerance);

    BoundingBox polygon_box = get_extents(polygon);
    
    coord_t center_x = (polygon_box.min.x() + polygon_box.max.x()) / 2;
    coord_t center_y = (polygon_box.min.y() + polygon_box.max.y()) / 2;
    
    if (decimated_polygon.points.size() >= 4)
    {
	while (true)
	{
	    grow_PolygonForContainedness(center_x, center_y, decimated_polygon);
	    	    
	    bool contains = true;
	    for (unsigned int i = 0; i < polygon.points.size(); ++i)
	    {
		if (!decimated_polygon.contains(polygon.points[i]))
		{
		    contains = false;
		    break;
		}
	    }
	    
	    if (contains)
	    {
		if (extra_safety)
		{
		    Slic3r::Polygon prefinal_polygon = decimated_polygon;

		    while (true)
		    {
			grow_PolygonForContainedness(center_x, center_y, decimated_polygon);
			Slic3r::Polygon truncated_polygon = truncate_PolygonAsSeenBySequentialSolver(SEQ_SLICER_SCALE_FACTOR, decimated_polygon);

			bool trunc_contains = true;		    
			for (unsigned int i = 0; i < prefinal_polygon.points.size(); ++i)
			{
			    if (!Slic3r::contains(truncated_polygon, prefinal_polygon.points[i], false))
			    {
				trunc_contains = false;
				break;
			    }
			}

			if (trunc_contains)
			{
			    return;
			}
		    }
		}
		return;
	    }
	}
    }
    else
    {
	BoundingBox polygon_box = get_extents(polygon);
	
	decimated_polygon = { { polygon_box.min.x(), polygon_box.min.y() },
			      { polygon_box.max.x(), polygon_box.min.y() },
			      { polygon_box.max.x(), polygon_box.max.y() },
			      { polygon_box.min.x(), polygon_box.max.y() } };
    }
    
    #ifdef DEBUG
    {
	printf("Comparison: %ld, %ld\n", polygon.points.size(), decimated_polygon.points.size());
    }
    #endif
}


void extend_PolygonConvexUnreachableZone(const SolverConfiguration          &SEQ_UNUSED(solver_configuration),
					 const Slic3r::Polygon              &polygon,
					 const std::vector<Slic3r::Polygon> &extruder_polygons,
					 std::vector<Slic3r::Polygon>       &unreachable_polygons)
{
    if (!polygon.points.empty())
    {
	Slic3r::ClipperLib::Paths paths;
	
	for (unsigned int i = 0; i < extruder_polygons.size(); ++i)
	{
	    ClipperLib::MinkowskiSum(extruder_polygons[i].points, polygon.points, paths, true);
	    
	    for (unsigned int j = 0; j < paths.size(); ++j)
	    {
		unreachable_polygons.push_back(Polygon(paths[j]));
	    }
	}
    }
}


void extend_PolygonBoxUnreachableZone(const SolverConfiguration          &SEQ_UNUSED(solver_configuration),
				      const Slic3r::Polygon              &polygon,
				      const std::vector<Slic3r::Polygon> &extruder_polygons,
				      std::vector<Slic3r::Polygon>       &unreachable_polygons)
{
    if (!polygon.points.empty())
    {
	BoundingBox polygon_box = get_extents(polygon);
    
	for (unsigned int i = 0; i < extruder_polygons.size(); ++i)
	{
	    BoundingBox extruder_box = get_extents(extruder_polygons[i]);
	
	    coord_t min_x = polygon_box.min.x() + extruder_box.min.x();
	    coord_t min_y = polygon_box.min.y() + extruder_box.min.y();
	    
	    coord_t max_x = polygon_box.max.x() + extruder_box.max.x();
	    coord_t max_y = polygon_box.max.y() + extruder_box.max.y();
	    
	    unreachable_polygons.push_back(Polygon({ { min_x, min_y },
						    { max_x, min_y },
						    { max_x, max_y },
						    { min_x, max_y } }));
	}
    }
}


void prepare_ExtruderPolygons(const SolverConfiguration                  &solver_configuration,
			      const PrinterGeometry                      &printer_geometry,
			      const ObjectToPrint                        &object_to_print,
			      std::vector<Slic3r::Polygon>               &convex_level_polygons,
			      std::vector<Slic3r::Polygon>               &box_level_polygons,
			      std::vector<std::vector<Slic3r::Polygon> > &extruder_convex_level_polygons,
			      std::vector<std::vector<Slic3r::Polygon> > &extruder_box_level_polygons,
			      bool                                        extra_safety)
{
    for (unsigned int j = 0; j < object_to_print.pgns_at_height.size(); ++j)
    {
	coord_t height = object_to_print.pgns_at_height[j].first;

	if (!object_to_print.pgns_at_height[j].second.points.empty())
	{
	    Polygon decimated_polygon;

	    if (solver_configuration.decimation_precision != SEQ_DECIMATION_PRECISION_UNDEFINED)
	    {
		decimate_PolygonForSequentialSolver(solver_configuration,
						    object_to_print.pgns_at_height[j].second,
						    decimated_polygon,
						    extra_safety);
	    }
	    else
	    {
		decimated_polygon = object_to_print.pgns_at_height[j].second;
		decimated_polygon.make_counter_clockwise();
	    }
	    
	    if (!check_PolygonSizeFitToPlate(solver_configuration, SEQ_SLICER_SCALE_FACTOR, decimated_polygon))
	    {
		#ifdef DEBUG
		{
		    printf("Object too large to fit onto plate.\n");
		}
		#endif
		throw ObjectTooLargeException("OBJECT TOO LARGE");
	    }
	    
	    if (printer_geometry.convex_heights.find(height) != printer_geometry.convex_heights.end())
	    {
		std::map<coord_t, std::vector<Polygon> >::const_iterator extruder_slice = printer_geometry.extruder_slices.find(height);
		assert(extruder_slice != printer_geometry.extruder_slices.end());
		
		convex_level_polygons.push_back(decimated_polygon);
		extruder_convex_level_polygons.push_back(extruder_slice->second);
	    }
	    else if (printer_geometry.box_heights.find(height) != printer_geometry.box_heights.end())
	    {
		std::map<coord_t, std::vector<Polygon> >::const_iterator extruder_slice = printer_geometry.extruder_slices.find(height);
		assert(extruder_slice != printer_geometry.extruder_slices.end());
		
		box_level_polygons.push_back(decimated_polygon);
		extruder_box_level_polygons.push_back(extruder_slice->second);
	    }
	    else
	    {
		throw std::runtime_error("MISMATCH BETWEEN OBJECT AND PRINTER SLICE HEIGHTS.");
	    }
	}
    }
}


void prepare_ObjectPolygons(const SolverConfiguration                        &solver_configuration,
			    const std::vector<Slic3r::Polygon>               &convex_level_polygons,
			    const std::vector<Slic3r::Polygon>               &box_level_polygons,
			    const std::vector<std::vector<Slic3r::Polygon> > &extruder_convex_level_polygons,
			    const std::vector<std::vector<Slic3r::Polygon> > &extruder_box_level_polygons,
			    Slic3r::Polygon                                  &object_polygon,
			    std::vector<Slic3r::Polygon>                     &unreachable_polygons)
{
    prepare_UnreachableZonePolygons(solver_configuration,
				    convex_level_polygons,
				    box_level_polygons,
				    extruder_convex_level_polygons,
				    extruder_box_level_polygons,
				    unreachable_polygons);

    assert(convex_level_polygons.size() >= 1);
    Polygon raw_polygon = convex_level_polygons[0];

    scaleDown_PolygonForSequentialSolver(raw_polygon,
					 object_polygon);
    object_polygon.make_counter_clockwise();    
}


void prepare_UnreachableZonePolygons(const SolverConfiguration                        &solver_configuration,
				     const Slic3r::Polygon                            &polygon,
				     const std::vector<std::vector<Slic3r::Polygon> > &extruder_convex_level_polygons,
				     const std::vector<std::vector<Slic3r::Polygon> > &extruder_box_level_polygons,					
				     std::vector<Slic3r::Polygon>                     &unreachable_polygons)
{
    std::vector<std::vector<Slic3r::Polygon> > scaled_unreachable_polygons;

    for (unsigned int i = 0; i < extruder_convex_level_polygons.size(); ++i)
    {
	std::vector<Slic3r::Polygon> scaled_level_unreachable_polygons;	
	extend_PolygonConvexUnreachableZone(solver_configuration,
					    polygon,
					    extruder_convex_level_polygons[i],
					    scaled_level_unreachable_polygons);	
	scaled_unreachable_polygons.push_back(scaled_level_unreachable_polygons);
    }

    for (unsigned int i = 0; i < extruder_box_level_polygons.size(); ++i)
    {
	std::vector<Slic3r::Polygon> scaled_level_unreachable_polygons;	
	extend_PolygonBoxUnreachableZone(solver_configuration,
					 polygon,
					 extruder_box_level_polygons[i],
					 scaled_level_unreachable_polygons);
	scaled_unreachable_polygons.push_back(scaled_level_unreachable_polygons);
    }
    scaled_unreachable_polygons = simplify_UnreachableZonePolygons(scaled_unreachable_polygons);

    for (unsigned int i = 0; i < scaled_unreachable_polygons.size(); ++i)
    {
	for (unsigned int j = 0; j < scaled_unreachable_polygons[i].size(); ++j)
	{	
	    Polygon scale_down_polygon;
	
	    scaleDown_PolygonForSequentialSolver(scaled_unreachable_polygons[i][j],
						 scale_down_polygon);
	    scale_down_polygon.make_counter_clockwise();
	    unreachable_polygons.push_back(scale_down_polygon);
	}
    }    
}


void prepare_UnreachableZonePolygons(const SolverConfiguration                        &solver_configuration,
				     const std::vector<Slic3r::Polygon>               &convex_level_polygons,
				     const std::vector<Slic3r::Polygon>               &box_level_polygons,					
				     const std::vector<std::vector<Slic3r::Polygon> > &extruder_convex_level_polygons,
				     const std::vector<std::vector<Slic3r::Polygon> > &extruder_box_level_polygons,					
				     std::vector<Slic3r::Polygon>                     &unreachable_polygons)
{
    std::vector<std::vector<Slic3r::Polygon> > scaled_unreachable_polygons;    
    assert(extruder_convex_level_polygons.size() == convex_level_polygons.size());

    for (unsigned int i = 0; i < extruder_convex_level_polygons.size(); ++i)
    {
	std::vector<Slic3r::Polygon> scaled_level_unreachable_polygons;
	extend_PolygonConvexUnreachableZone(solver_configuration,
					    convex_level_polygons[i],
					    extruder_convex_level_polygons[i],
					    scaled_level_unreachable_polygons);
	scaled_unreachable_polygons.push_back(scaled_level_unreachable_polygons);
    }
    assert(extruder_box_level_polygons.size() == box_level_polygons.size());

    for (unsigned int i = 0; i < extruder_box_level_polygons.size(); ++i)
    {
	std::vector<Slic3r::Polygon> scaled_level_unreachable_polygons;
	extend_PolygonBoxUnreachableZone(solver_configuration,
					 box_level_polygons[i],
					 extruder_box_level_polygons[i],
					 scaled_level_unreachable_polygons);
	scaled_unreachable_polygons.push_back(scaled_level_unreachable_polygons);
    }
    scaled_unreachable_polygons = simplify_UnreachableZonePolygons(scaled_unreachable_polygons);    

    for (unsigned int i = 0; i < scaled_unreachable_polygons.size(); ++i)
    {
	for (unsigned int j = 0; j < scaled_unreachable_polygons[i].size(); ++j)
	{	
	    Polygon scale_down_polygon;
	    
	    scaleDown_PolygonForSequentialSolver(scaled_unreachable_polygons[i][j],
						 scale_down_polygon);
	    scale_down_polygon.make_counter_clockwise();	
	    unreachable_polygons.push_back(scale_down_polygon);
	}
    }
}


bool check_PolygonSizeFitToPlate(const SolverConfiguration &solver_configuration, const Slic3r::Polygon &polygon)
{
    BoundingBox polygon_box = get_extents(polygon);

    if (solver_configuration.plate_bounding_polygon.points.size() == 0)
    {
	coord_t x_size = polygon_box.max.x() - polygon_box.min.x();
	if (x_size > (solver_configuration.plate_bounding_box.max.x() - solver_configuration.plate_bounding_box.min.x()))
	{
	    return false;
	}
	
	coord_t y_size = polygon_box.max.y() - polygon_box.min.y();
	if (y_size > (solver_configuration.plate_bounding_box.max.y() - solver_configuration.plate_bounding_box.min.y()))
	{
	    return false;
	}
    }
    else
    {
	BoundingBox plate_box = get_extents(solver_configuration.plate_bounding_polygon);

	coord_t x_size = polygon_box.max.x() - polygon_box.min.x();
	if (x_size > (plate_box.max.x() - plate_box.min.x()))
	{
	    return false;
	}
	
	coord_t y_size = polygon_box.max.y() - polygon_box.min.y();
	if (y_size > (plate_box.max.y() - plate_box.min.y()))
	{
	    return false;
	}
    }
    return true;
}

bool check_PolygonPositionWithinPlate(const SolverConfiguration &solver_configuration, coord_t x, coord_t y, const Slic3r::Polygon &polygon)
{
    BoundingBox polygon_box = get_extents(polygon);

    if (solver_configuration.plate_bounding_polygon.points.size() == 0)
    {
	if (x + polygon_box.min.x() < solver_configuration.plate_bounding_box.min.x() || x + polygon_box.max.x() > solver_configuration.plate_bounding_box.max.x())
	{
	    return false;
	}
	if (y + polygon_box.min.y() < solver_configuration.plate_bounding_box.min.y() || y + polygon_box.max.y() > solver_configuration.plate_bounding_box.max.y())    
	{
	    return false;
	}
    }
    else
    {
	if (   contains(solver_configuration.plate_bounding_polygon, Point(x + polygon_box.min.x(), y + polygon_box.min.y()))
	    && contains(solver_configuration.plate_bounding_polygon, Point(x + polygon_box.max.x(), y + polygon_box.min.y()))
	    && contains(solver_configuration.plate_bounding_polygon, Point(x + polygon_box.max.x(), y + polygon_box.max.y()))
	    && contains(solver_configuration.plate_bounding_polygon, Point(x + polygon_box.min.x(), y + polygon_box.max.y())))
	{
	    return true;
	}
	else
	{
	    return false;
	}
    }
    return true;
}


bool check_PolygonSizeFitToPlate(const SolverConfiguration &solver_configuration, coord_t scale_factor, const Slic3r::Polygon &polygon)
{
    BoundingBox polygon_box = get_extents(polygon);

    if (solver_configuration.plate_bounding_polygon.points.size() == 0)
    {
	coord_t x_size = polygon_box.max.x() - polygon_box.min.x();
	if (x_size > (solver_configuration.plate_bounding_box.max.x() - solver_configuration.plate_bounding_box.min.x()) * scale_factor)
	{
	    return false;
	}    
	coord_t y_size = polygon_box.max.y() - polygon_box.min.y();
	if (y_size > (solver_configuration.plate_bounding_box.max.x() - solver_configuration.plate_bounding_box.min.x()) * scale_factor)
	{
	    return false;
	}
    }
    else
    {
	BoundingBox plate_box = get_extents(solver_configuration.plate_bounding_polygon);

	coord_t x_size = polygon_box.max.x() - polygon_box.min.x();
	if (x_size > (plate_box.max.x() - plate_box.min.x()) * scale_factor)
	{
	    return false;
	}
	
	coord_t y_size = polygon_box.max.y() - polygon_box.min.y();
	if (y_size > (plate_box.max.y() - plate_box.min.y()) * scale_factor)
	{
	    return false;
	}
    }    

    return true;    
}


bool check_PolygonPositionWithinPlate(const SolverConfiguration &solver_configuration, coord_t scale_factor, coord_t x, coord_t y, const Slic3r::Polygon &polygon)
{
    BoundingBox polygon_box = get_extents(polygon);

    #ifdef DEBUG
    {
	printf("x: %d,%d\n", polygon_box.min.x() + x, polygon_box.max.x() + x);
	printf("y: %d,%d\n", polygon_box.min.y() + y, polygon_box.max.y() + y);
	printf("X: %d\n", (solver_configuration.plate_bounding_box.max.x() - solver_configuration.plate_bounding_box.min.x()) * scale_factor);
	printf("Y: %d\n", (solver_configuration.plate_bounding_box.max.y() - solver_configuration.plate_bounding_box.min.x()) * scale_factor);
    }
    #endif

    if (solver_configuration.plate_bounding_polygon.points.size() == 0)
    {
	if (x + polygon_box.min.x() < solver_configuration.plate_bounding_box.min.x() * scale_factor || x + polygon_box.max.x() > solver_configuration.plate_bounding_box.max.x() * scale_factor)
	{
	    return false;
	}
	if (y + polygon_box.min.y() < solver_configuration.plate_bounding_box.min.y() * scale_factor || y + polygon_box.max.y() > solver_configuration.plate_bounding_box.max.y() * scale_factor)
	{
	    return false;
	}
    }
    else
    {
	Polygon plate_polygon = solver_configuration.plate_bounding_polygon;

	for (unsigned int i = 0; i < plate_polygon.points.size(); ++i)
	{
	    plate_polygon.points[i] *= scale_factor;
	}    
	if (   contains(plate_polygon, Point(x + polygon_box.min.x(), y + polygon_box.min.y()))
	    && contains(plate_polygon, Point(x + polygon_box.max.x(), y + polygon_box.min.y()))
	    && contains(plate_polygon, Point(x + polygon_box.max.x(), y + polygon_box.max.y()))
	    && contains(plate_polygon, Point(x + polygon_box.min.x(), y + polygon_box.max.y())))
	{
	    return true;
	}
	else
	{
	    return false;
	}
    }    
    return true;    
}


/*----------------------------------------------------------------*/

bool check_PolygonConsumation(const std::vector<Slic3r::Polygon> &polygons, const std::vector<Slic3r::Polygon> &consumer_polygons)
{
    std::vector<Slic3r::Polygon> polygons_to_clip;
    std::vector<Slic3r::Polygon> next_polygons_to_clip;	       

    polygons_to_clip = polygons;
				    
    for (unsigned int poly_cons = 0; poly_cons < consumer_polygons.size(); ++poly_cons)
    {
	for (unsigned int clip_poly = 0; clip_poly < polygons_to_clip.size(); ++clip_poly)
	{
	    Slic3r::Polygons clip_result;
	    clip_result = diff(polygons_to_clip[clip_poly], consumer_polygons[poly_cons]);
	    
	    for (const auto& clipped_polygon: clip_result)
	    {
		next_polygons_to_clip.push_back(clipped_polygon);
	    }
	}
	polygons_to_clip = next_polygons_to_clip;
    }
    
    if (polygons_to_clip.empty())
    {
	return true;
    }
    return false;
}


std::vector<std::vector<Slic3r::Polygon> > simplify_UnreachableZonePolygons(const std::vector<std::vector<Slic3r::Polygon> > &unreachable_polygons)
{
    std::vector<std::vector<Slic3r::Polygon> > simplified_unreachable_polygons;

    for (unsigned int i = 0; i < unreachable_polygons.size(); ++i)
    {
	bool consumed = false;
	
	for (unsigned int j = 0; j < unreachable_polygons.size(); ++j)
	{
	    if (i != j)
	    {
		double area_i = calc_PolygonUnreachableZoneArea(unreachable_polygons[i]);
		double area_j = calc_PolygonUnreachableZoneArea(unreachable_polygons[j]);

		if (area_j > area_i)
		{
		    if (check_PolygonConsumation(unreachable_polygons[i], unreachable_polygons[j]))
		    {
			#ifdef DEBUG
			{
			    printf("Consumed: %d vs %d\n", i, j);
			}
			#endif
			consumed = true;
			break;
		    }
		}
	    }
	}
	if (!consumed)
	{	    
	    simplified_unreachable_polygons.push_back(unreachable_polygons[i]);
	}
    }

    return simplified_unreachable_polygons;
}


void glue_LowObjects(std::vector<SolvableObject> &solvable_objects)
{
    int low = 0;
	
    for (unsigned int i = 0; i < solvable_objects.size(); ++i)
    {	
	double polygon_area = calc_PolygonArea(solvable_objects[i].polygon);
	double unreachable_area = calc_PolygonUnreachableZoneArea(solvable_objects[i].polygon, solvable_objects[i].unreachable_polygons);	

	if (2 * polygon_area > unreachable_area)
	{
	    if (++low >= 2)
	    {
		assert(i > 0);
		solvable_objects[i-1].lepox_to_next = true;
		low = 1;
	    }
	}
	else
	{
	    low = 0;
	}
    }
}


/*----------------------------------------------------------------*/

double calc_PolygonArea(const Slic3r::Polygon &polygon)
{
    Polygons overlapping_polygons;

    overlapping_polygons.push_back(polygon);
    ExPolygons union_polygons = union_ex(overlapping_polygons);    

    double area = 0;
    for (const auto& union_polygon: union_polygons)
    {
	area += union_polygon.area();
    }

    return area;    
}



double calc_PolygonUnreachableZoneArea(const std::vector<Slic3r::Polygon> &unreachable_polygons)
{
    Polygons overlapping_polygons;

    for (const auto& unreachable_polygon: unreachable_polygons)
    {
	overlapping_polygons.push_back(unreachable_polygon);
    }
    ExPolygons union_polygons = union_ex(overlapping_polygons);

    double area = 0;
    for (const auto& union_polygon: union_polygons)
    {
	area += union_polygon.area();
    }

    return area;
}


double calc_PolygonUnreachableZoneArea(const Slic3r::Polygon              &polygon,
				       const std::vector<Slic3r::Polygon> &unreachable_polygons)
{
    Polygons overlapping_polygons;

    overlapping_polygons.push_back(polygon);
    for (const auto& unreachable_polygon: unreachable_polygons)
    {
	overlapping_polygons.push_back(unreachable_polygon);
    }
    ExPolygons union_polygons = union_ex(overlapping_polygons);

    double area = 0;
    for (const auto& union_polygon: union_polygons)
    {
	area += union_polygon.area();
    }

    return area;
}


double calc_PolygonArea(const std::vector<Slic3r::Polygon> &polygons)
{
    double area = 0;

    for (const auto &polygon: polygons)
    {
	area += calc_PolygonArea(polygon);
    }
    return area;
}


double calc_PolygonArea(const std::vector<int>             &fixed,
			const std::vector<int>             &undecided,
			const std::vector<Slic3r::Polygon> &polygons)
{
    double area = 0;

    for (unsigned int i = 0; i < fixed.size(); ++i)
    {
	area += calc_PolygonArea(polygons[i]);
    }
    for (unsigned int i = 0; i < undecided.size(); ++i)
    {
	area += calc_PolygonArea(polygons[i]);
    }
    
    return area;    
}


double calc_PolygonUnreachableZoneArea(const std::vector<Slic3r::Polygon>               &polygons,
				       const std::vector<std::vector<Slic3r::Polygon> > &unreachable_polygons)
{
    assert(polygons.size() == unreachable_polygons.size());
    double area = 0;
    
    for (unsigned int i = 0; i < polygons.size(); ++i)
    {
	area += calc_PolygonUnreachableZoneArea(polygons[i], unreachable_polygons[i]);
    }

    return area;
}


/*----------------------------------------------------------------*/

} // namespace Sequential

