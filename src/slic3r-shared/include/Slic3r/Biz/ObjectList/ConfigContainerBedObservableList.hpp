#pragma once

#include "Slic3r/Biz/ObservableList.hpp"
#include "Slic3r/Biz/ObjectList/ObjectObservableList.hpp"
#include "Slic3r/Biz/StatusCache.hpp"
#include "Slic3r/Biz/ISelectedBedInstanceChangedListener.hpp"

#include <unordered_map>

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::Biz::ObjectList {

struct Bed
{
    std::string name;
    size_t id;
    size_t config_container_id;
    bool can_be_deleted;
    Biz::UnsharedPointer<ObjectObservableList> objects_list;

    bool selected{false};
    bool has_wipe_tower{false};
    bool wipe_tower_selected{false};
    std::optional<Biz::Slicing::StatusCode> status_code;
};

/**
 * @brief Owns, per Bed row, its ObjectObservableList, mirroring how
 * ConfigContainerObservableList owns per-row bed lists. Child object lists are kept alive and
 * refreshed in place (keyed by bed id) across a bed's lifetime.
 */
class ConfigContainerBedObservableList :
    public Biz::IObservableList<Bed>,
    public Scene::ISceneSelectionChangedListener,
    public Scene::ISceneChangedListener,
    public ISelectedBedInstancesChangedListener,
    public IStatusCacheChangedListener
{
public:
    ConfigContainerBedObservableList(
        Biz::ProjectInteractor& project_interactor,
        size_t project_id,
        size_t config_container_id
    );

    const Bed& at(size_t index) const override;

    size_t size() const override;

    void update_data();
    void remove_from_listeners();

    // Scene::ISceneSelectionChangedListener
    void on_scene_selection_changed(
        Domain::SelectionId project_id,
        const Scene::ObjectSelection& selection
    ) override;

    // Scene::ISceneChangedListener
    void on_wipe_tower_changed(
        Domain::SlicingId slicing_id,
        const Slicing::WipeTowerGeometry& wipe_tower
    ) override;
    void on_wipe_tower_moved(Domain::SlicingId slicing_id) override;
    void on_wipe_tower_removed(Domain::SlicingId slicing_id) override;

    // ISelectedBedInstancesChangedListener
    void on_selected_bed_instances_changed(
        Domain::SelectionId project_id,
        const Scene::BedSelection& bed_selection
    ) override;

    // IStatusCacheChangedListener
    void on_status_cache_status_code_changed(const Domain::SlicingId id) override;
    void on_status_cache_progress_changed(const Domain::SlicingId id) override;

private:
    enum class SelectionType
    {
        Bed,
        WipeTower
    };
    void refresh_selection(Domain::SelectionId project_id, SelectionType type);

    void on_status_cache_changed(const Domain::SlicingId id);

    std::vector<Bed> m_items;
    std::unordered_map<size_t, Biz::UnsharedPointer<ObjectObservableList>> m_objects_lists;

    ListenerScope<
        Scene::ISceneSelectionChangedListener,
        Scene::SceneInteractor,
        ConfigContainerBedObservableList>
        m_scene_selection_listener_scope;
    ListenerScope<
        Scene::ISceneChangedListener,
        Scene::SceneInteractor,
        ConfigContainerBedObservableList>
        m_scene_changed_listener_scope;
    ListenerScope<
        ISelectedBedInstancesChangedListener,
        Scene::SceneInteractor,
        ConfigContainerBedObservableList>
        m_bed_selection_listener_scope;

    Biz::ProjectInteractor& m_project_interactor;
    size_t m_project_id;
    size_t m_config_container_id;
};

} // namespace Slic3r::Biz::ObjectList
