#pragma once

#include <string>

namespace Slic3r::Biz::Preset {
std::string merge_json(const std::string& base_json, const std::string& override_json);
} // namespace Slic3r::Biz::Preset
