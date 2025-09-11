/*================================================================*/
/*
 * Author:  Pavel Surynek, 2023 - 2025
 * Company: Prusa Research
 *
 * File:    seq_test_sequential.cpp
 *
 * Basic steel plate sequential object scheduling via SMT.
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

#include "seq_defs.hpp"
#include "seq_preprocess.hpp"
#include "seq_sequential.hpp"

#include "seq_test_sequential.hpp"


/*----------------------------------------------------------------*/

using namespace z3;

using namespace Slic3r;
using namespace Slic3r::Geometry;

using namespace Sequential;


#define SCALE_FACTOR                  100000.0

/*----------------------------------------------------------------*/

const int SEQ_PRUSA_MK3S_X_SIZE = 250000000;
const int SEQ_PRUSA_MK3S_Y_SIZE = 210000000;    


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


TEST_CASE("Sequential test 1", "[Sequential Arrangement Core]")
{
    INFO("Testing sequential scheduling 1 ...");

    z3::context z_context;
    
    z3::expr x(z_context.bool_const("x"));
    z3::expr y(z_context.bool_const("y"));
    z3::expr z(z_context.bool_const("z"));

    z3::expr a(z_context.int_const("a"));
    z3::expr b(z_context.int_const("b"));

    z3::expr c(z_context.real_const("cf"));
    z3::expr d(z_context.real_const("df"));    
    
    z3::expr lhs(x || y);    
    z3::expr rhs(implies(x, y));
    z3::expr final(lhs == rhs);

    z3::expr lhs1(a);
    z3::expr rhs1(b);
    z3::expr final2(lhs1 == rhs1);

    z3::expr lhs2(a > 2);
    z3::expr rhs2(b < 4);
    z3::expr final3(lhs2 || rhs2);
    z3::expr final4(a > 5);
    z3::expr final5(final3 && final4);

    z3::expr ef1((c > 3 && d < 6) && c < d);
    
    z3::solver z_solver(z_context);

    z_solver.add(final2);
    z_solver.add(final5);
    z_solver.add(ef1);

    #ifdef DEBUG
    {
	printf("Printing solver status:\n");
	cout << z_solver << "\n";
    
	printf("Printing smt status:\n");
	cout << z_solver.to_smt2() << "\n";
    }
    #endif

    bool sat = false;
    switch (z_solver.check())
    {
    case z3::sat:
    {
	sat = true;
	#ifdef DEBUG
	{
	    printf("  SATISFIABLE\n");
	}
	#endif
	break;
    }
    case z3::unsat:	
    {
	#ifdef DEBUG
	{	
	    printf("  UNSATISFIABLE\n");
	}
	#endif
	break;
    }
    case z3::unknown:
    {
        #ifdef DEBUG
	{
	    printf("  UNKNOWN\n");
	}
	#endif
	break;
    }
    default:
    {
	break;
    }
    }
    REQUIRE(sat);

    z3::model z_model(z_solver.get_model());

    #ifdef DEBUG
    {
	printf("Printing model:\n");
	cout << z_model << "\n";
    }
    #endif
    
    for (unsigned int i = 0; i < z_model.size(); ++i)
    {
	#ifdef DEBUG
	{
	    printf("Variable:%s\n", z_model[i].name().str().c_str());
	    printf("Printing interpretation:\n");	    
	
	    cout << z_model.get_const_interp(z_model[i]) << "\n";
	}
	#endif

	#ifdef DEBUG
	{
	    if (z_model.get_const_interp(z_model[i]).is_bool())
	    {	    
		printf("   value: TRUE\n");
	    }
	    else
	    {
		printf("   value: FALSE\n");	    
	    }
	}
        #endif
    }    
    
    INFO("Testing sequential scheduling 1 ... finished");    
}


int complex_sheet_resolution_X = 200;
int complex_sheet_resolution_Y = 50;

int complex_sheet_resolution_X_min = 10;
int complex_sheet_resolution_X_max = 200;
int complex_sheet_resolution_Y_min = 10;
int complex_sheet_resolution_Y_max = 200;

int complex_time_resolution = 1000;
int complex_height_threshold = 25;

const int complex_Obj_count = 26;

int complex_Obj_widths[complex_Obj_count];
int complex_Obj_heights[complex_Obj_count];
int complex_Obj_durations[complex_Obj_count];

const int min_width = 4;
const int max_width = 20;

const int min_height = 4;
const int max_height = 20;

const int min_duration = 2;
const int max_duration = 50;

const int gantry_left_height = 10;
const int gantry_left_shift = 4;

const int gantry_right_height = 10;
const int gantry_right_shift = 4;


void generate_random_complex_objects(void)
{
    int width_span =  max_width - min_width;
    int height_span =  max_height - min_height;
    int duration_span =  max_duration - min_duration;
	
    for (int i = 0; i < complex_Obj_count; ++i)
    {
	#ifdef DEBUG
	{
	    printf("Generating random object %d ...\n", i);
	}
	#endif
	
	complex_Obj_widths[i] = min_width + rand() % width_span;
	complex_Obj_heights[i] = min_height + rand() % height_span;
	complex_Obj_durations[i] = min_duration + rand() % duration_span;

	#ifdef DEBUG
	{
	    printf("O %d: w:%d h:%d d:%d\n", i, complex_Obj_widths[i], complex_Obj_heights[i], complex_Obj_durations[i]);
	}
	#endif
    }
}


typedef std::basic_string<char> sString;

