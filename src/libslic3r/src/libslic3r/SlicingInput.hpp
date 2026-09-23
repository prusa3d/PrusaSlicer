#pragma once

#include <tl/expected.hpp>
#include "Slic3r/Domain/ConfigPack.hpp"
#include "Slic3r/Domain/FullConfigFDM.hpp"
#include "libslic3r/SlicingStatus.hpp"

namespace Slic3r {

[[nodiscard]] tl::expected<Domain::FullConfigFDMPtr, std::vector<Biz::Slicing::Error>>
prepare_slicing_input(
    const Domain::ConfigPackFDM& config_pack,
    const std::vector<unsigned>& extruder_candidates,
    const Domain::Preset::HwPrinterConfig& hw_config
);

} // namespace Slic3r
