#pragma once

#include <string>

#include "Slic3r/Domain/Preset/Bundle.hpp"

namespace Slic3r::App::CLI {

std::string get_json_printer_models(const Domain::Preset::Bundle& preset_bundle);
std::string get_json_print_tool_filament_profiles(
    const Domain::Preset::Bundle& preset_bundle,
    const std::string& printer_profile
);

} // namespace Slic3r::App::CLI
