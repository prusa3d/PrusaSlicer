#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <variant>

namespace Slic3r {

namespace Biz::Parser {
    class PlaceholderParser;
}


using CustomParameterType = std::variant<std::monostate, std::string, int, double, bool>;
using CustomParameterValues = std::map<std::string, CustomParameterType>;

std::optional<CustomParameterValues> parse_custom_parameters(const std::string& input);

void add_custom_parameters_into_placeholder_parser(
    const std::string& cp_print,
    const std::string& cp_printer,
    const std::vector<std::string>& cp_filaments,
    Biz::Parser::PlaceholderParser& parser);


bool check_custom_parameters(
    const std::string& cp_print,
    const std::string& cp_printer,
    const std::vector<std::string>& cp_filaments,
    std::string* error = nullptr
);

} // namespace Slic3r
