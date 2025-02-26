/*================================================================*/
/*
 * Author:  Pavel Surynek, 2023 - 2025
 * Company: Prusa Research
 *
 * File:    sequential_decimator.hpp
 *
 * Polygon decimator utility (especially for extruder models).
 */
/*================================================================*/

#ifndef __SEQUENTIAL_DECIMATOR_HPP__
#define __SEQUENTIAL_DECIMATOR_HPP__

/*----------------------------------------------------------------*/

#include "seq_sequential.hpp"
#include "seq_preprocess.hpp"
#include "libseqarrange/seq_interface.hpp"


/*----------------------------------------------------------------*/

const double SEQ_DECIMATION_TOLERANCE = 400000.0;


/*----------------------------------------------------------------*/

struct CommandParameters
{	
    CommandParameters()
	: tolerance(SEQ_DECIMATION_TOLERANCE)
	, input_filename("arrange_data_export.txt")
	, output_filename("arrange_data_import.txt")
	, x_position(0)
	, y_position(0)
	, random_position(true)
	, help(false)
	, x_nozzle(0)
	, y_nozzle(0)
    {
	/* nothing */
    }
   
    double tolerance;    
    
    string input_filename;
    string output_filename;

    double x_position;
    double y_position;
    bool random_position;

    coord_t x_nozzle;
    coord_t y_nozzle;

    bool help;
};


/*----------------------------------------------------------------------------*/

void print_IntroductoryMessage(void);
void print_ConcludingMessage(void);
void print_Help(void);

int parse_CommandLineParameter(const string &parameter, CommandParameters &parameters);
int decimate_Polygons(const CommandParameters &command_parameters);


/*----------------------------------------------------------------*/

#endif /* __SEQUENTIAL_DECIMATOR_HPP__ */
