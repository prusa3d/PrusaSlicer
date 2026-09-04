#pragma once

#include "Slic3r/Domain/ElementRef.hpp"
#include "Slic3r/Domain/Project.hpp"
#include "Slic3r/Domain/Model.hpp"

#include <string>
#include <vector>
#include <boost/filesystem/path.hpp>
#include <tl/expected.hpp>

namespace Slic3r::Biz {
class IMessageDialogProvider;
class ProjectInteractor;
namespace Scene { class SceneInteractor; }
} // namespace Slic3r::Biz

namespace Slic3r::Domain::Preset {
struct Bundle;
} // namespace Slic3r::Domain::Preset

namespace Slic3r::Biz::FileLoadingLogic {

/**
 * Loading project from file.
 *
 * @note During loading, the file will be scanned for zero-volume objects.
 * Any found will be automatically removed, and the user will be notified.
 */
Domain::Project load_file_as_project(
    const boost::filesystem::path& project_file_path,
    const Domain::Preset::Bundle& bundle,
    IMessageDialogProvider* dialog_provider
);

/**
 * Load meshes (e.g., STL, OBJ) and complex models (e.g., 3MF) from multiple source files
 * and insert them into the scene graph.
 *
 * @return Instance-level element refs of all newly added objects.
 */
Domain::ElementRefs import_files_and_add_to_scene(
    const std::vector<boost::filesystem::path>& input_file_paths,
    int tool_count,
    Scene::SceneInteractor& scene_interactor,
    const Domain::Vec2d& bed_center,
    IMessageDialogProvider* dialog_provider
);

/**
 * Load meshes from multiple source files and add them into selected object
 */
void import_volumes_into_selected_object(
    const std::vector<boost::filesystem::path>& input_file_paths,
    const Domain::ModelVolumeType& type,
    Scene::SceneInteractor& scene_interactor,
    IMessageDialogProvider* dialog_provider
);

/**
 * Reads a 3D model from the specified input file and processes it.
 *
 * The input file may be a simple geometry file (e.g., STL, OBJ) or a more complex project file.
 * This function attempts to load the file, process its data, and construct a `Domain::Model` object.
 * If the file contains valid model data, a `Domain::Model` is returned. If it contains a valid mesh,
 * the mesh will be added to the model and default instances are created. If neither valid model nor mesh
 * data is found, an error message is returned instead.
 *
 * @param input_file A string specifying the path to the input file to be read and processed.
 * @return A `tl::expected` object containing the `Domain::Model` object on success, or an error string
 *         on failure.
 */
tl::expected<Domain::Model, std::string>
read_model_from_file(const std::string& input_file, IMessageDialogProvider* dialog_provider);

/**
 * Returns all file extensions that the slicer can import (projects and models).
 *
 * Extensions are lowercase and dot-prefixed (e.g. ".stl", ".3mf").
 * This is the single source of truth for importable formats; both
 * is_supported_file() and the Wildcards file-dialog strings are derived from it.
 */
const std::vector<std::string>& get_import_extensions();

/**
 * Checks if the given file is a project file.
 *
 * A project file is defined as a file with a ".3mf" extension.
 * The check is case-insensitive.
 *
 * @param input_file The name of the file to check.
 * @return True if the input_file has a ".3mf" extension, false otherwise.
 */
bool is_project_file(const std::string& input_file);

/**
 * Checks if the given file is a supported input file (project or model).
 *
 * Delegates to get_import_extensions() — no separate extension list to maintain.
 * The check is case-insensitive.
 *
 * @param input_file The name of the file to check.
 * @return True if the file can be loaded by the slicer, false otherwise.
 */
bool is_supported_file(const std::string& input_file);

} // namespace Slic3r::Biz::FileLoadingLogic
