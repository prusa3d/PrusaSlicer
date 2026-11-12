#pragma once
#include <string>
#include <boost/filesystem/path.hpp>

namespace Slic3r::Biz::Preset::IO {

/**
 * @brief Container for file system paths related to preset storage.
 */
struct BundlePaths
{
    /**
     * @brief Path to the presets bundled with the app
     */
    std::string app_bundle_path;
    /**
     * @brief Path to a presets downloaded locally (via preset updater)
     * @note It may be an empty string indicating no locally downloaded presets are available.
     */
    std::string local_bundle_path;

    bool populate_local_bundle = false;

    /**
     * @brief Path to a user saved presets
     * @note It may be an empty string indicating no user saved presets are available.
     */
    std::string user_bundle_path;

    /**
     * @brief Path where use configurations are saved.
     */
    std::string user_config_path;

    boost::filesystem::path
    user_preset_dir_path(const std::string& vendor_id, const std::string& repo_id) const
    {
        return boost::filesystem::path{user_bundle_path} / repo_id / vendor_id;
    }

    static BundlePaths make_standard_runtime();
    static BundlePaths make_test_runtime(const boost::filesystem::path& test_data_dir);
};

} // namespace Slic3r::Biz::Preset::IO
