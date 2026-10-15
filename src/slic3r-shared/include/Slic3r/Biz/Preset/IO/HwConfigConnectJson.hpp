#pragma once
#include <string>
#include <string_view>

#include <tl/expected.hpp>

#include "Slic3r/Domain/Preset/HwConfig.hpp"
#include "Slic3r/Domain/Preset/Bundle.hpp"

namespace Slic3r::Biz::Preset::IO {

tl::expected<Domain::Preset::HwPrinterConfig, std::string>
load_external_hw_config(std::string_view json);

} // namespace Slic3r::Biz::Preset::IO
