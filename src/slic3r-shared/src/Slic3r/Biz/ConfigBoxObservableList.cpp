#include "Slic3r/Biz/ConfigBoxObservableList.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"

namespace Slic3r::Biz {

ConfigBoxObservableList::ConfigBoxObservableList(
    Scene::SceneInteractor* scene_interctor,
    Slicing::SlicingInteractor* slicing_interactor) :
    m_scene_interactor{scene_interctor},
    m_slicing_interactor{slicing_interactor}
{
    if (m_scene_interactor) {
        ASSERT(m_slicing_interactor);
        m_scene_interactor->add_listener<ISelectedBedInstancesChangedListener>(this);
        m_slicing_interactor->add_listener<Slicing::IBackgroundProcessStateListener>(this);
    }
}

ConfigBoxObservableList::~ConfigBoxObservableList()
{
    if (m_scene_interactor) {
        ASSERT(m_slicing_interactor);
        m_scene_interactor->remove_listener<ISelectedBedInstancesChangedListener>(this);
        m_slicing_interactor->remove_listener<Slicing::IBackgroundProcessStateListener>(this);
    }
}

void ConfigBoxObservableList::set_config_box(
    Domain::ConfigBox* observable_config_box,
    const Domain::ConfigBox* original_config_box
)
{
    // if (m_config_box != config_box) {
    invoke_listeners<IListObserver<ConfigItemContext>>([&](IListObserver<ConfigItemContext>* l)
                                                        { l->on_will_be_reset(); });
    m_config_box = observable_config_box;

    const std::vector<Domain::ConfigItem>& all_items = m_config_box->items.all_items();
    m_items.clear();
    m_items.reserve(all_items.size());
    for (const Domain::ConfigItem& config_item : all_items) {
        const Domain::ConfigItem* original_config_item =
            original_config_box ? original_config_box->find(config_item.name()).item : nullptr;

        m_items.emplace_back(config_item.name(), &config_item, original_config_item);
    }

    invoke_listeners<IListObserver<ConfigItemContext>>([&](IListObserver<ConfigItemContext>* l)
                                                        { l->on_reset(); });
    // }
}

const ConfigItemContext& ConfigBoxObservableList::at(size_t index) const
{
    return m_items.at(index);
}

size_t ConfigBoxObservableList::size() const
{
    return m_items.size();
}

void
ConfigBoxObservableList::set_value(const std::string_view key, const Domain::ConfigValue& value)
{
    const std::vector<Domain::ConfigItem>& all_items = m_config_box->items.all_items();

    const std::vector<Domain::ConfigItem>::const_iterator index_it = std::find_if(
        all_items.cbegin(),
        all_items.cend(),
        [key](const Domain::ConfigItem& item) { return item.def().name == key; }
    );

    if (index_it != all_items.cend() && index_it->value() != value) {
        const size_t index = std::distance(all_items.cbegin(), index_it);

        m_config_box->items.opt(key).set(value);

        invoke_listeners<IListObserver<ConfigItemContext>>(
            [index](IListObserver<ConfigItemContext>* l) { l->on_updated(index); }
        );
    }
}

static const ConfigItemContext&
find_item_context(const std::vector<ConfigItemContext>& items, const std::string& key)
{
    auto it = std::find_if(
        items.cbegin(),
        items.cend(),
        [&](const ConfigItemContext& item) { return item.name == key; }
    );
    ASSERT(it != items.cend());

    return *it;
}

bool ConfigBoxObservableList::is_dirty(const std::string& key) const
{
    return find_item_context(m_items, key).is_dirty();
}

bool ConfigBoxObservableList::is_dirty() const
{
    for (const auto& item : m_items) {
        if (item.is_dirty())
            return true;
    }
    return false;
}

void ConfigBoxObservableList::set_from_original_value(const std::string& key)
{
    const ConfigItemContext& item_context = find_item_context(m_items, key);
    if (item_context.original_config_item) {
        set_value(key, item_context.original_config_item->value());
    }
}

void ConfigBoxObservableList::on_selected_bed_instances_changed(
    Domain::SelectionId project_id,
    const Scene::BedSelection& bed_selection)
{
    update_effective_value_statuses({project_id, bed_selection.last_selected_bed().instance_id});
}

void
ConfigBoxObservableList::on_background_process_state_changed(const Domain::SlicingId slicing_id)
{
    update_effective_value_statuses(slicing_id);
}

void ConfigBoxObservableList::update_effective_value_statuses(const Domain::SlicingId slicing_id)
{
    ASSERT(m_slicing_interactor);

    const Slicing::BackgroundProcessState* state{
        m_slicing_interactor->find_process_state(slicing_id)};

    if (!state) {
        return;
    }

    for (size_t index{}; index < m_items.size(); ++index) {
        const EffectiveValueStatus status{
            get_effective_value_status(m_items[index].name, m_config_box->location, state->effective_values)};
        if (m_items[index].effective_value_status != status) {
            m_items[index].effective_value_status = status;
            invoke_listeners<IListObserver<ConfigItemContext>>(
                [index](IListObserver<ConfigItemContext>* l) { l->on_updated(index); });
        }
    }
}

const Domain::ConfigValue* ConfigBoxObservableList::find(const std::string& name) const
{
    Domain::ConfigItem* found_item = m_config_box->items.find(name);
    return found_item ? &found_item->value() : nullptr;
}

} // namespace Slic3r::Biz
