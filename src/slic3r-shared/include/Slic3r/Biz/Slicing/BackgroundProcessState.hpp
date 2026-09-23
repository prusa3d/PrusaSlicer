#pragma once

#include <vector>

namespace Slic3r::Biz::Slicing {

struct BackgroundProcessState
{
    std::vector<unsigned> extruder_candidates;
};

} // namespace Slic3r::Biz::Slicing
