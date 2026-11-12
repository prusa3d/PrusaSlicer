#include "Slic3r/Biz/ObjectList/ConfigContainerObservableList.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"

#include "Slic3r/Domain/ConfigContainer.hpp"

namespace Slic3r::Biz::ObjectList {

ConfigContainerObservableList::ConfigContainerObservableList(
    Biz::ProjectInteractor& project_interactor,
    const ConfigContainerList& config_containers,
    size_t project_id
) :
    m_project_interactor(project_interactor),
    m_config_containers(config_containers),
    m_project_id(project_id)
{
    project_interactor.preset_interactor().add_listener<Preset::IPresetChangedListener>(this);
    update_data();
}

const PrinterGroup& ConfigContainerObservableList::at(size_t index) const
{
    return m_items.at(index);
}

size_t ConfigContainerObservableList::size() const
{
    return m_items.size();
}

void ConfigContainerObservableList::on_preset_selection_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    Preset::PresetItemType type
)
{
    if (m_project_id != project_id || type != Preset::PresetItemType::PrinterPreset)
        return;
    auto it = std::find_if(
        m_items.begin(),
        m_items.end(),
        [config_container_id](const PrinterGroup& printer_group)
        { return printer_group.config_container_id == config_container_id; }
    );
    if (it != m_items.end()) {
        it->name =
            m_project_interactor.project(m_project_id)
                .find_config_container(config_container_id)
                ->selected_preset()
                .hw_config.name;
        m_beds_lists.at(config_container_id)->update_data();

        this->invoke_listeners<IListObserver<PrinterGroup>>(
            [this, it](auto* l) { l->on_updated(std::distance(m_items.begin(), it)); }
        );
    }
}

void ConfigContainerObservableList::update_data()
{
    this->template invoke_listeners<IListObserver<PrinterGroup>>(
        [](auto* l) { l->on_will_be_reset(); }
    );

    m_items.clear();

    remove_beds_from_listeners();
    m_beds_lists.clear();

    for (auto& cc : m_config_containers) {
        const size_t cc_id = cc->id().id;
        auto it =
            m_beds_lists
                .emplace(
                    cc_id,
                    std::make_shared<
                        ConfigContainerBedObservableList>(m_project_interactor, m_project_id, cc_id)
                )
                .first;

        m_items.emplace_back(cc->selected_preset().hw_config.name, cc_id, m_project_id, it->second);
    }

    this->template invoke_listeners<IListObserver<PrinterGroup>>([](auto* l) { l->on_reset(); });
}

void ConfigContainerObservableList::remove_from_listeners()
{
    m_project_interactor.preset_interactor().remove_listener<Preset::IPresetChangedListener>(this);
    remove_beds_from_listeners();
}

void ConfigContainerObservableList::remove_beds_from_listeners()
{
    for (auto [cc_id, beds_list] : m_beds_lists) {
        beds_list->remove_from_listeners();
    }
}

} // namespace Slic3r::Biz::ObjectList
