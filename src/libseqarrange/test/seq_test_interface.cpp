/*================================================================*/
/*
 * Author:  Pavel Surynek, 2023 - 2024
 * Company: Prusa Research
 *
 * File:    seq_test_interface.cpp
 *
 * Tests of the sequential printing interface for Prusa Slic3r
 */
/*================================================================*/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>

#include <sstream>
#include <iostream>
#include <fstream>
#include <vector>

#include "libslic3r/Polygon.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Geometry/ConvexHull.hpp"
#include "libslic3r/SVG.hpp"

#include <z3++.h>

#include "libseqarrange/seq_interface.hpp"
#include "seq_utilities.hpp"
#include "seq_preprocess.hpp"

#include "seq_test_interface.hpp"


/*----------------------------------------------------------------*/


using namespace Sequential;


/*----------------------------------------------------------------*/

const int SEQ_PRUSA_MK3S_X_SIZE = 2500;
const int SEQ_PRUSA_MK3S_Y_SIZE = 2100;    


/*----------------------------------------------------------------*/

const std::string arrange_data_export_text = "OBJECT_ID131\n\
TOTAL_HEIGHT62265434\n\
POLYGON_AT_HEIGHT0\n\
POINT-21000000 -16000000\n\
POINT21000000 -16000000\n\
POINT21000000 12000000\n\
POINT17000000 16000000\n\
POINT-17000000 16000000\n\
POINT-21000000 12000000\n\
POLYGON_AT_HEIGHT2000000\n\
POINT-21000000 -16000000\n\
POINT21000000 -16000000\n\
POINT21000000 12000000\n\
POINT17000000 16000000\n\
POINT-17000000 16000000\n\
POINT-21000000 12000000\n\
POLYGON_AT_HEIGHT18000000\n\
POINT-21000000 -16000000\n\
POINT21000000 -16000000\n\
POINT21000000 4000000\n\
POINT-21000000 4000000\n\
POLYGON_AT_HEIGHT26000000\n\
POINT-21000000 -16000000\n\
POINT21000000 -16000000\n\
POINT21000000 4000000\n\
POINT-21000000 4000000\n\
OBJECT_ID66\n\
TOTAL_HEIGHT10000000\n\
POLYGON_AT_HEIGHT0\n\
POINT-21000000 -16000000\n\
POINT21000000 -16000000\n\
POINT21000000 12000000\n\
POINT17000000 16000000\n\
POINT-17000000 16000000\n\
POINT-21000000 12000000\n\
POLYGON_AT_HEIGHT2000000\n\
POINT-21000000 -16000000\n\
POINT21000000 -16000000\n\
POINT21000000 4000000\n\
POINT-21000000 4000000\n\
POLYGON_AT_HEIGHT18000000\n\
POLYGON_AT_HEIGHT26000000\n\
OBJECT_ID44\n\
TOTAL_HEIGHT10000000\n\
POLYGON_AT_HEIGHT0\n\
POINT-21000000 -16000000\n\
POINT21000000 -16000000\n\
POINT21000000 11999992\n\
POINT17000000 15999992\n\
POINT-17000000 15999992\n\
POINT-21000000 11999992\n\
POLYGON_AT_HEIGHT2000000\n\
POINT-21000000 -16000000\n\
POINT21000000 -16000000\n\
POINT21000000 3999992\n\
POINT-21000000 3999992\n\
POLYGON_AT_HEIGHT18000000\n\
POLYGON_AT_HEIGHT26000000\n\
OBJECT_ID88\n\
TOTAL_HEIGHT10000000\n\
POLYGON_AT_HEIGHT0\n\
POINT-21000000 -16000000\n\
POINT21000000 -16000000\n\
POINT21000000 12000000\n\
POINT17000000 16000000\n\
POINT-17000000 16000000\n\
POINT-21000000 12000000\n\
POLYGON_AT_HEIGHT2000000\n\
POINT-21000000 -16000000\n\
POINT21000000 -16000000\n\
POINT21000000 4000000\n\
POINT-21000000 4000000\n\
POLYGON_AT_HEIGHT18000000\n\
POLYGON_AT_HEIGHT26000000\n\
OBJECT_ID77\n\
TOTAL_HEIGHT10000000\n\
POLYGON_AT_HEIGHT0\n\
POINT-21000000 -16000000\n\
POINT21000000 -16000000\n\
POINT21000000 12000008\n\
POINT17000000 16000008\n\
POINT-17000000 16000008\n\
POINT-21000000 12000008\n\
POLYGON_AT_HEIGHT2000000\n\
POINT-21000000 -16000000\n\
POINT21000000 -16000000\n\
POINT21000000 4000000\n\
POINT-21000000 4000000\n\
POLYGON_AT_HEIGHT18000000\n\
POLYGON_AT_HEIGHT26000000\n\
OBJECT_ID120\n\
TOTAL_HEIGHT62265434\n\
POLYGON_AT_HEIGHT0\n\
POINT-21000000 -15999992\n\
POINT21000000 -15999992\n\
POINT21000000 12000000\n\
POINT17000000 16000000\n\
POINT-17000000 16000000\n\
POINT-21000000 12000000\n\
POLYGON_AT_HEIGHT2000000\n\
POINT-21000000 -15999992\n\
POINT21000000 -15999992\n\
POINT21000000 12000000\n\
POINT17000000 16000000\n\
POINT-17000000 16000000\n\
POINT-21000000 12000000\n\
POLYGON_AT_HEIGHT18000000\n\
POINT-21000000 -15999992\n\
POINT21000000 -15999992\n\
POINT21000000 4000000\n\
POINT-21000000 4000000\n\
POLYGON_AT_HEIGHT26000000\n\
POINT-21000000 -15999992\n\
POINT21000000 -15999992\n\
POINT21000000 4000000\n\
POINT-21000000 4000000\n\
OBJECT_ID99\n\
TOTAL_HEIGHT62265434\n\
POLYGON_AT_HEIGHT0\n\
POINT-21000000 -16000000\n\
POINT21000000 -16000000\n\
POINT21000000 12000000\n\
POINT17000000 16000000\n\
POINT-17000000 16000000\n\
POINT-21000000 12000000\n\
POLYGON_AT_HEIGHT2000000\n\
POINT-21000000 -16000000\n\
POINT21000000 -16000000\n\
POINT21000000 12000000\n\
POINT17000000 16000000\n\
POINT-17000000 16000000\n\
POINT-21000000 12000000\n\
POLYGON_AT_HEIGHT18000000\n\
POINT-21000000 -16000000\n\
POINT21000000 -16000000\n\
POINT21000000 4000000\n\
POINT-21000000 4000000\n\
POLYGON_AT_HEIGHT26000000\n\
POINT-21000000 -16000000\n\
POINT21000000 -16000000\n\
POINT21000000 4000000\n\
POINT-21000000 4000000\n\
OBJECT_ID151\n\
TOTAL_HEIGHT62265434\n\
POLYGON_AT_HEIGHT0\n\
POINT-21000000 -16000000\n\
POINT21000000 -16000000\n\
POINT21000000 12000000\n\
POINT17000000 16000000\n\
POINT-17000000 16000000\n\
POINT-21000000 12000000\n\
POLYGON_AT_HEIGHT2000000\n\
POINT-21000000 -16000000\n\
POINT21000000 -16000000\n\
POINT21000000 12000000\n\
POINT17000000 16000000\n\
POINT-17000000 16000000\n\
POINT-21000000 12000000\n\
POLYGON_AT_HEIGHT18000000\n\
POINT-21000000 -16000000\n\
POINT21000000 -16000000\n\
POINT21000000 4000000\n\
POINT-21000000 4000000\n\
POLYGON_AT_HEIGHT26000000\n\
POINT-21000000 -16000000\n\
POINT21000000 -16000000\n\
POINT21000000 4000000\n\
POINT-21000000 4000000\n\
OBJECT_ID162\n\
TOTAL_HEIGHT62265434\n\
POLYGON_AT_HEIGHT0\n\
POINT-30189590 -16000000\n\
POINT30189576 -16000000\n\
POINT30189576 12000000\n\
POINT24439178 16000000\n\
POINT-24439194 16000000\n\
POINT-30189590 12000000\n\
POLYGON_AT_HEIGHT2000000\n\
POINT-30189590 -16000000\n\
POINT30189576 -16000000\n\
POINT30189576 12000000\n\
POINT26286238 14715178\n\
POINT24439178 16000000\n\
POINT-24439194 16000000\n\
POINT-28342532 13284822\n\
POINT-30189590 12000000\n\
POLYGON_AT_HEIGHT18000000\n\
POINT-30189590 -16000000\n\
POINT30189576 -16000000\n\
POINT30189576 4000000\n\
POINT-30189590 4000000\n\
POLYGON_AT_HEIGHT26000000\n\
POINT-30189590 -16000000\n\
POINT30189576 -16000000\n\
POINT30189576 4000000\n\
POINT-30189590 4000000\n\
OBJECT_ID192\n\
TOTAL_HEIGHT62265434\n\
POLYGON_AT_HEIGHT0\n\
POINT-21000000 -16000000\n\
POINT21000000 -16000000\n\
POINT21000000 12000000\n\
POINT17000000 16000000\n\
POINT-17000000 16000000\n\
POINT-21000000 12000000\n\
POLYGON_AT_HEIGHT2000000\n\
POINT-21000000 -16000000\n\
POINT21000000 -16000000\n\
POINT21000000 12000000\n\
POINT17000000 16000000\n\
POINT-17000000 16000000\n\
POINT-21000000 12000000\n\
POLYGON_AT_HEIGHT18000000\n\
POINT-21000000 -16000000\n\
POINT21000000 -16000000\n\
POINT21000000 4000000\n\
POINT-21000000 4000000\n\
POLYGON_AT_HEIGHT26000000\n\
POINT-21000000 -16000000\n\
POINT21000000 -16000000\n\
POINT21000000 4000000\n\
POINT-21000000 4000000\n\
OBJECT_ID203\n\
TOTAL_HEIGHT62265434\n\
POLYGON_AT_HEIGHT0\n\
POINT-21000000 -15999999\n\
POINT21000000 -15999999\n\
POINT21000000 12000002\n\
POINT17000000 16000002\n\
POINT-17000000 16000002\n\
POINT-21000000 12000002\n\
POLYGON_AT_HEIGHT2000000\n\
POINT-21000000 -15999999\n\
POINT21000000 -15999999\n\
POINT21000000 12000002\n\
POINT17000000 16000002\n\
POINT-17000000 16000002\n\
POINT-21000000 12000002\n\
POLYGON_AT_HEIGHT18000000\n\
POINT-21000000 -15999999\n\
POINT21000000 -15999999\n\
POINT21000000 4000000\n\
POINT-21000000 4000000\n\
POLYGON_AT_HEIGHT26000000\n\
POINT-21000000 -15999999\n\
POINT21000000 -15999999\n\
POINT21000000 4000000\n\
POINT-21000000 4000000\n\
OBJECT_ID223\n\
TOTAL_HEIGHT62265434\n\
POLYGON_AT_HEIGHT0\n\
POINT-20999998 -16000000\n\
POINT21000004 -16000000\n\
POINT21000004 12000000\n\
POINT17000004 16000000\n\
POINT-16999998 16000000\n\
POINT-20999998 12000000\n\
POLYGON_AT_HEIGHT2000000\n\
POINT-20999998 -16000000\n\
POINT21000004 -16000000\n\
POINT21000004 12000000\n\
POINT17000004 16000000\n\
POINT-16999998 16000000\n\
POINT-20999998 12000000\n\
POLYGON_AT_HEIGHT18000000\n\
POINT-20999998 -16000000\n\
POINT21000004 -16000000\n\
POINT21000004 4000000\n\
POINT-20999998 4000000\n\
POLYGON_AT_HEIGHT26000000\n\
POINT-20999998 -16000000\n\
POINT21000004 -16000000\n\
POINT21000004 4000000\n\
POINT-20999998 4000000\n\
OBJECT_ID234\n\
TOTAL_HEIGHT62265434\n\
POLYGON_AT_HEIGHT0\n\
POINT-21000002 -16000000\n\
POINT21000000 -16000000\n\
POINT21000000 12000000\n\
POINT17000000 16000000\n\
POINT-17000002 16000000\n\
POINT-21000002 12000000\n\
POLYGON_AT_HEIGHT2000000\n\
POINT-21000002 -16000000\n\
POINT21000000 -16000000\n\
POINT21000000 12000000\n\
POINT17000000 16000000\n\
POINT-17000002 16000000\n\
POINT-21000002 12000000\n\
POLYGON_AT_HEIGHT18000000\n\
POINT-21000002 -16000000\n\
POINT21000000 -16000000\n\
POINT21000000 4000000\n\
POINT-21000002 4000000\n\
POLYGON_AT_HEIGHT26000000\n\
POINT-21000002 -16000000\n\
POINT21000000 -16000000\n\
POINT21000000 4000000\n\
POINT-21000002 4000000\n\
";

