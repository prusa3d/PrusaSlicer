#include "Slic3r/Biz/ObjectSettingsObservableList.hpp"

namespace Slic3r::Biz {

ObjectSettingsObservableList::ObjectSettingsObservableList(
    Scene::SceneInteractor& scene_interactor
) :
    m_scene_interactor{scene_interactor}
{}

const OverrideItem& ObjectSettingsObservableList::at(size_t index) const
{
    ASSERT(index < m_items.size());

    return *m_items.at(index).get();
}

size_t ObjectSettingsObservableList::size() const
{
    return m_items.size();
}

void ObjectSettingsObservableList::set_object_settings_source(
    const Domain::ConfigBox* object_settings_source
)
{
    m_object_settings_source = object_settings_source;
}

void ObjectSettingsObservableList::set_sources(const std::vector<Domain::ConfigBox*>& sources)
{
    std::vector<OverrideItemPtr> new_items;
    std::unordered_map<OverrideItem*, std::set<Domain::ConfigBox*>> new_item_sources;
    ItemMap new_item_index;

    auto find_item_new = [&](const std::string& name) -> OverrideItem*
    {
        ItemMap::const_iterator it = new_item_index.find(name);

        return it == new_item_index.end() ? nullptr : new_items.at(it->second).get();
    };

    // 1. Go through all Config Boxes and populate m_item_sources, m_items and m_item_index
    for (Domain::ConfigBox* box : sources) {
        for (const Domain::ConfigItem& config_item : box->items.all_items()) {
            OverrideItem* item = find_item_new(config_item.name());
            if (!item) {
                OverrideItemPtr& item_ptr = new_items.emplace_back(
                    std::make_unique<OverrideItem>(
                        config_item.name(),
                        false,
                        std::optional<bool>(),
                        &config_item
                    )
                );
                new_item_sources[item_ptr.get()].insert(box);
                new_item_index.insert({config_item.name(), new_items.size() - 1});
            } else {
                new_item_sources[item].insert(box);
            }
        }

        for (const Domain::ConfigItem& config_item : box->overrides.all_items()) {
            OverrideItem* item = find_item_new(config_item.name());
            if (!item) {
                OverrideItemPtr& item_ptr = new_items.emplace_back(
                    std::make_unique<OverrideItem>(
                        config_item.name(),
                        false,
                        box->overrides.get(config_item.name()).has_value(),
                        &config_item
                    )
                );
                new_item_sources[item_ptr.get()].insert(box);
                new_item_index.insert({config_item.name(), new_items.size() - 1});
            } else {
                new_item_sources[item].insert(box);
                item->overriden =
                    item->overriden.value() || box->overrides.get(item->name).has_value();
            }
        }
    }

    invoke_listeners<IListObserver<OverrideItem>>([&](IListObserver<OverrideItem>* l)
                                                  { l->on_will_be_reset(); });

    m_sources = sources;

    m_items        = std::move(new_items);
    m_item_index   = std::move(new_item_index);
    m_item_sources = std::move(new_item_sources);

    // 2. Go through all items and cache if the item values are mixed
    for (OverrideItemPtr& item : m_items) {
        update_overriden(item.get());
    }

    invoke_listeners<IListObserver<OverrideItem>>([&](IListObserver<OverrideItem>* l)
                                                  { l->on_reset(); });
}

void
ObjectSettingsObservableList::set_value(const std::string& key, const Domain::ConfigValue& value)
{
    OverrideItem* item = find_item(key);

    ASSERT(item, "key has to correspond to existing item");

    if (item->is_override()) {
        for (Domain::ConfigBox* box : m_item_sources.at(item)) {
            box->overrides.set(key, value);
        }
        item->overriden = true;
    } else {
        for (Domain::ConfigBox* box : m_item_sources.at(item)) {
            box->items.opt(key).set(value);
        }
    }

    update_overriden(item);

    m_scene_interactor.undo_provider().take_snapshot(UndoSnapshotType::SetPartSettingsValue);

    invoke_listeners<IListObserver<OverrideItem>>([&](IListObserver<OverrideItem>* l)
                                                  { l->on_updated({m_item_index.at(key)}); });
}

void ObjectSettingsObservableList::set_override(const std::string& key, bool enabled)
{
    OverrideItem* item = find_item(key);

    ASSERT(item && item->is_override(), "key has to correspond with existing override item");

    item->overriden = enabled;

    for (Domain::ConfigBox* box : m_item_sources.at(item)) {
        enabled ? box->overrides.enable(key) : box->overrides.disable(key);
    }

    invoke_listeners<IListObserver<OverrideItem>>([&](IListObserver<OverrideItem>* l)
                                                  { l->on_updated({m_item_index.at(key)}); });
}

const Domain::ConfigValue*
ObjectSettingsObservableList::find_object_value(const std::string& key, size_t index)
{
    if (m_object_settings_source) {
        return &m_object_settings_source->find(key).item->value();
    }
    return nullptr;
}

OverrideItem* ObjectSettingsObservableList::find_item(const std::string& name)
{
    ItemMap::const_iterator it = m_item_index.find(name);

    return it == m_item_index.end() ? nullptr : m_items.at(it->second).get();
}

void ObjectSettingsObservableList::update_overriden(OverrideItem* item)
{
    const Domain::ConfigValue value = item->config_item->value();

    item->mixed = false;
    if (item->overriden.has_value()) { // override
        bool overriden = item->overriden.value();
        for (Domain::ConfigBox* source : m_item_sources.at(item)) {
            std::optional<Domain::ConfigItem> source_val = source->overrides.get(item->name);
            if (overriden != source_val.has_value()
                || value != source->overrides.find(item->name)->value())
            {
                item->mixed = true;
                break;
            }
        }
    } else { // item
        for (Domain::ConfigBox* source : m_item_sources.at(item)) {
            if (value != source->items.find(item->name)->value()) {
                item->mixed = true;
                break;
            }
        }
    }
}

} // namespace Slic3r::Biz