TEST_CASE("Sequential test 2", "[Sequential Arrangement Core]")
{
    #ifdef DEBUG
    clock_t start, finish;
    #endif
    
    INFO("Testing sequential scheduling 2 ...");
    generate_random_complex_objects();

    #ifdef DEBUG
    start = clock();
    #endif

    z3::context z_context;    
    z3::expr_vector X_positions(z_context);
    z3::expr_vector Y_positions(z_context);
    z3::expr_vector T_schedules(z_context);
    
    z3::expr_vector gantry_lefts(z_context);
    z3::expr_vector gantry_rights(z_context);    

    for (int i = 0; i < complex_Obj_count; ++i)
    {
	sString name = "x_pos-" + to_string(i);
	#ifdef DEBUG
	{
	    printf("name: %s\n", name.c_str());
	}
	#endif	
	X_positions.push_back(expr(z_context.real_const(name.c_str())));
    }

    for (int i = 0; i < complex_Obj_count; ++i)
    {
	sString name = "y_pos-" + to_string(i);
	#ifdef DEBUG
	{
	    printf("name: %s\n", name.c_str());
	}
	#endif
	Y_positions.push_back(expr(z_context.real_const(name.c_str())));
    }

    for (int i = 0; i < complex_Obj_count; ++i)
    {
	sString name = "time-" + to_string(i);
	#ifdef DEBUG
	{	
	    printf("name: %s\n", name.c_str());
	}
	#endif
	T_schedules.push_back(expr(z_context.real_const(name.c_str())));
    }

    for (int i = 0; i < complex_Obj_count; ++i)
    {
	sString name_L = "gantry_L-" + to_string(i);
	#ifdef DEBUG
	{
	    printf("name_L: %s\n", name_L.c_str());
	}
	#endif
	sString name_R = "gantry_R-" + to_string(i);
	#ifdef DEBUG
	{
	    printf("name_R: %s\n", name_R.c_str());
	}
	#endif       
	gantry_lefts.push_back(expr(z_context.real_const(name_L.c_str())));
	gantry_rights.push_back(expr(z_context.real_const(name_R.c_str())));	
    }        
    
    z3::solver z_solver(z_context);

    for (int i = 0; i < complex_Obj_count; ++i)
    {
	z_solver.add(X_positions[i] >= 0 && X_positions[i] + complex_Obj_widths[i] <= complex_sheet_resolution_X);
	z_solver.add(Y_positions[i] >= 0 && Y_positions[i] + complex_Obj_heights[i] <= complex_sheet_resolution_Y);
	z_solver.add(T_schedules[i] >= 0 && T_schedules[i] + complex_Obj_durations[i] <= complex_time_resolution);
    }

    for (int i = 0; i < complex_Obj_count; ++i)
    {
	for (int j = 0; j < complex_Obj_count; ++j)
	{
	    if (i < j)
	    {
		z_solver.add(   X_positions[i] >= X_positions[j] + complex_Obj_widths[j]
			     || X_positions[j] >= X_positions[i] + complex_Obj_widths[i]
			     || Y_positions[i] >= Y_positions[j] + complex_Obj_heights[j]
			     || Y_positions[j] >= Y_positions[i] + complex_Obj_heights[i]);
	    }
	}
    }

    for (int i = 0; i < complex_Obj_count; ++i)
    {
	for (int j = 0; j < complex_Obj_count; ++j)
	{
	    if (i < j)
	    {
		z_solver.add(   T_schedules[i] >= T_schedules[j] + complex_Obj_durations[j]
			     || T_schedules[j] >= T_schedules[i] + complex_Obj_durations[i]);
	    }
	}
	
    }

    for (int i = 0; i < complex_Obj_count; ++i)
    {
	if (complex_Obj_durations[i] >= complex_height_threshold)
	{
	    z_solver.add(gantry_lefts[i] == Y_positions[i] + gantry_left_shift && gantry_rights[i] == Y_positions[i] + gantry_right_shift);
	}
    }

    for (int i = 0; i < complex_Obj_count; ++i)
    {
	if (complex_Obj_durations[i] >= complex_height_threshold)
	{
	    for (int j = 0; j < complex_Obj_count; ++j)
	    {
		if (i != j)
		{
		    z_solver.add(   T_schedules[j] < T_schedules[i]
				 || Y_positions[j] >= gantry_rights[i] + gantry_right_height
				 || gantry_rights[i] >= Y_positions[j] + complex_Obj_heights[j]);
		    
		    z_solver.add(   T_schedules[j] < T_schedules[i]
				 || Y_positions[j] >= gantry_lefts[i] + gantry_left_height
				 || Y_positions[i] >= Y_positions[j] + complex_Obj_heights[j]);  
		}
	    }
	}
    }       	    

    #ifdef DEBUG
    {
	printf("Printing solver status:\n");
	cout << z_solver << "\n";
    
	printf("Printing smt status:\n");
	cout << z_solver.to_smt2() << "\n";
    }
    #endif

    bool sat = false;
    switch (z_solver.check())
    {
    case z3::sat:
    {
	sat = true;
	#ifdef DEBUG
	{
	    printf("  SATISFIABLE\n");
	}
	#endif
	break;
    }
    case z3::unsat:	
    {
	#ifdef DEBUG
	{
	    printf("  UNSATISFIABLE\n");
	}
	#endif
	break;
    }
    case z3::unknown:
    {
	#ifdef DEBUG
	{
	    printf("  UNKNOWN\n");
	}
	#endif
	break;
    }
    default:
    {
	break;
    }
    }
    REQUIRE(!sat);

    #ifdef DEBUG
    z3::model z_model(z_solver.get_model());
    printf("Printing model:\n");
    cout << z_model << "\n";
    #endif

    #ifdef DEBUG
    finish = clock();
    #endif

    #ifdef DEBUG
    {
	for (unsigned int i = 0; i < z_model.size(); ++i)
	{
	    printf("Variable:%s\n", z_model[i].name().str().c_str());
	    
	    printf("Printing interpretation:\n");
	    cout << z_model.get_const_interp(z_model[i]) << "\n";
	    
	    cout << float(z_model[i]) << "\n";
	    
	    switch (z_model.get_const_interp(z_model[i]).bool_value())
	    {
	    case Z3_L_FALSE:
	    {
		printf("   value: FALSE\n");
		break;
	    }
	    case Z3_L_TRUE:
	    {
		printf("   value: TRUE\n");
		break;
	    }
	    case Z3_L_UNDEF:
	    {
		printf("   value: UNDEF\n");
		break;
	    }	    
	    default:
	    {
		break;
	    }
	    }
	}	
    }
    #endif

    #ifdef DEBUG
    {
	for (int i = 0; i < complex_Obj_count; ++i)
	{
	    printf("%s\n", z_model.get_const_interp(z_model[i]).get_string().c_str());
	}
	
	printf("Time: %.3f\n", (finish - start) / (double)CLOCKS_PER_SEC);
    }
    #endif
    
    INFO("Testing sequential scheduling 2 ... finished");    
}


const int complex_max_rotation = 8;

int rotated_Obj_widths[complex_Obj_count][complex_max_rotation];
int rotated_Obj_heights[complex_Obj_count][complex_max_rotation];

void generate_random_rotated_complex_objects(void)
{
    int width_span =  max_width - min_width;
    int height_span =  max_height - min_height;
    int duration_span =  max_duration - min_duration;
	
    for (int i = 0; i < complex_Obj_count; ++i)
    {
	#ifdef DEBUG
	{
	    printf("Generating random object %d ...\n", i);
	}
	#endif

	int base_width = min_width + rand() % width_span;
	int base_height = min_height + rand() % height_span;

	double angle = 0;
	double angle_step = 0.5 * M_PI / complex_max_rotation;
	
	for (int r = 0; r < complex_max_rotation; ++r)
	{
	    int width = cos(angle) * base_width + min_width;
	    int height = sin(angle) * base_height + min_height;

            #ifdef DEBUG
	    {
		printf("w: %d, h: %d\n", width, height);
	    }
	    #endif
	    
	    rotated_Obj_widths[i][r] = width;
	    rotated_Obj_heights[i][r] = height;
	    
	    angle += angle_step;

	    #ifdef DEBUG
	    {
		printf("O %d: w:%d h:%d d:%d\n", i, rotated_Obj_widths[i][r], rotated_Obj_heights[i][r], complex_Obj_durations[i]);
	    }
	    #endif
	}
	
	complex_Obj_durations[i] = min_duration + rand() % duration_span;
    }
}