const std::string printer_geometry_mk4_compatibility_text = "X_SIZE250000000\n\
Y_SIZE210000000\n\
CONVEX_HEIGHT0\n\
CONVEX_HEIGHT2000000\n\
BOX_HEIGHT18000000\n\
BOX_HEIGHT26000000\n\
POLYGON_AT_HEIGHT0\n\
POINT-500000 -500000\n\
POINT500000 -500000\n\
POINT500000 500000\n\
POINT-500000 500000\n\
POLYGON_AT_HEIGHT2000000\n\
POINT-1000000 -21000000	\n\
POINT37000000 -21000000\n\
POINT37000000  44000000\n\
POINT-1000000  44000000\n\
POLYGON_AT_HEIGHT2000000\n\
POINT-40000000 -45000000\n\
POINT38000000 -45000000\n\
POINT38000000  20000000\n\
POINT-40000000  20000000\n\
POLYGON_AT_HEIGHT18000000\n\
POINT-350000000 -23000000\n\
POINT350000000 -23000000\n\
POINT350000000 -35000000\n\
POINT-350000000 -35000000\n\
POLYGON_AT_HEIGHT26000000\n\
POINT-12000000 -350000000\n\
POINT9000000 -350000000\n\
POINT9000000 -39000000\n\
POINT-12000000 -39000000\n\
POLYGON_AT_HEIGHT26000000\n\
POINT-12000000 -350000000\n\
POINT250000000 -350000000\n\
POINT250000000  -82000000\n\
POINT-12000000  -82000000\n\
";


