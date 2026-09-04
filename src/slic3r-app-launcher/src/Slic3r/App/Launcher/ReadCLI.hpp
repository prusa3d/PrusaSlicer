#pragma once

#include "Slic3r/App/Init.hpp"

#include <CLI/App.hpp>

namespace Slic3r::App::Launcher {

InitParams read_cli(::CLI::App& app, const std::string& app_version, int argc, char** argv);

} // namespace Slic3r::App::Launcher
