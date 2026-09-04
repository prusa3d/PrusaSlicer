#pragma once

#include <string>
#include <vector>

namespace Slic3r::App {
const std::vector<std::pair<std::string, std::string>>& get_renames();

std::string migrate_custom_gcode(const std::string& custom_gcode);
} // namespace Slic3r::App
