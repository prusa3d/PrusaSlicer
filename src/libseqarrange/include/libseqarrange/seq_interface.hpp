/*================================================================*/
/*
 * Author:  Pavel Surynek, 2023 - 2024
 * Company: Prusa Research
 *
 * File:    seq_interface.hpp
 *
 * Interface of the sequential printing SMT model for Prusa Slic3r
 */
/*================================================================*/

#ifndef __SEQ_INTERFACE_HPP__
#define __SEQ_INTERFACE_HPP__


/*----------------------------------------------------------------*/

#include "libslic3r/Polygon.hpp"
#include "libslic3r/Geometry/ConvexHull.hpp"

/*----------------------------------------------------------------*/

using namespace Slic3r;


/*----------------------------------------------------------------*/

namespace Sequential
{
    class ObjectTooLargeException : public std::runtime_error { public: explicit ObjectTooLargeException(const std::string& msg) : std::runtime_error(msg) {}};


    
/*----------------------------------------------------------------*/
    
struct PrinterGeometry
{
    // must be convex; for best performance a rectangle is recommended
    Slic3r::Polygon plate;

    // at least height 0 (corresponding to nozzle) must be present in convex_heights
    std::set<coord_t> convex_heights;
    std::set<coord_t> box_heights;

    // <height, polygons at given height>, at least one polygon must be present for height 0
    std::map<coord_t, std::vector<Slic3r::Polygon> > extruder_slices;

    bool convert_Geometry2PlateBounds(Slic3r::BoundingBox &plate_bounding_box, Slic3r::Polygon &plate_bounding_polygon) const;
};

    
/*----------------------------------------------------------------*/

enum DecimationPrecision
{
    SEQ_DECIMATION_PRECISION_UNDEFINED,
    SEQ_DECIMATION_PRECISION_LOW,
    SEQ_DECIMATION_PRECISION_HIGH    
};
    

/*----------------------------------------------------------------*/    
    
struct SolverConfiguration
{
    SolverConfiguration();
    SolverConfiguration(const PrinterGeometry &printer_geometry);
    
    void set_DecimationPrecision(DecimationPrecision decimation_precision);
    void set_ObjectGroupSize(int object_group_size);

    void setup(const PrinterGeometry &printer_geometry);

    static double convert_DecimationPrecision2Tolerance(DecimationPrecision decimation_precision);

    int bounding_box_size_optimization_step;
    int minimum_bounding_box_size;
    
    Slic3r::BoundingBox plate_bounding_box;
    Slic3r::Polygon plate_bounding_polygon;
    
    int max_refines;
    
    int object_group_size;
    int fixed_object_grouping_limit;    
    int temporal_spread;

    DecimationPrecision decimation_precision;   
    std::string optimization_timeout;    
};

    
/*----------------------------------------------------------------*/
    
struct ObjectToPrint
{
    int id = 0;
    bool glued_to_next = false; /* the next object must be scheduled right after this object */ 
    coord_t total_height = 0;
    std::vector<std::pair<coord_t, Slic3r::Polygon>> pgns_at_height;
};


struct ScheduledObject {
    ScheduledObject(int _id, coord_t _x, coord_t _y)
	: id(_id)
	, x(_x)
	, y(_y) { /* */ }
    
    int id = 0;
    coord_t x, y;
};


struct ScheduledPlate {
    std::vector<ScheduledObject> scheduled_objects;
};


/*----------------------------------------------------------------*/
/*
  This is the recommended interface for checking sequential printability.

  Returns true if objects are sequentially printable according to their
  ordering in the input vector and the arrangement on the plate specified
  by the schedule. Printable means that the extruder never hits printed
  objects during printing. Otherwise returns false.

  Please see the corresponding example of usage (seq_test_interface.cpp)

  Note: The function always succeeds, does not throw any exception.
*/

bool check_ScheduledObjectsForSequentialPrintability(const SolverConfiguration         &solver_configuration,
						     const PrinterGeometry             &printer_geometry,
						     const std::vector<ObjectToPrint>  &objects_to_print,
						     const std::vector<ScheduledPlate> &scheduled_plates);

/*
  This is a variant of the interface for checking sequential printability.

  If not sequentially printable returns a pair of object IDs that are in conflict,
  that is, when the second object is printed the extruder will collide with the
  first object. The returned conflict is not necessarily the first collision to
  occur when printing the object according to the given input schedule.

  Note: The function always succeeds, does not throw any exception.
 */  
std::optional<std::pair<int, int> > check_ScheduledObjectsForSequentialConflict(const SolverConfiguration         &solver_configuration,
										const PrinterGeometry             &printer_geometry,
										const std::vector<ObjectToPrint>  &objects_to_print,
										const std::vector<ScheduledPlate> &scheduled_plates);

    
/*----------------------------------------------------------------*/
/*
  This is the recommended interface for sequential scheduling/arranging.

  Please see the corresponding example of usage (seq_test_interface.cpp)

  Note: The function should succeed except the case when there is an
  object that does not fit on the plate and in the case when the solver
  is unable to scedule even single object on the plate. The latter case
  is detected by timeout and should not normally happen. These failures
  are reported via exceptions.

  The trans_bed_glue parameter should be set to false when scheduling
  all objects. If only objects on a separate bed are scheduled, then
  trans_bed_glue should be set to true when there is an object on the
  previous bed that is temporally glued to the first scheduled object.
  In such a case, the first object will be scheduled as first temporally.
*/

std::vector<ScheduledPlate> schedule_ObjectsForSequentialPrint(const SolverConfiguration        &solver_configuration,
							       const PrinterGeometry            &printer_geometry,
							       const std::vector<ObjectToPrint> &objects_to_print,
							       std::function<void(int)>          progress_callback = [](int progress){});
    
void schedule_ObjectsForSequentialPrint(const SolverConfiguration        &solver_configuration,
					const PrinterGeometry            &printer_geometry,
					const std::vector<ObjectToPrint> &objects_to_print,
					std::vector<ScheduledPlate>      &scheduled_plates,
					std::function<void(int)>          progress_callback = [](int progress){});
    
    
/*----------------------------------------------------------------*/
/*
  The following interface is for more internal use.
 */
           
int schedule_ObjectsForSequentialPrint(const SolverConfiguration        &solver_configuration,
				       const std::vector<ObjectToPrint> &objects_to_print,
				       std::vector<ScheduledPlate>      &scheduled_plates,
				       std::function<void(int)>          progress_callback = [](int progress){});

void setup_ExtruderUnreachableZones(const SolverConfiguration                  &solver_configuration,
				    std::vector<std::vector<Slic3r::Polygon> > &convex_unreachable_zones,
				    std::vector<std::vector<Slic3r::Polygon> > &box_unreachable_zones);

int schedule_ObjectsForSequentialPrint(const SolverConfiguration                        &solver_configuration,
				       const std::vector<ObjectToPrint>                 &objects_to_print,
				       const std::vector<std::vector<Slic3r::Polygon> > &convex_unreachable_zones,
				       const std::vector<std::vector<Slic3r::Polygon> > &box_unreachable_zones,     
				       std::vector<ScheduledPlate>                      &scheduled_plates,
				       std::function<void(int)>                          progress_callback = [](int progress){});

    
/*----------------------------------------------------------------*/

} // namespace Sequential


/*----------------------------------------------------------------*/

#endif /* __SEQ_INTERFACE_HPP__ */
