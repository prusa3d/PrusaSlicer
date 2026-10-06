#pragma once

#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterConfig.hpp"

#include <string>
#include <functional>

namespace Slic3r::App {

int score_preset_item(
    std::function<const std::string&(const std::string&)> process_string,
    const Biz::Preset::PresetItem& item,
    const std::string& search_text
);

int score_physical_printer(
    std::function<const std::string&(const std::string&)> process_string,
    const Biz::PhysicalPrinter::PhysicalPrinterConfig item,
    const std::string& search_text
);

} // namespace Slic3r::App
