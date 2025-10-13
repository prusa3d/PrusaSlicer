/*================================================================*/
/*
 * Author:  Pavel Surynek, 2023 - 2025
 * Company: Prusa Research
 *
 * File:    seq_test_preprocess.cpp
 *
 * Object preprocessing preprocess priting via SMT.
 */
/*================================================================*/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

#include <vector>
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Geometry/ConvexHull.hpp"
#include "libslic3r/SVG.hpp"

#include <z3++.h>

#include "prusaparts.hpp"

#include "seq_defs.hpp"

#include "seq_sequential.hpp"
#include "seq_preprocess.hpp"

#include "seq_test_sequential.hpp"


/*----------------------------------------------------------------*/

using namespace z3;

using namespace Slic3r;
using namespace Slic3r::Geometry;

using namespace Sequential;


#define SCALE_FACTOR                  50000.0


/*----------------------------------------------------------------*/

const coord_t SEQ_PRUSA_MK3S_X_SIZE = 250000000;
const coord_t SEQ_PRUSA_MK3S_Y_SIZE = 210000000;    

/*----------------------------------------------------------------*/

/*
static Polygon scale_UP(const Polygon &polygon)
{
    Polygon poly = polygon;

    for (unsigned int i = 0; i < poly.points.size(); ++i)
    {
	poly.points[i] = Point(poly.points[i].x() * SCALE_FACTOR, poly.points[i].y() * SCALE_FACTOR);
    }

    return poly;
}
*/

static Polygon scale_UP(const Polygon &polygon, double x_pos, double y_pos)
{
    Polygon poly = polygon;

    for (unsigned int i = 0; i < poly.points.size(); ++i)
    {	
	poly.points[i] = Point(poly.points[i].x() * SCALE_FACTOR + x_pos * SCALE_FACTOR,
			       poly.points[i].y() * SCALE_FACTOR + y_pos * SCALE_FACTOR);
    }

    return poly;
}


std::vector<Polygon> test_polygons;

TEST_CASE("Preprocessing test 1", "[Sequential Arrangement Preprocessing]")
{
    #ifdef DEBUG
    clock_t start, finish;
    #endif
    
    INFO("Testing preprocessing 1 ...");

    SolverConfiguration solver_configuration;
    solver_configuration.plate_bounding_box = BoundingBox({0,0}, {SEQ_PRUSA_MK3S_X_SIZE / SEQ_SLICER_SCALE_FACTOR, SEQ_PRUSA_MK3S_Y_SIZE / SEQ_SLICER_SCALE_FACTOR});
    #ifdef DEBUG
    start = clock();
    #endif
    for (unsigned int i = 0; i < PRUSA_PART_POLYGONS.size(); ++i)
    {
	Polygon scale_down_polygon;
	scaleDown_PolygonForSequentialSolver(PRUSA_PART_POLYGONS[i], scale_down_polygon);       
	test_polygons.push_back(scale_down_polygon);
    }
    REQUIRE(!test_polygons.empty());

    for (unsigned int i = 0; i < test_polygons.size(); ++i)
    {
	SVG preview_svg("preprocess_test_1.svg");
	Polygon display_polygon = scale_UP(test_polygons[i], 1000, 1000);
	preview_svg.draw(display_polygon, "blue");
	preview_svg.Close();
    }
    #ifdef DEBUG
    finish = clock();
    #endif

    #ifdef DEBUG
    {
	printf("Time: %.3f\n", (finish - start) / (double)CLOCKS_PER_SEC);
    }
    #endif
    INFO("Testing preprocessing 1 ... finished");    
}


