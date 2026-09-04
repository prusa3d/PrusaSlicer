#pragma once

#include <vector>
#include <optional>

#include "Slic3r/Domain/Point.hpp"

namespace Slic3r::Biz::Algorithms::TravelingSalesman {

std::vector<std::size_t> chain_points(
    const Domain::Points& points, const std::optional<Domain::Point>& start_near
);

} // namespace Slic3r::Biz::Algorithms::TravelingSalesman
