/*================================================================*/
/*
 * Author:  Pavel Surynek, 2023 - 2025
 * Company: Prusa Research
 *
 * File:    sequential_decimator.cpp
 *
 * Polygon decimator utility (especially for extruder models).
 */
/*================================================================*/


#include <sstream>
#include <iostream>
#include <fstream>

#include "libslic3r/Polygon.hpp"
#include "libslic3r/Geometry/ConvexHull.hpp"
#include "libslic3r/SVG.hpp"

#include "seq_utilities.hpp"

#include "sequential_decimator.hpp"

/*----------------------------------------------------------------*/

using namespace Slic3r;
using namespace Sequential;


/*----------------------------------------------------------------*/

#define SCALE_FACTOR                  50000.0


/*----------------------------------------------------------------*/

//const Point polygon_offset_0(28000000, -16000000);  // body
//const Point polygon_offset_1(-3000000, -60000000);  // hose
//const Point polygon_offset_2(28000000, -16000000);  // fan
//const Point polygon_offset_3(0,-24000000);          // gantry
//const Point polygon_offset_4(0,0);                  // nozzle

const int SEQ_PRUSA_MK3S_X_SIZE = 2500;
const int SEQ_PRUSA_MK3S_Y_SIZE = 2100;    


/*----------------------------------------------------------------*/


void print_IntroductoryMessage(void)
{
    printf("----------------------------------------------------------------\n");
    printf("Polygon decimation utility\n");
    printf("(C) 2024 Prusa Research \n");
    printf("================================================================\n");	
}


void print_ConcludingMessage(void)
{
    printf("----------------------------------------------------------------\n");
}


void print_Help(void)
{
    printf("Usage:\n");
    printf("sequential_decimator [--input-file=<string>]\n");
    printf("                     [--output-file=<string>]\n");
    printf("                     [--tolerance=<double>]\n");
    printf("                     [--x-pos=<double> (in mm)]\n");
    printf("                     [--y-pos=<double> (in mm)]\n");
    printf("                     [--x-nozzle=<int> (in coord_t)]\n");
    printf("                     [--y-nozzle=<int> (in coord_t)]\n");            
    printf("                     [--help]\n");		
    printf("\n");
    printf("\n");
    printf("Defaults: --input-file=arrange_data_export.txt\n");
    printf("          --output-file=arrange_data_import.txt\n");
    printf("          --x-pos='random'\n");
    printf("          --y-pos='random'\n");
    printf("          --x-nozzle=0\n");
    printf("          --y-nozzle=0\n");                
    printf("          --tolerance=400000 \n");    
    printf("\n");
}


int parse_CommandLineParameter(const string &parameter, CommandParameters &command_parameters)
{
    if (parameter.find("--input-file=") == 0)
    {
	command_parameters.input_filename = parameter.substr(13, parameter.size());
    }
    else if (parameter.find("--output-file=") == 0)
    {
	command_parameters.output_filename = parameter.substr(14, parameter.size());
    }
    else if (parameter.find("--tolerance=") == 0)
    {
	command_parameters.tolerance = std::atof(parameter.substr(12, parameter.size()).c_str());
    }
    else if (parameter.find("--x-pos=") == 0)
    {
	command_parameters.x_position = std::atof(parameter.substr(8, parameter.size()).c_str());
	command_parameters.random_position = false;	
	
    }
    else if (parameter.find("--y-pos=") == 0)
    {
	command_parameters.y_position = std::atof(parameter.substr(8, parameter.size()).c_str());
	command_parameters.random_position = false;
    }
    else if (parameter.find("--x-nozzle=") == 0)
    {
	command_parameters.x_nozzle = std::atoi(parameter.substr(11, parameter.size()).c_str());
	
    }
    else if (parameter.find("--y-nozzle=") == 0)
    {
	command_parameters.y_nozzle = std::atoi(parameter.substr(11, parameter.size()).c_str());
    }            
    else if (parameter.find("--help") == 0)
    {
	command_parameters.help = true;
    }    
    else
    {
	return -1;
    }
    return 0;
}