const std::string printer_geometry_mk4_text = "X_SIZE250000000\n\
Y_SIZE210000000\n\
CONVEX_HEIGHT0\n\
CONVEX_HEIGHT3000000\n\
BOX_HEIGHT11000000\n\
BOX_HEIGHT13000000\n\
POLYGON_AT_HEIGHT0\n\
POINT-500000 -500000\n\
POINT500000 -500000\n\
POINT500000 500000\n\
POINT-500000 500000\n\
POLYGON_AT_HEIGHT3000000\n\
POINT-1000000 -21000000\n\
POINT37000000 -21000000\n\
POINT37000000  44000000\n\
POINT-1000000  44000000\n\
POLYGON_AT_HEIGHT3000000\n\
POINT-40000000 -45000000\n\
POINT38000000 -45000000\n\
POINT38000000  20000000\n\
POINT-40000000  20000000\n\
POLYGON_AT_HEIGHT11000000\n\
POINT-350000000 -23000000\n\
POINT350000000 -23000000\n\
POINT350000000 -35000000\n\
POINT-350000000 -35000000\n\
POLYGON_AT_HEIGHT13000000\n\
POINT-12000000 -350000000\n\
POINT9000000 -350000000\n\
POINT9000000 -39000000\n\
POINT-12000000 -39000000\n\
POLYGON_AT_HEIGHT13000000\n\
POINT-12000000 -350000000\n\
POINT250000000 -350000000\n\
POINT250000000  -82000000\n\
POINT-12000000  -82000000\n\
";


