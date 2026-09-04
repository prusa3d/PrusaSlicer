#pragma once

#include <nlohmann/json_fwd.hpp>
#include <tl/expected.hpp>
#include "Slic3r/Domain/Preset/SelectedPreset.hpp"

namespace Slic3r::Domain::Preset {

void to_json(nlohmann::ordered_json& j, const SelectedPresetMetadata& v);

} // namespace Slic3r::Domain::Preset

namespace Slic3r::Biz::Config {

tl::expected<Domain::Preset::SelectedPresetMetadata, std::string> load_preset_metadata(
    const nlohmann::ordered_json& j
);

} // namespace Slic3r::Biz::Config
