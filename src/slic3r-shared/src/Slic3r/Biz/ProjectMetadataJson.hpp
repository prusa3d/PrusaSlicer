#pragma once

#include <nlohmann/json_fwd.hpp>

#include "Slic3r/Domain/ProjectMetadata.hpp"

namespace Slic3r::Domain {

void to_json(nlohmann::ordered_json& j, const ProjectMetadata& v);
void from_json(const nlohmann::ordered_json& j, ProjectMetadata& v);

} // namespace Slic3r::Domain
