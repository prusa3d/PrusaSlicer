#pragma once

#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/CustomGCode.hpp"

// Temporary header (hopefully) before we find a better place to put this.

namespace Slic3r::Domain {

using WipeTowersOnBeds = std::map<int, Domain::ModelWipeTower>;
using CustomGCodesOnBeds = std::map<int, Domain::CustomGCode::Info>;

}
