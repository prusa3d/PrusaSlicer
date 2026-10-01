#pragma once

#include "Slic3r/Biz/ObjectList/VolumeObservableList.hpp"
#include "Slic3r/Biz/ObjectList/InstanceObservableList.hpp"
#include "Slic3r/Domain/SelectionId.hpp"

#include "Slic3r/Biz/Platform/ListenerScope.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"

#include <unordered_map>

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::Biz::ObjectList {

struct Object
{
    std::string name;
    size_t id;
    bool printable;
    bool has_connectors;
    Biz::UnsharedPointer<VolumeObservableList> volumes_list;
    Biz::UnsharedPointer<InstanceObservableList> instances_list;

    bool selected{false};
    bool connectors_selected{false};
    std::optional<size_t> config_container_id;
    std::optional<size_t> bed_id;
};

/**
 * @brief Owns, per Object row, its VolumeObservableList and InstanceObservableList, mirroring
 * how ConfigContainerBedObservableList owns per-row object lists. Child lists are kept alive
 * and refreshed in place (keyed by object id) across an object's lifetime.
 */
class ObjectObservableList :
    public Biz::IObservableList<Object>,
    public Biz::Scene::ISceneChangedListener,
    public Scene::ISceneSelectionChangedListener
{
public:
    ObjectObservableList(
        Biz::ProjectInteractor& project_interactor,
        size_t project_id,
        std::optional<size_t> config_container_id = std::nullopt,
        std::optional<size_t> bed_id              = std::nullopt
    );

    const Object& at(size_t index) const override;

    size_t size() const override;

    void on_instance_transformed(
        Domain::SelectionId project_id,
        const Domain::ElementRefs& elements,
        Biz::Scene::TransformState state,
        const Biz::BedTrackingChanges& bed_tracking_changes
    ) override;
    void on_instance_added(
        Domain::SelectionId project_id,
        const Domain::ElementRefs& instances
    ) override;
    void on_instance_removed(
        Domain::SelectionId project_id,
        const Domain::ElementRefs& instances
    ) override;
    void on_model_reloaded(Domain::SelectionId project_id) override;
    void on_element_renamed(Domain::SelectionId project_id, const Domain::ElementRef& id) override;

    void
    on_volume_added(Domain::SelectionId project_id, const Domain::ElementRefs& volumes) override;
    void
    on_volume_removed(Domain::SelectionId project_id, const Domain::ElementRefs& volumes) override;
    void on_volume_transformed(
        Domain::SelectionId project_id,
        const Domain::ElementRefs& elements,
        Biz::Scene::TransformState state,
        const Biz::BedTrackingChanges& bed_tracking_changes
    ) override;
    void on_volume_type_changed(
        Domain::SelectionId project_id,
        const Domain::ElementRefs& volumes
    ) override;

    // Scene::ISceneSelectionChangedListener
    void on_scene_selection_changed(
        Domain::SelectionId project_id,
        const Scene::ObjectSelection& selection
    ) override;

    void update_data();

private:
    const Domain::ModelInstanceList* get_bed_model_instances();

    void update_from_changes(
        Domain::SelectionId project_id,
        const Biz::BedTrackingChanges& bed_tracking_changes
    );
    void update_volumes(Domain::SelectionId project_id, size_t object_id);

    std::vector<Object> m_items;
    std::unordered_map<size_t, Biz::UnsharedPointer<VolumeObservableList>> m_volumes_lists;
    std::unordered_map<size_t, Biz::UnsharedPointer<InstanceObservableList>> m_instances_lists;

    ProjectInteractor& m_project_interactor;
    ListenerScope<Scene::ISceneChangedListener, Biz::Scene::SceneInteractor, ObjectObservableList>
        m_scene_changed_listener_scope;
    ListenerScope<
        Scene::ISceneSelectionChangedListener,
        Scene::SceneInteractor,
        ObjectObservableList>
        m_scene_selection_listener_scope;

    size_t m_project_id;
    std::optional<size_t> m_config_container_id;
    std::optional<size_t> m_bed_id;

    bool m_is_out_of_bed_list{false};
};

} // namespace Slic3r::Biz::ObjectList
