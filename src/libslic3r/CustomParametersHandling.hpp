#ifndef LIBSLIC3R_CUSTOMPARAMETERSHANDLING_HPP
#define LIBSLIC3R_CUSTOMPARAMETERSHANDLING_HPP

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <variant>

#include "PrintConfig.hpp"

namespace Slic3r {

using CustomParameterType = std::variant<std::monostate, std::string, int, double, bool>;
using CustomParameterValues = std::map<std::string, CustomParameterType>;

std::optional<CustomParameterValues> parse_custom_parameters(const std::string& input);

bool check_custom_parameters(const std::string& cp_print, const std::string& cp_printer, const std::vector<std::string>& cp_filaments, std::string* error = nullptr);

DynamicConfig parse_custom_parameters_to_dynamic_config(const std::string& cp_print, const std::string& cp_printer, const std::vector<std::string>& cp_filaments);

std::string merge_json(const std::string& bottom_json, const std::string& top_json);
} // namespace Slic3r

#endif // LIBSLIC3R_CUSTOMPARAMETERSHANDLING_HPP