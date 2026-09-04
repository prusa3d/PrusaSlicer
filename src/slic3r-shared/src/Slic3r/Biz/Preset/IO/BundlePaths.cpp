#include "Slic3r/Biz/Preset/IO/BundlePaths.hpp"
#include "Slic3r/Directories.hpp"

#include <boost/filesystem/operations.hpp>

namespace Slic3r::Biz::Preset::IO {

namespace fs = boost::filesystem;

BundlePaths BundlePaths::make_standard_runtime()
{
    const auto app_preset_bundle_dir   = fs::path{Slic3r::resources_dir()} / "presets";
    const auto local_preset_bundle_dir = fs::path{Slic3r::data_dir()} / "presets" / "local";
    const auto user_preset_bundle_dir  = fs::path{Slic3r::data_dir()} / "presets" / "user";
    const auto config_dir              = fs::path{Slic3r::data_dir()} / "configs";

    return {
        .app_bundle_path   = app_preset_bundle_dir.string(),
        .local_bundle_path = local_preset_bundle_dir.string(),
        .populate_local_bundle = true,
        .user_bundle_path  = user_preset_bundle_dir.string(),
        .user_config_path  = config_dir.string(),
    };
}

BundlePaths BundlePaths::make_test_runtime(const boost::filesystem::path& test_data_dir)
{
    const auto app_preset_bundle_dir = test_data_dir / "presets";
    const auto config_dir            = test_data_dir / "configs";

    const fs::path tmp{fs::temp_directory_path() / fs::unique_path()};
    if (fs::exists(tmp))
        fs::remove_all(tmp);

    const fs::path local_bundle = tmp / "local";
    const fs::path user_bundle = tmp / "user";

    fs::create_directories(local_bundle);
    fs::create_directories(user_bundle);

    return {
        .app_bundle_path  = app_preset_bundle_dir.string(),
        .local_bundle_path = local_bundle.string(),
        .populate_local_bundle = false,
        .user_bundle_path  = user_bundle.string(),
        .user_config_path = config_dir.string(),
    };
}

} // namespace Slic3r::Biz::Preset::IO