//TEST_CASE("Preprocessing test 2", "[Sequential Arrangement Preprocessing]")
void preprocessing_test_2(void)
{
    #ifdef DEBUG
    clock_t start, finish;
    #endif
    
    INFO("Testing preprocess 2 ...");

    #ifdef DEBUG
    start = clock();
    #endif

    SolverConfiguration solver_configuration;
    solver_configuration.plate_bounding_box = BoundingBox({0,0}, {SEQ_PRUSA_MK3S_X_SIZE / SEQ_SLICER_SCALE_FACTOR, SEQ_PRUSA_MK3S_Y_SIZE / SEQ_SLICER_SCALE_FACTOR});

    vector<Polygon> polygons;
    vector<Polygon> unreachable_polygons;
    
    for (unsigned int i = 0; i < 8 /*PRUSA_PART_POLYGONS.size()*/; ++i)
    {
	Polygon scale_down_polygon;
	scaleDown_PolygonForSequentialSolver(PRUSA_PART_POLYGONS[i], scale_down_polygon);
	scale_down_polygon.make_counter_clockwise();
	polygons.push_back(scale_down_polygon);
	unreachable_polygons.push_back(scale_down_polygon);
    }
    
    vector<int> remaining_polygons;
    vector<int> polygon_index_map;
    vector<int> decided_polygons;

    for (unsigned int index = 0; index < polygons.size(); ++index)
    {
	polygon_index_map.push_back(index);
    }
    
    vector<Rational> poly_positions_X;
    vector<Rational> poly_positions_Y;
    vector<Rational> times_T;    
    
    do
    {
	decided_polygons.clear();
	remaining_polygons.clear();
	
	bool optimized = optimize_SubglobalSequentialPolygonNonoverlappingBinaryCentered(solver_configuration,
											 poly_positions_X,
											 poly_positions_Y,
											 times_T,
											 polygons,
											 unreachable_polygons,
											 polygon_index_map,
											 decided_polygons,
											 remaining_polygons);

	#ifdef DEBUG
	{
	    printf("----> Optimization finished <----\n");
	}
	#endif
	REQUIRE(optimized);	
	
	if (optimized)
	{
	    #ifdef DEBUG
	    {
		printf("Polygon positions:\n");
		for (unsigned int i = 0; i < decided_polygons.size(); ++i)
		{
		    printf("  [%d] %.3f, %.3f (%.3f)\n", decided_polygons[i], poly_positions_X[decided_polygons[i]].as_double(), poly_positions_Y[decided_polygons[i]].as_double(), times_T[decided_polygons[i]].as_double());
		}
		printf("Remaining polygons: %ld\n", remaining_polygons.size());
		for (unsigned int i = 0; i < remaining_polygons.size(); ++i)
		{
		    printf("  %d\n", remaining_polygons[i]);
		}
	    }
	    #endif
	
	    SVG preview_svg("preprocess_test_2.svg");

	    if (!unreachable_polygons.empty())
	    {
		for (unsigned int i = 0; i < decided_polygons.size(); ++i)
		{
		    #ifdef DEBUG
		    {
			printf("----> %.3f,%.3f\n", poly_positions_X[decided_polygons[i]].as_double(), poly_positions_Y[decided_polygons[i]].as_double());		    
			for (unsigned int k = 0; k < polygons[decided_polygons[i]].points.size(); ++k)
			{
			    printf("    xy: %d, %d\n", polygons[decided_polygons[i]].points[k].x(), polygons[decided_polygons[i]].points[k].y());
			}
		    }
		    #endif

		    #ifdef DEBUG
		    {
			for (unsigned int k = 0; k < unreachable_polygons[decided_polygons[i]].points.size(); ++k)
			{
			    printf("    Pxy: %d, %d\n", unreachable_polygons[decided_polygons[i]].points[k].x(), unreachable_polygons[decided_polygons[i]].points[k].y());
			}
		    }
		    #endif
			
		    Polygon display_unreachable_polygon = scale_UP(unreachable_polygons[decided_polygons[i]],
								   poly_positions_X[decided_polygons[i]].as_double(),
								   poly_positions_Y[decided_polygons[i]].as_double());
		    preview_svg.draw(display_unreachable_polygon, "lightgrey");   
		}
	    }	    

	    for (unsigned int i = 0; i < decided_polygons.size(); ++i)
	    {
		Polygon display_polygon = scale_UP(polygons[decided_polygons[i]],
						   poly_positions_X[decided_polygons[i]].as_double(),
						   poly_positions_Y[decided_polygons[i]].as_double());
		
		string color;
		
		switch(i)
		{
		case 0:
		{
		    color = "green";
		    break;
		}
		case 1:
		{
		    color = "blue";
		    break;
		}
		case 2:
		{
		    color = "red";	    
		    break;
		}
		case 3:
		{
		    color = "grey";	    
		    break;
		}
		case 4:
		{
		    color = "cyan";
		    break;
		}
		case 5:
		{
		    color = "magenta";
		    break;
		}
		case 6:
		{
		    color = "yellow";
		    break;
		}
		case 7:
		{
		    color = "black";
		    break;
		}
		case 8:
		{
		    color = "indigo";
		    break;
		}
		case 9:
		{
		    color = "olive";
		    break;
		}
		case 10:
		{
		    color = "aqua";
		    break;
		}
		case 11:
		{
		    color = "violet";
		    break;
		}			    	    	    
		default:
		{
		    break;
		}
		}
		
		preview_svg.draw(display_polygon, color);
	    }
	    
	    preview_svg.Close();
	}
	else
	{
	    #ifdef DEBUG
	    {
		printf("Polygon optimization FAILED.\n");
	    }
	    #endif
	}
	#ifdef DEBUG
	finish = clock();
	#endif

        #ifdef DEBUG
	{
	    printf("Intermediate time: %.3f\n", (finish - start) / (double)CLOCKS_PER_SEC);
	}
	#endif
	
	vector<Polygon> next_polygons;
	vector<Polygon> next_unreachable_polygons;

	#ifdef DEBUG
	{
	    for (unsigned int i = 0; i < polygon_index_map.size(); ++i)
	    {
		printf("  %d\n", polygon_index_map[i]);
	    }
	}
	#endif
	for (unsigned int i = 0; i < remaining_polygons.size(); ++i)
	{
	    next_polygons.push_back(polygons[remaining_polygons[i]]);	    	    
	    next_unreachable_polygons.push_back(unreachable_polygons[remaining_polygons[i]]);
	}
		
	polygons.clear();
	unreachable_polygons.clear();
	polygon_index_map.clear();	
	
	polygons = next_polygons;
	unreachable_polygons = next_unreachable_polygons;

	for (unsigned int index = 0; index < polygons.size(); ++index)
	{
	    polygon_index_map.push_back(index);
	}
    }
    while (!remaining_polygons.empty());

    #ifdef DEBUG
    finish = clock();
    #endif

    #ifdef DEBUG
    {
	printf("Time: %.3f\n", (finish - start) / (double)CLOCKS_PER_SEC);
    }
    #endif
    INFO("Testing preprocess 2 ... finished");    
}    


