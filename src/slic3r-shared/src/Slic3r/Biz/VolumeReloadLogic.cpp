#include "Slic3r/Biz/VolumeReloadLogic.hpp"

#include "Slic3r/Biz/Algorithms/ModelVolume.hpp"
#include "Slic3r/Biz/FileLoadingLogic.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/IMessageDialogProvider.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Biz/Scene/SelectionExtents.hpp"
#include "Slic3r/Domain/ElementRef.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/ModelVolume.hpp"
#include "Slic3r/Domain/Project.hpp"
#include "Slic3r/Utils.hpp"

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem/operations.hpp>
#include <optional>
#include <string>
#include <utility>

using Slic3r::Biz::Scene::SceneInteractor;
using Slic3r::Biz::Scene::SelectionExtents;
using Slic3r::Domain::ElementRef;
using Slic3r::Domain::ElementRefs;
using Slic3r::Domain::Model;
using Slic3r::Domain::ModelObject;
using Slic3r::Domain::ModelVolume;
using Slic3r::Domain::Project;
using Slic3r::Domain::SquareMatrix4d;

namespace Slic3r::Biz::VolumeReloadLogic {

namespace {

tl::expected<void, std::string> replace_volume_from_file(
    const ElementRef& volume_element,
    const boost::filesystem::path& new_file_path,
    const Project& project,
    SceneInteractor& scene_interactor,
    IMessageDialogProvider* dialog_provider
)
{
    tl::expected<Model, std::string> loaded_model =
        FileLoadingLogic::read_model_from_file(new_file_path.string(), dialog_provider);
    if (!loaded_model) {
        return tl::make_unexpected(loaded_model.error());
    }

    if (loaded_model->objects.size() != 1 || loaded_model->objects.front()->volumes.size() != 1) {
        return tl::make_unexpected(_u8L("Unable to replace with more than one volume"));
    }

    const ModelVolume* old_volume =
        project.find_volume_by_id(volume_element.object_id, volume_element.volume_id);
    if (old_volume == nullptr) {
        return tl::make_unexpected(_u8L("Volume to replace was not found"));
    }

    const ModelVolume& new_volume  = *loaded_model->objects.front()->volumes.front();
    ModelVolume::Source new_source = new_volume.source;
    new_source.input_file          = new_file_path.string();
    new_source.object_idx          = 0;
    new_source.volume_idx          = 0;

    SceneInteractor::VolumeMeshReplacement replacement = {
        .element            = volume_element,
        .mesh               = new_volume.mesh(),
        .new_source         = std::move(new_source),
        .new_transformation = old_volume->get_matrix()
            * Domain::translation_transform(
                                  new_volume.source.mesh_offset - old_volume->source.mesh_offset
            ),
        .new_name = new_volume.name.empty() ? new_file_path.filename().string() : new_volume.name
    };

    SceneInteractor::VolumeMeshReplacements replacements;
    replacements.push_back(std::move(replacement));
    scene_interactor.change_volume_meshes(std::move(replacements));

    const ModelObject* object = project.find_object_by_id(volume_element.object_id);
    if (object != nullptr && object->volumes.size() == 1) {
        const std::optional<SelectionExtents> selection_extents =
            scene_interactor.selection_bounding_box();
        if (selection_extents.has_value() && selection_extents->is_floating()) {
            SquareMatrix4d drop_transform = SquareMatrix4d::Identity();
            drop_transform.col(3).z()     = -selection_extents->min_z();
            scene_interactor.transform_selection(drop_transform);
        }
    }

    return {};
}

struct ReloadableVolume
{
    ElementRef element;
    boost::filesystem::path original_source_path;
    boost::filesystem::path resolved_path;
};

using ReloadableVolumes = std::vector<ReloadableVolume>;

ReloadableVolumes resolve_reloadable_volumes(const ElementRefs& volume_refs, const Project& project)
{
    ReloadableVolumes reloadable_volumes;
    reloadable_volumes.reserve(volume_refs.size());

    for (const ElementRef& element : volume_refs) {
        const ModelVolume* volume = project.find_volume_by_id(element.object_id, element.volume_id);
        if (volume == nullptr || !Algorithms::ModelVolume::is_reloadable_from_disk(*volume)) {
            continue;
        }

        ReloadableVolume reloadable_volume{
            .element              = element,
            .original_source_path = boost::filesystem::path(volume->source.input_file)
        };

        if (boost::filesystem::exists(reloadable_volume.original_source_path)) {
            reloadable_volume.resolved_path = reloadable_volume.original_source_path;
        } else {
            const ModelObject* object = project.find_object_by_id(element.object_id);
            if (object != nullptr && !object->input_file.empty()) {
                boost::filesystem::path candidate_path =
                    boost::filesystem::path(object->input_file).remove_filename();
                if (!candidate_path.empty()) {
                    candidate_path /= reloadable_volume.original_source_path.filename();
                    if (boost::filesystem::exists(candidate_path)) {
                        reloadable_volume.resolved_path = candidate_path;
                    }
                }
            }
        }

        reloadable_volumes.push_back(std::move(reloadable_volume));
    }

    return reloadable_volumes;
}

const ModelVolume* find_matching_volume(const Model& loaded_model, const ModelVolume& old_volume)
{
    const ModelVolume::Source& old_source = old_volume.source;
    if (old_source.object_idx >= 0
        && old_source.object_idx < static_cast<int>(loaded_model.objects.size()))
    {
        const ModelObject* loaded_object = loaded_model.objects[old_source.object_idx];
        if (old_source.volume_idx >= 0
            && old_source.volume_idx < static_cast<int>(loaded_object->volumes.size())
            && loaded_object->volumes[old_source.volume_idx]->name == old_volume.name)
        {
            return loaded_object->volumes[old_source.volume_idx];
        }
    }

    for (const ModelObject* loaded_object : loaded_model.objects) {
        for (const ModelVolume* candidate_volume : loaded_object->volumes) {
            if (candidate_volume->name == old_volume.name) {
                return candidate_volume;
            }
        }
    }

    return nullptr;
}

struct VolumeReplaceRequest
{
    boost::filesystem::path original_source_path;
    boost::filesystem::path replacement_path;
};

using VolumeReplaceRequests = std::vector<VolumeReplaceRequest>;

bool resolve_missing_files(
    ReloadableVolumes& reloadable_volumes,
    VolumeReplaceRequests& replace_requests,
    const MissingFileResolver& missing_file_resolver,
    IMessageDialogProvider* dialog_provider
)
{
    std::vector<boost::filesystem::path> missing_paths;
    for (const ReloadableVolume& reloadable_volume : reloadable_volumes) {
        if (reloadable_volume.resolved_path.empty()) {
            missing_paths.push_back(reloadable_volume.original_source_path);
        }
    }

    sort_remove_duplicates(missing_paths);

    const auto assign_resolved_path = [&reloadable_volumes](
                                          const boost::filesystem::path& original_path,
                                          const boost::filesystem::path& resolved_path
                                      )
    {
        for (ReloadableVolume& reloadable_volume : reloadable_volumes) {
            if (reloadable_volume.resolved_path.empty()
                && reloadable_volume.original_source_path == original_path)
            {
                reloadable_volume.resolved_path = resolved_path;
            }
        }
    };

    while (!missing_paths.empty()) {
        const boost::filesystem::path missing_path = missing_paths.back();
        const std::optional<boost::filesystem::path> selected_path =
            missing_file_resolver(missing_path);

        if (!selected_path.has_value()) {
            return false;
        }

        missing_paths.pop_back();
        if (!boost::algorithm::iequals(
                missing_path.filename().string(),
                selected_path->filename().string()
            ))
        {
            // A file with a different name can only replace the volume, not reload it.
            bool replace_confirmed = false;
            if (dialog_provider != nullptr) {
                const std::string message = fmt::format(
                    fmt::runtime(
                        _u8L("The selected file ({}) differs from the original file ({}).")
                        + "\n"
                        + _u8L("Do you want to replace it?")
                    ),
                    selected_path->filename().string(),
                    missing_path.filename().string()
                );

                dialog_provider->show_yesno_dialog(
                    _u8L("Replace file?"),
                    message,
                    [&replace_confirmed](bool answer) { replace_confirmed = answer; }
                );
            }

            if (replace_confirmed) {
                replace_requests.push_back(VolumeReplaceRequest{missing_path, *selected_path});
            }

            continue;
        }

        assign_resolved_path(missing_path, *selected_path);

        std::erase_if(
            missing_paths,
            [&selected_path, &assign_resolved_path](const boost::filesystem::path& other_path)
            {
                const boost::filesystem::path repathed_file =
                    selected_path->parent_path() / other_path.filename();
                if (!boost::filesystem::exists(repathed_file)) {
                    return false;
                }

                assign_resolved_path(other_path, repathed_file);

                return true;
            }
        );
    }

    return true;
}

} // namespace

std::string proposed_replace_file_name(const SceneInteractor& scene_interactor)
{
    const ElementRefs volume_refs = scene_interactor.selected_volumes();
    if (volume_refs.size() != 1) {
        return {};
    }

    const ElementRef& volume_element = volume_refs.front();
    const ModelVolume* volume        = scene_interactor.selected_project().find_volume_by_id(
        volume_element.object_id,
        volume_element.volume_id
    );
    if (volume == nullptr
        || volume->source.input_file.empty()
        || !boost::filesystem::exists(volume->source.input_file))
    {
        return {};
    }

    return boost::filesystem::path(volume->source.input_file).filename().string();
}

tl::expected<void, std::string> replace_selected_volume(
    const boost::filesystem::path& new_file_path,
    SceneInteractor& scene_interactor,
    IMessageDialogProvider* dialog_provider
)
{
    const ElementRefs volume_refs = scene_interactor.selected_volumes();
    if (volume_refs.size() != 1) {
        return tl::make_unexpected(_u8L("Volume to replace was not found"));
    }

    return replace_volume_from_file(
        volume_refs.front(),
        new_file_path,
        scene_interactor.selected_project(),
        scene_interactor,
        dialog_provider
    );
}

ReloadFromDiskResult reload_selection(
    const MissingFileResolver& missing_file_resolver,
    SceneInteractor& scene_interactor,
    IMessageDialogProvider* dialog_provider
)
{
    const Project& project               = scene_interactor.selected_project();
    const ElementRefs& selected_volumes  = scene_interactor.selected_volumes();
    ReloadableVolumes reloadable_volumes = resolve_reloadable_volumes(selected_volumes, project);
    if (reloadable_volumes.empty()) {
        return {};
    }

    VolumeReplaceRequests replace_requests;
    if (!resolve_missing_files(
            reloadable_volumes,
            replace_requests,
            missing_file_resolver,
            dialog_provider
        ))
    {
        return {};
    }

    std::vector<boost::filesystem::path> input_paths;
    for (const ReloadableVolume& reloadable_volume : reloadable_volumes) {
        if (!reloadable_volume.resolved_path.empty()) {
            input_paths.push_back(reloadable_volume.resolved_path);
        }
    }

    sort_remove_duplicates(input_paths);

    ReloadFromDiskResult result;
    SceneInteractor::VolumeMeshReplacements replacements;
    for (const boost::filesystem::path& input_path : input_paths) {
        tl::expected<Model, std::string> loaded_model =
            FileLoadingLogic::read_model_from_file(input_path.string(), dialog_provider);
        if (!loaded_model) {
            result.failed_volume_names.push_back(input_path.filename().string());
            continue;
        }

        for (const ReloadableVolume& reloadable_volume : reloadable_volumes) {
            if (reloadable_volume.resolved_path != input_path) {
                continue;
            }

            const ModelVolume* old_volume = project.find_volume_by_id(
                reloadable_volume.element.object_id,
                reloadable_volume.element.volume_id
            );
            if (old_volume == nullptr) {
                result.failed_volume_names.push_back(
                    reloadable_volume.original_source_path.filename().string()
                );

                continue;
            }

            const ModelVolume* new_volume = find_matching_volume(*loaded_model, *old_volume);
            const ModelVolume::Source& old_source = old_volume->source;
            if (new_volume == nullptr) {
                result.failed_volume_names.push_back(
                    !old_source.input_file.empty() ? old_source.input_file : old_volume->name
                );

                continue;
            }

            ModelVolume::Source new_source = new_volume->source;
            new_source.object_idx          = old_source.object_idx;
            new_source.volume_idx          = old_source.volume_idx;

            SceneInteractor::VolumeMeshReplacement replacement = {
                .element            = reloadable_volume.element,
                .mesh               = new_volume->mesh(),
                .new_source         = std::move(new_source),
                .new_transformation = old_volume->get_matrix()
                    * old_source.transform.get_matrix_no_offset()
                    * Domain::translation_transform(
                                          new_volume->source.mesh_offset - old_source.mesh_offset
                    )
                    * new_volume->source.transform.get_matrix_no_offset().inverse()
            };

            replacements.push_back(std::move(replacement));
        }
    }

    if (!replacements.empty()) {
        result.changed_volume_count =
            scene_interactor.change_volume_meshes(std::move(replacements)).size();
    }

    // Files the user substituted can only be applied as a replace.
    for (const VolumeReplaceRequest& replace_request : replace_requests) {
        for (const ReloadableVolume& reloadable_volume : reloadable_volumes) {
            if (!boost::algorithm::iequals(
                    reloadable_volume.original_source_path.string(),
                    replace_request.original_source_path.string()
                ))
            {
                continue;
            }

            const tl::expected<void, std::string> replace_result = replace_volume_from_file(
                reloadable_volume.element,
                replace_request.replacement_path,
                project,
                scene_interactor,
                dialog_provider
            );

            if (replace_result) {
                ++result.changed_volume_count;
            } else {
                result.failed_volume_names.push_back(
                    reloadable_volume.original_source_path.filename().string()
                );
            }
        }
    }

    return result;
}

} // namespace Slic3r::Biz::VolumeReloadLogic
