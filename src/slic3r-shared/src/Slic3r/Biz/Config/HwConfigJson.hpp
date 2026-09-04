#pragma once

#include <nlohmann/json_fwd.hpp>
#include <tl/expected.hpp>
#include "Slic3r/Domain/Preset/HwConfig.hpp"

namespace Slic3r::Domain::Preset {
void to_json(nlohmann::ordered_json& j, const HwPrinterConfig& v);
} // namespace Slic3r::Domain::Preset

namespace Slic3r::Biz::Config {

tl::expected<Domain::Preset::HwPrinterConfig, std::string> load_hw_config(
    const nlohmann::ordered_json& json
);
} // namespace Slic3r::Biz::Config
