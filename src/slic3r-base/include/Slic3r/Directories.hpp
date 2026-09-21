#pragma once

#include <string>
#include <boost/filesystem/path.hpp>

namespace Slic3r {

// Set a path with GUI resource files.
void set_var_dir(const std::string& path);
// Return a full path to the GUI resource files.
const std::string& var_dir();
// Return a full resource path for a file_name.
std::string var(const std::string& file_name);

// Set a path with various static definition data (for example the initial config bundles).
void set_resources_dir(const std::string& path);
// Return a full path to the resources directory.
const std::string& resources_dir();

// Set a path with GUI localization files.
void set_local_dir(const std::string& path);
// Return a full path to the localization directory.
const std::string& localization_dir();

// Set a path with shapes gallery files.
void set_sys_shapes_dir(const std::string& path);
// Return a full path to the system shapes gallery directory.
const std::string& sys_shapes_dir();

// Return a full path to the custom shapes gallery directory.
std::string custom_shapes_dir();

// Set a path with shapes gallery files.
void set_custom_gcodes_dir(const std::string& path);
// Return a full path to the system shapes gallery directory.
const std::string& custom_gcodes_dir();

// Set a path with preset files.
void set_data_dir(const std::string& path);
// Return a full path to the GUI resource files.
const std::string& data_dir();

std::string get_default_datadir();

// Directory name this build uses under the platform config and cache roots,
// e.g. "PrusaSlicer3", or "PrusaSlicer3-dev" for an alpha or beta.
std::string app_versioned_dir_name();

void set_cache_dir(const std::string& path);

const std::string& cache_dir();

std::string get_default_cachedir();

/** Return the web view data directory of this application version, or an empty string to let the web view backend pick its own. */
std::string webview_data_dir();

/** Remove web view data directories of other application versions. Safe to call from any thread. */
void prune_stale_webview_data_dirs();
/**
 * @brief Where the preset updater keeps downloads it has not installed yet.
 *
 * Staging has to share a volume with the installed presets, so that finishing an install is a
 * rename and cannot fail once the download succeeded. When the data directory was chosen
 * explicitly, staging therefore belongs inside it; otherwise the platform cache location is used.
 *
 * @param datadir_overridden whether --datadir was passed. Reads data_dir() when it was, so
 * set_data_dir must already have been called.
 */
std::string get_default_updater_cache_dir(bool datadir_overridden);

void set_updater_cache_dir(const std::string& path);

const std::string& updater_cache_dir();

// Where the preset updater downloads to before installing. Inside updater_cache_dir().
boost::filesystem::path updater_staging_dir();

boost::filesystem::path system_downloads_dir();

boost::filesystem::path temp_dir();
// set_temp_dir is test only - it is used to provide temp dir in test enviroment when filesystem with real temp is not available.  
void set_temp_dir(const boost::filesystem::path& path);
} // namespace Slic3r::Biz