//TEST_CASE("Sequential test 3", "[Sequential Arrangement Core]")
void sequential_test_3(void)
{
    #ifdef DEBUG
    clock_t start, finish;
    #endif
    
    INFO("Testing sequential scheduling 3 ...");
    generate_random_rotated_complex_objects();

    #ifdef DEBUG
    start = clock();
    #endif

    z3::context z_context;    
    z3::expr_vector X_positions(z_context);
    z3::expr_vector Y_positions(z_context);
    z3::expr_vector T_schedules(z_context);
    
    z3::expr_vector gantry_lefts(z_context);
    z3::expr_vector gantry_rights(z_context);

    z3::expr_vector rotations(z_context);
    z3::expr_vector widths(z_context);
    z3::expr_vector heights(z_context);    

    for (int i = 0; i < complex_Obj_count; ++i)
    {
	sString name = "x_pos-" + to_string(i);
	#ifdef DEBUG
	{
	    printf("name: %s\n", name.c_str());
	}
	#endif
	X_positions.push_back(expr(z_context.real_const(name.c_str())));
    }

    for (int i = 0; i < complex_Obj_count; ++i)
    {
	sString name = "y_pos-" + to_string(i);
	#ifdef DEBUG
	{
	    printf("name: %s\n", name.c_str());
	}
	#endif
	Y_positions.push_back(expr(z_context.real_const(name.c_str())));
    }

    for (int i = 0; i < complex_Obj_count; ++i)
    {
	sString name = "time-" + to_string(i);
	#ifdef DEBUG
	{
	    printf("name: %s\n", name.c_str());
	}
	#endif
	T_schedules.push_back(expr(z_context.real_const(name.c_str())));
    }

    for (int i = 0; i < complex_Obj_count; ++i)
    {
	sString name = "width-" + to_string(i);
	#ifdef DEBUG
	{
	    printf("name: %s\n", name.c_str());
	}
	#endif
	widths.push_back(expr(z_context.real_const(name.c_str())));
    }

    for (int i = 0; i < complex_Obj_count; ++i)
    {
	sString name = "height-" + to_string(i);
	#ifdef DEBUG
	{
	    printf("name: %s\n", name.c_str());
	}
	#endif
	heights.push_back(expr(z_context.real_const(name.c_str())));
    }        

    for (int i = 0; i < complex_Obj_count; ++i)
    {
	sString name = "rot-" + to_string(i);
        #ifdef DEBUG
	{
	    printf("name: %s\n", name.c_str());
	}
	#endif
	rotations.push_back(expr(z_context.int_const(name.c_str())));
    }    

    for (int i = 0; i < complex_Obj_count; ++i)
    {
	sString name_L = "gantry_L-" + to_string(i);
	#ifdef DEBUG
	{
	    printf("name_L: %s\n", name_L.c_str());
	}
	#endif
	sString name_R = "gantry_R-" + to_string(i);
	#ifdef DEBUG
	{
	    printf("name_R: %s\n", name_R.c_str());
	}
	#endif
	gantry_lefts.push_back(expr(z_context.real_const(name_L.c_str())));
	gantry_rights.push_back(expr(z_context.real_const(name_R.c_str())));	
    }        
    
    z3::solver z_solver(z_context);

    for (int i = 0; i < complex_Obj_count; ++i)
    {
	z_solver.add(X_positions[i] >= 0 && X_positions[i] + complex_Obj_widths[i] <= complex_sheet_resolution_X);
	z_solver.add(Y_positions[i] >= 0 && Y_positions[i] + complex_Obj_heights[i] <= complex_sheet_resolution_Y);
	z_solver.add(T_schedules[i] >= 0 && T_schedules[i] + complex_Obj_durations[i] <= complex_time_resolution);
	z_solver.add(rotations[i] >= 0 && rotations[i] < complex_max_rotation);	
    }

    for (int i = 0; i < complex_Obj_count; ++i)
    {
	for (int r = 0; r < complex_max_rotation; ++r)
	{
	    z_solver.add(rotations[i] != r || widths[i] == rotated_Obj_widths[i][r]);
	    z_solver.add(rotations[i] != r || heights[i] == rotated_Obj_heights[i][r]);
	}	
    }

    for (int i = 0; i < complex_Obj_count; ++i)
    {
	for (int j = 0; j < complex_Obj_count; ++j)
	{
	    if (i < j)
	    {
		z_solver.add(   X_positions[i] >= X_positions[j] + widths[j]
			     || X_positions[j] >= X_positions[i] + widths[i]
			     || Y_positions[i] >= Y_positions[j] + heights[j]
			     || Y_positions[j] >= Y_positions[i] + heights[i]);
	    }
	}
    }

    for (int i = 0; i < complex_Obj_count; ++i)
    {
	for (int j = 0; j < complex_Obj_count; ++j)
	{
	    if (i < j)
	    {
		z_solver.add(   T_schedules[i] >= T_schedules[j] + complex_Obj_durations[j]
			     || T_schedules[j] >= T_schedules[i] + complex_Obj_durations[i]);
	    }
	}
	
    }

    for (int i = 0; i < complex_Obj_count; ++i)
    {
	if (complex_Obj_durations[i] >= complex_height_threshold)
	{
	    z_solver.add(gantry_lefts[i] == Y_positions[i] + gantry_left_shift && gantry_rights[i] == Y_positions[i] + gantry_right_shift);
	}
    }

    for (int i = 0; i < complex_Obj_count; ++i)
    {
	if (complex_Obj_durations[i] >= complex_height_threshold)
	{
	    for (int j = 0; j < complex_Obj_count; ++j)
	    {
		if (i != j)
		{
		    z_solver.add(   T_schedules[j] < T_schedules[i]
				 || Y_positions[j] >= gantry_rights[i] + gantry_right_height
				 || gantry_rights[i] >= Y_positions[j] + heights[j]);
		    
		    z_solver.add(   T_schedules[j] < T_schedules[i]
				 || Y_positions[j] >= gantry_lefts[i] + gantry_left_height
				 || Y_positions[i] >= Y_positions[j] + heights[j]);  
		}
	    }
	}
    }       	    

    #ifdef DEBUG
    {
	printf("Printing solver status:\n");
	cout << z_solver << "\n";
    
	printf("Printing smt status:\n");
	cout << z_solver.to_smt2() << "\n";
    }
    #endif

    bool sat = false;
    switch (z_solver.check())
    {
    case z3::sat:
    {
	sat = true;
	#ifdef DEBUG
	{
	    printf("  SATISFIABLE\n");
	}
	#endif
	break;
    }
    case z3::unsat:	
    {
	#ifdef DEBUG
	{
	    printf("  UNSATISFIABLE\n");
	}
	#endif
	break;
    }
    case z3::unknown:
    {
	#ifdef DEBUG
	{
	    printf("  UNKNOWN\n");
	}
	#endif
	break;
    }
    default:
    {
	break;
    }
    }
    REQUIRE(sat);

    #ifdef DEBUG
    z3::model z_model(z_solver.get_model());
    printf("Printing model:\n");
    cout << z_model << "\n";    
    #endif

    #ifdef DEBUG
    finish = clock();
    #endif

    #ifdef DEBUG
    {
	for (unsigned int i = 0; i < z_model.size(); ++i)
	{
	    printf("Variable:%s\n", z_model[i].name().str().c_str());
	    
	    printf("Printing interpretation:\n");
	    cout << z_model.get_const_interp(z_model[i]) << "\n";
	
	    cout << float(z_model[i]) << "\n";
        
	    switch (z_model.get_const_interp(z_model[i]).bool_value())
	    {
	    case Z3_L_FALSE:
	    {
		printf("   value: FALSE\n");
		break;
	    }
	    case Z3_L_TRUE:
	    {
		printf("   value: TRUE\n");
		break;
	    }
	    case Z3_L_UNDEF:
	    {
		printf("   value: UNDEF\n");
		break;
	    }	    
	    default:
	    {
		break;
	    }
	    }	    
	}
    }
    #endif

    #ifdef DEBUG
    {
	for (int i = 0; i < complex_Obj_count; ++i)
	{
	    printf("%s\n", z_model.get_const_interp(z_model[i]).get_string().c_str());
	}
	
	printf("Time: %.3f\n", (finish - start) / (double)CLOCKS_PER_SEC);
    }
    #endif
    
    INFO("Testing sequential scheduling 3 ... finished");    
}

namespace {
	Polygon polygon_1 = { {0, 0}, {50, 0}, {50, 50}, {0, 50} };
	Polygon polygon_2 = { {0, 0}, {150, 10}, {150, 50}, {75, 120}, {0, 50} };
	//Polygon polygon_2 = {{0, 0}, {0, 50}, {75, 120}, {150, 50}, {150, 0} };
	Polygon polygon_3 = { {40, 0}, {80, 40}, {40, 80}, {0, 40} };
	//Polygon polygon_3 = {{40, 0}, {0, 40},{40, 80}, {80, 40}};
	Polygon polygon_4 = { {20, 0}, {40, 0}, {60, 30}, {30, 50}, {0, 30} };
	//Polygon polygon_4 = {{20, 0}, {0, 30}, {30, 50}, {60, 30}, {40, 0} };

	Polygon unreachable_polygon_1 = { {-5, -5}, {60, -5}, {60, 60}, {-5, 60} };
	Polygon unreachable_polygon_2 = { {-20, -20}, {170, -20}, {170, 86}, {85, 140}, {-20, 60} };
	Polygon unreachable_polygon_3 = { {40, -10}, {90, 40}, {40, 90}, {-10, 40} };
	Polygon unreachable_polygon_4 = { {10, -10}, {40, -10}, {70, 40}, {30, 60}, {-10, 40} };
	//Polygon unreachable_polygon_4 = {{10, -1}, {40, -1}, {70, 40}, {30, 60}, {0, 40}};
	//Polygon unreachable_polygon_4 = {{10, -10}, {-10, 40}, {30, 60}, {70, 40}, {40, -10}};


	std::vector<Polygon> unreachable_polygons_1 = {
		{{-5, -5}, {60, -5}, {60, 60}, {-5, 60}},
		//    {{-20,-20}, {-25,-20}, {-25,-25}, {-20,-25}}
		//    {{-20,20}, {-25,20}, {-25,25}, {-20,25}}
		//    {{-20,20}, {-40,20}, {-40,40}, {-20,40}}
		//    {{-20,20}, {-80,20}, {-80,40}, {-20,40}} /* CW */
			{{-20,20}, {-20,40}, {-180,40}, {-180,20}}, /* CCW */
			{{80,20}, {240,20}, {240,40}, {80,40}} /* CCW */
			//    {{-5,-5}, {-100,-5}, {-100,10}, {-5,10}}
	};

	std::vector<Polygon> unreachable_polygons_2 = {
		{{-20, -20}, {170, -20}, {170, 86}, {85, 140}, {-20, 60} }
	};

