#include "Slic3r/Biz/ObjectList/VolumeObservableList.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"

#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/ModelVolume.hpp"
#include "Slic3r/Domain/ElementRef.hpp"

namespace Slic3r::Biz::ObjectList {

VolumeObservableList::VolumeObservableList(
    Biz::ProjectInteractor& project_interactor,
    size_t project_id,
    size_t object_id,
    std::optional<size_t> config_container_id
) :
    m_project_interactor(project_interactor),
    m_project_id(project_id),
    m_object_id(object_id),
    m_config_container_id(config_container_id)
{
    update_data();
}

const Volume& VolumeObservableList::at(size_t index) const
{
    return m_items.at(index);
}

size_t VolumeObservableList::size() const
{
    return m_items.size();
}

void VolumeObservableList::update_data()
{
    this->invoke_listeners<IListObserver<Volume>>([](auto* l) { l->on_will_be_reset(); });

    m_items.clear();

    const Domain::ModelObject* object =
        m_project_interactor.project(m_project_id).find_object_by_id(m_object_id);
    if (object) {
        const Biz::Scene::ObjectSelection& sel =
            m_project_interactor.scene_interactor().object_selection(m_project_id);
        for (const Domain::ModelVolume* volume : object->volumes) {
            if (volume->is_cut_connector()) {
                continue;
            }
            const size_t volume_id = volume->id().id;
            const bool selected    = std::any_of(
                sel.elements.begin(),
                sel.elements.end(),
                [object_id = m_object_id, volume_id](const Domain::ElementRef& el)
                { return el.object_id == object_id && el.volume_id == volume_id; }
            );
            m_items.emplace_back(
                object->id().id,
                volume_id,
                volume->type(),
                volume->name,
                volume->is_text(),
                volume->is_svg(),
                selected,
                m_config_container_id
            );
        }
    }

    this->invoke_listeners<IListObserver<Volume>>([](auto* l) { l->on_reset(); });
}

void VolumeObservableList::refresh_name(size_t volume_id)
{
    const Domain::ModelVolume* volume =
        m_project_interactor.project(m_project_id).find_volume_by_id(m_object_id, volume_id);
    ASSERT(volume);

    auto it = std::find_if(
        m_items.begin(),
        m_items.end(),
        [volume_id](const Volume& volume) { return volume.vol_id == volume_id; }
    );
    ASSERT(it != m_items.end());
    it->name = volume->name;

    const IndexRange index_range(std::distance(m_items.begin(), it));
    this->invoke_listeners<IListObserver<Volume>>(
        [index_range](auto* l) { l->on_updated(index_range); }
    );
}

} // namespace Slic3r::Biz::ObjectList
