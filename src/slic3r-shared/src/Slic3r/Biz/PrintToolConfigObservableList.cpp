#include "Slic3r/Biz/PrintToolConfigObservableList.hpp"

#include <Slic3r/Domain/Preset/HwConfig.hpp>
#include <Slic3r/Domain/Config.hpp>
#include <Slic3r/Domain/Preset/SelectedPreset.hpp>

namespace Slic3r::Biz {

PrintToolConfigObservableList::PrintToolConfigObservableList(
    const Domain::Workbench& workbench,
    Scene::SceneInteractor& scene_interactor
) :
    m_workbench(workbench),
    m_scene_interactor(scene_interactor),
    m_scene_bed_changed_listener_scope(scene_interactor, *this),
    m_selected_bed_changed_listener_scope(scene_interactor, *this),
    m_print_tool_shared_context{false, m_extruder_candidates}
{}

const PrintToolItem& PrintToolConfigObservableList::at(size_t index) const
{
    return m_items.at(index);
}

size_t PrintToolConfigObservableList::size() const
{
    return m_items.size();
}

void PrintToolConfigObservableList::set_sources(
    const Domain::SelectionId selected_project_id,
    const Domain::SelectionId selected_container_id,
    Domain::Preset::SelectedPreset& selected_preset,
    const std::vector<Domain::ConfigBox*>& tool_config_boxes,
    const Domain::ConfigBox* original_print_config_box,
    const std::vector<const Domain::ConfigBox*>& original_tool_config_boxes
)
{
    m_selected_project_id   = selected_project_id;
    m_selected_container_id = selected_container_id;
    m_extruder_candidates.clear();

    Domain::ConfigBox& print_config_box = selected_preset.print.config_box();
    m_print_tool_shared_context.has_multiple_extruders =
        Domain::Preset::get_feature<bool>(selected_preset.hw_config.features, "multi_extruder")
            .value_or(false);

    const std::vector<Domain::ConfigItem>& print_items = print_config_box.items.all_items();
    const std::vector<Domain::ConfigItem>& original_print_items = original_print_config_box->items.all_items();
    if (!m_items.empty()
        && print_items.size() == m_items.size()
        && m_tool_config_boxes.size() == tool_config_boxes.size())
    {
        m_print_config_box  = &print_config_box;
        m_tool_config_boxes = tool_config_boxes;
        m_original_print_config_box  = original_print_config_box;
        m_original_tool_config_boxes = original_tool_config_boxes;
        // we shall simply update items
        update_items();
    } else {
        // construct items from scratch
        invoke_listeners<IListObserver<PrintToolItem>>([](IListObserver<PrintToolItem>* l)
                                                       { l->on_will_be_reset(); });

        m_print_config_box  = &print_config_box;
        m_tool_config_boxes = tool_config_boxes;
        m_original_print_config_box  = original_print_config_box;
        m_original_tool_config_boxes = original_tool_config_boxes;
        m_items.clear();
        for (const Domain::ConfigItem& print_item : print_items) {
            std::vector<const Domain::ConfigItem*> tool_overrides;
            std::vector<const Domain::ConfigItem*> original_tool_overrides;
            if (print_item.def().overrides_in.contains(Domain::FDMConfigLocation::Tool)) {
                tool_overrides.reserve(tool_config_boxes.size());
                for (const Domain::ConfigBox* tool_config_box : tool_config_boxes) {
                    const Domain::ConfigItem* tool_override =
                        tool_config_box->overrides.find(print_item.name());
                    tool_overrides.emplace_back(tool_override);
                }
                original_tool_overrides.reserve(tool_config_boxes.size());
                for (const Domain::ConfigBox* tool_config_box : original_tool_config_boxes) {
                    const Domain::ConfigItem* tool_override =
                        tool_config_box->overrides.find(print_item.name());
                    original_tool_overrides.emplace_back(tool_override);
                }
            }
            m_items.emplace_back(
                print_item.name(),
                false,
                &print_item,
                tool_overrides,
                original_print_config_box->items.find(print_item.name()),
                original_tool_overrides,
                std::pair<Domain::ConfigValue, bool>{print_item.value(), false},
                m_print_tool_shared_context,
                m_favorites.contains(print_item.name())
            );
            // HOTFIX: apply_compatibility_rule() must not be called directly on
            // unresolved FloatOrPercentage overrides (it asserts). update_value()
            // already guards against this, so recompute the value through it
            // instead of duplicating that guard here.
            m_items.back().update_value();
        }

        invoke_listeners<IListObserver<PrintToolItem>>([](IListObserver<PrintToolItem>* l)
                                                       { l->on_reset(); });
    }
}

void PrintToolConfigObservableList::set_print_value(
    const std::string& key,
    const Domain::ConfigValue& value
)
{
    PrintToolItems::iterator index_it = find_item(key);

    if (index_it != m_items.end() && index_it->print_item->value() != value) {
        const size_t index = std::distance(m_items.begin(), index_it);

        m_print_config_box->items.opt(key).set(value);

        index_it->update_value();
        invoke_listeners<IListObserver<PrintToolItem>>([index](IListObserver<PrintToolItem>* l)
                                                       { l->on_updated(index); });
    }
}

void PrintToolConfigObservableList::set_tool_override(
    const std::string& key,
    size_t index,
    bool override
)
{
    // Does nothing, override is always set
}

void PrintToolConfigObservableList::set_tool_value(
    const std::string& key,
    const std::vector<size_t>& indexes,
    const Domain::ConfigValue& value
)
{
    PrintToolItems::iterator index_it = find_item(key);

    if (index_it != m_items.cend()) {
        for (size_t index : indexes) {
            if (index > m_tool_config_boxes.size()) {
                continue;
            }
            m_tool_config_boxes.at(index)->overrides.set(key, value);
        }

        index_it->update_value();

        const size_t override_index = std::distance(m_items.begin(), index_it);
        invoke_listeners<IListObserver<PrintToolItem>>(
            [override_index](IListObserver<PrintToolItem>* l) { l->on_updated(override_index); }
        );
    }
}

const Domain::ConfigValue* PrintToolConfigObservableList::find_print_value(
    const std::string& name
) const
{
    Domain::ConfigItem* found_item = m_print_config_box->items.find(name);
    return found_item ? &found_item->value() : nullptr;
}

const Domain::ConfigValue*
PrintToolConfigObservableList::find_tool_value(const std::string& name, size_t index) const
{
    Domain::ConfigItem* found_item = m_tool_config_boxes.at(index)->items.find(name);
    return found_item ? &found_item->value() : nullptr;
}

void PrintToolConfigObservableList::on_bed_instance_extruder_candidates_changed(
    Domain::SelectionId project_id,
    Domain::BedRef instance,
    const std::vector<unsigned int>& extruder_candidates
)
{
    if (m_selected_project_id == project_id
        && instance.config_container_id == m_selected_container_id
        && m_scene_interactor.bed_selection().last_selected_bed() == instance)
    {
        update_extruders();
    }
}

void PrintToolConfigObservableList::on_selected_bed_instances_changed(
    Domain::SelectionId project_id,
    const Scene::BedSelection& bed_selection
)
{
    if (m_selected_project_id == project_id
        && m_selected_container_id == bed_selection.last_selected_bed().config_container_id)
    {
        update_extruders();
    }
}

void PrintToolConfigObservableList::set_favorites(const std::vector<std::string>& favorites)
{
    m_favorites = std::set<std::string>(favorites.begin(), favorites.end());

    for (PrintToolItem& tool_print_item : m_items) {
        tool_print_item.is_favorite = m_favorites.contains(tool_print_item.name);
    }

    if (m_items.empty()) {
        return;
    }
    invoke_listeners<IListObserver<PrintToolItem>>(
        [this](IListObserver<PrintToolItem>* l)
        { l->on_updated(IndexRange{0, m_items.size() - 1}); }
    );
}

static const PrintToolItem&
find_print_tool_item(const std::vector<PrintToolItem>& items, const std::string& key)
{
    auto it = std::find_if(
        items.cbegin(),
        items.cend(),
        [&](const PrintToolItem& item) { return item.name == key; }
    );
    ASSERT(it != items.cend());

    return *it;
}

bool PrintToolConfigObservableList::is_dirty(const std::string& key) const
{
    return find_print_tool_item(m_items, key).is_dirty();
}

bool PrintToolConfigObservableList::is_dirty_print(const std::string& key) const
{
    return find_print_tool_item(m_items, key).is_dirty_print();
}

bool PrintToolConfigObservableList::is_dirty_tool(const std::string& key, size_t index) const
{
    return find_print_tool_item(m_items, key).is_dirty_tool(index);
}

bool PrintToolConfigObservableList::is_dirty() const
{
    for (const auto& item: m_items) {
        if (item.is_dirty()) {
            return true;
        }
    }
    return false;
}

bool PrintToolConfigObservableList::is_dirty_print() const
{
    for (const auto& item: m_items) {
        if (item.is_dirty_print()) {
            return true;
        }
    }
    return false;
}

bool PrintToolConfigObservableList::is_dirty_tool(size_t index) const
{
    for (const auto& item: m_items) {
        if (item.is_dirty_tool(index)) {
            return true;
        }
    }
    return false;
}

void PrintToolConfigObservableList::set_from_original_value(const std::string& key)
{
    auto item = find_item(key);
    if (item->original_print_item) {
        set_print_value(key, item->original_print_item->value());
    }

    if (!item->original_tool_overrides.empty()) {
        for (size_t tool_id{}; tool_id < item->original_tool_overrides.size(); tool_id++) {
            set_tool_value(key, {tool_id}, item->original_tool_overrides.at(tool_id)->value());
        }
    }
}

void PrintToolConfigObservableList::set_from_original_print_value(const std::string& key)
{
    auto item = find_item(key);
    if (item->original_print_item) {
        set_print_value(key, item->original_print_item->value());
    }
}

void
PrintToolConfigObservableList::set_from_original_tool_value(const std::string& key, size_t index)
{
    auto item = find_item(key);

    if (!item->original_tool_overrides.empty()) {
        set_tool_value(key, {index}, item->original_tool_overrides.at(index)->value());
    }
}

PrintToolConfigObservableList::PrintToolItems::iterator PrintToolConfigObservableList::find_item(
    const std::string& name
)
{
    return std::find_if(
        m_items.begin(),
        m_items.end(),
        [name](const PrintToolItem& item) { return item.name == name; }
    );
}

void PrintToolConfigObservableList::update_extruders()
{
    const std::vector<unsigned> extruder_candidates =
        m_workbench.project(m_selected_project_id)
            .find_bed_instance_by_id(
                m_scene_interactor.bed_selection().last_selected_bed().instance_id
            )
            ->extruder_candidates;

    const std::set<unsigned> extruder_candidates_set{
        extruder_candidates.cbegin(),
        extruder_candidates.cend()
    };
    if (m_extruder_candidates != extruder_candidates_set) {
        m_extruder_candidates = extruder_candidates_set;
        update_items();
    }
}

void PrintToolConfigObservableList::update_items()
{
    for (PrintToolItem& tool_print_item : m_items) {
        tool_print_item.is_favorite = m_favorites.find(tool_print_item.name) != m_favorites.end();

        tool_print_item.print_item = m_print_config_box->items.find(tool_print_item.name);
        tool_print_item.original_print_item = m_original_print_config_box->items.find(tool_print_item.name);

        std::vector<const Domain::ConfigItem*> tool_overrides;
        if (tool_print_item.print_item->def().overrides_in.contains(
                Domain::FDMConfigLocation::Tool
            ))
        {
            for (const Domain::ConfigBox* tool_config_box : m_tool_config_boxes) {
                const Domain::ConfigItem* tool_override =
                    tool_config_box->overrides.find(tool_print_item.print_item->name());
                tool_overrides.emplace_back(tool_override);
            }
        }
        tool_print_item.tool_overrides = tool_overrides;
        // HOTFIX: see comment in set_sources() above.
        tool_print_item.update_value();

        std::vector<const Domain::ConfigItem*> original_tool_overrides;
        if (tool_print_item.original_print_item->def().overrides_in.contains(
                Domain::FDMConfigLocation::Tool
            ))
        {
            for (const Domain::ConfigBox* tool_config_box : m_original_tool_config_boxes) {
                const Domain::ConfigItem* tool_override =
                    tool_config_box->overrides.find(tool_print_item.original_print_item->name());
                original_tool_overrides.emplace_back(tool_override);
            }
        }
        tool_print_item.original_tool_overrides = original_tool_overrides;
    }

    invoke_listeners<IListObserver<PrintToolItem>>(
        [this](IListObserver<PrintToolItem>* l)
        { l->on_updated(IndexRange{0, m_items.size() - 1}); }
    );
}

} // namespace Slic3r::Biz