	std::vector<Polygon> unreachable_polygons_3 = {
		{{40, -10}, {90, 40}, {40, 90}, {-10, 40}},
		{{-20,20}, {-20,40}, {-180,40}, {-180,20}}, /* CCW */
		{{80,20}, {240,20}, {240,40}, {80,40}} /* CCW */
	};

	std::vector<Polygon> unreachable_polygons_4 = {
		{{10, -10}, {40, -10}, {70, 40}, {30, 60}, {-10, 40}}
	};
}


//TEST_CASE("Sequential test 4", "[Sequential Arrangement Core]")
void sequential_test_4(void)
{
    #ifdef DEBUG
    clock_t start, finish;
    #endif
    
    INFO("Testing sequential 4 ...");

    #ifdef DEBUG
    start = clock();
    #endif

    z3::context z_context;    
    z3::expr_vector X_positions(z_context);
    z3::expr_vector Y_positions(z_context);
    z3::expr_vector T_times(z_context);
    
    for (int i = 0; i < 4; ++i)
    {
	string name = "x_pos-" + to_string(i);
	#ifdef DEBUG
	{
	    printf("i:%d\n", i);	
	    printf("name: %s\n", name.c_str());
	}
	#endif	
	X_positions.push_back(expr(z_context.real_const(name.c_str())));
    }

    for (int i = 0; i < 4; ++i)
    {
	string name = "y_pos-" + to_string(i);
	#ifdef DEBUG
	{
	    printf("name: %s\n", name.c_str());
	}
	#endif
	Y_positions.push_back(expr(z_context.real_const(name.c_str())));
    }

    for (unsigned int i = 0; i < polygon_1.points.size(); ++i)
    {
	string name = "t_time-" + to_string(i);
	#ifdef DEBUG
	{
	    printf("name: %s\n", name.c_str());
	}
	#endif	
	T_times.push_back(expr(z_context.real_const(name.c_str())));	
    }
        
    z3::set_param("parallel.enable", "true");    
    z3::solver z_solver(z_context);    

    vector<Polygon> polygons;
    polygons.push_back(polygon_1);
    polygons.push_back(polygon_2);
    polygons.push_back(polygon_3);
    polygons.push_back(polygon_4);

    vector<Polygon> unreachable_polygons;
    unreachable_polygons.push_back(unreachable_polygon_1);
    unreachable_polygons.push_back(unreachable_polygon_2);
    unreachable_polygons.push_back(unreachable_polygon_3);
    unreachable_polygons.push_back(unreachable_polygon_4);    
          
    introduce_SequentialPolygonWeakNonoverlapping(z_solver,
						  z_context,
						  X_positions,
						  Y_positions,
						  T_times,
						  polygons,
						  unreachable_polygons);
    introduce_TemporalOrdering(z_solver, z_context, T_times, 16, polygons);

    #ifdef DEBUG
    {
	printf("Printing solver status:\n");
	cout << z_solver << "\n";
    
	printf("Printing smt status:\n");
	cout << z_solver.to_smt2() << "\n";
    }
    #endif

    int last_solvable_bounding_box_size = -1;
    double poly_1_pos_x, poly_1_pos_y, poly_2_pos_x, poly_2_pos_y, poly_3_pos_x, poly_3_pos_y, poly_4_pos_x, poly_4_pos_y;
    poly_1_pos_x = poly_1_pos_y = poly_2_pos_x = poly_2_pos_y = poly_3_pos_x = poly_3_pos_y = poly_4_pos_x = poly_4_pos_y = 0.0;
    
    double time_1_t, time_2_t, time_3_t, time_4_t;
    time_1_t = time_2_t = time_3_t = time_4_t = -1.0;    

    double _poly_1_pos_x, _poly_1_pos_y, _poly_2_pos_x, _poly_2_pos_y, _poly_3_pos_x, _poly_3_pos_y, _poly_4_pos_x, _poly_4_pos_y;
    _poly_1_pos_x = _poly_1_pos_y = _poly_2_pos_x = _poly_2_pos_y = _poly_3_pos_x = _poly_3_pos_y = _poly_4_pos_x = _poly_4_pos_y = 0.0;

    #ifdef DEBUG
    double _time_1_t, _time_2_t, _time_3_t, _time_4_t;    
    _time_1_t = _time_2_t = _time_3_t = _time_4_t = -1.0;
    #endif
    
    for (int bounding_box_size = 200; bounding_box_size > 10; bounding_box_size -= 4)
    {
	#ifdef DEBUG
	{
	    printf("BB: %d\n", bounding_box_size);
	}
	#endif
	z3::expr_vector bounding_box_assumptions(z_context);

	assume_BedBoundingBox(X_positions[0], Y_positions[0], polygons[0], bounding_box_size, bounding_box_size, bounding_box_assumptions);
	assume_BedBoundingBox(X_positions[1], Y_positions[1], polygons[1], bounding_box_size, bounding_box_size, bounding_box_assumptions);
	assume_BedBoundingBox(X_positions[2], Y_positions[2], polygons[2], bounding_box_size, bounding_box_size, bounding_box_assumptions);
	assume_BedBoundingBox(X_positions[3], Y_positions[3], polygons[3], bounding_box_size, bounding_box_size, bounding_box_assumptions);		
	
	bool sat = false;
	
	switch (z_solver.check(bounding_box_assumptions))
	{
	case z3::sat:
	{
            #ifdef DEBUG
	    {
		printf("  SATISFIABLE\n");
	    }
	    #endif
	    sat = true;	    
	    break;
	}
	case z3::unsat:	
	{
	    #ifdef DEBUG
	    {
		printf("  UNSATISFIABLE\n");
	    }
	    #endif
	    sat = false;	    
	    break;
	}
	case z3::unknown:
	{
	    #ifdef DEBUG
	    {	    
		printf("  UNKNOWN\n");
	    }
	    #endif
	    break;
	}
	default:
	{
	    break;
	}
	}

	if (sat)
	{
	    z3::model z_model(z_solver.get_model());

	    #ifdef DEBUG
	    {
		printf("Printing model:\n");
		cout << z_model << "\n";
	    }
	    #endif

	    #ifdef DEBUG
	    {
		printf("Printing interpretation:\n");
	    }
	    #endif
	    for (unsigned int i = 0; i < z_model.size(); ++i)
	    {
		double value = z_model.get_const_interp(z_model[i]).as_double();

		#ifdef DEBUG
		{
		    printf("Variable:%s  ", z_model[i].name().str().c_str());
		    cout << z_model.get_const_interp(z_model[i]).as_double() << "\n";
		    printf("value: %.3f\n", value);
		}
		#endif
	    
		if (z_model[i].name().str() == "x_pos-0")
		{
		    poly_1_pos_x = value;
		}
		else if (z_model[i].name().str() == "y_pos-0")
		{
		    poly_1_pos_y = value;
		}
		else if (z_model[i].name().str() == "x_pos-1")
		{
		    poly_2_pos_x = value;
		}
		else if (z_model[i].name().str() == "y_pos-1")
		{
		    poly_2_pos_y = value;
		}
		else if (z_model[i].name().str() == "x_pos-2")
		{
		    poly_3_pos_x = value;
		}
		else if (z_model[i].name().str() == "y_pos-2")
		{
		    poly_3_pos_y = value;
		}
		else if (z_model[i].name().str() == "x_pos-3")
		{
		    poly_4_pos_x = value;
		}
		else if (z_model[i].name().str() == "y_pos-3")
		{
		    poly_4_pos_y = value;
		}
		else if (z_model[i].name().str() == "t_time-0")
		{
		    time_1_t = value;
		}
		else if (z_model[i].name().str() == "t_time-1")
		{
		    time_2_t = value;
		}
		else if (z_model[i].name().str() == "t_time-2")
		{
		    time_3_t = value;
		}
		else if (z_model[i].name().str() == "t_time-3")
		{
		    time_4_t = value;
		}												
	    }

	    #ifdef DEBUG
	    {
		printf("Times: %.3f, %.3f, %.3f, %3f\n",
		       time_1_t,
		       time_2_t,
		       time_3_t,
		       time_4_t);
	    
		printf("preRefined positions: %.3f, %.3f [%.3f], %.3f, %.3f [%.3f], %.3f, %.3f [%.3f], %.3f, %.3f [%.3f]\n",
		       poly_1_pos_x,
		       poly_1_pos_y,
		       time_1_t,
		       poly_2_pos_x,
		       poly_2_pos_y,
		       time_2_t,		   
		       poly_3_pos_x,
		       poly_3_pos_y,
		       time_3_t,		   
		       poly_4_pos_x,
		       poly_4_pos_y,
		       time_4_t);
	    }
	    #endif
	    
	    while (true)
	    {
		vector<Rational> dec_values_X;
		dec_values_X.push_back(poly_1_pos_x);
		dec_values_X.push_back(poly_2_pos_x);
		dec_values_X.push_back(poly_3_pos_x);
		dec_values_X.push_back(poly_4_pos_x);
	    
		vector<Rational> dec_values_Y;
		dec_values_Y.push_back(poly_1_pos_y);
		dec_values_Y.push_back(poly_2_pos_y);
		dec_values_Y.push_back(poly_3_pos_y);
		dec_values_Y.push_back(poly_4_pos_y);

		vector<Rational> dec_values_T;
		dec_values_T.push_back(time_1_t);
		dec_values_T.push_back(time_2_t);
		dec_values_T.push_back(time_3_t);
		dec_values_T.push_back(time_4_t);

		bool refined = refine_SequentialPolygonWeakNonoverlapping(z_solver,
									  z_context,
									  X_positions,
									  Y_positions,
									  T_times,
									  dec_values_X,
									  dec_values_Y,
									  dec_values_T,
									  polygons,
									  unreachable_polygons);

		bool refined_sat = false;

		if (refined)
		{
		    switch (z_solver.check(bounding_box_assumptions))
		    {
		    case z3::sat:
		    {
			#ifdef DEBUG
			{
			    printf("  sat\n");
			}
			#endif
			refined_sat = true;	    
			break;
		    }
		    case z3::unsat:	
		    {
			#ifdef DEBUG
			{
			    printf("  unsat\n");
			}
			#endif
			refined_sat = false;	    
			break;
		    }
		    case z3::unknown:
		    {
			#ifdef DEBUG
			{
			    printf("  unknown\n");
			}
			#endif
			break;
		    }
		    default:
		    {
			break;
		    }
		    }

		    if (refined_sat)
		    {
			z3::model z_model(z_solver.get_model());

			#ifdef DEBUG
			{
			    printf("Printing model:\n");
			    cout << z_model << "\n";
			}
			#endif
    
			for (unsigned int i = 0; i < z_model.size(); ++i)
			{
			    #ifdef DEBUG
			    {
				printf("Variable:%s  ", z_model[i].name().str().c_str());
			    }
			    #endif
			    
			    double value = z_model.get_const_interp(z_model[i]).as_double();
			
			    if (z_model[i].name().str() == "x_pos-0")
			    {
				poly_1_pos_x = value;
			    }
			    else if (z_model[i].name().str() == "y_pos-0")
			    {
				poly_1_pos_y = value;
			    }
			    else if (z_model[i].name().str() == "x_pos-1")
			    {
				poly_2_pos_x = value;
			    }
			    else if (z_model[i].name().str() == "y_pos-1")
			    {
				poly_2_pos_y = value;
			    }
			    else if (z_model[i].name().str() == "x_pos-2")
			    {
				poly_3_pos_x = value;
			    }
			    else if (z_model[i].name().str() == "y_pos-2")
			    {
				poly_3_pos_y = value;
			    }
			    else if (z_model[i].name().str() == "x_pos-3")
			    {
				poly_4_pos_x = value;
			    }
			    else if (z_model[i].name().str() == "y_pos-3")
			    {
				poly_4_pos_y = value;
			    }
			    else if (z_model[i].name().str() == "t_time-0")
			    {
				time_1_t = value;
			    }
			    else if (z_model[i].name().str() == "t_time-1")
			    {
				time_2_t = value;
			    }
			    else if (z_model[i].name().str() == "t_time-2")
			    {
				time_3_t = value;
			    }
			    else if (z_model[i].name().str() == "t_time-3")
			    {
				time_4_t = value;
			    }
			}
			#ifdef DEBUG
			{
			    printf("Refined positions: %.3f, %.3f [%.3f], %.3f, %.3f [%.3f], %.3f, %.3f [%.3f], %.3f, %.3f [%.3f]\n",
				   poly_1_pos_x,
				   poly_1_pos_y,
				   time_1_t,
				   poly_2_pos_x,
				   poly_2_pos_y,
				   time_2_t,		   
				   poly_3_pos_x,
				   poly_3_pos_y,
				   time_3_t,		   
				   poly_4_pos_x,
				   poly_4_pos_y,
				   time_4_t);
			}
			#endif
		    }
		    else
		    {
			break;
		    }
		}
		else
		{
                    #ifdef DEBUG
		    {
			printf("-------------------------------------------------------------------\n");

			_poly_1_pos_x = poly_1_pos_x;
			_poly_1_pos_y = poly_1_pos_y;
			_time_1_t = time_1_t;
			_poly_2_pos_x = poly_2_pos_x;
			_poly_2_pos_y = poly_2_pos_y;
			_time_2_t = time_2_t;
			_poly_3_pos_x = poly_3_pos_x;
			_poly_3_pos_y = poly_3_pos_y;
			_time_3_t = time_3_t;
			_poly_4_pos_x = poly_4_pos_x;
			_poly_4_pos_y = poly_4_pos_y;
			_time_4_t = time_4_t;
		    }
		    #endif

		    last_solvable_bounding_box_size = bounding_box_size;
		    break;
		}
	    }
	}
	else
	{
	    break;
	}	
    }
    #ifdef DEBUG
    finish = clock();
    #endif

    REQUIRE(last_solvable_bounding_box_size > 0);

    #ifdef DEBUG
    {
	printf("Solvable bounding box: %d\n", last_solvable_bounding_box_size);

	printf("Final spatio-temporal positions: %.3f, %.3f [%.3f], %.3f, %.3f [%.3f], %.3f, %.3f [%.3f], %.3f, %.3f [%.3f]\n",
	       _poly_1_pos_x,
	       _poly_1_pos_y,
	       _time_1_t,
	       _poly_2_pos_x,
	       _poly_2_pos_y,
	       _time_2_t,		   
	       _poly_3_pos_x,
	       _poly_3_pos_y,
	       _time_3_t,		   
	       _poly_4_pos_x,
	       _poly_4_pos_y,
	       _time_4_t);
    }
    #endif
    
    #ifdef DEBUG
    {
	for (int i = 0; i < 2; ++i)
	{	
	    double value = X_positions[i];
	    printf("Orig X: %.3f\n", value);

	    value = Y_positions[i];
	    printf("Orig Y: %.3f\n", value);	
	}
    }
    #endif
    
    SVG preview_svg("sequential_test_4.svg");

    Polygon display_pro_polygon_1 = scale_UP(unreachable_polygons[0], _poly_1_pos_x, _poly_1_pos_y);
    Polygon display_pro_polygon_2 = scale_UP(unreachable_polygons[1], _poly_2_pos_x, _poly_2_pos_y);
    Polygon display_pro_polygon_3 = scale_UP(unreachable_polygons[2], _poly_3_pos_x, _poly_3_pos_y);
    Polygon display_pro_polygon_4 = scale_UP(unreachable_polygons[3], _poly_4_pos_x, _poly_4_pos_y);

    preview_svg.draw(display_pro_polygon_1, "lightgrey");
    preview_svg.draw(display_pro_polygon_2, "lightgrey");
    preview_svg.draw(display_pro_polygon_3, "lightgrey");
    preview_svg.draw(display_pro_polygon_4, "lightgrey");
    
    Polygon display_polygon_1 = scale_UP(polygons[0], _poly_1_pos_x, _poly_1_pos_y);
    Polygon display_polygon_2 = scale_UP(polygons[1], _poly_2_pos_x, _poly_2_pos_y);
    Polygon display_polygon_3 = scale_UP(polygons[2], _poly_3_pos_x, _poly_3_pos_y);
    Polygon display_polygon_4 = scale_UP(polygons[3], _poly_4_pos_x, _poly_4_pos_y);

    preview_svg.draw(display_polygon_1, "green");
    preview_svg.draw(display_polygon_2, "blue");
    preview_svg.draw(display_polygon_3, "red");
    preview_svg.draw(display_polygon_4, "grey");        
    
    preview_svg.Close();

    #ifdef DEBUG
    {
	printf("Time: %.3f\n", (finish - start) / (double)CLOCKS_PER_SEC);
    }
    #endif
    INFO("Testing sequential 4 ... finished");    
}


