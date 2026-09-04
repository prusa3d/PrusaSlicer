#pragma once

#include <nlohmann/json_fwd.hpp>

#include "Slic3r/Domain/GCodeMetadata.hpp"


namespace Slic3r::Domain {

void to_json(nlohmann::ordered_json& j, const GCodeMetadata& metadata);

}

namespace Slic3r::Biz::Config {

}