TEST_CASE("Preprocessing test 3", "[Sequential Arrangement Preprocessing]")
{
    #ifdef DEBUG
    clock_t start, finish;
    #endif

    SolverConfiguration solver_configuration;
    solver_configuration.plate_bounding_box = BoundingBox({0,0}, {SEQ_PRUSA_MK3S_X_SIZE / SEQ_SLICER_SCALE_FACTOR, SEQ_PRUSA_MK3S_Y_SIZE / SEQ_SLICER_SCALE_FACTOR});
    
    INFO("Testing preprocessing 3 ...");

    #ifdef DEBUG
    start = clock();
    #endif

    std::vector<Slic3r::Polygon> nozzle_unreachable_polygons;
    std::vector<Slic3r::Polygon> extruder_unreachable_polygons;
    std::vector<Slic3r::Polygon> hose_unreachable_polygons;
    std::vector<Slic3r::Polygon> gantry_unreachable_polygons;            

    for (unsigned int p = 0; p < PRUSA_PART_POLYGONS.size(); ++p)
    {
	{
	    nozzle_unreachable_polygons.clear();
	    
	    extend_PolygonConvexUnreachableZone(solver_configuration,
						PRUSA_PART_POLYGONS[p],
						SEQ_UNREACHABLE_POLYGON_NOZZLE_LEVEL_MK3S,
						nozzle_unreachable_polygons);
	    REQUIRE(nozzle_unreachable_polygons.size() > 0);
	    
	    SVG preview_svg("preprocess_test_3.svg");

	    for (unsigned int j = 0; j < SEQ_UNREACHABLE_POLYGON_NOZZLE_LEVEL_MK3S.size(); ++j)
	    {
		preview_svg.draw(SEQ_UNREACHABLE_POLYGON_NOZZLE_LEVEL_MK3S[j], "lightgrey");
	    }
	    
	    if (!nozzle_unreachable_polygons.empty())
	    {
		for (unsigned int j = 0; j < nozzle_unreachable_polygons.size(); ++j)
		{
		    preview_svg.draw(nozzle_unreachable_polygons[j], "lightgrey");		    
		}		
	    }
	    preview_svg.draw(PRUSA_PART_POLYGONS[p], "blue");
	
	    preview_svg.Close();
	}

	{
	    nozzle_unreachable_polygons.clear();
	    
	    extend_PolygonBoxUnreachableZone(solver_configuration,
					    PRUSA_PART_POLYGONS[p],
					    SEQ_UNREACHABLE_POLYGON_NOZZLE_LEVEL_MK3S,
					    nozzle_unreachable_polygons);
	    REQUIRE(nozzle_unreachable_polygons.size() > 0);	    
	    
	    SVG preview_svg("preprocess_test_3.svg");	    

	    for (unsigned int j = 0; j < SEQ_UNREACHABLE_POLYGON_NOZZLE_LEVEL_MK3S.size(); ++j)
	    {
		preview_svg.draw(SEQ_UNREACHABLE_POLYGON_NOZZLE_LEVEL_MK3S[j], "lightgrey");
	    }
	
	    if (!nozzle_unreachable_polygons.empty())
	    {
		for (unsigned int j = 0; j < nozzle_unreachable_polygons.size(); ++j)
		{
		    preview_svg.draw(nozzle_unreachable_polygons[j], "lightgrey");		    
		}		
	    }
	    preview_svg.draw(PRUSA_PART_POLYGONS[p], "red");
	
	    preview_svg.Close();
	}

	{
	    extruder_unreachable_polygons.clear();
	    
	    extend_PolygonConvexUnreachableZone(solver_configuration,
					       PRUSA_PART_POLYGONS[p],
					       SEQ_UNREACHABLE_POLYGON_EXTRUDER_LEVEL_MK3S,
					       extruder_unreachable_polygons);
	    REQUIRE(extruder_unreachable_polygons.size() > 0);
	    
	    SVG preview_svg("preprocess_test_3.svg");

	    for (unsigned int j = 0; j < SEQ_UNREACHABLE_POLYGON_EXTRUDER_LEVEL_MK3S.size(); ++j)
	    {
		preview_svg.draw(SEQ_UNREACHABLE_POLYGON_EXTRUDER_LEVEL_MK3S[j], "lightgrey");
	    }
	    
	    if (!extruder_unreachable_polygons.empty())
	    {
		for (unsigned int j = 0; j < extruder_unreachable_polygons.size(); ++j)
		{
		    preview_svg.draw(extruder_unreachable_polygons[j], "lightgrey");		    
		}		
	    }
	    preview_svg.draw(PRUSA_PART_POLYGONS[p], "green");
	
	    preview_svg.Close();
	}

	{
	    extruder_unreachable_polygons.clear();
	    
	    extend_PolygonBoxUnreachableZone(solver_configuration,
					    PRUSA_PART_POLYGONS[p],
					    SEQ_UNREACHABLE_POLYGON_EXTRUDER_LEVEL_MK3S,
					    extruder_unreachable_polygons);
	    REQUIRE(extruder_unreachable_polygons.size() > 0);	    
	    
	    SVG preview_svg("preprocess_test_3.svg");	    

	    for (unsigned int j = 0; j < SEQ_UNREACHABLE_POLYGON_EXTRUDER_LEVEL_MK3S.size(); ++j)
	    {
		preview_svg.draw(SEQ_UNREACHABLE_POLYGON_EXTRUDER_LEVEL_MK3S[j], "lightgrey");
	    }
	
	    if (!extruder_unreachable_polygons.empty())
	    {
		for (unsigned int j = 0; j < extruder_unreachable_polygons.size(); ++j)
		{
		    preview_svg.draw(extruder_unreachable_polygons[j], "lightgrey");		    
		}		
	    }
	    preview_svg.draw(PRUSA_PART_POLYGONS[p], "magenta");
	
	    preview_svg.Close();
	}

	{
	    hose_unreachable_polygons.clear();
	    
	    extend_PolygonConvexUnreachableZone(solver_configuration,
					       PRUSA_PART_POLYGONS[p],
					       SEQ_UNREACHABLE_POLYGON_HOSE_LEVEL_MK3S,
					       hose_unreachable_polygons);
	    REQUIRE(hose_unreachable_polygons.size() > 0);
		    
	    SVG preview_svg("preprocess_test_3.svg");

	    for (unsigned int j = 0; j < SEQ_UNREACHABLE_POLYGON_HOSE_LEVEL_MK3S.size(); ++j)
	    {
		preview_svg.draw(SEQ_UNREACHABLE_POLYGON_HOSE_LEVEL_MK3S[j], "lightgrey");
	    }
	    
	    if (!hose_unreachable_polygons.empty())
	    {
		for (unsigned int j = 0; j < hose_unreachable_polygons.size(); ++j)
		{
		    preview_svg.draw(hose_unreachable_polygons[j], "lightgrey");		    
		}		
	    }
	    preview_svg.draw(PRUSA_PART_POLYGONS[p], "yellow");
	
	    preview_svg.Close();
	}

	{
	    hose_unreachable_polygons.clear();
	    
	    extend_PolygonBoxUnreachableZone(solver_configuration,
					    PRUSA_PART_POLYGONS[p],
					    SEQ_UNREACHABLE_POLYGON_HOSE_LEVEL_MK3S,
					    hose_unreachable_polygons);
	    REQUIRE(hose_unreachable_polygons.size() > 0);
	    
	    SVG preview_svg("preprocess_test_3.svg");	    

	    for (unsigned int j = 0; j < SEQ_UNREACHABLE_POLYGON_HOSE_LEVEL_MK3S.size(); ++j)
	    {
		preview_svg.draw(SEQ_UNREACHABLE_POLYGON_HOSE_LEVEL_MK3S[j], "lightgrey");
	    }
	
	    if (!hose_unreachable_polygons.empty())
	    {
		for (unsigned int j = 0; j < hose_unreachable_polygons.size(); ++j)
		{
		    preview_svg.draw(hose_unreachable_polygons[j], "lightgrey");		    
		}		
	    }
	    preview_svg.draw(PRUSA_PART_POLYGONS[p], "orange");
	
	    preview_svg.Close();
	}

	{
	    gantry_unreachable_polygons.clear();
	    
	    extend_PolygonConvexUnreachableZone(solver_configuration,
					       PRUSA_PART_POLYGONS[p],
					       SEQ_UNREACHABLE_POLYGON_GANTRY_LEVEL_MK3S,
					       gantry_unreachable_polygons);
	    REQUIRE(gantry_unreachable_polygons.size() > 0);
	    
	    SVG preview_svg("preprocess_test_3.svg");

	    for (unsigned int j = 0; j < SEQ_UNREACHABLE_POLYGON_GANTRY_LEVEL_MK3S.size(); ++j)
	    {
		preview_svg.draw(SEQ_UNREACHABLE_POLYGON_GANTRY_LEVEL_MK3S[j], "lightgrey");
	    }
	    
	    if (!gantry_unreachable_polygons.empty())
	    {
		for (unsigned int j = 0; j < gantry_unreachable_polygons.size(); ++j)
		{
		    preview_svg.draw(gantry_unreachable_polygons[j], "lightgrey");		    
		}		
	    }
	    preview_svg.draw(PRUSA_PART_POLYGONS[p], "grey");
	
	    preview_svg.Close();
	}

	{
	    gantry_unreachable_polygons.clear();
	    
	    extend_PolygonBoxUnreachableZone(solver_configuration,
					    PRUSA_PART_POLYGONS[p],
					    SEQ_UNREACHABLE_POLYGON_GANTRY_LEVEL_MK3S,
					    gantry_unreachable_polygons);
	    REQUIRE(gantry_unreachable_polygons.size() > 0);	    
	    
	    SVG preview_svg("preprocess_test_3.svg");	    

	    for (unsigned int j = 0; j < SEQ_UNREACHABLE_POLYGON_GANTRY_LEVEL_MK3S.size(); ++j)
	    {
		preview_svg.draw(SEQ_UNREACHABLE_POLYGON_GANTRY_LEVEL_MK3S[j], "lightgrey");
	    }
	
	    if (!gantry_unreachable_polygons.empty())
	    {
		for (unsigned int j = 0; j < gantry_unreachable_polygons.size(); ++j)
		{
		    preview_svg.draw(gantry_unreachable_polygons[j], "lightgrey");		    
		}		
	    }
	    preview_svg.draw(PRUSA_PART_POLYGONS[p], "black");
	
	    preview_svg.Close();
	}			
    }    
    #ifdef DEBUG
    finish = clock();
    #endif

    #ifdef DEBUG
    {
	printf("Time: %.3f\n", (finish - start) / (double)CLOCKS_PER_SEC);
    }
    #endif
    INFO("Testing preprocess 3 ... finished");
}


