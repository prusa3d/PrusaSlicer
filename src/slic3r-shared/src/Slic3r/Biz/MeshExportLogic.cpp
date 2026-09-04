#include "Slic3r/Biz/MeshExportLogic.hpp"

#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/CGAL/Algorithms/MergeObjectVolumes.hpp"
#include "Slic3r/Biz/Format/OBJ.hpp"
#include "Slic3r/Biz/Format/STL.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/IMessageDialogProvider.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Biz/Scene/Selection.hpp"
#include "Slic3r/Domain/ModelInstance.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/ModelVolume.hpp"
#include "Slic3r/Domain/Project.hpp"

#include <boost/algorithm/string/predicate.hpp>
#include <optional>
#include <string>

using Slic3r::Biz::Scene::ObjectSelection;
using Slic3r::Biz::Scene::SceneInteractor;
using Slic3r::Biz::Scene::SelectionMode;
using Slic3r::Domain::ElementRef;
using Slic3r::Domain::ModelInstance;
using Slic3r::Domain::ModelObject;
using Slic3r::Domain::ModelVolume;
using Slic3r::Domain::Project;
using Slic3r::Domain::TriangleMesh;

namespace Slic3r::Biz::MeshExportLogic {

namespace {

bool store_mesh_by_extension(const boost::filesystem::path& output_path, const TriangleMesh& mesh)
{
    const std::string extension = output_path.extension().string();
    if (boost::algorithm::iequals(extension, ".stl")) {
        return store_stl(output_path.string(), mesh, true);
    }

    if (boost::algorithm::iequals(extension, ".obj")) {
        return store_obj(output_path.string(), mesh);
    }

    return false;
}

TriangleMesh mesh_for_volume_export(const ModelVolume& model_volume)
{
    TriangleMesh volume_mesh(model_volume.mesh());
    volume_mesh.transform(model_volume.get_matrix(), true);
    return volume_mesh;
}

struct ObjectExportMesh
{
    TriangleMesh mesh;
    bool used_positive_parts_fallback = false;
};

/**
 * @brief Combined mesh of the whole object, volumes joined by a CSG boolean with a positive parts fallback.
 *
 * @param model_object    Object the mesh is assembled from.
 * @param single_instance With nullptr all printable instances are merged into one mesh,
 *                        otherwise the mesh is transformed by the given instance only.
 * @return The mesh together with the flag telling whether the fallback was used.
 */
ObjectExportMesh
mesh_for_object_export(const ModelObject& model_object, const ModelInstance* single_instance)
{
    ObjectExportMesh result;

    std::optional<TriangleMesh> merged_mesh = CGAL::Algorithms::merge_object_volumes(model_object);
    if (merged_mesh.has_value()) {
        result.mesh = std::move(*merged_mesh);
    }

    if (result.mesh.empty()) {
        result.used_positive_parts_fallback = true;
        for (const ModelVolume* volume : model_object.volumes) {
            if (volume != nullptr && volume->is_model_part()) {
                result.mesh.merge(mesh_for_volume_export(*volume));
            }
        }
    }

    if (single_instance == nullptr) {
        const TriangleMesh volumes_mesh(std::move(result.mesh));
        result.mesh = TriangleMesh();

        for (const ModelInstance* instance : model_object.instances) {
            if (!instance->is_printable()) {
                continue;
            }

            TriangleMesh instance_mesh = volumes_mesh;
            instance_mesh.transform(instance->get_matrix(), true);
            result.mesh.merge(instance_mesh);
        }
    } else {
        result.mesh.transform(single_instance->get_matrix(), true);
    }

    return result;
}

} // namespace

std::string proposed_export_file_name(const SceneInteractor& scene_interactor)
{
    const ObjectSelection& selection = scene_interactor.object_selection();
    if (selection.empty()) {
        return {};
    }

    const Project& project    = scene_interactor.selected_project();
    const ElementRef& element = selection.elements.front();
    const ModelObject* object = project.find_object_by_id(element.object_id);
    if (object == nullptr) {
        return {};
    }

    std::string proposed_name;
    if (selection.mode == SelectionMode::Volume) {
        const ModelVolume* volume = project.find_volume_by_id(element.object_id, element.volume_id);
        if (volume != nullptr) {
            proposed_name = volume->name;
        }
    }

    if (proposed_name.empty()) {
        proposed_name = Algorithms::ModelObject::get_export_filename(*object);
    }

    if (proposed_name.empty()) {
        proposed_name = _u8L("Untitled");
    }

    return boost::filesystem::path(proposed_name).stem().string() + ".stl";
}

bool export_selection(
    const boost::filesystem::path& output_path,
    const SceneInteractor& scene_interactor,
    IMessageDialogProvider* dialog_provider
)
{
    const ObjectSelection& selection = scene_interactor.object_selection();
    if (selection.empty()) {
        return false;
    }

    const Project& project    = scene_interactor.selected_project();
    const ElementRef& element = selection.elements.front();
    const ModelObject* object = project.find_object_by_id(element.object_id);
    if (object == nullptr) {
        return false;
    }

    bool used_positive_parts_fallback = false;

    TriangleMesh mesh_to_store;
    if (selection.mode == SelectionMode::Volume) {
        const ModelVolume* volume = project.find_volume_by_id(element.object_id, element.volume_id);
        if (volume == nullptr) {
            return false;
        }

        mesh_to_store = mesh_for_volume_export(*volume);
        mesh_to_store.translate(-object->origin_translation.cast<float>());
    } else {
        const bool all_instances_selected = selection.elements.size() == object->instances.size();
        const ModelInstance* single_instance = nullptr;
        if (!all_instances_selected || object->instances.size() == 1) {
            single_instance = project.find_instance_by_id(element.object_id, element.instance_id);
        }

        ObjectExportMesh export_mesh = mesh_for_object_export(*object, single_instance);
        used_positive_parts_fallback = export_mesh.used_positive_parts_fallback;
        mesh_to_store                = std::move(export_mesh.mesh);

        if (single_instance != nullptr) {
            mesh_to_store.translate(-object->origin_translation.cast<float>());
        }
    }

    if (used_positive_parts_fallback && dialog_provider != nullptr) {
        dialog_provider->show_warning_dialog(
            _u8L(
                "Unable to perform boolean operation on model meshes. "
                "Only positive parts will be exported."
            ),
            _u8L("Export as STL/OBJ")
        );
    }

    if (!store_mesh_by_extension(output_path, mesh_to_store)) {
        if (dialog_provider != nullptr) {
            dialog_provider->show_error_dialog(
                _u8L("Unable to save the file") + ":\n" + output_path.string(),
                _u8L("Export as STL/OBJ")
            );
        }
        return false;
    }

    return true;
}

} // namespace Slic3r::Biz::MeshExportLogic