/*
static bool find_and_remove(std::string& src, const std::string& key)
{
    size_t pos = src.find(key);
    if (pos != std::string::npos) {
        src.erase(pos, key.length());
        return true;
    }
    return false;
}
*/

/*
std::vector<ObjectToPrint> load_exported_data(const std::string& filename)
{
    std::vector<ObjectToPrint> objects_to_print;

    std::ifstream in(filename);
    if (!in)
        throw std::runtime_error("NO EXPORTED FILE WAS FOUND");
    std::string line;

    while (in) {        
        std::getline(in, line);
        if (find_and_remove(line, "OBJECT_ID")) {
            objects_to_print.push_back(ObjectToPrint());
            objects_to_print.back().id = std::stoi(line);
        }
        if (find_and_remove(line, "TOTAL_HEIGHT"))
            objects_to_print.back().total_height = std::stoi(line);
        if (find_and_remove(line, "POLYGON_AT_HEIGHT"))
            objects_to_print.back().pgns_at_height.emplace_back(std::make_pair(std::stoi(line), Polygon()));
        if (find_and_remove(line, "POINT")) {
            std::stringstream ss(line);
            std::string val;
            ss >> val;
            Point pt(std::stoi(val), 0);
            ss >> val;
            pt.y() = std::stoi(val);
            objects_to_print.back().pgns_at_height.back().second.append(pt);
        }
    }
    return objects_to_print;
}
*/


