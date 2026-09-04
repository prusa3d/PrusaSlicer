#pragma once

#include <string>

namespace Slic3r::Biz::Utils {

std::string xml_escape(std::string text, bool is_marked = false);

} // namespace Slic3r::Biz::Utils
