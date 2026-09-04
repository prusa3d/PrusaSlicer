#pragma once

#include <functional>

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App::ExportActions {

/** @brief Reusable UI actions for exporting sliced results.
 *
 * Provides ready-to-use callables that handle main-thread dispatching
 * and required UI dialogs internally.
 */
std::function<void()> export_gcode(Biz::ProjectInteractor& project_interactor);
std::function<void()> export_gcode_to_flash(Biz::ProjectInteractor& project_interactor);
std::function<void()> send_gcode_to_connect(Biz::ProjectInteractor& project_interactor);
std::function<void()> upload_gcode_to_print_host(Biz::ProjectInteractor& project_interactor);


/**
 * @brief Check whether export is currently possible.
 */
bool can_export(Biz::ProjectInteractor& project_interactor);

} // namespace Slic3r::App::ExportActions