//TEST_CASE("Preprocessing test 4", "[Sequential Arrangement Preprocessing]")
void preprocessing_test_4(void)
{
    #ifdef DEBUG
    clock_t start, finish;
    #endif
    
    INFO("Testing preprocess 4 ...");

    #ifdef DEBUG
    start = clock();
    #endif

    SolverConfiguration solver_configuration;
    solver_configuration.plate_bounding_box = BoundingBox({0,0}, {SEQ_PRUSA_MK3S_X_SIZE / SEQ_SLICER_SCALE_FACTOR, SEQ_PRUSA_MK3S_Y_SIZE / SEQ_SLICER_SCALE_FACTOR});    

    std::vector<Slic3r::Polygon> polygons;
    std::vector<std::vector<Slic3r::Polygon> > unreachable_polygons;
    
    for (int i = 0; i < 12; ++i)
    {
	Polygon scale_down_polygon;
	scaleDown_PolygonForSequentialSolver(PRUSA_PART_POLYGONS[i], scale_down_polygon);
	polygons.push_back(scale_down_polygon);

	std::vector<Slic3r::Polygon> convex_level_polygons;
	convex_level_polygons.push_back(PRUSA_PART_POLYGONS[i]);
	convex_level_polygons.push_back(PRUSA_PART_POLYGONS[i]);	
	std::vector<Slic3r::Polygon> box_level_polygons;
	box_level_polygons.push_back(PRUSA_PART_POLYGONS[i]);
	box_level_polygons.push_back(PRUSA_PART_POLYGONS[i]);		
	
	std::vector<Slic3r::Polygon> scale_down_unreachable_polygons;
	prepare_UnreachableZonePolygons(solver_configuration,
				       convex_level_polygons,
				       box_level_polygons,
				       SEQ_UNREACHABLE_POLYGON_CONVEX_LEVELS_MK3S,
				       SEQ_UNREACHABLE_POLYGON_BOX_LEVELS_MK3S,
				       scale_down_unreachable_polygons);
	unreachable_polygons.push_back(scale_down_unreachable_polygons);
    }
    
    vector<int> remaining_polygons;
    vector<int> polygon_index_map;
    vector<int> decided_polygons;

    for (unsigned int index = 0; index < polygons.size(); ++index)
    {
	polygon_index_map.push_back(index);
    }
    
    vector<Rational> poly_positions_X;
    vector<Rational> poly_positions_Y;
    vector<Rational> times_T;    
    
    do
    {
	decided_polygons.clear();
	remaining_polygons.clear();
	
	bool optimized = optimize_SubglobalSequentialPolygonNonoverlappingBinaryCentered(solver_configuration,
											 poly_positions_X,
											 poly_positions_Y,
											 times_T,
											 polygons,
											 unreachable_polygons,
											 polygon_index_map,
											 decided_polygons,
											 remaining_polygons);

	#ifdef DEBUG
	{
	    printf("----> Optimization finished <----\n");
	}
	#endif
	REQUIRE(optimized);
	
	if (optimized)
	{
            #ifdef DEBUG
	    {
		printf("Polygon positions:\n");
		for (unsigned int i = 0; i < decided_polygons.size(); ++i)
		{
		    printf("  [%d] %.3f, %.3f (%.3f)\n", decided_polygons[i], poly_positions_X[decided_polygons[i]].as_double(), poly_positions_Y[decided_polygons[i]].as_double(), times_T[decided_polygons[i]].as_double());
		}
		printf("Remaining polygons: %ld\n", remaining_polygons.size());
		for (unsigned int i = 0; i < remaining_polygons.size(); ++i)
		{
		    printf("  %d\n", remaining_polygons[i]);
		}
	    }
	    #endif
	
	    SVG preview_svg("preprocess_test_4.svg");

	    if (!unreachable_polygons.empty())
	    {
		for (unsigned int i = 0; i < decided_polygons.size(); ++i)
		{
		    #ifdef DEBUG
		    {	
			printf("----> %.3f,%.3f\n", poly_positions_X[decided_polygons[i]].as_double(), poly_positions_Y[decided_polygons[i]].as_double());		    
			for (unsigned int k = 0; k < polygons[decided_polygons[i]].points.size(); ++k)
			{
			    printf("    xy: %d, %d\n", polygons[decided_polygons[i]].points[k].x(), polygons[decided_polygons[i]].points[k].y());
			}
		    }
		    #endif
		    
		    for (unsigned int j = 0; j < unreachable_polygons[decided_polygons[i]].size(); ++j)
		    {
			#ifdef DEBUG
			{
			    for (unsigned int k = 0; k < unreachable_polygons[decided_polygons[i]][j].points.size(); ++k)
			    {
				printf("    Pxy: %d, %d\n", unreachable_polygons[decided_polygons[i]][j].points[k].x(), unreachable_polygons[decided_polygons[i]][j].points[k].y());
			    }
			}
			#endif
			
			Polygon display_unreachable_polygon = scale_UP(unreachable_polygons[decided_polygons[i]][j],
								       poly_positions_X[decided_polygons[i]].as_double(),
								       poly_positions_Y[decided_polygons[i]].as_double());
			preview_svg.draw(display_unreachable_polygon, "lightgrey");   			    			    
		    }
		}
	    }	    

	    for (unsigned int i = 0; i < decided_polygons.size(); ++i)
	    {
		Polygon display_polygon = scale_UP(polygons[decided_polygons[i]],
						   poly_positions_X[decided_polygons[i]].as_double(),
						   poly_positions_Y[decided_polygons[i]].as_double());
		string color;
		
		switch(i)
		{
		case 0:
		{
		    color = "green";
		    break;
		}
		case 1:
		{
		    color = "blue";
		    break;
		}
		case 2:
		{
		    color = "red";	    
		    break;
		}
		case 3:
		{
		    color = "grey";	    
		    break;
		}
		case 4:
		{
		    color = "cyan";
		    break;
		}
		case 5:
		{
		    color = "magenta";
		    break;
		}
		case 6:
		{
		    color = "yellow";
		    break;
		}
		case 7:
		{
		    color = "black";
		    break;
		}
		case 8:
		{
		    color = "indigo";
		    break;
		}
		case 9:
		{
		    color = "olive";
		    break;
		}
		case 10:
		{
		    color = "aqua";
		    break;
		}
		case 11:
		{
		    color = "violet";
		    break;
		}			    	    	    
		default:
		{
		    break;
		}
		}
		
		preview_svg.draw(display_polygon, color);
	    }
	    preview_svg.Close();
	}
	else
	{
	    #ifdef DEBUG
	    {
		printf("Polygon optimization FAILED.\n");
	    }
	    #endif	    
	}
        #ifdef DEBUG
	finish = clock();
	#endif

	#ifdef DEBUG
	{
	    printf("Intermediate time: %.3f\n", (finish - start) / (double)CLOCKS_PER_SEC);
	}
	#endif
	
	vector<Polygon> next_polygons;
	vector<vector<Polygon> > next_unreachable_polygons;

	#ifdef DEBUG
	{
	    for (unsigned int i = 0; i < polygon_index_map.size(); ++i)
	    {
		printf("  %d\n", polygon_index_map[i]);
	    }
	}
	#endif
	for (unsigned int i = 0; i < remaining_polygons.size(); ++i)
	{
	    next_polygons.push_back(polygons[remaining_polygons[i]]);	    	    
	    next_unreachable_polygons.push_back(unreachable_polygons[remaining_polygons[i]]);
	}
		
	polygons.clear();
	unreachable_polygons.clear();
	polygon_index_map.clear();	
	
	polygons = next_polygons;
	unreachable_polygons = next_unreachable_polygons;

	for (unsigned int index = 0; index < polygons.size(); ++index)
	{
	    polygon_index_map.push_back(index);
	}
    }
    while (!remaining_polygons.empty());

    #ifdef DEBUG
    finish = clock();
    #endif

    #ifdef DEBUG
    {
	printf("Time: %.3f\n", (finish - start) / (double)CLOCKS_PER_SEC);
    }
    #endif
    INFO("Testing preprocess 4 ... finished");
}    


