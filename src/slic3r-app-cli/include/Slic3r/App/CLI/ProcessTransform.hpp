#pragma once

#include "Slic3r/Domain/Point.hpp"
#include "Slic3r/Domain/SelectionId.hpp"

#include <vector>

namespace Slic3r::App {
class InitParams;
} // namespace Slic3r::App

namespace Slic3r::App::CLI {
class CLIRuntime;

/**
 * @brief Moves all instances of the selected project so that the center of their
 * common bounding box lies at @p center_point (XY only, Z is kept).
 */
void center_selected_project_around_point(CLIRuntime& runtime, const Domain::Vec2d& center_point);

/**
 * @brief Arranges all instances of the given project on its beds through the
 * ArrangeInteractor and waits for the asynchronous arrangement to finish.
 */
void arrange_and_wait(CLIRuntime& runtime, Domain::SelectionId project_id);

bool process_transform(
    CLIRuntime& runtime,
    const InitParams& init_params,
    std::vector<Domain::SelectionId>& project_ids
);

} // namespace Slic3r::App::CLI
