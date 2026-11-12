#include "Slic3r/App/Plater/SelectionHandler.hpp"
#include "Slic3r/App/Scene/SceneNodeTag.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"

#include <unordered_set>

using Slic3r::App::Scene::SceneNodeTag;

namespace Slic3r::App::Plater {

void SelectionHandler::mark_selected(
    Scene::Node& n,
    bool replace,
    bool dragging,
    bool force_volume_selection
)
{
    const auto* tag = n.tag_of_type<SceneNodeTag>();
    if (tag == nullptr)
        return;

    Domain::ElementRef element = tag->is_wipe_tower() ?
        Domain::ElementRef{tag->wipe_tower_id} :
        Domain::ElementRef{tag->object_id, tag->instance_id, tag->volume_id};

    const Biz::Scene::ObjectSelection& object_selection = m_scene_interactor.object_selection();

    Biz::Scene::SelectionMode new_selection_mode{};
    if (element.is_wipe_tower()) {
        new_selection_mode = object_selection.mode;
    } else if (!force_volume_selection) {
        new_selection_mode = Biz::Scene::SelectionMode::Instance;
    } else if (replace || force_volume_selection) {
        new_selection_mode = Biz::Scene::SelectionMode::Volume;
    } else {
        new_selection_mode = object_selection.mode;
    }

    if (new_selection_mode == Biz::Scene::SelectionMode::Instance)
        element.volume_id = 0;

    Biz::Scene::ObjectSelection selection = replace
        ? Biz::Scene::ObjectSelection{new_selection_mode}
        : object_selection;
    if (!selection.remove(element)) {
        selection.elements.push_back(element);
    }
    m_scene_interactor.set_object_selection(selection);
}

void SelectionHandler::mark_unselected(Scene::Node& n, bool force_volume_mode)
{
    Biz::Scene::ObjectSelection selection = m_scene_interactor.object_selection();
    const auto* tag                       = n.tag_of_type<SceneNodeTag>();
    if (tag == nullptr)
        return;

    const Domain::ElementRef element = tag->wipe_tower_id != Domain::SlicingId{} ?
        Domain::ElementRef{tag->wipe_tower_id} :
        Domain::ElementRef{tag->object_id, tag->instance_id, tag->volume_id};

    if (force_volume_mode
        && selection.mode == Biz::Scene::SelectionMode::Instance
        && selection.elements.size() == 1)
    {
        Domain::ElementRefs new_elements = m_scene_interactor.selected_instance_all_volumes();
        ASSERT(!new_elements.empty());
        // We will remove volume from full selected instance
        // Normalize selection to volume mode first
        selection.mode = Biz::Scene::SelectionMode::Volume;
        selection.elements = new_elements;
    }

    selection.remove(element);

    m_scene_interactor.set_object_selection(selection);
}

void SelectionHandler::clear_selection()
{
    m_scene_interactor.clear_object_selection();
}

} // namespace Slic3r::App::Plater