//TEST_CASE("Sequential test 5", "[Sequential Arrangement Core]")
void sequential_test_5(void)
{
    #ifdef DEBUG
    clock_t start, finish;
    #endif
    
    INFO("Testing sequential 5 ...");

    #ifdef DEBUG
    start = clock();
    #endif

    z3::context z_context;    
    z3::expr_vector X_positions(z_context);
    z3::expr_vector Y_positions(z_context);
    z3::expr_vector T_times(z_context);
    
    for (int i = 0; i < 4; ++i)
    {
	string name = "x_pos-" + to_string(i);
	#ifdef DEBUG
	{
	    printf("i:%d\n", i);	
	    printf("name: %s\n", name.c_str());
	}
	#endif
	X_positions.push_back(expr(z_context.real_const(name.c_str())));
    }

    for (int i = 0; i < 4; ++i)
    {
	string name = "y_pos-" + to_string(i);
	#ifdef DEBUG
	{
	    printf("name: %s\n", name.c_str());
	}
	#endif
	Y_positions.push_back(expr(z_context.real_const(name.c_str())));
    }

    for (unsigned int i = 0; i < polygon_1.points.size(); ++i)
    {
	string name = "t_time-" + to_string(i);
	#ifdef DEBUG
	{	
	    printf("name: %s\n", name.c_str());
	}
	#endif
	T_times.push_back(expr(z_context.real_const(name.c_str())));	
    }
    
    z3::solver z_solver(z_context);    
    Z3_global_param_set("parallel.enable", "false");    

    vector<Polygon> polygons;
    polygons.push_back(polygon_1);
    polygons.push_back(polygon_2);
    polygons.push_back(polygon_3);
    polygons.push_back(polygon_4);

    vector<vector<Polygon> > unreachable_polygons;
    unreachable_polygons.push_back(unreachable_polygons_1);
    unreachable_polygons.push_back(unreachable_polygons_2);
    unreachable_polygons.push_back(unreachable_polygons_3);
    unreachable_polygons.push_back(unreachable_polygons_4);

    #ifdef DEBUG
    {
	printf("pp: %ld\n", unreachable_polygons[0].size());
	printf("pp: %ld\n", unreachable_polygons[1].size());
	printf("pp: %ld\n", unreachable_polygons[2].size());
	printf("pp: %ld\n", unreachable_polygons[3].size());
    }
    #endif
    
    introduce_SequentialPolygonWeakNonoverlapping(z_solver,
						  z_context,
						  X_positions,
						  Y_positions,
						  T_times,
						  polygons,
						  unreachable_polygons);
    introduce_TemporalOrdering(z_solver, z_context, T_times, 16, polygons);
  
    #ifdef DEBUG
    {
	printf("Printing solver status:\n");
	cout << z_solver << "\n";
    
	printf("Printing smt status:\n");
	cout << z_solver.to_smt2() << "\n";
    }
    #endif

    int last_solvable_bounding_box_size = -1;
    Rational poly_1_pos_x, poly_1_pos_y, poly_2_pos_x, poly_2_pos_y, poly_3_pos_x, poly_3_pos_y, poly_4_pos_x, poly_4_pos_y;
    Rational time_1_t, time_2_t, time_3_t, time_4_t;

    Rational _poly_1_pos_x, _poly_1_pos_y, _poly_2_pos_x, _poly_2_pos_y, _poly_3_pos_x, _poly_3_pos_y, _poly_4_pos_x, _poly_4_pos_y;
    Rational _time_1_t, _time_2_t, _time_3_t, _time_4_t;    
    
    for (int bounding_box_size = 200; bounding_box_size > 10; bounding_box_size -= 4)
    {
	#ifdef DEBUG
	{
	    printf("BB: %d\n", bounding_box_size);
	}
	#endif
	z3::expr_vector bounding_box_assumptions(z_context);

	assume_BedBoundingBox(X_positions[0], Y_positions[0], polygons[0], bounding_box_size, bounding_box_size, bounding_box_assumptions);
	assume_BedBoundingBox(X_positions[1], Y_positions[1], polygons[1], bounding_box_size, bounding_box_size, bounding_box_assumptions);
	assume_BedBoundingBox(X_positions[2], Y_positions[2], polygons[2], bounding_box_size, bounding_box_size, bounding_box_assumptions);
	assume_BedBoundingBox(X_positions[3], Y_positions[3], polygons[3], bounding_box_size, bounding_box_size, bounding_box_assumptions);		
	
	bool sat = false;
	
	switch (z_solver.check(bounding_box_assumptions))
	{
	case z3::sat:
	{
	    #ifdef DEBUG
	    {
		printf("  SATISFIABLE\n");
	    }
	    #endif
	    sat = true;	    
	    break;
	}
	case z3::unsat:	
	{
	    #ifdef DEBUG
	    {
		printf("  UNSATISFIABLE\n");
	    }
	    #endif
	    sat = false;	    
	    break;
	}
	case z3::unknown:
	{
	    #ifdef DEBUG
	    {
		printf("  UNKNOWN\n");
	    }
	    #endif
	    break;
	}
	default:
	{
	    break;
	}
	}

	if (sat)
	{
	    z3::model z_model(z_solver.get_model());

	    #ifdef DEBUG
	    {
		printf("Printing model:\n");
		cout << z_model << "\n";

		printf("Printing interpretation:\n");    		
	    }
	    #endif
	    
	    for (unsigned int i = 0; i < z_model.size(); ++i)
	    {
		#ifdef DEBUG
		{
		    double value = z_model.get_const_interp(z_model[i]).as_double();
		    
		    printf("Variable:%s  ", z_model[i].name().str().c_str());		
		    cout << z_model.get_const_interp(z_model[i]).as_double() << "\n";
		    printf("value: %.3f\n", value);
		}
		#endif

		Rational rational(z_model.get_const_interp(z_model[i]).numerator().as_int64(), z_model.get_const_interp(z_model[i]).denominator().as_int64());
	    
		if (z_model[i].name().str() == "x_pos-0")
		{
		    poly_1_pos_x = rational;
		}
		else if (z_model[i].name().str() == "y_pos-0")
		{
		    poly_1_pos_y = rational;
		}
		else if (z_model[i].name().str() == "x_pos-1")
		{
		    poly_2_pos_x = rational;
		}
		else if (z_model[i].name().str() == "y_pos-1")
		{
		    poly_2_pos_y = rational;
		}
		else if (z_model[i].name().str() == "x_pos-2")
		{
		    poly_3_pos_x = rational;
		}
		else if (z_model[i].name().str() == "y_pos-2")
		{
		    poly_3_pos_y = rational;
		}
		else if (z_model[i].name().str() == "x_pos-3")
		{
		    poly_4_pos_x = rational;
		}
		else if (z_model[i].name().str() == "y_pos-3")
		{
		    poly_4_pos_y = rational;
		}
		else if (z_model[i].name().str() == "t_time-0")
		{
		    time_1_t = rational;
		}
		else if (z_model[i].name().str() == "t_time-1")
		{
		    time_2_t = rational;
		}
		else if (z_model[i].name().str() == "t_time-2")
		{
		    time_3_t = rational;
		}
		else if (z_model[i].name().str() == "t_time-3")
		{
		    time_4_t = rational;
		}												
	    }

	    #ifdef DEBUG
	    {
		printf("Times: %.3f, %.3f, %.3f, %3f\n",
		       time_1_t.as_double(),
		       time_2_t.as_double(),
		       time_3_t.as_double(),
		       time_4_t.as_double());
	    
		printf("preRefined positions: %.3f, %.3f [%.3f], %.3f, %.3f [%.3f], %.3f, %.3f [%.3f], %.3f, %.3f [%.3f]\n",
		       poly_1_pos_x.as_double(),
		       poly_1_pos_y.as_double(),
		       time_1_t.as_double(),
		       poly_2_pos_x.as_double(),
		       poly_2_pos_y.as_double(),
		       time_2_t.as_double(),		   
		       poly_3_pos_x.as_double(),
		       poly_3_pos_y.as_double(),
		       time_3_t.as_double(),		   
		       poly_4_pos_x.as_double(),
		       poly_4_pos_y.as_double(),
		       time_4_t.as_double());
	    }
	    #endif
	    
	    while (true)
	    {
		vector<Rational> dec_values_X;
		dec_values_X.push_back(poly_1_pos_x);
		dec_values_X.push_back(poly_2_pos_x);
		dec_values_X.push_back(poly_3_pos_x);
		dec_values_X.push_back(poly_4_pos_x);
	    
		vector<Rational> dec_values_Y;
		dec_values_Y.push_back(poly_1_pos_y);
		dec_values_Y.push_back(poly_2_pos_y);
		dec_values_Y.push_back(poly_3_pos_y);
		dec_values_Y.push_back(poly_4_pos_y);

		vector<Rational> dec_values_T;
		dec_values_T.push_back(time_1_t);
		dec_values_T.push_back(time_2_t);
		dec_values_T.push_back(time_3_t);
		dec_values_T.push_back(time_4_t);

		bool refined = refine_SequentialPolygonWeakNonoverlapping(z_solver,
									  z_context,
									  X_positions,
									  Y_positions,
									  T_times,
									  dec_values_X,
									  dec_values_Y,
									  dec_values_T,
									  polygons,
									  unreachable_polygons);

		bool refined_sat = false;

		if (refined)
		{
		    switch (z_solver.check(bounding_box_assumptions))
		    {
		    case z3::sat:
		    {
			#ifdef DEBUG
			{
			    printf("  sat\n");
			}
			#endif
			refined_sat = true;	    
			break;
		    }
		    case z3::unsat:	
		    {
			#ifdef DEBUG
			{
			    printf("  unsat\n");
			}
			#endif
			refined_sat = false;	    
			break;
		    }
		    case z3::unknown:
		    {
                        #ifdef DEBUG
			{
			    printf("  unknown\n");
			}
			#endif
			break;
		    }
		    default:
		    {
			break;
		    }
		    }

		    if (refined_sat)
		    {
			z3::model z_model(z_solver.get_model());
			
			#ifdef DEBUG
			{
			    printf("Printing model:\n");
			    cout << z_model << "\n";
			}
			#endif
    
			for (unsigned int i = 0; i < z_model.size(); ++i)
			{
			    #ifdef DEBUG
			    {
				double value = z_model.get_const_interp(z_model[i]).as_double();				
				printf("Variable:%s  ", z_model[i].name().str().c_str());
				printf("value: %.3f\n", value);
			    }
			    #endif

			    Rational rational(z_model.get_const_interp(z_model[i]).numerator().as_int64(), z_model.get_const_interp(z_model[i]).denominator().as_int64());			    
			
			    if (z_model[i].name().str() == "x_pos-0")
			    {
				poly_1_pos_x = rational;
			    }
			    else if (z_model[i].name().str() == "y_pos-0")
			    {
				poly_1_pos_y = rational;
			    }
			    else if (z_model[i].name().str() == "x_pos-1")
			    {
				poly_2_pos_x = rational;
			    }
			    else if (z_model[i].name().str() == "y_pos-1")
			    {
				poly_2_pos_y = rational;
			    }
			    else if (z_model[i].name().str() == "x_pos-2")
			    {
				poly_3_pos_x = rational;
			    }
			    else if (z_model[i].name().str() == "y_pos-2")
			    {
				poly_3_pos_y = rational;
			    }
			    else if (z_model[i].name().str() == "x_pos-3")
			    {
				poly_4_pos_x = rational;
			    }
			    else if (z_model[i].name().str() == "y_pos-3")
			    {
				poly_4_pos_y = rational;
			    }
			    else if (z_model[i].name().str() == "t_time-0")
			    {
				time_1_t = rational;
			    }
			    else if (z_model[i].name().str() == "t_time-1")
			    {
				time_2_t = rational;
			    }
			    else if (z_model[i].name().str() == "t_time-2")
			    {
				time_3_t = rational;
			    }
			    else if (z_model[i].name().str() == "t_time-3")
			    {
				time_4_t = rational;
			    }
			}
			#ifdef DEBUG
			{
			    printf("Refined positions: %.3f, %.3f [%.3f], %.3f, %.3f [%.3f], %.3f, %.3f [%.3f], %.3f, %.3f [%.3f]\n",
				   poly_1_pos_x.as_double(),
				   poly_1_pos_y.as_double(),
				   time_1_t.as_double(),
				   poly_2_pos_x.as_double(),
				   poly_2_pos_y.as_double(),
				   time_2_t.as_double(),		   
				   poly_3_pos_x.as_double(),
				   poly_3_pos_y.as_double(),
				   time_3_t.as_double(),		   
				   poly_4_pos_x.as_double(),
				   poly_4_pos_y.as_double(),
				   time_4_t.as_double());
			}
			#endif			    
		    }
		    else
		    {
			break;
		    }
		}
		else
		{
		    #ifdef DEBUG
		    {
			printf("-------------------------------------------------------------------\n");
		    }
		    #endif
		    _poly_1_pos_x = poly_1_pos_x;
		    _poly_1_pos_y = poly_1_pos_y;
		    _time_1_t = time_1_t;
		    _poly_2_pos_x = poly_2_pos_x;
		    _poly_2_pos_y = poly_2_pos_y;
		    _time_2_t = time_2_t;
		    _poly_3_pos_x = poly_3_pos_x;
		    _poly_3_pos_y = poly_3_pos_y;
		    _time_3_t = time_3_t;
		    _poly_4_pos_x = poly_4_pos_x;
		    _poly_4_pos_y = poly_4_pos_y;
		    _time_4_t = time_4_t;
		    
		    last_solvable_bounding_box_size = bounding_box_size;
		    break;
		}
	    }
	}
	else
	{
	    break;
	}	
    }
    #ifdef DEBUG
    finish = clock();
    #endif
    REQUIRE(last_solvable_bounding_box_size > 0);

    #ifdef DEBUG
    {
	printf("Solvable bounding box: %d\n", last_solvable_bounding_box_size);

	printf("Final spatio-temporal positions: %.3f, %.3f [%.3f], %.3f, %.3f [%.3f], %.3f, %.3f [%.3f], %.3f, %.3f [%.3f]\n",
	       _poly_1_pos_x.as_double(),
	       _poly_1_pos_y.as_double(),
	       _time_1_t.as_double(),
	       _poly_2_pos_x.as_double(),
	       _poly_2_pos_y.as_double(),
	       _time_2_t.as_double(),		   
	       _poly_3_pos_x.as_double(),
	       _poly_3_pos_y.as_double(),
	       _time_3_t.as_double(),		   
	       _poly_4_pos_x.as_double(),
	       _poly_4_pos_y.as_double(),
	       _time_4_t.as_double());
    }
    #endif

    #ifdef DEBUG
    {
	for (int i = 0; i < 2; ++i)
	{	
	    double value = X_positions[i].as_double();
	    printf("Orig X: %.3f\n", value);

	    value = Y_positions[i].as_double();
	    printf("Orig Y: %.3f\n", value);	
	}
    }
    #endif
    
    
    SVG preview_svg("sequential_test_5.svg");

    for (unsigned int i = 0; i < unreachable_polygons[0].size(); ++i)
    {
	Polygon display_pro_polygon_1 = scale_UP(unreachable_polygons[0][i], _poly_1_pos_x.as_double(), _poly_1_pos_y.as_double());
	preview_svg.draw(display_pro_polygon_1, "lightgrey");
    }

    for (unsigned int i = 0; i < unreachable_polygons[1].size(); ++i)
    {
	Polygon display_pro_polygon_2 = scale_UP(unreachable_polygons[1][i], _poly_2_pos_x.as_double(), _poly_2_pos_y.as_double());
	preview_svg.draw(display_pro_polygon_2, "lightgrey");    
    }

    for (unsigned int i = 0; i < unreachable_polygons[2].size(); ++i)
    {    
	Polygon display_pro_polygon_3 = scale_UP(unreachable_polygons[2][i], _poly_3_pos_x.as_double(), _poly_3_pos_y.as_double());
	preview_svg.draw(display_pro_polygon_3, "lightgrey");	
    }

    for (unsigned int i = 0; i < unreachable_polygons[3].size(); ++i)
    {        
	Polygon display_pro_polygon_4 = scale_UP(unreachable_polygons[3][i], _poly_4_pos_x.as_double(), _poly_4_pos_y.as_double());
	preview_svg.draw(display_pro_polygon_4, "lightgrey");	
    }
  
    Polygon display_polygon_1 = scale_UP(polygons[0], _poly_1_pos_x.as_double(), _poly_1_pos_y.as_double());
    Polygon display_polygon_2 = scale_UP(polygons[1], _poly_2_pos_x.as_double(), _poly_2_pos_y.as_double());
    Polygon display_polygon_3 = scale_UP(polygons[2], _poly_3_pos_x.as_double(), _poly_3_pos_y.as_double());
    Polygon display_polygon_4 = scale_UP(polygons[3], _poly_4_pos_x.as_double(), _poly_4_pos_y.as_double());

    preview_svg.draw(display_polygon_1, "green");
    preview_svg.draw(display_polygon_2, "blue");
    preview_svg.draw(display_polygon_3, "red");
    preview_svg.draw(display_polygon_4, "grey");        
    
    preview_svg.Close();    

    #ifdef DEBUG
    {
	printf("Time: %.3f\n", (finish - start) / (double)CLOCKS_PER_SEC);
    }
    #endif
    INFO("Testing sequential 5 ... finished");
}


