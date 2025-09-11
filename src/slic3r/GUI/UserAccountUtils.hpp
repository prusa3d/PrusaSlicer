#ifndef slic3r_UserAccountUtils_hpp_
#define slic3r_UserAccountUtils_hpp_

#include <string>
#include <vector>
#include <map>

#include <boost/property_tree/ptree.hpp>

namespace Slic3r { 
class Preset;
class PrinterPresetCollection;
namespace GUI {
namespace UserAccountUtils {

// If ptree parameter is empty, json parameter needs to contain data and ptree is filled.
// If ptree is non-epty, json parameter is not used.
std::string get_keyword_from_json(boost::property_tree::ptree& ptree, const std::string& json, const std::string& keyword);
// Only ptree is passed since these functions are called on places that already has the ptree from get_keyword_from_json call
std::string get_nozzle_from_json(const boost::property_tree::ptree &ptree);
void fill_supported_printer_models_from_json(const boost::property_tree::ptree& ptree, std::vector<std::string>& result);
void fill_config_options_from_json(const boost::property_tree::ptree& ptree, std::map<std::string,std::vector<std::string>>& result);

// Since fill_material_from_json is called only from one place where ptree doesnt need to be shared, it is not always read from json.
void fill_material_from_json(const std::string& json, std::vector<std::string>& material_result, std::vector<bool>& avoid_abrasive_result);

std::string get_print_data_from_json(const std::string &json, const std::string &keyword);

const Preset* find_preset_by_nozzle_and_options( const PrinterPresetCollection& collection, const std::string& model_id, std::map<std::string, std::vector<std::string>>& options);
}}} // Slic3r::GUI::UserAccountUtils

#endif // slic3r_UserAccountUtils_hpp_
