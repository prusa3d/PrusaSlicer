#pragma once

#include "Slic3r/Domain/SelectionId.hpp"

#include <vector>

namespace Slic3r::App {
class InitParams;
} // namespace Slic3r::App

namespace Slic3r::App::CLI {
class CLIRuntime;

bool has_full_config_from_profiles(const InitParams& init_params);

bool process_actions(
    CLIRuntime& runtime,
    const InitParams& init_params,
    const std::vector<Domain::SelectionId>& project_ids
);

bool process_profiles_sharing(CLIRuntime& runtime, const InitParams& init_params);
bool process_plugin_subcommand(CLIRuntime& runtime, InitParams& init_params);
} // namespace Slic3r::App::CLI