TEST_CASE("Preprocessing test 5", "[Sequential Arrangement Preprocessing]")
{
    #ifdef DEBUG
    clock_t start, finish;
    #endif
    
    INFO("Testing preprocess 5 ...");

    #ifdef DEBUG
    start = clock();
    #endif

    SolverConfiguration solver_configuration;
    solver_configuration.plate_bounding_box = BoundingBox({0,0}, {SEQ_PRUSA_MK3S_X_SIZE / SEQ_SLICER_SCALE_FACTOR, SEQ_PRUSA_MK3S_Y_SIZE / SEQ_SLICER_SCALE_FACTOR});    

    std::vector<Slic3r::Polygon> polygons;
    std::vector<std::vector<Slic3r::Polygon> > unreachable_polygons;
    
    for (unsigned int i = 0; i < PRUSA_PART_POLYGONS.size(); ++i)
    {
	Polygon simplified_polygon, scale_down_polygon;
	
	decimate_PolygonForSequentialSolver(solver_configuration,
					    PRUSA_PART_POLYGONS[i],
					    simplified_polygon,
					    false);
	REQUIRE(simplified_polygon.size() > 0);
	
	std::vector<Slic3r::Polygon> convex_level_polygons;
	convex_level_polygons.push_back(PRUSA_PART_POLYGONS[i]);
	convex_level_polygons.push_back(PRUSA_PART_POLYGONS[i]);	
	std::vector<Slic3r::Polygon> box_level_polygons;
	box_level_polygons.push_back(PRUSA_PART_POLYGONS[i]);
	box_level_polygons.push_back(PRUSA_PART_POLYGONS[i]);		
	
	std::vector<Slic3r::Polygon> scale_down_unreachable_polygons;
	prepare_UnreachableZonePolygons(solver_configuration,
				       convex_level_polygons,
				       box_level_polygons,
				       SEQ_UNREACHABLE_POLYGON_CONVEX_LEVELS_MK3S,
				       SEQ_UNREACHABLE_POLYGON_BOX_LEVELS_MK3S,
				       scale_down_unreachable_polygons);
	REQUIRE(scale_down_unreachable_polygons.size() > 0);
	unreachable_polygons.push_back(scale_down_unreachable_polygons);
	
	SVG preview_svg("preprocess_test_5.svg");

	preview_svg.draw(simplified_polygon, "lightgrey");	
	preview_svg.draw(PRUSA_PART_POLYGONS[i], "blue");	    
	
	preview_svg.Close();
    }

    #ifdef DEBUG
    finish = clock();
    #endif

    #ifdef DEBUG
    {
	printf("Time: %.3f\n", (finish - start) / (double)CLOCKS_PER_SEC);
    }
    #endif
    INFO("Testing preprocess 5 ... finished");
}