TEST_CASE("Sequential test 6", "[Sequential Arrangement Core]")
{
    #ifdef DEBUG
    clock_t start, finish;
    #endif
    
    INFO("Testing polygon 6 ...");

    #ifdef DEBUG
    start = clock();
    #endif

    SolverConfiguration solver_configuration;
    solver_configuration.plate_bounding_box = BoundingBox({0,0}, {SEQ_PRUSA_MK3S_X_SIZE / SEQ_SLICER_SCALE_FACTOR, SEQ_PRUSA_MK3S_Y_SIZE / SEQ_SLICER_SCALE_FACTOR});    

    vector<Polygon> polygons;
    vector<Polygon> unreachable_polygons;
    
    vector<int> remaining_polygons;
    vector<int> polygon_index_map;
    vector<int> decided_polygons;

    polygons.push_back(polygon_1);
    unreachable_polygons.push_back(unreachable_polygon_1);
    polygons.push_back(polygon_2);
    unreachable_polygons.push_back(unreachable_polygon_2);
    polygons.push_back(polygon_3);
    unreachable_polygons.push_back(unreachable_polygon_3);
    polygons.push_back(polygon_4);
    unreachable_polygons.push_back(unreachable_polygon_4);
    
    polygons.push_back(polygon_1);
    unreachable_polygons.push_back(unreachable_polygon_1);    
    polygons.push_back(polygon_2);
    unreachable_polygons.push_back(unreachable_polygon_2);    
    polygons.push_back(polygon_3);
    unreachable_polygons.push_back(unreachable_polygon_3);    
    polygons.push_back(polygon_4);
    unreachable_polygons.push_back(unreachable_polygon_4);    

    polygons.push_back(polygon_1);
    unreachable_polygons.push_back(unreachable_polygon_1);    
    polygons.push_back(polygon_2);
    unreachable_polygons.push_back(unreachable_polygon_2);    
    polygons.push_back(polygon_3);
    unreachable_polygons.push_back(unreachable_polygon_3);    
    polygons.push_back(polygon_4);
    unreachable_polygons.push_back(unreachable_polygon_4);    

    polygons.push_back(polygon_1);
    unreachable_polygons.push_back(unreachable_polygon_1);    
    polygons.push_back(polygon_2);
    unreachable_polygons.push_back(unreachable_polygon_2);    
    polygons.push_back(polygon_3);
    unreachable_polygons.push_back(unreachable_polygon_3);    
    polygons.push_back(polygon_4);
    unreachable_polygons.push_back(unreachable_polygon_4);    

    polygons.push_back(polygon_1);
    unreachable_polygons.push_back(unreachable_polygon_1);    
    polygons.push_back(polygon_2);
    unreachable_polygons.push_back(unreachable_polygon_2);    
    polygons.push_back(polygon_3);
    unreachable_polygons.push_back(unreachable_polygon_3);    
    polygons.push_back(polygon_4);
    unreachable_polygons.push_back(unreachable_polygon_4);

    #ifdef DEBUG
    {
	for (unsigned int j = 0; j < unreachable_polygons_1.size(); ++j)
	{
	    for (unsigned int k = 0; k < unreachable_polygons_1[j].points.size(); ++k)
	    {
		printf("    Ppxy: %d, %d\n", unreachable_polygons_1[j].points[k].x(), unreachable_polygons_1[j].points[k].y());
	    }
	}
    }
    #endif

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

	bool optimized = optimize_SubglobalSequentialPolygonNonoverlapping(solver_configuration,
									   poly_positions_X,
									   poly_positions_Y,
									   times_T,
									   polygons,
									   unreachable_polygons,
									   polygon_index_map,
									   decided_polygons,
									   remaining_polygons);
	REQUIRE(optimized);

	#ifdef DEBUG
	{
	    printf("----> Optimization finished <----\n");
	}
	#endif
	
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
	
	    SVG preview_svg("sequential_test_6.svg");

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
		    
		    Polygon display_unreachable_polygon = scale_UP(unreachable_polygons[decided_polygons[i]],
								  poly_positions_X[decided_polygons[i]].as_double(),
								  poly_positions_Y[decided_polygons[i]].as_double());
		    {
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
    INFO("Testing sequential 6 ... finished");
}


TEST_CASE("Sequential test 7", "[Sequential Arrangement Core]")
{
    #ifdef DEBUG
    clock_t start, finish;
    #endif
    
    INFO("Testing polygon 7 ...");

    #ifdef DEBUG
    start = clock();
    #endif

    SolverConfiguration solver_configuration;
    solver_configuration.plate_bounding_box = BoundingBox({0,0}, {SEQ_PRUSA_MK3S_X_SIZE / SEQ_SLICER_SCALE_FACTOR, SEQ_PRUSA_MK3S_Y_SIZE / SEQ_SLICER_SCALE_FACTOR});    

    vector<Polygon> polygons;
    vector<vector<Polygon> > unreachable_polygons;
    
    vector<int> remaining_polygons;
    vector<int> polygon_index_map;
    vector<int> decided_polygons;

    polygons.push_back(polygon_1);
    unreachable_polygons.push_back(unreachable_polygons_1);
    polygons.push_back(polygon_2);
    unreachable_polygons.push_back(unreachable_polygons_2);    
    polygons.push_back(polygon_3);
    unreachable_polygons.push_back(unreachable_polygons_3);    
    polygons.push_back(polygon_4);
    unreachable_polygons.push_back(unreachable_polygons_4);    

    polygons.push_back(polygon_1);
    unreachable_polygons.push_back(unreachable_polygons_1);
    polygons.push_back(polygon_2);
    unreachable_polygons.push_back(unreachable_polygons_2);    
    polygons.push_back(polygon_3);
    unreachable_polygons.push_back(unreachable_polygons_3);
    
    polygons.push_back(polygon_1);
    unreachable_polygons.push_back(unreachable_polygons_1);    
    polygons.push_back(polygon_2);
    unreachable_polygons.push_back(unreachable_polygons_2);    
    polygons.push_back(polygon_3);
    unreachable_polygons.push_back(unreachable_polygons_3);    
    polygons.push_back(polygon_4);
    unreachable_polygons.push_back(unreachable_polygons_4);    

    #ifdef DEBUG
    {
	for (unsigned int j = 0; j < unreachable_polygons_1.size(); ++j)
	{
	    for (unsigned int k = 0; k < unreachable_polygons_1[j].points.size(); ++k)
	    {
		printf("    Ppxy: %d, %d\n", unreachable_polygons_1[j].points[k].x(), unreachable_polygons_1[j].points[k].y());
	    }
	}
    }
    #endif

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
	
	bool optimized = optimize_SubglobalSequentialPolygonNonoverlapping(solver_configuration,
									   poly_positions_X,
									   poly_positions_Y,
									   times_T,
									   polygons,
									   unreachable_polygons,
									   polygon_index_map,
									   decided_polygons,
									   remaining_polygons);
	REQUIRE(optimized);

	#ifdef DEBUG
	{
	    printf("----> Optimization finished <----\n");
	}
	#endif
	
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
	
	    SVG preview_svg("sequential_test_7.svg");

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
    INFO("Testing sequential 7 ... finished");    
}    



/*----------------------------------------------------------------*/
