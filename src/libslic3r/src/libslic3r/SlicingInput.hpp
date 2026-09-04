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

[[nodiscard]] tl::expected<Domain::PartialObjectConfigFDMPtr, std::vector<Biz::Slicing::Error>>
prepare_slicing_object_input(
    const Domain::ObjectSettings& object_settings,
    const Domain::Preset::HwPrinterConfig& hw_config,
    const std::size_t material_slot_count
);

[[nodiscard]] tl::expected<Domain::PartialVolumeConfigFDMPtr, std::vector<Biz::Slicing::Error>>
prepare_slicing_volume_input(
    const Domain::VolumeSettings& volume_settings,
    const Domain::Preset::HwPrinterConfig& hw_config,
    const std::size_t material_slot_count
);
} // namespace Slic3r
