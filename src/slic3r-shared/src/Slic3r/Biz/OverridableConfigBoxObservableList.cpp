#include "Slic3r/Biz/OverridableConfigBoxObservableList.hpp"

#include "Slic3r/Domain/Config.hpp"

namespace Slic3r::Biz {

void OverridableConfigBoxObservableList::set_config_box(
    Domain::ConfigBox* config_box,
    const Domain::ConfigBox* original_config_box
)
{
    invoke_listeners<IListObserver<OverrideItem>>([&](IListObserver<OverrideItem>* l)
                                                  { l->on_will_be_reset(); });

    m_config_box = config_box;
    m_items.clear();

    // 1. Go through all Config Boxes and populate m_item_sources, m_items and m_item_index
    for (const Domain::ConfigItem& config_item : config_box->items.all_items()) {
        const Domain::ConfigItem* original_config_item =
            original_config_box ? original_config_box->find(config_item.name()).item : nullptr;

        m_items.emplace_back(
            config_item.name(),
            false,
            std::nullopt,
            &config_item,
            std::nullopt,
            original_config_item
        );
    }

    for (const Domain::ConfigItem& config_item : config_box->overrides.all_items()) {
        const Domain::ConfigItem* original_config_item =
            original_config_box ? original_config_box->find(config_item.name()).item : nullptr;
        std::optional<bool> original_overridden = std::nullopt;
        if (original_config_box)
            original_overridden =
                original_config_box->overrides.get(config_item.name()).has_value();
        m_items.emplace_back(
            config_item.name(),
            false,
            config_box->overrides.get(config_item.name()).has_value(),
            &config_item,
            original_overridden,
            original_config_item
        );
    }

    invoke_listeners<IListObserver<OverrideItem>>([&](IListObserver<OverrideItem>* l)
                                                  { l->on_reset(); });
}

void OverridableConfigBoxObservableList::set_value(
    const std::string_view key,
    const Domain::ConfigValue& value
)
{
    Items::iterator override_item_it = std::find_if(
        m_items.begin(),
        m_items.end(),
        [key](const Biz::OverrideItem& item) { return item.name == key; }
    );
    ASSERT(override_item_it != m_items.end());
    size_t override_item_index = std::distance(m_items.begin(), override_item_it);

    // 1. try set ConfigItem
    {
        const std::vector<Domain::ConfigItem>& all_items = m_config_box->items.all_items();

        const std::vector<Domain::ConfigItem>::const_iterator index_it = std::find_if(
            all_items.cbegin(),
            all_items.cend(),
            [key](const Domain::ConfigItem& item) { return item.def().name == key; }
        );

        if (index_it != all_items.cend() && index_it->value() != value) {
            m_config_box->items.opt(key).set(value);

            invoke_listeners<IListObserver<Biz::OverrideItem>>(
                [override_item_index](IListObserver<Biz::OverrideItem>* l)
                { l->on_updated(override_item_index); }
            );

            return;
        }
    }

    // 2. try set Override
    {
        const std::vector<Domain::ConfigItem>& all_items = m_config_box->overrides.all_items();

        std::vector<Domain::ConfigItem>::const_iterator index = std::find_if(
            all_items.cbegin(),
            all_items.cend(),
            [key](const Domain::ConfigItem& item) { return item.def().name == key; }
        );

        if (index != all_items.cend() && index->value() != value) {
            m_config_box->overrides.set(std::string{key}, value);

            invoke_listeners<IListObserver<Biz::OverrideItem>>(
                [override_item_index](IListObserver<Biz::OverrideItem>* l)
                { l->on_updated(override_item_index); }
            );

            return;
        }
    }
}

void OverridableConfigBoxObservableList::set_override(const std::string& key, bool enable)
{
    const std::vector<Domain::ConfigItem>& all_items      = m_config_box->overrides.all_items();
    std::vector<Domain::ConfigItem>::const_iterator index = std::find_if(
        all_items.cbegin(),
        all_items.cend(),
        [key](const Domain::ConfigItem& item) { return item.def().name == key; }
    );
    ASSERT(index != all_items.cend());

    Items::iterator it = std::find_if(
        m_items.begin(),
        m_items.end(),
        [key](const Biz::OverrideItem& item) { return item.name == key; }
    );
    ASSERT(it != m_items.end());

    const size_t it_index = std::distance(m_items.begin(), it);

    it->overriden = enable;
    if (enable) {
        m_config_box->overrides.enable(key);
    } else {
        m_config_box->overrides.disable(key);
    }

    invoke_listeners<IListObserver<Biz::OverrideItem>>(
        [it_index](IListObserver<Biz::OverrideItem>* l) { l->on_updated(it_index); }
    );
}

static const OverrideItem&
find_override_item(const std::vector<OverrideItem>& items, const std::string& key)
{
    std::vector<OverrideItem>::const_iterator it = std::find_if(
        items.cbegin(),
        items.cend(),
        [&](const OverrideItem& item) { return item.name == key; }
    );
    ASSERT(it != items.cend());

    return *it;
}

std::pair<const Domain::ConfigValue*, std::optional<bool>> OverridableConfigBoxObservableList::find(
    const std::string& key
) const
{
    const OverrideItem& override_item = find_override_item(m_items, key);
    return {&override_item.config_item->value(), override_item.overriden};
}

bool OverridableConfigBoxObservableList::is_dirty(const std::string& key) const
{
    const OverrideItem& override_item = find_override_item(m_items, key);
    return override_item.is_dirty();
}

bool OverridableConfigBoxObservableList::is_dirty() const
{
    for (const auto& item : m_items) {
        if (item.is_dirty())
            return true;
    }
    return false;
}

void OverridableConfigBoxObservableList::set_from_original_value(const std::string& key)
{
    const OverrideItem& override_item = find_override_item(m_items, key);
    if (override_item.original_config_item) {
        set_value(key, override_item.original_config_item->value());
        if (override_item.original_overridden.has_value())
            set_override(key, override_item.original_overridden.value());
    }
}

const OverrideItem& OverridableConfigBoxObservableList::at(size_t index) const
{
    return m_items.at(index);
}

size_t OverridableConfigBoxObservableList::size() const
{
    return m_items.size();
}

} // namespace Slic3r::Biz
