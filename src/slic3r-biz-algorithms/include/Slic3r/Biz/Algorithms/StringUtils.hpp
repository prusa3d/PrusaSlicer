#pragma once

#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace Slic3r::Biz::Algorithms {

/**
 * Function to detect containing of the illegal characters
 */
bool has_illegal_characters(const std::string& str);

std::string escape_string_cstyle(const std::string& str);

std::string escape_strings_cstyle(const std::vector<std::string>& strs);

bool unescape_string_cstyle(const std::string& str, std::string& str_out);

bool unescape_strings_cstyle(const std::string& str, std::vector<std::string>& out);

std::string to_lower_ascii(std::string_view data);

} // namespace Slic3r::Biz::Algorithms
