#include "Slic3r/Biz/ObjectList/ObjectObservableList.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"

#include "Slic3r/Domain/BedInstance.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/ModelInstance.hpp"
#include "Slic3r/Domain/ElementRef.hpp"

namespace Slic3r::Biz::ObjectList {

ObjectObservableList::ObjectObservableList(
    Biz::ProjectInteractor& project_interactor,
    size_t project_id,
    std::optional<size_t> config_container_id,
    std::optional<size_t> bed_id
) :
    m_scene_changed_listener_scope(project_interactor.scene_interactor(), *this),
    m_scene_selection_listener_scope(project_interactor.scene_interactor(), *this),
    m_project_interactor(project_interactor),
    m_project_id(project_id),
    m_config_container_id(config_container_id),
    m_bed_id(bed_id)
{
    m_is_out_of_bed_list = !m_config_container_id && !m_bed_id;
    update_data();
}

const Object& ObjectObservableList::at(size_t index) const
{
    return m_items.at(index);
}

size_t ObjectObservableList::size() const
{
    return m_items.size();
}

void ObjectObservableList::on_instance_transformed(
    Domain::SelectionId project_id,
    const Domain::ElementRefs& elements,
    Biz::Scene::TransformState /*state*/,
    const Biz::BedTrackingChanges& bed_tracking_changes
)
{
    update_from_changes(project_id, bed_tracking_changes);
}

void ObjectObservableList::
    on_instance_added(Domain::SelectionId project_id, const Domain::ElementRefs& /*instances*/)
{
    if (project_id == m_project_id)
        update_data();
}

void ObjectObservableList::on_instance_removed(
    Domain::SelectionId project_id,
    const Domain::ElementRefs& instances_to_remove
)
{
    if (project_id == m_project_id && !instances_to_remove.empty()) {
        std::unordered_map<size_t, std::vector<size_t>> instances_per_object;
        for (const Domain::ElementRef& inst : instances_to_remove) {
            instances_per_object[inst.object_id].push_back(inst.instance_id);
        }

        for (auto& [object_id, instances] : instances_per_object) {
            auto obj_it = std::find_if(
                m_items.begin(),
                m_items.end(),
                [object_id](const Object& o) { return o.id == object_id; }
            );
            if (obj_it == m_items.end()) {
                // This object list doesn't contain this object
                continue;
            }

            const IndexRange index_range(std::distance(m_items.begin(), obj_it));
            Domain::ModelObject* object =
                m_project_interactor.project(m_project_id).find_object_by_id(object_id);
            if (object) {
                // The object exists still in the model, just some instance(s) was removed
                auto object_instances = m_instances_lists.at(object_id);
                bool remove_at_least_one{false};
                bool leave_at_least_one{false};
                for (size_t i = 0;
                     i < object_instances->size() && (!remove_at_least_one || !leave_at_least_one);
                     i++)
                {
                    auto inst_it =
                        std::find(instances.begin(), instances.end(), object_instances->at(i).id);
                    if (inst_it != instances.end())
                        remove_at_least_one = true;
                    else
                        leave_at_least_one = true;
                }
                if (!remove_at_least_one) {
                    continue;
                }
                if (remove_at_least_one && leave_at_least_one) {
                    // There is at least one instance to remove
                    m_instances_lists.at(object_id)->update_data();
                    this->invoke_listeners<IListObserver<Object>>(
                        [index_range](auto* l) { l->on_updated(index_range); }
                    );
                    continue;
                }
            }

            // Object is completely removed from the model
            this->invoke_listeners<IListObserver<Object>>(
                [index_range](auto* l) { l->on_will_be_removed(index_range); }
            );

            m_instances_lists.erase(object_id);
            m_volumes_lists.erase(object_id);
            m_items.erase(obj_it);

            this->invoke_listeners<IListObserver<Object>>(
                [index_range](auto* l) { l->on_removed(index_range); }
            );
        }
    }
}

void ObjectObservableList::on_model_reloaded(Domain::SelectionId project_id)
{
    if (project_id == m_project_id)
        update_data();
}

void ObjectObservableList::on_volume_transformed(
    Domain::SelectionId project_id,
    const Domain::ElementRefs& elements,
    Biz::Scene::TransformState state,
    const Biz::BedTrackingChanges& bed_tracking_changes
)
{
    update_from_changes(project_id, bed_tracking_changes);
}

static bool is_selected(size_t object_id, const Biz::Scene::ObjectSelection& sel)
{
    return std::any_of(
        sel.elements.begin(),
        sel.elements.end(),
        [object_id](const Domain::ElementRef& el) { return el.object_id == object_id; }
    );
}

static bool is_connectors_selected(size_t object_id, const Biz::Scene::ObjectSelection& sel)
{
    return std::any_of(
        sel.elements.begin(),
        sel.elements.end(),
        [object_id](const Domain::ElementRef& el)
        { return el.object_id == object_id && el.has_volume(); }
    );
}

static bool
on_this_bed(const Domain::ModelObject* object, const Domain::ModelInstanceList* bed_model_instances)
{
    return std::any_of(
        bed_model_instances->begin(),
        bed_model_instances->end(),
        [object](const Domain::ModelInstance* instance) { return instance->get_object() == object; }
    );
}

void ObjectObservableList::
    on_element_renamed(Domain::SelectionId project_id, const Domain::ElementRef& element)
{
    if (project_id != m_project_id)
        return;

    size_t object_id = element.object_id;
    auto obj_it      = std::find_if(
        m_items.begin(),
        m_items.end(),
        [object_id](const Object& o) { return o.id == object_id; }
    );
    if (obj_it == m_items.end()) {
        // this list doesn't contain element
        return;
    }

    if (element.has_volume()) {
        m_volumes_lists.at(object_id)->refresh_name(element.volume_id);
    } else {
        Domain::ModelObject* object =
            m_project_interactor.project(m_project_id).find_object_by_id(element.object_id);
        obj_it->name = object->name;
    }

    const IndexRange index_range(std::distance(m_items.begin(), obj_it));
    this->invoke_listeners<IListObserver<Object>>(
        [index_range](auto* l) { l->on_updated(index_range); }
    );
}

void ObjectObservableList::
    on_volume_added(Domain::SelectionId project_id, const Domain::ElementRefs& volumes)
{
    if (!volumes.empty()) {
        update_volumes(project_id, volumes.front().object_id);
    }
}

void ObjectObservableList::
    on_volume_removed(Domain::SelectionId project_id, const Domain::ElementRefs& volumes)
{
    if (!volumes.empty()) {
        update_volumes(project_id, volumes.front().object_id);
    }
}

void ObjectObservableList::
    on_volume_type_changed(Domain::SelectionId project_id, const Domain::ElementRefs& volumes)
{
    if (!volumes.empty()) {
        update_volumes(project_id, volumes.front().object_id);
    }
}

void ObjectObservableList::
    on_volume_reordered(Domain::SelectionId project_id, const Domain::ElementRefs& volumes)
{
    if (!volumes.empty()) {
        update_volumes(project_id, volumes.front().object_id);
    }
}

void ObjectObservableList::on_scene_selection_changed(
    Domain::SelectionId project_id,
    const Scene::ObjectSelection& /*selection*/
)
{
    if (project_id != m_project_id || m_items.empty())
        return;

    const Domain::ModelInstanceList* bed_model_instances = get_bed_model_instances();
    Domain::ModelObjectPtrs& objects = m_project_interactor.project(m_project_id).model().objects;

    const Biz::Scene::ObjectSelection& sel =
        m_project_interactor.scene_interactor().object_selection(m_project_id);

    for (const Domain::ModelObject* object : objects) {
        if (on_this_bed(object, bed_model_instances)) {
            size_t object_id = object->id().id;
            auto obj_it      = std::find_if(
                m_items.begin(),
                m_items.end(),
                [obj_id = object->id().id](const Object& o) { return o.id == obj_id; }
            );
            ASSERT(obj_it != m_items.end());

            m_volumes_lists.at(object_id)->update_data();
            m_instances_lists.at(object_id)->update_data();

            obj_it->selected            = is_selected(object_id, sel);
            obj_it->connectors_selected = is_connectors_selected(object_id, sel);

            const IndexRange index_range(std::distance(m_items.begin(), obj_it));
            this->invoke_listeners<IListObserver<Object>>(
                [index_range](auto* l) { l->on_updated(index_range); }
            );
        }
    }
}

const Domain::ModelInstanceList* ObjectObservableList::get_bed_model_instances()
{
    // bed_id == 0 is the "Out of bed" sentinel:
    // match ObjectListOld::render_out_of_beds, which sources from unplaced instances instead of
    // a specific bed's model_instances.
    static const Domain::ModelInstanceList s_empty_instances;
    const Domain::ModelInstanceList* bed_model_instances = &s_empty_instances;
    if (m_is_out_of_bed_list) {
        bed_model_instances =
            &m_project_interactor.scene_interactor().unplaced_model_instances(m_project_id);
    } else if (const Domain::BedInstance* bed =
                   m_project_interactor.project(m_project_id).find_bed_instance_by_id(*m_bed_id))
    {
        bed_model_instances = &bed->model_instances;
    }
    return bed_model_instances;
}

void ObjectObservableList::update_data()
{
    const Domain::ModelInstanceList* bed_model_instances = get_bed_model_instances();
    Domain::ModelObjectPtrs& objects = m_project_interactor.project(m_project_id).model().objects;

    const Biz::Scene::ObjectSelection& sel =
        m_project_interactor.scene_interactor().object_selection(m_project_id);

    for (const Domain::ModelObject* object : objects) {
        auto obj_it = std::find_if(
            m_items.begin(),
            m_items.end(),
            [obj_id = object->id().id](const Object& o) { return o.id == obj_id; }
        );
        const IndexRange index_range(std::distance(m_items.begin(), obj_it));

        if (!on_this_bed(object, bed_model_instances)) {
            if (obj_it != m_items.end()) {
                this->invoke_listeners<IListObserver<Object>>(
                    [index_range](auto* l) { l->on_will_be_removed(index_range); }
                );

                m_instances_lists.erase(obj_it->id);
                m_volumes_lists.erase(obj_it->id);
                m_items.erase(obj_it);

                this->invoke_listeners<IListObserver<Object>>(
                    [index_range](auto* l) { l->on_removed(index_range); }
                );
            }
        } else {
            const bool is_printable = std::any_of(
                object->instances.begin(),
                object->instances.end(),
                [](const Domain::ModelInstance* instance) { return instance->printable; }
            );
            const bool has_connectors = object->is_cut() && object->volumes.size() > 1;
            const size_t object_id    = object->id().id;

            auto vit = m_volumes_lists.find(object_id);
            if (vit == m_volumes_lists.end()) {
                vit =
                    m_volumes_lists
                        .emplace(
                            object_id,
                            std::make_shared<VolumeObservableList>(
                                m_project_interactor,
                                m_project_id,
                                object_id,
                                m_config_container_id
                            )
                        )
                        .first;
            } else {
                vit->second->update_data();
            }

            auto iit = m_instances_lists.find(object_id);
            if (iit == m_instances_lists.end()) {
                iit =
                    m_instances_lists
                        .emplace(
                            object_id,
                            std::make_shared<InstanceObservableList>(
                                m_project_interactor,
                                m_project_id,
                                m_bed_id,
                                object_id
                            )
                        )
                        .first;
            } else {
                iit->second->update_data();
            }

            const bool selected            = is_selected(object_id, sel);
            const bool connectors_selected = is_connectors_selected(object_id, sel);

            if (obj_it == m_items.end()) {
                auto new_obj = m_items.emplace_back(
                    object->name,
                    object_id,
                    is_printable,
                    has_connectors,
                    vit->second,
                    iit->second,
                    selected,
                    connectors_selected,
                    m_config_container_id,
                    m_bed_id
                );
                this->invoke_listeners<IListObserver<Object>>(
                    [this, new_obj](auto* l) { l->on_inserted(new_obj, m_items.size() - 1); }
                );
            } else {
                obj_it->name                = object->name;
                obj_it->printable           = is_printable;
                obj_it->connectors_selected = connectors_selected;
                obj_it->has_connectors      = has_connectors;
                obj_it->selected            = selected;

                this->invoke_listeners<IListObserver<Object>>(
                    [index_range](auto* l) { l->on_updated(index_range); }
                );
            }
        }
    }
}

void ObjectObservableList::update_from_changes(
    Domain::SelectionId project_id,
    const Biz::BedTrackingChanges& bed_tracking_changes
)
{
    if (m_project_id == project_id
        && (m_is_out_of_bed_list
            || bed_tracking_changes.updated_beds.contains({*m_config_container_id, *m_bed_id})))
    {
        update_data();
    }
}

void ObjectObservableList::update_volumes(Domain::SelectionId project_id, size_t object_id)
{
    if (project_id != m_project_id)
        return;

    auto obj_it = std::find_if(
        m_items.begin(),
        m_items.end(),
        [object_id](const Object& o) { return o.id == object_id; }
    );
    if (obj_it == m_items.end()) {
        // this list doesn't contain object with volumes
        return;
    }

    m_volumes_lists.at(object_id)->update_data();

    const IndexRange index_range(std::distance(m_items.begin(), obj_it));
    this->invoke_listeners<IListObserver<Object>>(
        [index_range](auto* l) { l->on_updated(index_range); }
    );
}

} // namespace Slic3r::Biz::ObjectList
