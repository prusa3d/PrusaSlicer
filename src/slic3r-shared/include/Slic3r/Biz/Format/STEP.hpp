// Original implementation of STEP format import created by Bambulab.
// https://github.com/bambulab/BambuStudio
// Forked off commit 1555904, modified by Prusa Research.

#pragma once

#include <string>
#include <utility>
#include <optional>

#include "tl/expected.hpp"

namespace Slic3r::Domain {
class Model;
} // namespace Slic3r::Domain

namespace Slic3r::Biz {

// Load a step file.
// Inside deflections pair:
// * first value is linear deflection
// * second value is angle deflection
tl::expected<Domain::Model, std::string> load_step(
    const std::string& path_str,
    std::optional<std::pair<double, double>> deflections = std::nullopt)
#if SLIC3R_ENABLE_FORMAT_STEP
;
#else
{
    return tl::make_unexpected(std::string("STEP format support is not enabled in this build."));
}
#endif

} // namespace Slic3r::Biz
