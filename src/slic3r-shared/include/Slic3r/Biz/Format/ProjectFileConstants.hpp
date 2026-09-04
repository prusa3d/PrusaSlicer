#pragma once

#include <string_view>

namespace Slic3r::Biz::Format::ProjectFileConstants {

inline constexpr std::string_view PRUSA_PROJECT_FILEPATH = "Metadata/PrusaSlicer3_project.json";
inline constexpr std::string_view PROJECT_METADATA       = "project";
inline constexpr std::string_view PRESET_METADATA        = "preset";
inline constexpr std::string_view CONFIGURATION          = "configuration";
inline constexpr std::string_view OBJECTS                = "objects";
inline constexpr std::string_view CONFIG_CONTAINERS      = "config_containers";
inline constexpr std::string_view VIRTUAL_EXTRUDERS      = "virtual_extruders";

} // namespace Slic3r::Biz::Format::ProjectFileConstants
