#pragma once

#include <string>
#include "Slic3r/Domain/Project.hpp"
#include "libslic3r/SlicingStatus.hpp"

namespace Slic3r::App {
std::string to_display_string(Biz::Slicing::ErrorCode code);
std::string to_display_string(Biz::Slicing::Error error, const Domain::Project& project);
std::string to_display_string(Biz::Slicing::Warning warning, const Domain::Project& project);
std::string to_display_string(Biz::Slicing::ProgressInfo info);
} // namespace Slic3r::App