//TEST_CASE("Preprocessing test 6", "[Sequential Arrangement Preprocessing]")
void preprocessing_test_6(void)
{
    #ifdef DEBUG
    clock_t start, finish;
    #endif
    
    INFO("Testing preprocess 6 ...");

    #ifdef DEBUG
    start = clock();
    #endif

    SolverConfiguration solver_configuration;
    solver_configuration.plate_bounding_box = BoundingBox({0,0}, {SEQ_PRUSA_MK3S_X_SIZE / SEQ_SLICER_SCALE_FACTOR, SEQ_PRUSA_MK3S_Y_SIZE / SEQ_SLICER_SCALE_FACTOR});    

    std::vector<Slic3r::Polygon> polygons;
    std::vector<std::vector<Slic3r::Polygon> > unreachable_polygons;
    for (unsigned int i = 0; i < 12 /*PRUSA_PART_POLYGONS.size()*/; ++i)
    {
	Polygon decimated_polygon;
	decimate_PolygonForSequentialSolver(solver_configuration,
					    PRUSA_PART_POLYGONS[i],
					    decimated_polygon,
					    false);		
	
	Polygon scale_down_polygon;
	scaleDown_PolygonForSequentialSolver(decimated_polygon, scale_down_polygon);
	polygons.push_back(scale_down_polygon);

	std::vector<Slic3r::Polygon> convex_level_polygons;
	convex_level_polygons.push_back(PRUSA_PART_POLYGONS[i]);
	convex_level_polygons.push_back(PRUSA_PART_POLYGONS[i]);	
	std::vector<Slic3r::Polygon> box_level_polygons;
	box_level_polygons.push_back(PRUSA_PART_POLYGONS[i]);
	box_level_polygons.push_back(PRUSA_PART_POLYGONS[i]);		
	
	std::vector<Slic3r::Polygon> scale_down_unreachable_polygons;
	prepare_UnreachableZonePolygons(solver_configuration,
				       convex_level_polygons,
				       box_level_polygons,
				       SEQ_UNREACHABLE_POLYGON_CONVEX_LEVELS_MK3S,
				       SEQ_UNREACHABLE_POLYGON_BOX_LEVELS_MK3S,
				       scale_down_unreachable_polygons);
	unreachable_polygons.push_back(scale_down_unreachable_polygons);
    }
    
    vector<int> remaining_polygons;
    vector<int> polygon_index_map;
    vector<int> decided_polygons;

    for (unsigned int index = 0; index < polygons.size(); ++index)
    {
	polygon_index_map.push_back(index);
    }
    
    vector<Rational> poly_positions_X;
    vector<Rational> poly_positions_Y;
    vector<Rational> times_T;    
    
    do
    {
	decided_polygons.clear();
	remaining_polygons.clear();
	
	bool optimized = optimize_SubglobalSequentialPolygonNonoverlappingBinaryCentered(solver_configuration,
											 poly_positions_X,
											 poly_positions_Y,
											 times_T,
											 polygons,
											 unreachable_polygons,
											 polygon_index_map,
											 decided_polygons,
											 remaining_polygons);

	#ifdef DEBUG
	{
	    printf("----> Optimization finished <----\n");
	}
	#endif
	REQUIRE(optimized);
	
	if (optimized)
	{
	    #ifdef DEBUG
	    {
		printf("Polygon positions:\n");
		for (unsigned int i = 0; i < decided_polygons.size(); ++i)
		{
		    printf("  [%d] %.3f, %.3f (%.3f)\n", decided_polygons[i], poly_positions_X[decided_polygons[i]].as_double(), poly_positions_Y[decided_polygons[i]].as_double(), times_T[decided_polygons[i]].as_double());
		}
		printf("Remaining polygons: %ld\n", remaining_polygons.size());
		for (unsigned int i = 0; i < remaining_polygons.size(); ++i)
		{
		    printf("  %d\n", remaining_polygons[i]);
		}
	    }
	    #endif
	
	    SVG preview_svg("preprocess_test_6.svg");

	    if (!unreachable_polygons.empty())
	    {
		for (unsigned int i = 0; i < decided_polygons.size(); ++i)
		{
		    #ifdef DEBUG
                    {
			printf("----> %.3f,%.3f\n", poly_positions_X[decided_polygons[i]].as_double(), poly_positions_Y[decided_polygons[i]].as_double());		    
			for (unsigned int k = 0; k < polygons[decided_polygons[i]].points.size(); ++k)
			{
			    printf("    xy: %d, %d\n", polygons[decided_polygons[i]].points[k].x(), polygons[decided_polygons[i]].points[k].y());
			}
		    }
		    #endif		    
		    for (unsigned int j = 0; j < unreachable_polygons[decided_polygons[i]].size(); ++j)
		    {
			#ifdef DEBUG
                        {
			    for (unsigned int k = 0; k < unreachable_polygons[decided_polygons[i]][j].points.size(); ++k)
			    {
				printf("    Pxy: %d, %d\n", unreachable_polygons[decided_polygons[i]][j].points[k].x(), unreachable_polygons[decided_polygons[i]][j].points[k].y());
			    }
			}
                        #endif
			Polygon display_unreachable_polygon = scale_UP(unreachable_polygons[decided_polygons[i]][j],
								      poly_positions_X[decided_polygons[i]].as_double(),
								      poly_positions_Y[decided_polygons[i]].as_double());
			preview_svg.draw(display_unreachable_polygon, "lightgrey");   
		    }
		}
	    }	    

	    for (unsigned int i = 0; i < decided_polygons.size(); ++i)
	    {
		Polygon display_polygon = scale_UP(polygons[decided_polygons[i]],
						   poly_positions_X[decided_polygons[i]].as_double(),
						   poly_positions_Y[decided_polygons[i]].as_double());
		
		string color;
		
		switch(i)
		{
		case 0:
		{
		    color = "green";
		    break;
		}
		case 1:
		{
		    color = "blue";
		    break;
		}
		case 2:
		{
		    color = "red";	    
		    break;
		}
		case 3:
		{
		    color = "grey";	    
		    break;
		}
		case 4:
		{
		    color = "cyan";
		    break;
		}
		case 5:
		{
		    color = "magenta";
		    break;
		}
		case 6:
		{
		    color = "yellow";
		    break;
		}
		case 7:
		{
		    color = "black";
		    break;
		}
		case 8:
		{
		    color = "indigo";
		    break;
		}
		case 9:
		{
		    color = "olive";
		    break;
		}
		case 10:
		{
		    color = "aqua";
		    break;
		}
		case 11:
		{
		    color = "violet";
		    break;
		}			    	    	    
		default:
		{
		    break;
		}
		}
		
		preview_svg.draw(display_polygon, color);
	    }
	    
	    preview_svg.Close();
	}
	else
	{
	    #ifdef DEBUG
	    {
		printf("Polygon optimization FAILED.\n");
	    }
	    #endif
	}
	#ifdef DEBUG
	finish = clock();
	#endif
	
	#ifdef DEBUG
	{
	    printf("Intermediate time: %.3f\n", (finish - start) / (double)CLOCKS_PER_SEC);
	}
	#endif
	
	vector<Polygon> next_polygons;
	vector<vector<Polygon> > next_unreachable_polygons;

	#ifdef DEBUG
	{
	    for (unsigned int i = 0; i < polygon_index_map.size(); ++i)
	    {
		printf("  %d\n", polygon_index_map[i]);
	    }
	}
	#endif
	for (unsigned int i = 0; i < remaining_polygons.size(); ++i)
	{
	    next_polygons.push_back(polygons[remaining_polygons[i]]);	    	    
	    next_unreachable_polygons.push_back(unreachable_polygons[remaining_polygons[i]]);
	}
		
	polygons.clear();
	unreachable_polygons.clear();
	polygon_index_map.clear();	
	
	polygons = next_polygons;
	unreachable_polygons = next_unreachable_polygons;

	for (unsigned int index = 0; index < polygons.size(); ++index)
	{
	    polygon_index_map.push_back(index);
	}
    }
    while (!remaining_polygons.empty());

    #ifdef DEBUG
    finish = clock();
    #endif

    #ifdef DEBUG
    {
	printf("Time: %.3f\n", (finish - start) / (double)CLOCKS_PER_SEC);
    }
    #endif
    INFO("Testing preprocess 6 ... finished");    
}    


/*----------------------------------------------------------------*/
