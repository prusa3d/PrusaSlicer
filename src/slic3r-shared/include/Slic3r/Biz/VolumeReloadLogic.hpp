#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <boost/filesystem/path.hpp>
#include <tl/expected.hpp>

namespace Slic3r::Biz {
class IMessageDialogProvider;

namespace Scene {
class SceneInteractor;
} // namespace Scene
} // namespace Slic3r::Biz

namespace Slic3r::Biz::VolumeReloadLogic {

/**
 * @brief File name to prefill the "Select the new file" dialog with.
 *
 * The source file of the selected volume when it still exists on disk, empty otherwise.
 */
std::string proposed_replace_file_name(const Scene::SceneInteractor& scene_interactor);

/**
 * @brief Replaces the mesh of the selected volume with the content of the given file.
 */
tl::expected<void, std::string> replace_selected_volume(
    const boost::filesystem::path& new_file_path,
    Scene::SceneInteractor& scene_interactor,
    IMessageDialogProvider* dialog_provider
);

struct ReloadFromDiskResult
{
    std::vector<std::string> failed_volume_names;
    std::size_t changed_volume_count = 0;
};

using MissingFileResolver = std::function<
    std::optional<boost::filesystem::path>(const boost::filesystem::path& missing_file)>;

/**
 * @brief Reloads the meshes of the current selection from their source files.
 *
 * Volumes whose source file is missing are resolved through missing_file_resolver.
 */
ReloadFromDiskResult reload_selection(
    const MissingFileResolver& missing_file_resolver,
    Scene::SceneInteractor& scene_interactor,
    IMessageDialogProvider* dialog_provider
);

} // namespace Slic3r::Biz::VolumeReloadLogic
