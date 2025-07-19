/*================================================================*/
/*
 * Author:  Pavel Surynek, 2023 - 2025
 * Company: Prusa Research
 *
 * File:    seq_utilities.hpp
 *
 * Various utilities for sequential print.
 */
/*================================================================*/

#ifndef __SEQ_UTILITIES_HPP__
#define __SEQ_UTILITIES_HPP__


/*----------------------------------------------------------------*/

#include "seq_sequential.hpp"
#include "libseqarrange/seq_interface.hpp"


/*----------------------------------------------------------------*/

namespace Sequential
{

    
bool find_and_remove(std::string &src, const std::string &key);
    
std::vector<ObjectToPrint> load_exported_data_from_file(const std::string &filename);
std::vector<ObjectToPrint> load_exported_data_from_text(const std::string &data_text);
std::vector<ObjectToPrint> load_exported_data_from_stream(std::istream &data_stream);        
    
int load_printer_geometry_from_file(const std::string& filename, PrinterGeometry &printer_geometry);
int load_printer_geometry_from_text(const std::string& geometry_text, PrinterGeometry &printer_geometry);
int load_printer_geometry_from_stream(std::istream& geometry_stream, PrinterGeometry &printer_geometry);        

void save_import_data_to_file(const std::string           &filename,
			      const std::map<double, int> &scheduled_polygons,
			      const map<int, int>         &original_index_map,
			      const vector<Rational>      &poly_positions_X,
			      const vector<Rational>      &poly_positions_Y);

    
/*----------------------------------------------------------------*/

} // namespace Sequential


/*----------------------------------------------------------------*/

#endif /* __SEQ_UTILITIES_HPP__ */
