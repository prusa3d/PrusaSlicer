#pragma once

#include <boost/filesystem/path.hpp>

#include <string>

namespace Slic3r::Biz {
class IMessageDialogProvider;

namespace Scene {
class SceneInteractor;
} // namespace Scene
} // namespace Slic3r::Biz

namespace Slic3r::Biz::MeshExportLogic {

/**
 * @brief File name to prefill the export dialog with, without a directory.
 */
std::string proposed_export_file_name(const Scene::SceneInteractor& scene_interactor);

/**
 * @brief Assemble the mesh for the current scene selection and store it into output_path.
 *
 * The format follows the extension of output_path (".stl" -> binary STL, ".obj" ->
 * Wavefront OBJ, case-insensitive). Problems are reported through dialog_provider.
 *
 * @return False when nothing was written.
 */
bool export_selection(
    const boost::filesystem::path& output_path,
    const Scene::SceneInteractor& scene_interactor,
    IMessageDialogProvider* dialog_provider
);

} // namespace Slic3r::Biz::MeshExportLogic
