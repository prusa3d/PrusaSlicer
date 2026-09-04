#include "JsonUtils.hpp"

#include <boost/algorithm/string/replace.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <regex>
#include <sstream>

namespace Slic3r {

namespace pt = boost::property_tree;

std::string write_json_with_post_process(const pt::ptree& ptree)
{
    std::stringstream oss;
    pt::write_json(oss, ptree);

    // fix json-out to show node values as a string just for string nodes
    std::regex reg("\\\"([0-9]+\\.{0,1}[0-9]*)\\\""); // code is borrowed from https://stackoverflow.com/questions/2855741/why-does-boost-property-tree-write-json-save-everything-as-string-is-it-possibl
    std::string result = std::regex_replace(oss.str(), reg, "$1");

    boost::replace_all(result, "\"true\"", "true");
    boost::replace_all(result, "\"false\"", "false");

    return result;
}

} // namespace Slic3r
