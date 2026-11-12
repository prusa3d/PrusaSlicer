#pragma once

#include "Slic3r/Biz/ObservableList.hpp"
#include "Slic3r/Biz/ObjectList/ConfigContainerBedObservableList.hpp"

#include <unordered_map>

namespace Slic3r::Domain {
class ConfigContainer;
} // namespace Slic3r::Domain

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::Biz::Preset {
class PresetInteractor;
} // namespace Slic3r::Biz::Preset

namespace Slic3r::Biz::ObjectList {

struct PrinterGroup
{
    std::string name;
    size_t config_container_id;
    size_t project_id;
    Biz::UnsharedPointer<ConfigContainerBedObservableList> beds_list;
};

/**
 * @brief Owns, per PrinterGroup row, its ConfigContainerBedObservableList so the App layer no
 * longer needs to construct/wire per-row child lists itself - it just observes state()->beds_list.
 * Child bed lists are kept alive (and refreshed in place via update_data) across a config
 * container's lifetime, keyed by config_container_id.
 */
class ConfigContainerObservableList :
    public Biz::IObservableList<PrinterGroup>,
    public Preset::IPresetChangedListener
{
    using ConfigContainerList = std::vector<std::unique_ptr<Domain::ConfigContainer>>;

public:
    ConfigContainerObservableList(
        Biz::ProjectInteractor& project_interactor,
        const ConfigContainerList& config_containers,
        size_t project_id
    );

    const PrinterGroup& at(size_t index) const override;

    size_t size() const override;

    // Preset::IPresetChangedListener
    void on_preset_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        Preset::PresetItemType type
    ) override;

    void update_data();
    void remove_from_listeners();

private:
    void remove_beds_from_listeners();

    std::vector<PrinterGroup> m_items;
    std::unordered_map<size_t, Biz::UnsharedPointer<ConfigContainerBedObservableList>> m_beds_lists;

    Biz::ProjectInteractor& m_project_interactor;
    const ConfigContainerList& m_config_containers;
    size_t m_project_id;
};

} // namespace Slic3r::Biz::ObjectList
