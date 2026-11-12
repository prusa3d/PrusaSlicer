#include "Slic3r/Biz/ObjectList/ConfigContainerBedObservableList.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"

#include "Slic3r/Domain/ConfigContainer.hpp"
#include "Slic3r/Domain/BedInstance.hpp"
#include "Slic3r/Domain/ElementRef.hpp"

namespace Slic3r::Biz::ObjectList {

ConfigContainerBedObservableList::ConfigContainerBedObservableList(
    Biz::ProjectInteractor& project_interactor,
    size_t project_id,
    size_t config_container_id
) :
    m_scene_selection_listener_scope(project_interactor.scene_interactor(), *this),
    m_scene_changed_listener_scope(project_interactor.scene_interactor(), *this),
    m_bed_selection_listener_scope(project_interactor.scene_interactor(), *this),
    m_project_interactor(project_interactor),
    m_project_id(project_id),
    m_config_container_id(config_container_id)
{
    m_project_interactor.status_cache().add_listener<IStatusCacheChangedListener>(this);
    update_data();
}

const Bed& ConfigContainerBedObservableList::at(size_t index) const
{
    return m_items.at(index);
}

size_t ConfigContainerBedObservableList::size() const
{
    return m_items.size();
}

void ConfigContainerBedObservableList::update_data()
{
    this->invoke_listeners<IListObserver<Bed>>([](auto* l) { l->on_will_be_reset(); });

    m_items.clear();
    m_objects_lists.clear();

    const Domain::ConfigContainer* config_container =
        m_project_interactor.project(m_project_id).find_config_container(m_config_container_id);
    ASSERT(config_container);

    const bool can_be_deleted = config_container->bed_instances().size() > 1
        || m_project_interactor.project(m_project_id).config_containers().size() > 1;
    for (const std::unique_ptr<Domain::BedInstance>& bed : config_container->bed_instances()) {
        const size_t bed_id = bed->id().id;

        auto it = m_objects_lists.find(bed_id);
        if (it == m_objects_lists.end()) {
            it =
                m_objects_lists
                    .emplace(
                        bed_id,
                        std::make_shared<ObjectObservableList>(
                            m_project_interactor,
                            m_project_id,
                            m_config_container_id,
                            bed_id
                        )
                    )
                    .first;
        } else {
            it->second->update_data();
        }

        const Biz::Scene::BedSelection* bed_selection =
            m_project_interactor.scene_interactor().bed_selection(m_project_id);
        const bool selected = bed_selection
            && !bed_selection->empty()
            && bed_selection->is_selected({m_config_container_id, bed_id});
        const bool has_wipe_tower =
            m_project_interactor.scene_interactor().wipe_tower_geometry(bed_id) != nullptr;
        const bool wipe_tower_selected =
            m_project_interactor.scene_interactor()
                .object_selection(m_project_id)
                .is_selected(Domain::ElementRef(Domain::SlicingId{m_project_id, bed_id}));
        const std::optional<Biz::Slicing::Status> status =
            m_project_interactor.status_cache().get_status({m_project_id, bed_id});
        const std::optional<Biz::Slicing::StatusCode> status_code =
            status ? std::optional{status->code} : std::nullopt;

        m_items.emplace_back(
            bed->name(),
            bed_id,
            m_config_container_id,
            can_be_deleted,
            it->second,
            selected,
            has_wipe_tower,
            wipe_tower_selected,
            status_code
        );
    }

    this->invoke_listeners<IListObserver<Bed>>([](auto* l) { l->on_reset(); });
}

void ConfigContainerBedObservableList::remove_from_listeners()
{
    m_project_interactor.status_cache().remove_listener<IStatusCacheChangedListener>(this);
}

void ConfigContainerBedObservableList::on_scene_selection_changed(
    Domain::SelectionId project_id,
    const Scene::ObjectSelection& /*selection*/
)
{
    refresh_selection(project_id, SelectionType::WipeTower);
}

void ConfigContainerBedObservableList::on_wipe_tower_changed(
    Domain::SlicingId slicing_id,
    const Slicing::WipeTowerGeometry& /*wipe_tower*/
)
{
    refresh_selection(slicing_id.project_id, SelectionType::WipeTower);
}

void ConfigContainerBedObservableList::on_wipe_tower_moved(Domain::SlicingId slicing_id)
{
    refresh_selection(slicing_id.project_id, SelectionType::WipeTower);
}

void ConfigContainerBedObservableList::on_wipe_tower_removed(Domain::SlicingId slicing_id)
{
    refresh_selection(slicing_id.project_id, SelectionType::WipeTower);
}

void ConfigContainerBedObservableList::on_selected_bed_instances_changed(
    Domain::SelectionId project_id,
    const Scene::BedSelection& bed_selection
)
{
    refresh_selection(project_id, SelectionType::Bed);
}

void
ConfigContainerBedObservableList::on_status_cache_status_code_changed(const Domain::SlicingId id)
{
    on_status_cache_changed(id);
}

void ConfigContainerBedObservableList::on_status_cache_progress_changed(const Domain::SlicingId id)
{
    on_status_cache_changed(id);
}

void ConfigContainerBedObservableList::
    refresh_selection(Domain::SelectionId project_id, SelectionType type)
{
    if (m_project_id == project_id) {
        const Domain::ConfigContainer* config_container =
            m_project_interactor.project(m_project_id).find_config_container(m_config_container_id);
        if (!config_container) {
            // There is a case(ConfigContainer/Bed/WipeTower removing),
            // when refresh is called before update_data()
            return;
        }

        for (const std::unique_ptr<Domain::BedInstance>& bed : config_container->bed_instances()) {
            const size_t bed_id = bed->id().id;
            auto it             = std::find_if(
                m_items.begin(),
                m_items.end(),
                [bed_id](const Bed& bed) { return bed.id == bed_id; }
            );
            if (it == m_items.end()) {
                // There is the case when new bed is already added,
                // but on_config_container_bed_list_changed() is not processed jet
                continue;
            }

            if (type == SelectionType::WipeTower) {
                it->has_wipe_tower =
                    m_project_interactor.scene_interactor().wipe_tower_geometry(bed_id) != nullptr;
                it->wipe_tower_selected =
                    m_project_interactor.scene_interactor()
                        .object_selection(m_project_id)
                        .is_selected(Domain::ElementRef(Domain::SlicingId{m_project_id, bed_id}));
            } else if (type == SelectionType::Bed) {
                const Biz::Scene::BedSelection* bed_selection =
                    m_project_interactor.scene_interactor().bed_selection(m_project_id);
                it->selected = bed_selection
                    && !bed_selection->empty()
                    && bed_selection->is_selected({m_config_container_id, bed_id});
            }

            const IndexRange index_range(std::distance(m_items.begin(), it));
            this->invoke_listeners<IListObserver<Bed>>(
                [index_range](auto* l) { l->on_updated(index_range); }
            );
        }
    }
}

void ConfigContainerBedObservableList::on_status_cache_changed(const Domain::SlicingId id)
{
    if (m_project_id == id.project_id) {
        size_t bed_id = id.bed_instance_id;
        auto it       = std::find_if(
            m_items.begin(),
            m_items.end(),
            [bed_id](const Bed& el) { return el.id == bed_id; }
        );
        if (it != m_items.end()) {
            const std::optional<Biz::Slicing::Status> status =
                m_project_interactor.status_cache().get_status({m_project_id, bed_id});
            it->status_code = status ? std::optional{status->code} : std::nullopt;
            this->invoke_listeners<IListObserver<Bed>>(
                [this, it](auto* l) { l->on_updated(std::distance(m_items.begin(), it)); }
            );
        }
    }
}

} // namespace Slic3r::Biz::ObjectList