void save_import_data(const std::string           &filename,
		      const std::map<double, int> &scheduled_polygons,
		      const map<int, int>         &original_index_map,
		      const vector<Rational>      &poly_positions_X,
		      const vector<Rational>      &poly_positions_Y)
{
    std::ofstream out(filename);
    if (!out)
        throw std::runtime_error("CANNOT CREATE IMPORT FILE");

    for (const auto& scheduled_polygon: scheduled_polygons)
    {
	coord_t X, Y;

	scaleUp_PositionForSlicer(poly_positions_X[scheduled_polygon.second],
				  poly_positions_Y[scheduled_polygon.second],
				  X,
				  Y);
	const auto& original_index = original_index_map.find(scheduled_polygon.second);
	    
//	out << original_index_map[scheduled_polygon.second] << " " << X << " " << Y << endl;
	out << original_index->second << " " << X << " " << Y << endl;	    
    }
}


/*----------------------------------------------------------------*/

TEST_CASE("Interface test 1", "[Sequential Arrangement Interface]")
{ 
    clock_t start, finish;
    
    printf("Testing interface 1 ...\n");

    start = clock();

    SolverConfiguration solver_configuration;
    solver_configuration.decimation_precision = SEQ_DECIMATION_PRECISION_HIGH;
    solver_configuration.plate_bounding_box = BoundingBox({0,0}, {SEQ_PRUSA_MK3S_X_SIZE, SEQ_PRUSA_MK3S_Y_SIZE});


    printf("Loading objects ...\n");
    std::vector<ObjectToPrint> objects_to_print = load_exported_data_from_text(arrange_data_export_text);
    REQUIRE(objects_to_print.size() > 0);
    printf("Loading objects ... finished\n");    

    std::vector<ScheduledPlate> scheduled_plates;
    printf("Scheduling objects for sequential print ...\n");
		
    int result = schedule_ObjectsForSequentialPrint(solver_configuration,
						    objects_to_print,
						    scheduled_plates);

    REQUIRE(result == 0);
    if (result == 0)
    {
	printf("Object scheduling for sequential print SUCCESSFUL !\n");

	printf("Number of plates: %ld\n", scheduled_plates.size());
	REQUIRE(scheduled_plates.size() > 0);

	for (unsigned int plate = 0; plate < scheduled_plates.size(); ++plate)
	{
	    printf("  Number of objects on plate: %ld\n", scheduled_plates[plate].scheduled_objects.size());
	    REQUIRE(scheduled_plates[plate].scheduled_objects.size() > 0);

	    for (const auto& scheduled_object: scheduled_plates[plate].scheduled_objects)
	    {
		cout << "    ID: " << scheduled_object.id << "  X: " << scheduled_object.x << "  Y: " << scheduled_object.y << endl;
		REQUIRE(scheduled_object.x >= 0);
		REQUIRE(scheduled_object.x <= solver_configuration.x_plate_bounding_box_size * SEQ_SLICER_SCALE_FACTOR);
		REQUIRE(scheduled_object.y >= 0);
		REQUIRE(scheduled_object.y <= solver_configuration.y_plate_bounding_box_size * SEQ_SLICER_SCALE_FACTOR);
	    }
	}
    }
    else
    {
	printf("Something went WRONG during sequential scheduling (code: %d)\n", result);
    }
    
    finish = clock();
    
    printf("Time: %.3f\n", (finish - start) / (double)CLOCKS_PER_SEC);
    printf("Testing interface 1 ... finished\n");    
}


