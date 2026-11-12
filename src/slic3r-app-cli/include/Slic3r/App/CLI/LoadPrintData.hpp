#pragma once

#include "Slic3r/Domain/SelectionId.hpp"

#include <vector>

namespace Slic3r::App {
class InitParams;
} // namespace Slic3r::App

namespace Slic3r::App::CLI {
class CLIRuntime;

bool load_print_data(
    CLIRuntime& runtime,
    std::vector<Domain::SelectionId>& project_ids,
    const InitParams& init_params
);

bool is_needed_post_processing(
    const CLIRuntime& runtime,
    const std::vector<Domain::SelectionId>& project_ids
);

} // namespace Slic3r::App::CLI
