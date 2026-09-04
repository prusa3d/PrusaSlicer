#include "Slic3r/Biz/ClipboardInteractor.hpp"

#include "Slic3r/Biz/ArrangeInteractor.hpp"
#include "Slic3r/Biz/IUndoProvider.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/ModelInstance.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/ModelVolume.hpp"
#include "Slic3r/Domain/Workbench.hpp"

#include <set>

using namespace Slic3r;
using namespace Slic3r::Biz;

using Slic3r::Biz::Scene::ObjectSelection;
using Slic3r::Biz::Scene::SelectionMode;
using Slic3r::Domain::BedRef;
using Slic3r::Domain::ElementRef;
using Slic3r::Domain::ElementRefs;
using Slic3r::Domain::ModelInstance;
using Slic3r::Domain::ModelObject;
using Slic3r::Domain::ModelObjectPtrs;
using Slic3r::Domain::ModelVolume;
using Slic3r::Domain::Project;
using Slic3r::Domain::SelectionId;
using Slic3r::Domain::Workbench;

namespace Slic3r::Biz {

ClipboardInteractor::ClipboardInteractor(
    Scene::SceneInteractor& scene_interactor,
    ArrangeInteractor& arrange_interactor,
    const Workbench& workbench
) :
    m_scene_interactor{scene_interactor},
    m_arrange_interactor{arrange_interactor},
    m_workbench{workbench}
{}

bool ClipboardInteractor::can_copy() const
{
    const ObjectSelection& object_selection = m_scene_interactor.object_selection();
    return !object_selection.empty() && !object_selection.contains_wipe_tower();
}

bool ClipboardInteractor::can_paste() const
{
    if (m_clipboard.is_empty()) {
        return false;
    } else if (!m_workbench.projects().contains(m_clipboard.source_project_id)) {
        return false;
    } else if (m_clipboard.mode == SelectionMode::Volume
               && !m_scene_interactor.object_selection().only_single_object())
    {
        // We allow pasting volume only when single object is sellected.
        return false;
    }

    const Project& source_project = m_workbench.project(m_clipboard.source_project_id);
    const bool any_source_exists  = std::ranges::any_of(
        m_clipboard.selected_elements,
        [&source_project](const ElementRef& element)
        { return source_project.find_object_by_id(element.object_id) != nullptr; }
    );

    return any_source_exists;
}

void ClipboardInteractor::copy(const SelectionId project_id)
{
    const ObjectSelection& selection = m_scene_interactor.object_selection();
    if (selection.empty()) {
        return;
    }

    m_clipboard.clear();
    m_clipboard.source_project_id = project_id;
    m_clipboard.mode              = selection.mode;
    m_clipboard.selected_elements = selection.elements;
}

void ClipboardInteractor::paste(const SelectionId project_id)
{
    if (m_clipboard.is_empty()) {
        return;
    }

    if (m_clipboard.mode == SelectionMode::Volume) {
        this->paste_volumes(project_id);
    } else {
        this->paste_objects(project_id);
    }
}

void ClipboardInteractor::Clipboard::clear()
{
    this->source_project_id = Domain::INVALID_ID;
    this->mode              = SelectionMode::Instance;
    this->selected_elements.clear();
}

bool ClipboardInteractor::Clipboard::is_empty() const
{
    return selected_elements.empty();
}

void ClipboardInteractor::paste_objects(const SelectionId project_id)
{
    const ModelObjectPtrs new_objects = m_scene_interactor.clone_objects_from_project(
        m_clipboard.source_project_id,
        m_clipboard.selected_elements
    );

    ElementRefs new_instances;
    for (const ModelObject* new_object : new_objects) {
        for (const ModelInstance* instance : new_object->instances) {
            new_instances.emplace_back(new_object->id().id, instance->id().id, 0);
        }
    }

    if (new_instances.empty()) {
        return;
    }

    const BedRef target_bed = m_scene_interactor.bed_selection().last_selected_bed();
    m_arrange_interactor.arrange_added_instances(
        project_id,
        new_instances,
        target_bed,
        UndoSnapshotType::PasteObjects
    );
}

void ClipboardInteractor::paste_volumes(SelectionId project_id)
{
    ASSERT(m_clipboard.mode == SelectionMode::Volume);
    ASSERT(!m_clipboard.selected_elements.empty());

    const ObjectSelection& selection = m_scene_interactor.object_selection();
    if (selection.empty() || !selection.only_single_object()) {
        return;
    }

    const Project& source_project    = m_workbench.project(m_clipboard.source_project_id);
    const size_t source_object_id    = m_clipboard.selected_elements.front().object_id;
    const ModelObject* source_object = source_project.find_object_by_id(source_object_id);
    if (source_object == nullptr) {
        return;
    }

    std::set<size_t> selected_volume_ids;
    for (const ElementRef& selected_element : m_clipboard.selected_elements) {
        if (selected_element.has_volume()) {
            selected_volume_ids.insert(selected_element.volume_id);
        }
    }

    if (selected_volume_ids.empty()) {
        return;
    }

    const size_t selection_instance_id = selection.elements.front().instance_id;
    for (const ModelVolume* source_volume : source_object->volumes) {
        if (!selected_volume_ids.contains(source_volume->id().id)) {
            continue;
        }

        m_scene_interactor.add_volume(
            project_id,
            selection_instance_id,
            [source_volume](ModelObject& target_object) -> ModelVolume*
            {
                ModelVolume* new_volume = target_object.add_volume(*source_volume);
                new_volume->set_new_unique_id();
                return new_volume;
            }
        );
    }

    m_scene_interactor.undo_provider().take_snapshot(UndoSnapshotType::PasteVolumes);
}

} // namespace Slic3r::Biz
