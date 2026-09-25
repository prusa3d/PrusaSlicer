#pragma once

#include <vector>
#include "Slic3r/Domain/EffectiveValues.hpp"

namespace Slic3r::Biz::Slicing {

struct BackgroundProcessState
{
    std::vector<unsigned> extruder_candidates;
    Domain::EffectiveValues effective_values;
};

} // namespace Slic3r::Biz::Slicing
