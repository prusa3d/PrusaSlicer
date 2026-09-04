#pragma once

#include "Slic3r/Biz/VirtualExtrudersConfig.hpp"
#include "Slic3r/Domain/VirtualExtruder.hpp"

#include <nlohmann/json_fwd.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace Slic3r::Biz::Format::VirtualExtruder {

inline constexpr std::string_view CONFIG_FILE = "Metadata/Prusa_Slicer_full_spectrum.json";

std::string serialize_virtual_extruders_to_json(
    const std::vector<std::string>& physical_extruders_colors,
    const Domain::VirtualExtruders& virtual_extruders
);

nlohmann::json serialize_virtual_extruders_to_project_json(
    const std::vector<std::string>& physical_extruders_colors,
    const Domain::VirtualExtruders& virtual_extruders
);

Biz::VirtualExtrudersConfig deserialize_virtual_extruders_from_json(
    const std::string& json_content
);

Domain::VirtualExtruders deserialize_virtual_extruders_from_project_json(
    const nlohmann::json& virtual_extruders_json
);

} // namespace Slic3r::Biz::Format::VirtualExtruder
