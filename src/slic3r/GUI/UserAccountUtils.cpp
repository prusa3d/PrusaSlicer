#include "UserAccountUtils.hpp"

#include "libslic3r/Preset.hpp"
#include "slic3r/GUI/Field.hpp"
#include "slic3r/GUI/GUI.hpp"

#include <boost/property_tree/json_parser.hpp>
#include <boost/log/trivial.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/lexical_cast/bad_lexical_cast.hpp>
#include <cassert>
#include <cstddef>
#include <exception>
#include <utility>

namespace pt = boost::property_tree;

namespace Slic3r { namespace GUI { namespace UserAccountUtils {

namespace {
std::string parse_tree_for_param(const pt::ptree& tree, const std::string& param) {
    for (const auto &section : tree) {
        if (section.first == param) {
            return section.second.data();
        }
        if (std::string res = parse_tree_for_param(section.second, param); !res.empty()) {
            return res;
        }
    }
    return {};
}

pt::ptree parse_tree_for_subtree(const pt::ptree& tree, const std::string& param) {
    for (const auto &section : tree) {
        if (section.first == param) {
            return section.second;
        } else {
            if (pt::ptree res = parse_tree_for_subtree(section.second, param); !res.empty())
                return res;
        }
    }
    return pt::ptree();
}

void json_to_ptree(boost::property_tree::ptree& out_ptree, const std::string& json) {
    try {
        std::stringstream ss(json);
        pt::read_json(ss, out_ptree);
    } catch (const std::exception &e) {
        BOOST_LOG_TRIVIAL(error) << "Failed to parse json to ptree. " << e.what();
        BOOST_LOG_TRIVIAL(error) << "json: " << json;
    }
}

} // namespace

std::string get_nozzle_from_json(const boost::property_tree::ptree& ptree) {
    assert(!ptree.empty());

    std::string out = parse_tree_for_param(ptree, "nozzle_diameter");
    // Get rid of trailing zeros.
    // This is because somtimes we get "nozzle_diameter":0.40000000000000002
    // This will return wrong result for f.e. 0.05. But we dont have such profiles right now.
    if (size_t fist_dot = out.find('.'); fist_dot != std::string::npos) {
        if (size_t first_zero = out.find('0', fist_dot); first_zero != std::string::npos) {
            return out.substr(0, first_zero);
        }
    }
    return out;
}

std::string get_keyword_from_json(boost::property_tree::ptree& ptree, const std::string& json, const std::string& keyword ) 
{
    if (ptree.empty()) {
        json_to_ptree(ptree, json);
    }
    assert(!ptree.empty());
    return parse_tree_for_param(ptree, keyword);
}

void fill_supported_printer_models_from_json(const boost::property_tree::ptree& ptree, std::vector<std::string>& result) 
{
    assert(!ptree.empty());
    std::string printer_model = parse_tree_for_param(ptree, "printer_model");
    if (!printer_model.empty()) {
        result.emplace_back(printer_model);
    }
    pt::ptree out = parse_tree_for_subtree(ptree, "supported_printer_models");
    if (out.empty()) {
        BOOST_LOG_TRIVIAL(error) << "Failed to find supported_printer_models in printer detail.";
        return;
    }
    for (const auto &sub : out) {
        if (printer_model != sub.second.data()) {
            result.emplace_back(sub.second.data());
        }
    }
}

namespace {
std::string json_var_to_opt_string(const std::string& json_var)
{
    if (json_var == "true")
        return "1";
    if (json_var == "false")
        return "0";
    return json_var;
}

void fill_config_options_from_json_inner(const boost::property_tree::ptree& ptree, std::map<std::string, std::vector<std::string>>& result,  const std::map<std::string, std::string>& parameters) 
{
    pt::ptree slots = parse_tree_for_subtree(ptree, "tools"); 
    for (const auto &subtree : slots) {
       size_t slot_id;
        try {
            // id could "1" for extruder
            // or "1.1" for MMU (than we need number after dot as id)
            size_t dot_pos = subtree.first.find('.');
            if (dot_pos != std::string::npos) {
                slot_id = boost::lexical_cast<size_t>(subtree.first.substr(dot_pos + 1));
            } else {
                slot_id = boost::lexical_cast<std::size_t>(subtree.first);
            }
        } catch (const boost::bad_lexical_cast&) {
            continue;
        }
       for (const auto &item : subtree.second) {
           if (parameters.find(item.first) == parameters.end()) {
               continue;
           }
           std::string config_name = parameters.at(item.first);
           // resolve value
           std::string val;
           if (item.second.size() > 0) {
               for (const auto &subitem : item.second) {
                   if (!val.empty()) {
                       val += ",";
                   }
                   val += json_var_to_opt_string(subitem.second.data());
               }
           } else {
               val = json_var_to_opt_string(item.second.data());
           }
           // insert value
           while (result[config_name].size() < slot_id)
               result[config_name].emplace_back();
           result[config_name][slot_id - 1] = val;
       }
    }
}
}

void fill_config_options_from_json(const boost::property_tree::ptree& ptree, std::map<std::string, std::vector<std::string>>& result) 
{
    assert(!ptree.empty());
    /*
    "slot": {
        "active": 3,
        "slots": {
            "1": {
                "material": "PETG",
                "temp": 32.0,
                "fan_hotend": 0.0,
                "fan_print": 0.0,
                "nozzle_diameter": 3.2,     // float
                "high_flow": true,          // boolean
                "high_temperature": false,  // boolean
                "hardened": true,           // boolean
            },
            "3": {
                "material": "ASA",
                "temp": 35.0,
                "fan_hotend": 0.0,
                "fan_print": 0.0,
                "nozzle_diameter": 3.2,     // float
                "high_flow": true,          // boolean
                "high_temperature": false,  // boolean
                "hardened": true,           // boolean
            },
        }
    }
    */
      const std::map<std::string, std::string> parameters = {
        // first name from connect, second config option
        {"nozzle_diameter","nozzle_diameter"},
        {"high_flow","nozzle_high_flow"},
        //{"",""}
    };
    fill_config_options_from_json_inner(ptree, result, parameters);
}

void fill_material_from_json(const std::string& json, std::vector<std::string>& material_result, std::vector<bool>& avoid_abrasive_result) 
{
    pt::ptree ptree;
    json_to_ptree(ptree, json);
    assert(!ptree.empty());

    /* option 1:
    "slot": {
            "active": 2,
            "slots": {
                "1": {
                    "material": "PLA",
                    "temp": 170,
                    "fan_hotend": 7689,
                    "fan_print": 0
                },
                "2": {
                    "material": "PLA",
                    "temp": 225,
                    "fan_hotend": 7798,
                    "fan_print": 6503
                },
                "3": {
                    "material": "PLA",
                    "temp": 36,
                    "fan_hotend": 6636,
                    "fan_print": 0
                },
                "4": {
                    "material": "PLA",
                    "temp": 35,
                    "fan_hotend": 0,
                    "fan_print": 0
                },
                "5": {
                    "material": "PETG",
                    "temp": 136,
                    "fan_hotend": 8132,
                    "fan_print": 0
                }
            }
        }
    */
    /* option 2
        "filament": {
            "material": "PLA",
            "bed_temperature": 60,
            "nozzle_temperature": 210
        }
    */
    // try finding "slot" subtree a use it to
    // if not found, find "filament" subtree

    // find "slot" subtree
    pt::ptree slot_subtree = parse_tree_for_subtree(ptree, "tools");
    if (slot_subtree.empty()) {
        // if not found, find "filament" subtree
        pt::ptree filament_subtree = parse_tree_for_subtree(ptree, "filament");
        if (!filament_subtree.empty()) {
            std::string material = parse_tree_for_param(filament_subtree, "material");
            if (!material.empty()) {
                material_result.emplace_back(std::move(material));
                avoid_abrasive_result.emplace_back(true);
            }
        }
        return;
    }
    // search "slot" subtree for all "material"s
    // this parses "slots" with respect to numbers of slots and adds empty string to missing numbers
    // if only filled should be used. Use: parse_tree_for_param_vector(slot_subtree, "material", result);
    /*
    pt::ptree slots = parse_tree_for_subtree(slot_subtree, "slots"); 
    assert(!slots.empty());
    for (const auto &subtree : slots) {
        size_t slot_id;
        try {
            slot_id = boost::lexical_cast<size_t>(subtree.first);
        } catch (const boost::bad_lexical_cast&) {
            continue;
        }
        std::string val = parse_tree_for_param(subtree.second, "material");
        // add empty for missing id
        while (result.size() < slot_id)
            result.emplace_back();
        result[slot_id - 1] = val;
    }
    */
    const std::map<std::string, std::string> parameters = {
        // first name from connect, second config option
        {"material","material"},
        {"hardened","hardened"},
        //{"",""}
    };
    std::map<std::string, std::vector<std::string>> result_map;
    fill_config_options_from_json_inner(ptree, result_map, parameters);
    if (result_map.find("material") != result_map.end())  {
        for (const std::string& val : result_map["material"]) {
            material_result.emplace_back(val);
        }
    }
    if (result_map.find("hardened") != result_map.end())  {
        for (const std::string& val : result_map["hardened"]) {
            avoid_abrasive_result.emplace_back(val == "0" ? 1 : 0);
        }
        // MMU has "hardened" only under tool 1
        if (avoid_abrasive_result.size() == 1 && material_result.size() > avoid_abrasive_result.size()) {
            for (size_t i = 1; i < material_result.size(); i++) {
                avoid_abrasive_result.emplace_back(avoid_abrasive_result[0]);
            }
        }
    }
}

std::string get_print_data_from_json(const std::string& json, const std::string& keyword) {
    // copy subtree string f.e.
    // { "<keyword>": {"param1": "something", "filename":"abcd.gcode", "param3":true},
    // "something_else" : 0 } into: {"param1": "something", "filename":"%1%", "param3":true,
    // "size":%2%} yes there will be 2 placeholders for later format

    // this will fail if not flat subtree
    size_t start_of_keyword = json.find("\"" + keyword + "\"");
    if (start_of_keyword == std::string::npos)
        return {};
    size_t start_of_sub = json.find('{', start_of_keyword);
    if (start_of_sub == std::string::npos)
        return {};
    size_t start_of_filename = json.find("\"filename\"", start_of_sub);
    if (start_of_filename == std::string::npos)
        return {};
    size_t filename_doubledot = json.find(':', start_of_filename);
    if (filename_doubledot == std::string::npos)
        return {};
    size_t start_of_filename_data = json.find('\"', filename_doubledot);
    if (start_of_filename_data == std::string::npos)
        return {};
    size_t end_of_filename_data = json.find('\"', start_of_filename_data + 1);
    if (end_of_filename_data == std::string::npos)
        return {};
    size_t end_of_sub = json.find('}', end_of_filename_data);
    if (end_of_sub == std::string::npos)
        return {};
    std::string result = json.substr(start_of_sub, start_of_filename_data - start_of_sub + 1);
    result += "%1%";
    result += json.substr(end_of_filename_data, end_of_sub - end_of_filename_data);
    result += ",\"size\":%2%}";
    return result;
}

const Preset* find_preset_by_nozzle_and_options(
    const PrinterPresetCollection& collection
    , const std::string& model_id
    , std::map<std::string, std::vector<std::string>>& options) 
{
    // find all matching presets when repo prefix is omitted
    std::vector<const Preset*> results;
    for (const Preset &preset : collection) {
        // trim repo prefix
        std::string printer_model = preset.config.opt_string("printer_model");
        const PresetWithVendorProfile& printer_with_vendor = collection.get_preset_with_vendor_profile(preset);
        printer_model = preset.trim_vendor_repo_prefix(printer_model, printer_with_vendor.vendor);
       
        if (!preset.is_system || printer_model != model_id)
            continue;
        // options (including nozzle_diameter)
        bool failed = false;
        for (const auto& opt : options) {
            assert(preset.config.has(opt.first));
            // We compare only first value now, but options contains data for all (some might be empty tho)
            std::string opt_val;
            if (preset.config.option(opt.first)->is_scalar()) {
                opt_val = preset.config.option(opt.first)->serialize();
            } else {
                switch (preset.config.option(opt.first)->type()) {
                case coInts:     opt_val = std::to_string(static_cast<const ConfigOptionInts*>(preset.config.option(opt.first))->values[0]); break;
                case coFloats:   
                    opt_val = GUI::into_u8(double_to_string(static_cast<const ConfigOptionFloats*>(preset.config.option(opt.first))->values[0]));
                    if (size_t pos = opt_val.find(",") != std::string::npos)
                        opt_val.replace(pos, 1, 1, '.');
                    break;
                case coStrings:  opt_val = static_cast<const ConfigOptionStrings*>(preset.config.option(opt.first))->values[0]; break;
                case coBools:    opt_val = static_cast<const ConfigOptionBools*>(preset.config.option(opt.first))->values[0] ? "1" : "0"; break;
                default:
                   assert(false);
                   continue;
                }
            }
        
            if (opt_val != opt.second[0])
            {
                failed = true;
                break;
            }
        }
        if (!failed) {
            results.push_back(&preset);
        }
        
    }
    // find one without prefix
    for (const Preset* preset : results) {
        if (preset->config.opt_string("printer_model") == model_id) {
            return preset;
        }
    }
    if (results.size() != 0) {
       return results.front();
    }
    return nullptr;
}

}}} // Slic3r::GUI::UserAccountUtils


