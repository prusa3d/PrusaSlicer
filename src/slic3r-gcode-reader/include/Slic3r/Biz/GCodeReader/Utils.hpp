#pragma once

#include <string>
#include <vector>

namespace Slic3r::Biz::GCodeReader {

bool contains_reserved_tags(
    const std::string& gcode,
    const std::vector<std::string_view>& reserved_tags,
    unsigned int max_count,
    std::vector<std::string>& found_tag
);

} // namespace Slic3r::Biz::GCodeReader