TEST_CASE("Interface test 2", "[Sequential Arrangement Interface]")
{ 
    clock_t start, finish;
    
    printf("Testing interface 2 ...\n");

    start = clock();

    SolverConfiguration solver_configuration;
    solver_configuration.decimation_precision = SEQ_DECIMATION_PRECISION_HIGH;
    solver_configuration.plate_bounding_box = BoundingBox({0,0}, {SEQ_PRUSA_MK3S_X_SIZE, SEQ_PRUSA_MK3S_Y_SIZE});    

    printf("Loading objects ...\n");    
    std::vector<ObjectToPrint> objects_to_print = load_exported_data_from_text(arrange_data_export_text);

    std::vector<std::vector<Slic3r::Polygon> > convex_unreachable_zones;
    std::vector<std::vector<Slic3r::Polygon> > box_unreachable_zones;    

    printf("Preparing extruder unreachable zones ...\n");
    setup_ExtruderUnreachableZones(solver_configuration, convex_unreachable_zones, box_unreachable_zones);

    std::vector<ScheduledPlate> scheduled_plates;
    printf("Scheduling objects for sequential print ...\n");

    int result = schedule_ObjectsForSequentialPrint(solver_configuration,
						    objects_to_print,
						    convex_unreachable_zones,
						    box_unreachable_zones,
						    scheduled_plates);

    REQUIRE(result == 0);    
    if (result == 0)
    {
	printf("Object scheduling for sequential print SUCCESSFUL !\n");

	printf("Number of plates: %ld\n", scheduled_plates.size());
	REQUIRE(scheduled_plates.size() > 0);	

	for (unsigned int plate = 0; plate < scheduled_plates.size(); ++plate)
	{
	    printf("  Number of objects on plate: %ld\n", scheduled_plates[plate].scheduled_objects.size());
	    REQUIRE(scheduled_plates[plate].scheduled_objects.size() > 0);	    

	    for (const auto& scheduled_object: scheduled_plates[plate].scheduled_objects)
	    {
		cout << "    ID: " << scheduled_object.id << "  X: " << scheduled_object.x << "  Y: " << scheduled_object.y << endl;
		REQUIRE(scheduled_object.x >= 0);
		REQUIRE(scheduled_object.x <= solver_configuration.x_plate_bounding_box_size * SEQ_SLICER_SCALE_FACTOR);
		REQUIRE(scheduled_object.y >= 0);
		REQUIRE(scheduled_object.y <= solver_configuration.y_plate_bounding_box_size * SEQ_SLICER_SCALE_FACTOR);		
	    }
	}
    }
    else
    {
	printf("Something went WRONG during sequential scheduling (code: %d)\n", result);
    }          
    
    finish = clock();
    
    printf("Time: %.3f\n", (finish - start) / (double)CLOCKS_PER_SEC);
    printf("Testing interface 2 ... finished\n");    
}


TEST_CASE("Interface test 3", "[Sequential Arrangement Interface]")
{ 
    clock_t start, finish;
    
    printf("Testing interface 3 ...\n");

    start = clock();
    
    PrinterGeometry printer_geometry;
    int result = load_printer_geometry_from_text(printer_geometry_mk4_text, printer_geometry);
    REQUIRE(result == 0);
    
    if (result != 0)
    {
	printf("Printer geometry load error.\n");
	return;
    }
    
    REQUIRE(printer_geometry.plate.points.size() == 4);

    for (const auto& convex_height: printer_geometry.convex_heights)
    {
	cout << "convex_height:" << convex_height << endl;
    }

    for (const auto& box_height: printer_geometry.box_heights)
    {
	cout << "box_height:" << box_height << endl;
    }
    printf("extruder slices:\n");
    REQUIRE(printer_geometry.extruder_slices.size() > 0);
    
    for (std::map<coord_t, std::vector<Polygon> >::const_iterator extruder_slice = printer_geometry.extruder_slices.begin(); extruder_slice != printer_geometry.extruder_slices.end(); ++extruder_slice)
    {
	for (const auto &polygon: extruder_slice->second)
	{
	    printf("  polygon height: %d\n", extruder_slice->first);
	    
	    for (const auto &point: polygon.points)
	    {
		cout << "    " << point.x() << "  " << point.y() << endl;
	    }
	}
    }

    finish = clock();
    
    printf("Time: %.3f\n", (finish - start) / (double)CLOCKS_PER_SEC);
    printf("Testing interface 3 ... finished\n");    
}    


