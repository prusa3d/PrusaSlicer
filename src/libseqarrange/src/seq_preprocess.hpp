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

#ifndef __SEQ_PREPROCESS_HPP__
#define __SEQ_PREPROCESS_HPP__


/*----------------------------------------------------------------*/

#include "seq_sequential.hpp"


/*----------------------------------------------------------------*/

namespace Sequential
{


/*----------------------------------------------------------------*/

const coord_t SEQ_SLICER_SCALE_FACTOR = 100000;    
const double SEQ_POLYGON_DECIMATION_GROW_FACTOR = 1.005;
  

/*----------------------------------------------------------------*/

struct ObjectToPrint;
    
    
/*----------------------------------------------------------------*/    

extern const std::vector<Slic3r::Polygon> SEQ_UNREACHABLE_POLYGON_NOZZLE_LEVEL_MK3S;
extern const std::vector<Slic3r::Polygon> SEQ_UNREACHABLE_POLYGON_EXTRUDER_LEVEL_MK3S;
extern const std::vector<Slic3r::Polygon> SEQ_UNREACHABLE_POLYGON_HOSE_LEVEL_MK3S;
extern const std::vector<Slic3r::Polygon> SEQ_UNREACHABLE_POLYGON_GANTRY_LEVEL_MK3S;

extern const std::vector<std::vector<Slic3r::Polygon> > SEQ_UNREACHABLE_POLYGON_ALL_LEVELS_MK3S;
extern const std::vector<std::vector<Slic3r::Polygon> > SEQ_UNREACHABLE_POLYGON_CONVEX_LEVELS_MK3S;
extern const std::vector<std::vector<Slic3r::Polygon> > SEQ_UNREACHABLE_POLYGON_BOX_LEVELS_MK3S;


extern const std::vector<Slic3r::Polygon> SEQ_UNREACHABLE_POLYGON_NOZZLE_LEVEL_MK4;
extern const std::vector<Slic3r::Polygon> SEQ_UNREACHABLE_POLYGON_EXTRUDER_LEVEL_MK4;
extern const std::vector<Slic3r::Polygon> SEQ_UNREACHABLE_POLYGON_HOSE_LEVEL_MK4;
extern const std::vector<Slic3r::Polygon> SEQ_UNREACHABLE_POLYGON_GANTRY_LEVEL_MK4;

extern const std::vector<std::vector<Slic3r::Polygon> > SEQ_UNREACHABLE_POLYGON_ALL_LEVELS_MK4;
extern const std::vector<std::vector<Slic3r::Polygon> > SEQ_UNREACHABLE_POLYGON_CONVEX_LEVELS_MK4;
extern const std::vector<std::vector<Slic3r::Polygon> > SEQ_UNREACHABLE_POLYGON_BOX_LEVELS_MK4;

extern const std::vector<Slic3r::Polygon> SEQ_UNREACHABLE_POLYGON_NOZZLE_LEVEL_XL;
extern const std::vector<Slic3r::Polygon> SEQ_UNREACHABLE_POLYGON_EXTRUDER_LEVEL_XL;
extern const std::vector<Slic3r::Polygon> SEQ_UNREACHABLE_POLYGON_HOSE_LEVEL_XL;
extern const std::vector<Slic3r::Polygon> SEQ_UNREACHABLE_POLYGON_GANTRY_LEVEL_XL;

extern const std::vector<std::vector<Slic3r::Polygon> > SEQ_UNREACHABLE_POLYGON_ALL_LEVELS_XL;
extern const std::vector<std::vector<Slic3r::Polygon> > SEQ_UNREACHABLE_POLYGON_CONVEX_LEVELS_XL;
extern const std::vector<std::vector<Slic3r::Polygon> > SEQ_UNREACHABLE_POLYGON_BOX_LEVELS_XL;        


/*----------------------------------------------------------------*/

Rational scaleDown_CoordinateForSequentialSolver(coord_t x);
    
void scaleDown_PolygonForSequentialSolver(const Slic3r::Polygon &polygon,
					  Slic3r::Polygon       &scaled_polygon);

void scaleDown_PolygonForSequentialSolver(coord_t                scale_factor,
					  const Slic3r::Polygon &polygon,
					  Slic3r::Polygon       &scaled_polygon);

Slic3r::Polygon scaleDown_PolygonForSequentialSolver(coord_t scale_factor, const Slic3r::Polygon &polygon);


void scaleUp_PositionForSlicer(const Rational &position_X,
			       const Rational &position_Y,			       
			       coord_t        &scaled_position_X,
			       coord_t        &scaled_position_Y);

void scaleUp_PositionForSlicer(coord_t         scale_factor,
			       const Rational &position_X,
			       const Rational &position_Y,			       
			       coord_t        &scaled_position_X,
			       coord_t        &scaled_position_Y);    

void scaleUp_PositionForSlicer(double   position_X,
			       double   position_Y,
			       coord_t &scaled_position_X,
			       coord_t &scaled_position_Y);

void scaleUp_PositionForSlicer(coord_t  scale_factor,
			       double   position_X,
			       double   position_Y,
			       coord_t &scaled_position_X,
			       coord_t &scaled_position_Y);			           

Slic3r::Polygon scaleUp_PolygonForSlicer(const Slic3r::Polygon &polygon);
Slic3r::Polygon scaleUp_PolygonForSlicer(coord_t scale_factor, const Slic3r::Polygon &polygon);
    
Slic3r::Polygon scaleUp_PolygonForSlicer(const Slic3r::Polygon &polygon, double x_pos, double y_pos);
Slic3r::Polygon scaleUp_PolygonForSlicer(coord_t scale_factor, const Slic3r::Polygon &polygon, double x_pos, double y_pos);    
    
void ground_PolygonByBoundingBox(Slic3r::Polygon &polygon);
void ground_PolygonByFirstPoint(Slic3r::Polygon &polygon);
    
void shift_Polygon(Slic3r::Polygon &polygon, coord_t x_offset, coord_t y_offset);
void shift_Polygon(Slic3r::Polygon &polygon, const Slic3r::Point &offset);    

    
/*----------------------------------------------------------------*/
    
Slic3r::Polygon transform_UpsideDown(const SolverConfiguration &solver_configuration, const Slic3r::Polygon &polygon);
Slic3r::Polygon transform_UpsideDown(const SolverConfiguration &solver_configuration, coord_t scale_factor, const Slic3r::Polygon &polygon);    
    
void transform_UpsideDown(const SolverConfiguration &solver_configuration,
			  const coord_t             &scaled_x_pos,
			  const coord_t             &scaled_y_pos,
			  coord_t                   &transformed_x_pos,
			  coord_t                   &transformed_y_pos);

void transform_UpsideDown(const SolverConfiguration &solver_configuration,
			  coord_t                    scale_factor,
			  const coord_t             &scaled_x_pos,
			  const coord_t             &scaled_y_pos,
			  coord_t                   &transformed_x_pos,
			  coord_t                   &transformed_y_pos);    

    
/*----------------------------------------------------------------*/

void grow_PolygonForContainedness(coord_t center_x, coord_t center_y, Slic3r::Polygon &polygon);
    
void decimate_PolygonForSequentialSolver(const SolverConfiguration &solver_configuration,
					 const Slic3r::Polygon     &polygon,
					 Slic3r::Polygon           &scale_down_polygon,
					 bool                       extra_safety);

void decimate_PolygonForSequentialSolver(double                 DP_tolerance,
					 const Slic3r::Polygon &polygon,
					 Slic3r::Polygon       &decimated_polygon,
					 bool                   extra_safety);

void extend_PolygonConvexUnreachableZone(const SolverConfiguration          &solver_configuration,
					 const Slic3r::Polygon              &polygon,
					 const std::vector<Slic3r::Polygon> &extruder_polygons,
					 std::vector<Slic3r::Polygon>       &unreachable_polygons);

void extend_PolygonBoxUnreachableZone(const SolverConfiguration          &solver_configuration,
				      const Slic3r::Polygon              &polygon,
				      const std::vector<Slic3r::Polygon> &extruder_polygons,
				      std::vector<Slic3r::Polygon>       &unreachable_polygons);

void extend_PolygonBoxUnreachableZone(const SolverConfiguration          &solver_configuration,
				      const Slic3r::Polygon              &polygon,
				      const std::vector<Slic3r::Polygon> &extruder_polygons,
				      std::vector<Slic3r::Polygon>       &unreachable_polygons);

void prepare_ExtruderPolygons(const SolverConfiguration                  &solver_configuration,
			      const PrinterGeometry                      &printer_geometry,
			      const ObjectToPrint                        &object_to_print,
			      std::vector<Slic3r::Polygon>               &convex_level_polygons,
			      std::vector<Slic3r::Polygon>               &box_level_polygons,
			      std::vector<std::vector<Slic3r::Polygon> > &extruder_convex_level_polygons,
			      std::vector<std::vector<Slic3r::Polygon> > &extruder_box_level_polygons,
			      bool                                        extra_safety);

void prepare_ObjectPolygons(const SolverConfiguration                        &solver_configuration,
			    const std::vector<Slic3r::Polygon>               &convex_level_polygons,
			    const std::vector<Slic3r::Polygon>               &box_level_polygons,
			    const std::vector<std::vector<Slic3r::Polygon> > &extruder_convex_level_polygons,
			    const std::vector<std::vector<Slic3r::Polygon> > &extruder_box_level_polygons,
			    Slic3r::Polygon                                  &object_polygon,
			    std::vector<Slic3r::Polygon>                     &unreachable_polygons);        

void prepare_UnreachableZonePolygons(const SolverConfiguration                        &solver_configuration,
				     const Slic3r::Polygon                            &polygon,					
				     const std::vector<std::vector<Slic3r::Polygon> > &extruder_convex_level_polygons,
				     const std::vector<std::vector<Slic3r::Polygon> > &extruder_box_level_polygons,
				     std::vector<Slic3r::Polygon>                     &unreachable_polygons);
    
void prepare_UnreachableZonePolygons(const SolverConfiguration                        &solver_configuration,
				     const std::vector<Slic3r::Polygon>               &convex_level_polygons,
				     const std::vector<Slic3r::Polygon>               &box_level_polygons,
				     const std::vector<std::vector<Slic3r::Polygon> > &extruder_convex_level_polygons,
				     const std::vector<std::vector<Slic3r::Polygon> > &extruder_box_level_polygons,
				     std::vector<Slic3r::Polygon>                     &unreachable_polygons);    

bool check_PolygonSizeFitToPlate(const SolverConfiguration &solver_configuration, const Slic3r::Polygon &polygon);
bool check_PolygonPositionWithinPlate(const SolverConfiguration &solver_configuration, coord_t x, coord_t y, const Slic3r::Polygon &polygon);
    
bool check_PolygonSizeFitToPlate(const SolverConfiguration &solver_configuration, coord_t scale_factor, const Slic3r::Polygon &polygon);    
bool check_PolygonPositionWithinPlate(const SolverConfiguration &solver_configuration, coord_t scale_factor, coord_t x, coord_t y, const Slic3r::Polygon &polygon);
    
/*----------------------------------------------------------------*/

bool check_PolygonConsumation(const std::vector<Slic3r::Polygon> &polygons, const std::vector<Slic3r::Polygon> &consumer_polygons);    
std::vector<std::vector<Slic3r::Polygon> > simplify_UnreachableZonePolygons(const std::vector<std::vector<Slic3r::Polygon> > &unreachable_polygons);
    
void glue_LowObjects(std::vector<SolvableObject> &solvable_ojects);


/*----------------------------------------------------------------*/

double calc_PolygonArea(const Slic3r::Polygon &polygon);

double calc_PolygonUnreachableZoneArea(const std::vector<Slic3r::Polygon> &unreachable_polygons);    
double calc_PolygonUnreachableZoneArea(const Slic3r::Polygon              &polygon,
				       const std::vector<Slic3r::Polygon> &unreachable_polygons);

double calc_PolygonArea(const std::vector<Slic3r::Polygon> &polygons);
double calc_PolygonArea(const std::vector<int>             &fixed,
			const std::vector<int>             &undecided,
			const std::vector<Slic3r::Polygon> &polygons);

double calc_PolygonUnreachableZoneArea(const std::vector<Slic3r::Polygon>               &polygons,
				       const std::vector<std::vector<Slic3r::Polygon> > &unreachable_polygons);


/*----------------------------------------------------------------*/

} // namespace Sequential


/*----------------------------------------------------------------*/

#endif /* __SEQ_PREPROCESS_HPP__ */
