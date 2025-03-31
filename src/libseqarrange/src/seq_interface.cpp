/*================================================================*/
/*
 * Author:  Pavel Surynek, 2023 - 2025
 * Company: Prusa Research
 *
 * File:    seq_interface.cpp
 *
 * Interface of the sequential printing SMT model for Prusa Slic3r
 */
/*================================================================*/

#include "seq_defs.hpp"

#include "seq_sequential.hpp"
#include "seq_preprocess.hpp"
#include "libseqarrange/seq_interface.hpp"


/*----------------------------------------------------------------*/

namespace Sequential
{


    

/*----------------------------------------------------------------*/

const int SEQ_OBJECT_GROUP_SIZE                   =  4;
const int SEQ_FIXED_OBJECT_GROUPING_LIMIT         = 64;
const int SEQ_SCHEDULING_TEMPORAL_SPREAD          = 16;

const int SEQ_BOUNDING_BOX_SIZE_OPTIMIZATION_STEP =  4;
const int SEQ_MINIMUM_BOUNDING_BOX_SIZE           = 16;

const int SEQ_MAX_REFINES                         =  2;


/*----------------------------------------------------------------*/
    
enum PrinterType
{
    SEQ_PRINTER_TYPE_UNDEFINED,
    SEQ_PRINTER_TYPE_PRUSA_MINI,    
    SEQ_PRINTER_TYPE_PRUSA_MK3S,
    SEQ_PRINTER_TYPE_PRUSA_MK4,
    SEQ_PRINTER_TYPE_PRUSA_XL
};
    
    
const int SEQ_PRUSA_MK3S_X_SIZE = 2500;
const int SEQ_PRUSA_MK3S_Y_SIZE = 2100;    
    
const coord_t SEQ_PRUSA_MK3S_NOZZLE_LEVEL   = 0;
const coord_t SEQ_PRUSA_MK3S_EXTRUDER_LEVEL = 2000000;
const coord_t SEQ_PRUSA_MK3S_HOSE_LEVEL     = 18000000;
const coord_t SEQ_PRUSA_MK3S_GANTRY_LEVEL   = 26000000;
  
const int SEQ_PRUSA_MK4_X_SIZE = 2500;
const int SEQ_PRUSA_MK4_Y_SIZE = 2100;    

const coord_t SEQ_PRUSA_MK4_NOZZLE_LEVEL   = 0;
const coord_t SEQ_PRUSA_MK4_EXTRUDER_LEVEL = 2000000;
const coord_t SEQ_PRUSA_MK4_HOSE_LEVEL     = 18000000;
const coord_t SEQ_PRUSA_MK4_GANTRY_LEVEL   = 26000000;

const int SEQ_PRUSA_XL_X_SIZE = 3600;
const int SEQ_PRUSA_XL_Y_SIZE = 3600;    
    
const coord_t SEQ_PRUSA_XL_NOZZLE_LEVEL   = 0;
const coord_t SEQ_PRUSA_XL_EXTRUDER_LEVEL = 2000000;
const coord_t SEQ_PRUSA_XL_HOSE_LEVEL     = 18000000;
const coord_t SEQ_PRUSA_XL_GANTRY_LEVEL   = 26000000;        


/*----------------------------------------------------------------*/
    
bool PrinterGeometry::convert_Geometry2PlateBounds(Slic3r::BoundingBox &plate_bounding_box, Slic3r::Polygon &plate_bounding_polygon) const
{
    BoundingBox plate_box = get_extents(plate);
    
    if (fabs(plate.area() - plate_box.polygon().area()) > EPSILON)
    {
	for (unsigned int i = 0; i < plate.points.size(); ++i)
	{
	    plate_bounding_polygon.points.insert(plate_bounding_polygon.points.begin() + i, Point(plate.points[i].x() / SEQ_SLICER_SCALE_FACTOR,
												  plate.points[i].y() / SEQ_SLICER_SCALE_FACTOR));
	}
	plate_bounding_polygon.make_counter_clockwise();    	
	return false;
    }
    else
    {	
	plate_bounding_box = BoundingBox({ plate_box.min.x() / SEQ_SLICER_SCALE_FACTOR, plate_box.min.y() / SEQ_SLICER_SCALE_FACTOR },
					 { plate_box.max.x() / SEQ_SLICER_SCALE_FACTOR, plate_box.max.y() / SEQ_SLICER_SCALE_FACTOR });

	return true;	
    }
}
    
    
/*----------------------------------------------------------------*/
    
SolverConfiguration::SolverConfiguration()
    : bounding_box_size_optimization_step(SEQ_BOUNDING_BOX_SIZE_OPTIMIZATION_STEP)
    , minimum_bounding_box_size(SEQ_MINIMUM_BOUNDING_BOX_SIZE)
    , max_refines(SEQ_MAX_REFINES)
    , object_group_size(SEQ_OBJECT_GROUP_SIZE)
    , fixed_object_grouping_limit(SEQ_FIXED_OBJECT_GROUPING_LIMIT)
    , temporal_spread(SEQ_SCHEDULING_TEMPORAL_SPREAD)
    , decimation_precision(SEQ_DECIMATION_PRECISION_LOW)
    , optimization_timeout(SEQ_Z3_SOLVER_TIMEOUT)
{
	/* nothing */
}


SolverConfiguration::SolverConfiguration(const PrinterGeometry &printer_geometry)
    : bounding_box_size_optimization_step(SEQ_BOUNDING_BOX_SIZE_OPTIMIZATION_STEP)
    , minimum_bounding_box_size(SEQ_MINIMUM_BOUNDING_BOX_SIZE)
    , max_refines(SEQ_MAX_REFINES)      
    , object_group_size(SEQ_OBJECT_GROUP_SIZE)
    , fixed_object_grouping_limit(SEQ_FIXED_OBJECT_GROUPING_LIMIT)
    , temporal_spread(SEQ_SCHEDULING_TEMPORAL_SPREAD)
    , decimation_precision(SEQ_DECIMATION_PRECISION_LOW)
    , optimization_timeout(SEQ_Z3_SOLVER_TIMEOUT)
{
    setup(printer_geometry);
}

    
double SolverConfiguration::convert_DecimationPrecision2Tolerance(DecimationPrecision decimation_precision)
{
    switch (decimation_precision)
    {
    case SEQ_DECIMATION_PRECISION_UNDEFINED:
    {
	return SEQ_DECIMATION_TOLERANCE_VALUE_UNDEFINED;	    
	break;
    }
    case SEQ_DECIMATION_PRECISION_LOW:
    {
	return SEQ_DECIMATION_TOLERANCE_VALUE_HIGH;
	break;
    }	
    case SEQ_DECIMATION_PRECISION_HIGH:
    {
	return SEQ_DECIMATION_TOLERANCE_VALUE_LOW;
	break;
    }
    default:
    {
	break;
    }
    }
    return SEQ_DECIMATION_TOLERANCE_VALUE_UNDEFINED;	
}

       
void SolverConfiguration::setup(const PrinterGeometry &printer_geometry)
{
    printer_geometry.convert_Geometry2PlateBounds(plate_bounding_box, plate_bounding_polygon);
}

    
void SolverConfiguration::set_DecimationPrecision(DecimationPrecision _decimation_precision)
{
    decimation_precision = _decimation_precision;
}

    
void SolverConfiguration::set_ObjectGroupSize(int _object_group_size)
{
    object_group_size = _object_group_size;
}
    
    
/*----------------------------------------------------------------*/


bool check_ScheduledObjectsForSequentialPrintability(const SolverConfiguration         &solver_configuration,
						     const PrinterGeometry             &printer_geometry,
						     const std::vector<ObjectToPrint>  &objects_to_print,
						     const std::vector<ScheduledPlate> &scheduled_plates)
{
    if (check_ScheduledObjectsForSequentialConflict(solver_configuration,
						    printer_geometry,
						    objects_to_print,
						    scheduled_plates))
    {
	return false;
    }
    return true;
}
    

std::optional<std::pair<int, int> > check_ScheduledObjectsForSequentialConflict(const SolverConfiguration         &solver_configuration,
										const PrinterGeometry             &printer_geometry,
										const std::vector<ObjectToPrint>  &objects_to_print,
										const std::vector<ScheduledPlate> &scheduled_plates)
{
    std::vector<Slic3r::Polygon> polygons;
    std::vector<std::vector<Slic3r::Polygon> > unreachable_polygons;

    std::map<int, int> flat_index_map;

    for (unsigned int i = 0; i < objects_to_print.size(); ++i)
    {
	std::vector<Slic3r::Polygon> convex_level_polygons;	    
	std::vector<Slic3r::Polygon> box_level_polygons;

	std::vector<std::vector<Slic3r::Polygon> > extruder_convex_level_polygons;	    
	std::vector<std::vector<Slic3r::Polygon> > extruder_box_level_polygons;	
	
	std::vector<Slic3r::Polygon> scale_down_unreachable_polygons;		

	flat_index_map[objects_to_print[i].id] = i;

	Polygon scale_down_object_polygon;
	    
	prepare_ExtruderPolygons(solver_configuration,
				 printer_geometry,
				 objects_to_print[i],
				 convex_level_polygons,
				 box_level_polygons,
				 extruder_convex_level_polygons,
				 extruder_box_level_polygons,
				 false);

	prepare_ObjectPolygons(solver_configuration,
			       convex_level_polygons,
			       box_level_polygons,
			       extruder_convex_level_polygons,
			       extruder_box_level_polygons,
			       scale_down_object_polygon,
			       scale_down_unreachable_polygons);
	
	unreachable_polygons.push_back(scale_down_unreachable_polygons);
	polygons.push_back(scale_down_object_polygon);       
    }
    
    for (const auto& scheduled_plate: scheduled_plates)
    {
	int time = SEQ_GROUND_PRESENCE_TIME;
	    
	std::vector<Slic3r::Polygon> plate_polygons;
	std::vector<std::vector<Slic3r::Polygon> > plate_unreachable_polygons;

	std::vector<Rational> dec_values_X;
	std::vector<Rational> dec_values_Y;
	std::vector<Rational> dec_values_T;
	
	for (const auto& scheduled_object: scheduled_plate.scheduled_objects)
	{   
	    const auto& flat_index = flat_index_map.find(scheduled_object.id)->second;

	    assert(!objects_to_print[flat_index].pgns_at_height.empty());

	    /*
	    if (!check_PolygonPositionWithinPlate(solver_configuration,
						  SEQ_SLICER_SCALE_FACTOR,
						  scheduled_object.x,
						  scheduled_object.y,
						  objects_to_print[flat_index].pgns_at_height[0].second))
	    {
                #ifdef DEBUG
		{
		    printf("Object placed outside plate.\n");
		}
	        #endif
		return false;
	    }
	    */

	    plate_polygons.push_back(polygons[flat_index]);
	    plate_unreachable_polygons.push_back(unreachable_polygons[flat_index]);

	    dec_values_X.push_back(scaleDown_CoordinateForSequentialSolver(scheduled_object.x));
	    dec_values_Y.push_back(scaleDown_CoordinateForSequentialSolver(scheduled_object.y));

	    time += 2 * solver_configuration.temporal_spread * solver_configuration.object_group_size;	    
	    dec_values_T.push_back(Rational(time));
	}

	#ifdef DEBUG
	{
	    printf("Point check ...\n");
	}
	#endif       
	
	if (auto conflict = check_PointsOutsidePolygons(dec_values_X,
							dec_values_Y,
							dec_values_T,
							plate_polygons,
							plate_unreachable_polygons))
	{
	    return std::pair<int, int>(scheduled_plate.scheduled_objects[conflict.value().first].id, scheduled_plate.scheduled_objects[conflict.value().second].id);
	}
	#ifdef DEBUG
	{
	    printf("Point check ... finished\n");
	}
	#endif 

	#ifdef DEBUG
	{
	    printf("Line check ...\n");
	}
	#endif

	if (auto conflict = check_PolygonLineIntersections(dec_values_X,
							    dec_values_Y,
							    dec_values_T,
							    plate_polygons,
							    plate_unreachable_polygons))
	{
	    return std::pair<int, int>(scheduled_plate.scheduled_objects[conflict.value().first].id, scheduled_plate.scheduled_objects[conflict.value().second].id);
	}
	#ifdef DEBUG
	{
	    printf("Line check ... finished\n");
	}
	#endif
    }
    #ifdef DEBUG
    {
	printf("Seems to be printable (you can try physically).\n");
    }
    #endif
    
    return {};    
}


/*----------------------------------------------------------------*/

std::vector<ScheduledPlate> schedule_ObjectsForSequentialPrint(const SolverConfiguration        &solver_configuration,
							       const PrinterGeometry            &printer_geometry,
							       const std::vector<ObjectToPrint> &objects_to_print,
							       std::function<void(int)>          progress_callback)
{
    std::vector<ScheduledPlate> scheduled_plates;

    schedule_ObjectsForSequentialPrint(solver_configuration,
				       printer_geometry,
				       objects_to_print,
				       scheduled_plates,
				       progress_callback);
    return scheduled_plates;
}


bool is_scheduled(int i, const std::vector<int> &decided_polygons)
{
    for (unsigned int j = 0; j < decided_polygons.size(); ++j)
    {
	if (decided_polygons[j] == i)
	{
	    return true;
	}
    }
    return false;
}


void schedule_ObjectsForSequentialPrint(const SolverConfiguration        &solver_configuration,
					const PrinterGeometry            &printer_geometry,
					const std::vector<ObjectToPrint> &objects_to_print,
					std::vector<ScheduledPlate>      &scheduled_plates,
					std::function<void(int)>          progress_callback)
{
    #ifdef PROFILE
    clock_t start, finish;
    start = clock();	
    #endif

    #ifdef DEBUG
    {
	printf("Sequential scheduling/arranging ...\n");
    }
    #endif

    std::map<int, int> original_index_map;
    std::vector<SolvableObject> solvable_objects;

    #ifdef DEBUG
    {
	printf("  Preparing objects ...\n");
    }
    #endif
    
    for (unsigned int i = 0; i < objects_to_print.size(); ++i)
    {
	std::vector<Slic3r::Polygon> convex_level_polygons;	    
	std::vector<Slic3r::Polygon> box_level_polygons;

	std::vector<std::vector<Slic3r::Polygon> > extruder_convex_level_polygons;	    
	std::vector<std::vector<Slic3r::Polygon> > extruder_box_level_polygons;	

	SolvableObject solvable_object;
	original_index_map[i] = objects_to_print[i].id;
	    
	prepare_ExtruderPolygons(solver_configuration,
				 printer_geometry,
				 objects_to_print[i],
				 convex_level_polygons,
				 box_level_polygons,
				 extruder_convex_level_polygons,
				 extruder_box_level_polygons,
				 true);

	prepare_ObjectPolygons(solver_configuration,
			       convex_level_polygons,
			       box_level_polygons,
			       extruder_convex_level_polygons,
			       extruder_box_level_polygons,
			       solvable_object.polygon,
			       solvable_object.unreachable_polygons);

	solvable_object.id = objects_to_print[i].id;
	solvable_object.lepox_to_next = objects_to_print[i].glued_to_next;

    	solvable_objects.push_back(solvable_object);
    }

    std::vector<int> remaining_polygons;
    std::vector<int> decided_polygons;

    std::vector<Rational> poly_positions_X;
    std::vector<Rational> poly_positions_Y;
    std::vector<Rational> times_T;

    #ifdef DEBUG
    {
	printf("  Preparing objects ... finished\n");
    }
    #endif

    int progress_object_phases_done = 0;
    int progress_object_phases_total = SEQ_MAKE_EXTRA_PROGRESS((objects_to_print.size() * SEQ_PROGRESS_PHASES_PER_OBJECT));

    bool trans_bed_lepox = false;
    
    do
    {
	ScheduledPlate scheduled_plate;
		
	decided_polygons.clear();
	remaining_polygons.clear();

	#ifdef DEBUG
	{
	    printf("  Object scheduling/arranging ...\n");
	}
	#endif
	
	bool optimized;

	optimized = optimize_SubglobalConsequentialPolygonNonoverlappingBinaryCentered(solver_configuration,
										       poly_positions_X,
										       poly_positions_Y,
										       times_T,
										       solvable_objects,
										       trans_bed_lepox,
										       decided_polygons,
										       remaining_polygons,
										       progress_object_phases_done,
										       progress_object_phases_total,
										       progress_callback);
	
	#ifdef DEBUG
	{
	    printf("  Object scheduling/arranging ... finished\n");
	}
	#endif
	
	if (optimized)
	{	
	    #ifdef DEBUG
	    {	    
		printf("Polygon positions:\n");
		for (unsigned int i = 0; i < decided_polygons.size(); ++i)
		{
		    printf("  [ID:%d,RID:%d] x:%.3f, y:%.3f (t:%.3f)\n",
			   original_index_map[decided_polygons[i]],
			   decided_polygons[i],
			   poly_positions_X[decided_polygons[i]].as_double(),
			   poly_positions_Y[decided_polygons[i]].as_double(),
			   times_T[decided_polygons[i]].as_double());
		}
		printf("Remaining polygons: %ld\n", remaining_polygons.size());
		for (unsigned int i = 0; i < remaining_polygons.size(); ++i)
		{
		    printf("  ID:%d\n", original_index_map[remaining_polygons[i]]);
		}
	    }
	    #endif

	    bool split = false;
	    for (unsigned int i = 0; i < decided_polygons.size(); ++i)
	    {
		if (solvable_objects[i].lepox_to_next && !is_scheduled(i + 1, decided_polygons))
		{
		    split = true;
		    break;
		}
	    }
	    if (split)
	    {
		trans_bed_lepox = true;
		#ifdef DEBUG
		{
		    printf("Lopoxed group split, implies trans-bed lepox\n");
		}
		#endif
	    }
	    else
	    {
		trans_bed_lepox = false;
	    }
	    std::map<double, int> scheduled_polygons;
	    for (unsigned int i = 0; i < decided_polygons.size(); ++i)
	    {		
		scheduled_polygons.insert(std::pair<double, int>(times_T[decided_polygons[i]].as_double(), decided_polygons[i]));
	    }

	    for (const auto& scheduled_polygon: scheduled_polygons)
	    {
		coord_t X, Y;
		
		scaleUp_PositionForSlicer(poly_positions_X[scheduled_polygon.second],
					  poly_positions_Y[scheduled_polygon.second],
					  X,
					  Y);
		const auto& original_index = original_index_map.find(scheduled_polygon.second);

		scheduled_plate.scheduled_objects.push_back(ScheduledObject(original_index->second, X, Y));
	    }
	}
	else
	{
	    #ifdef DEBUG
	    {	    	    
		printf("Polygon sequential schedule optimization FAILED.\n");
	    }
	    #endif

	    throw std::runtime_error("COMPLETE SCHEDULING FAILURE (UNABLE TO SCHEDULE EVEN SINGLE OBJECT)");
	}
	#ifdef PROFILE
	{
	    finish = clock();
	}
	#endif
	
        #ifdef PROFILE
	{	    
	    printf("Intermediate CPU time: %.3f\n", (finish - start) / (double)CLOCKS_PER_SEC);
	}
	#endif

	std::vector<SolvableObject> next_solvable_objects;

	for (unsigned int i = 0; i < remaining_polygons.size(); ++i)
	{
	    next_solvable_objects.push_back(solvable_objects[remaining_polygons[i]]);	    	    
	}	
	solvable_objects = next_solvable_objects;

	std::map<int, int> next_original_index_map;

	for (unsigned int index = 0; index < solvable_objects.size(); ++index)
	{
	    next_original_index_map[index] = original_index_map[remaining_polygons[index]];
	}
	original_index_map = next_original_index_map;

	scheduled_plates.push_back(scheduled_plate);
    }
    while (!remaining_polygons.empty());

    progress_callback(SEQ_PROGRESS_RANGE);

    #ifdef PROFILE
    {
	finish = clock();
    }
    #endif

    #ifdef DEBUG
    {	    
	printf("Sequential scheduling/arranging ... finished\n");
    }
    #endif
    
    #ifdef PROFILE
    {
	printf("Total CPU time: %.3f\n", (finish - start) / (double)CLOCKS_PER_SEC);
    }
    #endif   
}

    
/*----------------------------------------------------------------*/
            
int schedule_ObjectsForSequentialPrint(const SolverConfiguration        &solver_configuration,
				       const std::vector<ObjectToPrint> &objects_to_print,
				       std::vector<ScheduledPlate>      &scheduled_plates,
				       std::function<void(int)>          progress_callback)
{
    #ifdef PROFILE
    clock_t start, finish;
    start = clock();	
    #endif

    #ifdef DEBUG
    {
	printf("Sequential scheduling/arranging ...\n");
    }
    #endif

    PrinterType printer_type = SEQ_PRINTER_TYPE_PRUSA_MK3S;

    std::vector<SolvableObject> solvable_objects;
    std::map<int, int> original_index_map;    

    #ifdef DEBUG
    {
	printf("  Preparing objects ...\n");
    }
    #endif
    
    for (unsigned int i = 0; i < objects_to_print.size(); ++i)
    {
	Polygon nozzle_polygon;
	Polygon extruder_polygon;
	Polygon hose_polygon;
	Polygon gantry_polygon;

	original_index_map[i] = objects_to_print[i].id;
	
	for (unsigned int j = 0; j < objects_to_print[i].pgns_at_height.size(); ++j)
	{
	    coord_t height = objects_to_print[i].pgns_at_height[j].first;

	    if (!objects_to_print[i].pgns_at_height[j].second.points.empty())
	    {
		Polygon decimated_polygon;

		if (solver_configuration.decimation_precision != SEQ_DECIMATION_PRECISION_UNDEFINED)
		{
		    decimate_PolygonForSequentialSolver(solver_configuration,
							objects_to_print[i].pgns_at_height[j].second,
							decimated_polygon,
							true);
		}
		else
		{
		    decimated_polygon = objects_to_print[i].pgns_at_height[j].second;
		    decimated_polygon.make_counter_clockwise();
		}
		if (!check_PolygonSizeFitToPlate(solver_configuration, SEQ_SLICER_SCALE_FACTOR, decimated_polygon))
		{
		    #ifdef DEBUG
		    {
			printf("Object too large to fit onto plate [ID:%d RID:%d].\n", original_index_map[i], i);
		    }
		    #endif		    
		    return -1;
		}		

		switch (printer_type)
		{
		case SEQ_PRINTER_TYPE_PRUSA_MK3S:
		{
		    switch (height)
		    {
		    case SEQ_PRUSA_MK3S_NOZZLE_LEVEL:    // nozzle
		    {
			nozzle_polygon = decimated_polygon;		    
			break;
		    }
		    case SEQ_PRUSA_MK3S_EXTRUDER_LEVEL:  // extruder
		    {
			extruder_polygon = decimated_polygon;		    		    
			break;
		    }
		    case SEQ_PRUSA_MK3S_HOSE_LEVEL:      // hose
		    {
			hose_polygon = decimated_polygon;		    		    		    
			break;
		    }
		    case SEQ_PRUSA_MK3S_GANTRY_LEVEL:    // gantry
		    {
			gantry_polygon = decimated_polygon;
			break;
		    }
		    default:
		    {
			throw std::runtime_error("UNSUPPORTED POLYGON HEIGHT");		    
			break;
		    }
		    }
		    break;
		}
		case SEQ_PRINTER_TYPE_PRUSA_MK4:
		{
		    switch (height)
		    {
		    case SEQ_PRUSA_MK4_NOZZLE_LEVEL:    // nozzle
		    {
			nozzle_polygon = decimated_polygon;		    
			break;
		    }
		    case SEQ_PRUSA_MK4_EXTRUDER_LEVEL:  // extruder
		    {
			extruder_polygon = decimated_polygon;		    		    
			break;
		    }
		    case SEQ_PRUSA_MK4_HOSE_LEVEL:      // hose
		    {
			hose_polygon = decimated_polygon;		    		    		    
			break;
		    }
		    case SEQ_PRUSA_MK4_GANTRY_LEVEL:    // gantry
		    {
			gantry_polygon = decimated_polygon;
			break;
		    }
		    default:
		    {
			throw std::runtime_error("UNSUPPORTED POLYGON HEIGHT");		    
			break;
		    }
		    }
		    break;
		}	       
		case SEQ_PRINTER_TYPE_PRUSA_XL:
		{
		    switch (height)
		    {
		    case SEQ_PRUSA_XL_NOZZLE_LEVEL:    // nozzle
		    {
			nozzle_polygon = decimated_polygon;		    
			break;
		    }
		    case SEQ_PRUSA_XL_EXTRUDER_LEVEL:  // extruder
		    {
			extruder_polygon = decimated_polygon;		    		    
			break;
		    }
		    case SEQ_PRUSA_XL_HOSE_LEVEL:      // hose (no hose in XL)
		    {
			hose_polygon = decimated_polygon;		    		    		    
			break;
		    }
		    case SEQ_PRUSA_XL_GANTRY_LEVEL:    // gantry
		    {
			gantry_polygon = decimated_polygon;
			break;
		    }
		    default:
		    {
			throw std::runtime_error("UNSUPPORTED POLYGON HEIGHT");		    
			break;
		    }
		    }
		    break;
		}	      
		default:
		{
		    throw std::runtime_error("UNSUPPORTED PRINTER TYPE");		    
		    break;		    
		}
		}
	    }
	}
	SolvableObject solvable_object;
	
	scaleDown_PolygonForSequentialSolver(nozzle_polygon, solvable_object.polygon);

	std::vector<Slic3r::Polygon> convex_level_polygons;
	convex_level_polygons.push_back(nozzle_polygon);
	convex_level_polygons.push_back(extruder_polygon);	
	std::vector<Slic3r::Polygon> box_level_polygons;
	box_level_polygons.push_back(hose_polygon);
	box_level_polygons.push_back(gantry_polygon);		
	
	std::vector<Slic3r::Polygon> scale_down_unreachable_polygons;

	switch (printer_type)
	{
	case SEQ_PRINTER_TYPE_PRUSA_MK3S:
	{
	    prepare_UnreachableZonePolygons(solver_configuration,
					    convex_level_polygons,
					    box_level_polygons,
					    SEQ_UNREACHABLE_POLYGON_CONVEX_LEVELS_MK3S,
					    SEQ_UNREACHABLE_POLYGON_BOX_LEVELS_MK3S,
					    solvable_object.unreachable_polygons);
	    break;
	}
	case SEQ_PRINTER_TYPE_PRUSA_MK4:
	{
	    prepare_UnreachableZonePolygons(solver_configuration,
					    convex_level_polygons,
					    box_level_polygons,
					    SEQ_UNREACHABLE_POLYGON_CONVEX_LEVELS_MK4,
					    SEQ_UNREACHABLE_POLYGON_BOX_LEVELS_MK4,
					    solvable_object.unreachable_polygons);					    
	    break;
	}
	case SEQ_PRINTER_TYPE_PRUSA_XL:
	{
	    prepare_UnreachableZonePolygons(solver_configuration,
					    convex_level_polygons,
					    box_level_polygons,
					    SEQ_UNREACHABLE_POLYGON_CONVEX_LEVELS_XL,
					    SEQ_UNREACHABLE_POLYGON_BOX_LEVELS_XL,
					    solvable_object.unreachable_polygons);
	    break;
	}
	default:
	{
	    throw std::runtime_error("UNSUPPORTED PRINTER TYPE");		    
	    break;
	}	
	}
	
	solvable_object.id = objects_to_print[i].id;
	solvable_object.lepox_to_next = objects_to_print[i].glued_to_next;

	solvable_objects.push_back(solvable_object);
    }

    std::vector<int> remaining_polygons;
    std::vector<int> decided_polygons;

    /*
    for (unsigned int index = 0; index < solvable_objects.size(); ++index)
    {
	polygon_index_map.push_back(index);
    }
    */
    
    std::vector<Rational> poly_positions_X;
    std::vector<Rational> poly_positions_Y;
    std::vector<Rational> times_T;

    #ifdef DEBUG
    {
	printf("  Preparing objects ... finished\n");
    }
    #endif

    int progress_object_phases_done = 0;
    int progress_object_phases_total = SEQ_MAKE_EXTRA_PROGRESS((objects_to_print.size() * SEQ_PROGRESS_PHASES_PER_OBJECT));

    bool trans_bed_lepox = false;

    do
    {
	ScheduledPlate scheduled_plate;
		
	decided_polygons.clear();
	remaining_polygons.clear();

	#ifdef DEBUG
	{
	    printf("  Object scheduling/arranging ...\n");
	}
	#endif
	
	bool optimized;
	
	optimized = optimize_SubglobalConsequentialPolygonNonoverlappingBinaryCentered(solver_configuration,
										       poly_positions_X,
										       poly_positions_Y,
										       times_T,
										       solvable_objects,
										       trans_bed_lepox,
										       decided_polygons,
										       remaining_polygons,
										       progress_object_phases_done,
										       progress_object_phases_total,
										       progress_callback);	
	
	#ifdef DEBUG
	{
	    printf("  Object scheduling/arranging ... finished\n");
	}
	#endif
	
	if (optimized)
	{
	    #ifdef DEBUG
	    {	    
		printf("Polygon positions:\n");
		for (unsigned int i = 0; i < decided_polygons.size(); ++i)
		{
		    printf("  [ID:%d,RID:%d] x:%.3f, y:%.3f (t:%.3f)\n",
			   original_index_map[decided_polygons[i]],
			   decided_polygons[i],
			   poly_positions_X[decided_polygons[i]].as_double(),
			   poly_positions_Y[decided_polygons[i]].as_double(),
			   times_T[decided_polygons[i]].as_double());
		}
		printf("Remaining polygons: %ld\n", remaining_polygons.size());
		for (unsigned int i = 0; i < remaining_polygons.size(); ++i)
		{
		    printf("  ID:%d\n", original_index_map[remaining_polygons[i]]);
		}
	    }
	    #endif

	    bool split = false;
	    for (unsigned int i = 0; i < decided_polygons.size(); ++i)
	    {
		if (solvable_objects[i].lepox_to_next && !is_scheduled(i + 1, decided_polygons))
		{
		    split = true;
		    break;
		}
	    }
	    if (split)
	    {
		trans_bed_lepox = true;
		#ifdef DEBUG
		{
		    printf("Lopoxed group split, implies trans-bed lepox\n");
		}
		#endif
	    }
	    else
	    {
		trans_bed_lepox = false;
	    }
	    std::map<double, int> scheduled_polygons;
	    for (unsigned int i = 0; i < decided_polygons.size(); ++i)
	    {
		scheduled_polygons.insert(std::pair<double, int>(times_T[decided_polygons[i]].as_double(), decided_polygons[i]));
	    }

	    for (const auto& scheduled_polygon: scheduled_polygons)
	    {
		coord_t X, Y;
		
		scaleUp_PositionForSlicer(poly_positions_X[scheduled_polygon.second],
					  poly_positions_Y[scheduled_polygon.second],
					  X,
					  Y);
		const auto& original_index = original_index_map.find(scheduled_polygon.second);

		scheduled_plate.scheduled_objects.push_back(ScheduledObject(original_index->second, X, Y));
	    }
	}
	else
	{
	    #ifdef DEBUG
	    {	    	    
		printf("Polygon sequential schedule optimization FAILED.\n");
	    }
	    #endif
	    return -2;
	}
	#ifdef PROFILE
	{
	    finish = clock();
	}
	#endif
	
        #ifdef PROFILE
	{	    
	    printf("Intermediate CPU time: %.3f\n", (finish - start) / (double)CLOCKS_PER_SEC);
	}
	#endif

	std::vector<SolvableObject> next_solvable_objects;

	for (unsigned int i = 0; i < remaining_polygons.size(); ++i)
	{
	    next_solvable_objects.push_back(solvable_objects[i]);
	}	
	solvable_objects = next_solvable_objects;
	std::map<int, int> next_original_index_map;

	for (unsigned int index = 0; index < solvable_objects.size(); ++index)
	{
	    next_original_index_map[index] = original_index_map[remaining_polygons[index]];
	}
	original_index_map = next_original_index_map;

	scheduled_plates.push_back(scheduled_plate);
    }
    while (!remaining_polygons.empty());

    progress_callback(SEQ_PROGRESS_RANGE);    

    #ifdef PROFILE
    {
	finish = clock();
    }
    #endif

    #ifdef DEBUG
    {	    
	printf("Sequential scheduling/arranging ... finished\n");
    }
    #endif
    
    #ifdef PROFILE
    {
	printf("Total CPU time: %.3f\n", (finish - start) / (double)CLOCKS_PER_SEC);
    }
    #endif
    
    return 0;
}


void setup_ExtruderUnreachableZones(const SolverConfiguration                 &solver_configuration,
				   std::vector<std::vector<Slic3r::Polygon> > &convex_unreachable_zones,
				   std::vector<std::vector<Slic3r::Polygon> > &box_unreachable_zones)
{
    PrinterType printer_type = SEQ_PRINTER_TYPE_PRUSA_MK3S;
	
    switch (printer_type)
    {
    case SEQ_PRINTER_TYPE_PRUSA_MK3S:
    {
	convex_unreachable_zones =  SEQ_UNREACHABLE_POLYGON_CONVEX_LEVELS_MK3S;
	box_unreachable_zones =  SEQ_UNREACHABLE_POLYGON_BOX_LEVELS_MK3S;	
	break;
    }
    case SEQ_PRINTER_TYPE_PRUSA_MK4:
    {
	convex_unreachable_zones =  SEQ_UNREACHABLE_POLYGON_CONVEX_LEVELS_MK4;
	box_unreachable_zones =  SEQ_UNREACHABLE_POLYGON_BOX_LEVELS_MK4;		
	break;
    }
    case SEQ_PRINTER_TYPE_PRUSA_XL:
    {
	convex_unreachable_zones =  SEQ_UNREACHABLE_POLYGON_CONVEX_LEVELS_XL;
	box_unreachable_zones =  SEQ_UNREACHABLE_POLYGON_BOX_LEVELS_XL;			
	break;
    }        
    default:
    {
	throw std::runtime_error("UNSUPPORTED PRINTER TYPE");		    
	break;		    
    }
    }
}


int schedule_ObjectsForSequentialPrint(const SolverConfiguration                        &solver_configuration,
				       const std::vector<ObjectToPrint>                 &objects_to_print,
				       const std::vector<std::vector<Slic3r::Polygon> > &convex_unreachable_zones,
				       const std::vector<std::vector<Slic3r::Polygon> > &box_unreachable_zones,
				       std::vector<ScheduledPlate>                      &scheduled_plates,
				       std::function<void(int)>                          progress_callback)
{
    #ifdef PROFILE
    clock_t start, finish;
    start = clock();	
    #endif

    #ifdef DEBUG
    {
	printf("Sequential scheduling/arranging ...\n");
    }
    #endif

    std::vector<SolvableObject> solvable_objects;
    std::map<int, int> original_index_map;

    #ifdef DEBUG
    {
	printf("  Preparing objects ...\n");
    }
    #endif
    
    for (unsigned int i = 0; i < objects_to_print.size(); ++i)
    {	
	Polygon nozzle_polygon;
	Polygon extruder_polygon;
	Polygon hose_polygon;
	Polygon gantry_polygon;

	original_index_map[i] = objects_to_print[i].id;

	int ht = 0;
	
	for (unsigned int j = 0; j < objects_to_print[i].pgns_at_height.size(); ++j)
	{
	    if (!objects_to_print[i].pgns_at_height[j].second.points.empty())
	    {
		Polygon decimated_polygon;

		if (solver_configuration.decimation_precision != SEQ_DECIMATION_PRECISION_UNDEFINED)
		{
		    decimate_PolygonForSequentialSolver(solver_configuration,
							objects_to_print[i].pgns_at_height[j].second,
							decimated_polygon,
							true);
		}
		else
		{
		    decimated_polygon = objects_to_print[i].pgns_at_height[j].second;
		    decimated_polygon.make_counter_clockwise();
		}
		
		if (!check_PolygonSizeFitToPlate(solver_configuration, SEQ_SLICER_SCALE_FACTOR, decimated_polygon))
		{
		    #ifdef DEBUG
		    {
			printf("Object too large to fit onto plate [ID:%d RID:%d].\n", original_index_map[i], i);
		    }
		    #endif
		    return -1;
		}		
		
		switch (ht)
		{
		case 0:    // nozzle
		{
		    nozzle_polygon = decimated_polygon;		    
		    break;
		}
		case 1:    // extruder
		{
		    extruder_polygon = decimated_polygon;		    		    
		    break;
		}
		case 2:    // hose
		{
		    hose_polygon = decimated_polygon;		    		    		    
		    break;
		}
		case 3:    // gantry
		{
		    gantry_polygon = decimated_polygon;
		    break;
		}
		default:
		{
		    throw std::runtime_error("UNSUPPORTED POLYGON HEIGHT");		    
		    break;
		}
		}
	    }
	    ++ht;
	}
	SolvableObject solvable_object;
	
	scaleDown_PolygonForSequentialSolver(nozzle_polygon, solvable_object.polygon);

	std::vector<Slic3r::Polygon> convex_level_polygons;
	convex_level_polygons.push_back(nozzle_polygon);	
	convex_level_polygons.push_back(extruder_polygon);
	
	std::vector<Slic3r::Polygon> box_level_polygons;
	box_level_polygons.push_back(hose_polygon);
	box_level_polygons.push_back(gantry_polygon);		
	
	std::vector<Slic3r::Polygon> scale_down_unreachable_polygons;

	prepare_UnreachableZonePolygons(solver_configuration,
				       convex_level_polygons,
				       box_level_polygons,
				       convex_unreachable_zones,
				       box_unreachable_zones,
				       solvable_object.unreachable_polygons);

	solvable_object.id = objects_to_print[i].id;	
	solvable_object.lepox_to_next = objects_to_print[i].glued_to_next;

	solvable_objects.push_back(solvable_object);
    }

    std::vector<int> remaining_polygons;
    std::vector<int> decided_polygons;
    
    std::vector<Rational> poly_positions_X;
    std::vector<Rational> poly_positions_Y;
    std::vector<Rational> times_T;

    #ifdef DEBUG
    {
	printf("  Preparing objects ... finished\n");
    }
    #endif

    int progress_object_phases_done = 0;
    int progress_object_phases_total = SEQ_MAKE_EXTRA_PROGRESS((objects_to_print.size() * SEQ_PROGRESS_PHASES_PER_OBJECT));

    bool trans_bed_lepox = false;
    
    do
    {
	ScheduledPlate scheduled_plate;
		
	decided_polygons.clear();
	remaining_polygons.clear();

	#ifdef DEBUG
	{
	    printf("  Object scheduling/arranging ...\n");
	}
	#endif
	
	bool optimized;
	
	optimized = optimize_SubglobalConsequentialPolygonNonoverlappingBinaryCentered(solver_configuration,
										       poly_positions_X,
										       poly_positions_Y,
										       times_T,
										       solvable_objects,
										       trans_bed_lepox,
										       decided_polygons,
										       remaining_polygons,
										       progress_object_phases_done,
										       progress_object_phases_total,
										       progress_callback);	

	
	#ifdef DEBUG
	{
	    printf("  Object scheduling/arranging ... finished\n");
	}
	#endif
	
	if (optimized)
	{
	    #ifdef DEBUG
	    {	    
		printf("Polygon positions:\n");
		for (unsigned int i = 0; i < decided_polygons.size(); ++i)
		{
		    printf("  [ID:%d,RID:%d] x:%.3f, y:%.3f (t:%.3f)\n",
			   original_index_map[decided_polygons[i]],
			   decided_polygons[i],
			   poly_positions_X[decided_polygons[i]].as_double(),
			   poly_positions_Y[decided_polygons[i]].as_double(),
			   times_T[decided_polygons[i]].as_double());
		}
		printf("Remaining polygons: %ld\n", remaining_polygons.size());
		for (unsigned int i = 0; i < remaining_polygons.size(); ++i)
		{
		    printf("  ID:%d\n", original_index_map[remaining_polygons[i]]);
		}
	    }
	    #endif

	    bool split = false;
	    for (unsigned int i = 0; i < decided_polygons.size(); ++i)
	    {
		if (solvable_objects[i].lepox_to_next && !is_scheduled(i + 1, decided_polygons))
		{
		    split = true;
		    break;
		}
	    }
	    if (split)
	    {
		trans_bed_lepox = true;
		#ifdef DEBUG
		{
		    printf("Lopoxed group split, implies trans-bed lepox\n");
		}
		#endif		
	    }
	    else
	    {
		trans_bed_lepox = false;
	    }
	    std::map<double, int> scheduled_polygons;
	    for (unsigned int i = 0; i < decided_polygons.size(); ++i)
	    {
		scheduled_polygons.insert(std::pair<double, int>(times_T[decided_polygons[i]].as_double(), decided_polygons[i]));
	    }

	    for (const auto& scheduled_polygon: scheduled_polygons)
	    {
		coord_t X, Y;
		
		scaleUp_PositionForSlicer(poly_positions_X[scheduled_polygon.second],
					  poly_positions_Y[scheduled_polygon.second],
					  X,
					  Y);
		const auto& original_index = original_index_map.find(scheduled_polygon.second);

		scheduled_plate.scheduled_objects.push_back(ScheduledObject(original_index->second, X, Y));
	    }
	}
	else
	{
	    #ifdef DEBUG
	    {	    	    
		printf("Polygon sequential schedule optimization FAILED.\n");
	    }
	    #endif
	    return -2;
	}
	#ifdef PROFILE
	{
	    finish = clock();
	}
	#endif
	
        #ifdef PROFILE
	{	    
	    printf("Intermediate CPU time: %.3f\n", (finish - start) / (double)CLOCKS_PER_SEC);
	}
	#endif

	std::vector<SolvableObject> next_solvable_objects;
	
	for (unsigned int i = 0; i < remaining_polygons.size(); ++i)
	{
	    next_solvable_objects.push_back(solvable_objects[i]);
	}	
	solvable_objects = next_solvable_objects;
	std::map<int, int> next_original_index_map;

	for (unsigned int index = 0; index < solvable_objects.size(); ++index)
	{
	    next_original_index_map[index] = original_index_map[remaining_polygons[index]];
	}
	original_index_map = next_original_index_map;

	scheduled_plates.push_back(scheduled_plate);
    }
    while (!remaining_polygons.empty());

    progress_callback(SEQ_PROGRESS_RANGE);    

    #ifdef PROFILE
    {
	finish = clock();
    }
    #endif

    #ifdef DEBUG
    {	    
	printf("Sequential scheduling/arranging ... finished\n");
    }
    #endif
    
    #ifdef PROFILE
    {
	printf("Total CPU time: %.3f\n", (finish - start) / (double)CLOCKS_PER_SEC);
    }
    #endif
    
    return 0;    
}

/*----------------------------------------------------------------*/

} // namespace Sequential
