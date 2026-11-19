#pragma once

#include "Slic3r/Domain/ElementRef.hpp"
#include "Slic3r/Domain/Project.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/TriangleMesh.hpp"
#include "Slic3r/Biz/FileOpen/FileLoadProgress.hpp"

#include <optional>
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
 *
 * @param on_progress Optional, called when a loading stage starts. It may throw
 *                    FileOpen::FileLoadCancelled to stop the loading.
 */
Domain::Project load_file_as_project(
    const boost::filesystem::path& project_file_path,
    const Domain::Preset::Bundle& bundle,
    IMessageDialogProvider* dialog_provider,
    const FileOpen::FileLoadProgressFn& on_progress = {}
);

/**
 * @brief Geometry read from one source file, see read_files_for_import().
 */
struct ImportedFileData
{
    std::string file_name;
    boost::filesystem::path file_path;
    std::optional<Domain::TriangleMesh> mesh;
    std::optional<Domain::Model> model;
    bool geometry_only_3mf        = false;
    bool is_converted_from_inches = false;
    bool is_converted_from_meters = false;
};

/**
 * Result of importing source files into the scene graph.
 */
struct ImportToSceneResult {
    // Instance-level element refs of all newly added objects.
    Domain::ElementRefs instances;
    // True if at least one 3MF was loaded as geometry only.
    bool geometry_only_3mf = false;
};

struct ImportOptions {
    bool temporary_sources = false; ///< Imported objects keep no source path, reload from disk is disabled.
};

/**
 * @brief STEP import parameters from the app config, the app config is only accessed on the main
 * thread.
 */
struct StepImportSettings
{
    double linear_precision{0.};
    double angle_precision{0.};
    bool show_dialog{true};
    bool changed{false}; ///< Changed in the STEP import dialog, see store_step_import_settings().
};

/**
 * @return std::nullopt when there is no app config.
 */
std::optional<StepImportSettings> load_step_import_settings();

/**
 * @brief Stores the settings into the app config when they were changed.
 */
void store_step_import_settings(const StepImportSettings& settings);

/**
 * @brief Reads meshes (e.g., STL, OBJ) and complex models (e.g., 3MF) from multiple source files.
 *
 * It doesn't touch the scene nor the app config, so it can run in a job. Questions about unit
 * conversion, STEP parameters and multi-part objects are asked through the dialog provider.
 *
 * @param tool_count More tools allow to convert the files into one multi-part object.
 * @param step_settings Optional, STEP files are loaded with default parameters without them.
 *                      Updated when the user changes them in the STEP import dialog.
 * @param on_progress Optional, called before each file is read. It may throw
 *                    FileOpen::FileLoadCancelled to stop the reading.
 */
std::vector<ImportedFileData> read_files_for_import(
    const std::vector<boost::filesystem::path>& input_file_paths,
    int tool_count,
    IMessageDialogProvider* dialog_provider,
    StepImportSettings* step_settings,
    const FileOpen::FileLoadProgressFn& on_progress = {}
);

/**
 * @brief Inserts the files read by read_files_for_import() into the scene graph as new objects.
 */
ImportToSceneResult add_imported_files_to_scene(
    std::vector<ImportedFileData> data,
    Scene::SceneInteractor& scene_interactor,
    const Domain::Vec2d& bed_center,
    const ImportOptions& options = {}
);

/**
 * @brief Adds the files read by read_files_for_import() into the selected object as volumes.
 */
void add_imported_volumes_to_selected_object(
    std::vector<ImportedFileData> data,
    const Domain::ModelVolumeType& type,
    Scene::SceneInteractor& scene_interactor
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
 * @brief Scales the mesh to millimeters the same way as during the original import of its source.
 */
void apply_unit_conversion_from_source(
    Domain::TriangleMesh& mesh,
    const Domain::ModelVolume::Source& source
);

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

bool is_archive_file(const std::string& input_file);

} // namespace Slic3r::Biz::FileLoadingLogic
