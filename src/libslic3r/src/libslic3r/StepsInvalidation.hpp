#pragma once

#include "libslic3r/Print.hpp"

namespace Slic3r::SlicingSync {

template <typename Set>
AllOrSome<Set> merge(const AllOrSome<Set>& a, const AllOrSome<Set>& b);

InvalidatedSteps merge(const InvalidatedSteps& a, const InvalidatedSteps& b);

InvalidatedSteps merge(const std::vector<InvalidatedSteps>& invalidated_steps);

using Step = std::variant<FDMPrintStep, FDMPrintObjectStep>;

std::vector<Step> propagate(Step step);

PrintAndObjectSteps get_invalidated_steps(
    const PrintObjectRegions& current_regions,
    const PrintObjectRegions& next_regions
);
}