void save_DecimatedPolygons(const CommandParameters            &command_parameters,
			    const std::vector<Slic3r::Polygon> &decimated_polygons)
{
    std::ofstream out(command_parameters.output_filename);
    if (!out)
        throw std::runtime_error("CANNOT CREATE OUTPUT FILE");

    Point nozzle_offset(-command_parameters.x_nozzle, -command_parameters.y_nozzle);
	
    for (unsigned int i = 0; i < decimated_polygons.size(); ++i)
    {
	out << "[" << i << "]" << endl;
	out << "{" << endl;

	Slic3r::Polygon shift_polygon = scaleUp_PolygonForSlicer(1,
								 decimated_polygons[i],
								 (command_parameters.x_position * SEQ_SLICER_SCALE_FACTOR) * 10,
								 (command_parameters.y_position * SEQ_SLICER_SCALE_FACTOR) * 10);
	shift_Polygon(shift_polygon, nozzle_offset);
    
	for (const auto& point: shift_polygon.points)
	{
	    out << "  { " << point.x() << ",  " << point.y() << "}," << endl;
	}
	out << "}" << endl;
    }
}


int decimate_Polygons(const CommandParameters &command_parameters)
{
    clock_t start, finish;
    
    printf("Decimation ...\n");

    start = clock();

    SolverConfiguration solver_configuration;
    std::vector<ObjectToPrint> objects_to_print = load_exported_data_from_file(command_parameters.input_filename);    

    std::vector<Slic3r::Polygon> decimated_polygons;
    std::vector<std::vector<Slic3r::Polygon> > unreachable_polygons;

    printf("  Decimating objects (polygons) ...\n");

    for (unsigned int i = 0; i < objects_to_print.size(); ++i)
    {
	for (unsigned int j = 0; j < objects_to_print[i].pgns_at_height.size(); ++j)
	{
	    //coord_t height = objects_to_print[i].pgns_at_height[j].first;

	    if (!objects_to_print[i].pgns_at_height[j].second.points.empty())
	    {
		Polygon decimated_polygon;
		//ground_PolygonByFirstPoint(objects_to_print[i].pgns_at_height[j].second);

		decimate_PolygonForSequentialSolver(command_parameters.tolerance,
						    objects_to_print[i].pgns_at_height[j].second,
						    decimated_polygon,
						    false);

		decimated_polygons.push_back(decimated_polygon);
	    }	    
	}
    }
    printf("  Decimating objects (polygons) ... finished\n");    

    Point nozzle_offset(-command_parameters.x_nozzle, -command_parameters.y_nozzle);
	
    for (unsigned int i = 0; i < decimated_polygons.size(); ++i)
    {
	printf("  [%d]\n", i);
	Slic3r::Polygon	shift_polygon = decimated_polygons[i];
	shift_Polygon(shift_polygon, nozzle_offset);	

	shift_polygon = scaleUp_PolygonForSlicer(1,
						 shift_polygon,
						 (command_parameters.x_position * SEQ_SLICER_SCALE_FACTOR) * 10,
						 (command_parameters.y_position * SEQ_SLICER_SCALE_FACTOR) * 10);
	
	for (const auto &point: shift_polygon.points)
	{	    
	    cout << "    " << point.x() << "  " << point.y() << endl;
	}

	BoundingBox bounding_box = get_extents(shift_polygon);

	cout << "    BB" << endl;
	cout << "    " << bounding_box.min.x() << "  " << bounding_box.min.y() << endl;
	cout << "    " << bounding_box.max.x() << "  " << bounding_box.max.y() << endl;
	cout << endl;	
    }
    
    if (command_parameters.output_filename != "")
    {
	save_DecimatedPolygons(command_parameters, decimated_polygons);
    }
    
    string svg_filename = "sequential_decimator.svg";
    SVG preview_svg(svg_filename);
    solver_configuration.plate_bounding_box = BoundingBox({0,0}, {SEQ_PRUSA_MK3S_X_SIZE, SEQ_PRUSA_MK3S_Y_SIZE});

    printf("  Generating output SVG ...\n");
    for (unsigned int i = 0; i < decimated_polygons.size(); ++i)
    {
	Polygon transformed_polygon;
	Polygon shift_polygon = decimated_polygons[i];

	shift_Polygon(shift_polygon, nozzle_offset);
	
	if (command_parameters.random_position)
	{
	    transformed_polygon = transform_UpsideDown(solver_configuration,
						       scaleUp_PolygonForSlicer(1,
										shift_polygon,
										(solver_configuration.plate_bounding_box.min.x() + rand() % (solver_configuration.plate_bounding_box.max.x() - solver_configuration.plate_bounding_box.min.x())) * SEQ_SLICER_SCALE_FACTOR,
										(solver_configuration.plate_bounding_box.min.y() + rand() % (solver_configuration.plate_bounding_box.max.y() - solver_configuration.plate_bounding_box.min.y()) * SEQ_SLICER_SCALE_FACTOR)));
	}
	else
	{
	    transformed_polygon = transform_UpsideDown(solver_configuration,
						       scaleUp_PolygonForSlicer(1,
										shift_polygon,
										(command_parameters.x_position * SEQ_SLICER_SCALE_FACTOR) * 10,
										(command_parameters.y_position * SEQ_SLICER_SCALE_FACTOR) * 10));
	}	
	Polygon display_polygon = scaleDown_PolygonForSequentialSolver(2, transformed_polygon);
	
	string color;
	
	switch(i % 16)
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
	    color = "firebrick";	    
	    break;
	}
	case 11:
	{
	    color = "violet";
	    break;
	}	
	case 12:
	{
	    color = "midnightblue";
	    break;
	}
	case 13:
	{
	    color = "khaki";
	    break;
	}
	case 14:
	{
	    color = "darkslategrey";
	    break;
	}
	case 15:
	{
	    color = "hotpink";
	    break;
	}			    	    	    
	
	default:
	{
	    break;
	}
	}
		
	preview_svg.draw(display_polygon, color);
    }

    // general plate polygons are currently not supported
    assert(solver_configuration.plate_bounding_polygon.points.size() == 0);
    
    Polygon bed_polygon({ { solver_configuration.plate_bounding_box.min.x(), solver_configuration.plate_bounding_box.min.y() },
			  { solver_configuration.plate_bounding_box.max.x(), solver_configuration.plate_bounding_box.min.y() },
			  { solver_configuration.plate_bounding_box.max.x(), solver_configuration.plate_bounding_box.max.y() },
			  { solver_configuration.plate_bounding_box.min.x(), solver_configuration.plate_bounding_box.max.y() } });
    
    Polygon display_bed_polygon = scaleUp_PolygonForSlicer(SEQ_SVG_SCALE_FACTOR,
							   bed_polygon,
							   0,
							   0);
    preview_svg.draw_outline(display_bed_polygon, "black");    
	    
    preview_svg.Close();
    printf("  Generating output SVG ... finised\n");    
	
    finish = clock();

    printf("Decimation ... finished\n");
    printf("Total CPU time: %.3f\n", (finish - start) / (double)CLOCKS_PER_SEC);

    return 0;
}


/*----------------------------------------------------------------------------*/
// main program

int main(int argc, char **argv)
{
    int result;
    CommandParameters command_parameters;

    print_IntroductoryMessage();
   
    if (argc >= 1 && argc <= 10)
    {		
	for (int i = 1; i < argc; ++i)
	{
	    result = parse_CommandLineParameter(argv[i], command_parameters);
	    if (result < 0)
	    {
		printf("Error: Cannot parse command line parameters (code = %d).\n", result);
		print_Help();
		
		return result;
	    }
	}
	if (command_parameters.help)
	{
	    print_Help();	    
	}
	else
	{	    
	    result = decimate_Polygons(command_parameters);
	    if (result < 0)
	    {
		return result;
	    }
	}
    }
    else
    {
	print_Help();
    }
    print_ConcludingMessage();
    
    return 0;
}