TEST_CASE("Interface test 4", "[Sequential Arrangement Interface]")
{
    clock_t start, finish;
    
    printf("Testing interface 4 ...\n");

    start = clock();

    SolverConfiguration solver_configuration;
    solver_configuration.decimation_precision = SEQ_DECIMATION_PRECISION_HIGH;
    solver_configuration.object_group_size = 4;
    solver_configuration.plate_bounding_box = BoundingBox({0,0}, {SEQ_PRUSA_MK3S_X_SIZE, SEQ_PRUSA_MK3S_Y_SIZE});    

    printf("Loading objects ...\n");    
    std::vector<ObjectToPrint> objects_to_print = load_exported_data_from_text(arrange_data_export_text);
    printf("Loading objects ... finished\n");

    PrinterGeometry printer_geometry;

    printf("Loading printer geometry ...\n");
    int result = load_printer_geometry_from_text(printer_geometry_mk4_compatibility_text, printer_geometry);
    
    REQUIRE(result == 0);    
    if (result != 0)
    {
	printf("Cannot load printer geometry (code: %d).\n", result);
	return;
    }
    solver_configuration.setup(printer_geometry);
    printf("Loading printer geometry ... finished\n");
    
    std::vector<ScheduledPlate> scheduled_plates;
    printf("Scheduling objects for sequential print ...\n");

    scheduled_plates = schedule_ObjectsForSequentialPrint(solver_configuration,
							  printer_geometry,
							  objects_to_print);    

    printf("Object scheduling for sequential print SUCCESSFUL !\n");
    
    printf("Number of plates: %ld\n", scheduled_plates.size());
    REQUIRE(scheduled_plates.size() > 0);    

    for (unsigned int plate = 0; plate < scheduled_plates.size(); ++plate)
    {
	printf("  Number of objects on plate: %ld\n", scheduled_plates[plate].scheduled_objects.size());
	REQUIRE(scheduled_plates[plate].scheduled_objects.size() > 0);	
	
	for (const auto& scheduled_object: scheduled_plates[plate].scheduled_objects)
	{
	    cout << "    ID: " << scheduled_object.id << "  X: " << scheduled_object.x << "  Y: " << scheduled_object.y << endl;

	    BoundingBox plate_box = get_extents(printer_geometry.plate);
	    
	    REQUIRE(scheduled_object.x >= plate_box.min.x());
	    REQUIRE(scheduled_object.x <= plate_box.max.x());
	    REQUIRE(scheduled_object.y >= plate_box.min.y());
	    REQUIRE(scheduled_object.y <= plate_box.max.y());		
	}
    }
    
    finish = clock();
    
    printf("Time: %.3f\n", (finish - start) / (double)CLOCKS_PER_SEC);
    printf("Testing interface 4 ... finished\n");
}


TEST_CASE("Interface test 5", "[Sequential Arrangement Interface]")
{
    clock_t start, finish;
    
    printf("Testing interface 5 ...\n");

    start = clock();

    SolverConfiguration solver_configuration;
    solver_configuration.decimation_precision = SEQ_DECIMATION_PRECISION_LOW;
    solver_configuration.object_group_size = 4;
    solver_configuration.plate_bounding_box = BoundingBox({0,0}, {SEQ_PRUSA_MK3S_X_SIZE, SEQ_PRUSA_MK3S_Y_SIZE});    

    printf("Loading objects ...\n");    
    std::vector<ObjectToPrint> objects_to_print = load_exported_data_from_text(arrange_data_export_text);
    printf("Loading objects ... finished\n");

    PrinterGeometry printer_geometry;

    printf("Loading printer geometry ...\n");
    int result = load_printer_geometry_from_text(printer_geometry_mk4_compatibility_text, printer_geometry);

    REQUIRE(result == 0);    
    if (result != 0)
    {
	printf("Cannot load printer geometry (code: %d).\n", result);
	return;
    }
    solver_configuration.setup(printer_geometry);
    printf("Loading printer geometry ... finished\n");
    
    std::vector<ScheduledPlate> scheduled_plates;
    printf("Scheduling objects for sequential print ...\n");

    scheduled_plates = schedule_ObjectsForSequentialPrint(solver_configuration,
							  printer_geometry,
							  objects_to_print,
							  [](int progress) { printf("Progress: %d\n", progress);
							                     REQUIRE(progress >= 0);
									     REQUIRE(progress <= 100); });

    printf("Object scheduling for sequential print SUCCESSFUL !\n");
    
    printf("Number of plates: %ld\n", scheduled_plates.size());
    REQUIRE(scheduled_plates.size() > 0);    

    for (unsigned int plate = 0; plate < scheduled_plates.size(); ++plate)
    {
	printf("  Number of objects on plate: %ld\n", scheduled_plates[plate].scheduled_objects.size());
	REQUIRE(scheduled_plates[plate].scheduled_objects.size() > 0);	
	
	for (const auto& scheduled_object: scheduled_plates[plate].scheduled_objects)
	{
	    cout << "    ID: " << scheduled_object.id << "  X: " << scheduled_object.x << "  Y: " << scheduled_object.y << endl;

	    BoundingBox plate_box = get_extents(printer_geometry.plate);
	    
	    REQUIRE(scheduled_object.x >= plate_box.min.x());
	    REQUIRE(scheduled_object.x <= plate_box.max.x());
	    REQUIRE(scheduled_object.y >= plate_box.min.y());
	    REQUIRE(scheduled_object.y <= plate_box.max.y());		
	}
    }
    
    finish = clock();    
    printf("Solving time: %.3f\n", (finish - start) / (double)CLOCKS_PER_SEC);

    start = clock();
    
    printf("Checking sequential printability ...\n");

    bool printable = check_ScheduledObjectsForSequentialPrintability(solver_configuration,
								     printer_geometry,
								     objects_to_print,
								     scheduled_plates);    
    printf("  Scheduled/arranged objects are sequentially printable: %s\n", (printable ? "YES" : "NO"));
    REQUIRE(printable);

    printf("Checking sequential printability ... finished\n");

    finish = clock();   
    printf("Checking time: %.3f\n", (finish - start) / (double)CLOCKS_PER_SEC);    
    
    printf("Testing interface 5 ... finished\n");
}


