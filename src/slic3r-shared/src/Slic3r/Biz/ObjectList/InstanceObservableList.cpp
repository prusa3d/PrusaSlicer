#include "Slic3r/Biz/ObjectList/InstanceObservableList.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"

#include "Slic3r/Domain/BedInstance.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/ModelInstance.hpp"
#include "Slic3r/Domain/ElementRef.hpp"

namespace Slic3r::Biz::ObjectList {

InstanceObservableList::InstanceObservableList(
    Biz::ProjectInteractor& project_interactor,
    size_t project_id,
    std::optional<size_t> bed_id,
    size_t object_id
) :
    m_project_interactor(project_interactor),
    m_project_id(project_id),
    m_bed_id(bed_id),
    m_object_id(object_id)
{
    update_data();
}

const Instance& InstanceObservableList::at(size_t index) const
{
    return m_items.at(index);
}

size_t InstanceObservableList::size() const
{
    return m_items.size();
}

void InstanceObservableList::update_data()
{
    this->template invoke_listeners<IListObserver<Instance>>(
        [](auto* l) { l->on_will_be_reset(); }
    );

    m_items.clear();

    const Domain::ModelObject* object =
        m_project_interactor.project(m_project_id).find_object_by_id(m_object_id);
    if (object) {
        // bed_id == std::nullopt is the "Out of bed" sentinel, see ObjectObservableList::update_data.
        static const Domain::ModelInstanceList s_empty_instances;
        const Domain::ModelInstanceList* bed_model_instances = &s_empty_instances;
        if (!m_bed_id) {
            bed_model_instances =
                &m_project_interactor.scene_interactor().unplaced_model_instances(m_project_id);
        } else if (const Domain::BedInstance* bed =
                       m_project_interactor.project(m_project_id)
                           .find_bed_instance_by_id(*m_bed_id))
        {
            bed_model_instances = &bed->model_instances;
        }

        const Biz::Scene::ObjectSelection& sel =
            m_project_interactor.scene_interactor().object_selection(m_project_id);
        size_t instance_ordinal_id{1};
        for (const Domain::ModelInstance* instance : object->instances) {
            const bool on_this_bed = std::any_of(
                bed_model_instances->begin(),
                bed_model_instances->end(),
                [instance](const Domain::ModelInstance* bed_instance)
                { return bed_instance == instance; }
            );
            if (on_this_bed) {
                const size_t instance_id = instance->id().id;
                const bool selected      = std::any_of(
                    sel.elements.begin(),
                    sel.elements.end(),
                    [object_id = m_object_id, instance_id](const Domain::ElementRef& el)
                    { return el.object_id == object_id && el.instance_id == instance_id; }
                );
                m_items.emplace_back(
                    instance_id,
                    instance_ordinal_id++,
                    m_object_id,
                    instance->printable,
                    selected
                );
            }
        }
    }

    this->template invoke_listeners<IListObserver<Instance>>([](auto* l) { l->on_reset(); });
}

} // namespace Slic3r::Biz::ObjectList
