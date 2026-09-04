#pragma once

#include <tl/expected.hpp>

#include "Slic3r/Domain/ConfigPack.hpp"
#include "Slic3r/Domain/Preset/Bundle.hpp"
#include "Slic3r/Domain/Preset/SelectedPreset.hpp"
#include "Slic3r/Biz/Config/ConfigLegacy.hpp"

namespace Slic3r::Biz::Preset::IO {
tl::expected<Domain::Preset::SelectedPresetMetadata, std::string>
load_legacy_preset_metadata(const LegacyPresetMetadata& legacy_preset, const Domain::ConfigPack& config, const Domain::Preset::Bundle& preset_bundle);
}