TEST_CASE("Interface test 6", "[Sequential Arrangement Interface]")
{
    clock_t start, finish;
    
    printf("Testing interface 6 ...\n");

    start = clock();

    SolverConfiguration solver_configuration;
    solver_configuration.decimation_precision = SEQ_DECIMATION_PRECISION_LOW;
    solver_configuration.object_group_size = 4;
    solver_configuration.plate_bounding_box = BoundingBox({0,0}, {SEQ_PRUSA_MK3S_X_SIZE, SEQ_PRUSA_MK3S_Y_SIZE});    

    printf("Loading objects ...\n");    
    std::vector<ObjectToPrint> objects_to_print = load_exported_data_from_text(arrange_data_export_text);
    REQUIRE(objects_to_print.size() > 0);    
    printf("Loading objects ... finished\n");

    for (auto& object_to_print: objects_to_print)
    {
	object_to_print.glued_to_next = true;
    }

    PrinterGeometry printer_geometry;

    printf("Loading printer geometry ...\n");
    int result = load_printer_geometry_from_text(printer_geometry_mk4_compatibility_text, printer_geometry);
    REQUIRE(result == 0);    
    if (result != 0)
    {
	printf("Cannot load printer geometry (code: %d).\n", result);
	return;
    }
    solver_configuration.setup(printer_geometry);
    printf("Loading printer geometry ... finished\n");
    
    std::vector<ScheduledPlate> scheduled_plates;
    printf("Scheduling objects for sequential print ...\n");

    scheduled_plates = schedule_ObjectsForSequentialPrint(solver_configuration,
							  printer_geometry,
							  objects_to_print,
							  [](int progress) { printf("Progress: %d\n", progress);
							                     REQUIRE(progress >= 0);
									     REQUIRE(progress <= 100); });

    printf("Object scheduling for sequential print SUCCESSFUL !\n");
    
    printf("Number of plates: %ld\n", scheduled_plates.size());
    REQUIRE(scheduled_plates.size() > 0);    

    for (unsigned int plate = 0; plate < scheduled_plates.size(); ++plate)
    {
	printf("  Number of objects on plate: %ld\n", scheduled_plates[plate].scheduled_objects.size());
	REQUIRE(scheduled_plates[plate].scheduled_objects.size() > 0);	
	
	for (const auto& scheduled_object: scheduled_plates[plate].scheduled_objects)
	{
	    cout << "    ID: " << scheduled_object.id << "  X: " << scheduled_object.x << "  Y: " << scheduled_object.y << endl;
		
	    BoundingBox plate_box = get_extents(printer_geometry.plate);
	    
	    REQUIRE(scheduled_object.x >= plate_box.min.x());
	    REQUIRE(scheduled_object.x <= plate_box.max.x());
	    REQUIRE(scheduled_object.y >= plate_box.min.y());
	    REQUIRE(scheduled_object.y <= plate_box.max.y());		
	}
    }
    
    finish = clock();    
    printf("Solving time: %.3f\n", (finish - start) / (double)CLOCKS_PER_SEC);

    start = clock();
    
    printf("Checking sequential printability ...\n");

    bool printable = check_ScheduledObjectsForSequentialPrintability(solver_configuration,
								     printer_geometry,
								     objects_to_print,
								     scheduled_plates);
    
    printf("  Scheduled/arranged objects are sequentially printable: %s\n", (printable ? "YES" : "NO"));
    REQUIRE(printable);    

    printf("Checking sequential printability ... finished\n");

    finish = clock();   
    printf("Checking time: %.3f\n", (finish - start) / (double)CLOCKS_PER_SEC);    
    
    printf("Testing interface 6 ... finished\n");
}


/*----------------------------------------------------------------*/


