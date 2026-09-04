#include "Slic3r/App/Config/PrintToolRowItem.hpp"

#include <Slic3r/Domain/Config.hpp>

#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Algorithms/Color.hpp"

#include "Slic3r/App/Config/ConfigRowItem.hpp"
#include "Slic3r/App/Config/ConfigItemUtils.hpp"
#include "Slic3r/App/Config/PrintToolRowButton.hpp"
#include "Slic3r/App/Config/ExplanationContainer.hpp"
#include "Slic3r/App/Config/FavoriteButton.hpp"
#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/AppConfigInteractor.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

class ToolDropArea : public RectangleButton
{
public:
    ToolDropArea()
    {
        set_background_color(Platform::Color::Transparent);
        set_background_border_width(2);
        set_background_color_border(m_theme->color_imgui(Platform::Color::Text));
        emplace_back<Text>(Biz::_u8L("Drop tool here to separate"));
        set_droppable(true);
        set_visible(false);
    }

protected:
    void dnd_key_accepted_changed_internal(bool could_accept) override
    {
        set_visible(could_accept);
    }

    void dnd_could_accept_changed_internal(bool could_accept) override
    {
        set_background_color_border(m_theme->color_imgui(
            could_accept ? Platform::Color::AccentSecondary : Platform::Color::Text
        ));
    }
};

PrintToolRowItem::PrintToolRowItem(
    size_t index,
    const Biz::PrintToolItem& data,
    Biz::PrintToolConfigBoxInteractor& cbi,
    Biz::IConfigBoxSetter& cb_setter,
    Biz::ProjectInteractor& project_interactor,
    const PrintToolRowItemDisplayOptions& options
) :
    Biz::DataObserver<Biz::PrintToolItem>(index, data),
    m_cbi(cbi),
    m_cb_setter(cb_setter),
    m_project_interactor(project_interactor),
    m_options(options),
    m_colors_changed_listener_scope(project_interactor.project_settings_interactor(), *this),
    m_preset_changed_listener_scope(project_interactor.preset_interactor(), *this)
{
    set_orientation(Orientation::Horizontal);
    set_fill(IM_COL32_BLACK_TRANS);
    set_border_width(2);
    set_border_color(IM_COL32_BLACK_TRANS);
    set_gap(4_fpx);
    set_object_name("PrintToolRowItem");
    set_align_items(YGAlignFlexStart);

    m_column = emplace_back<Item>();
    m_column->set_flex_grow(1);
    m_column->set_orientation(Orientation::Vertical);

    m_header = m_column->emplace_back<Item>();
    m_header->set_orientation(Orientation::Horizontal);
    m_header->set_flex_grow(1.f);
    m_header->set_align_items(YGAlignFlexStart);
    m_header->set_gap(3.f);

    m_favorite_button = emplace_back<FavoriteButton>();
    m_favorite_button->set_width(12_fpx);
    m_favorite_button->set_height(12_fpx);
    m_favorite_button->set_visible(options.show_favorites);
    m_favorite_button->callbacks().action = [this]()
    { AppServices::instance().app_config_interactor().toggle_favorite_param(m_state->name); };

    on_data_update();
}

PrintToolRowItem::~PrintToolRowItem()
{
    if (m_tool_list_view) {
        m_tool_list_view->set_source_list(nullptr, true);
    }
}

void PrintToolRowItem::navigate_to_item(const Domain::ConfigItem* config_item)
{
    if (config_item && m_state->print_item->name() == config_item->name()) {
        set_border_color(m_theme->color_imgui(Platform::Color::AccentTertiary));
        if (m_main_button) {
            m_main_button->set_checked(true);
        }
    } else {
        set_border_color(IM_COL32_BLACK_TRANS);
    }
}

void PrintToolRowItem::clear_navigation()
{
    set_border_color(IM_COL32_BLACK_TRANS);
}

const ToolRowOverrideGroup& PrintToolRowItem::at(size_t index) const
{
    return m_tool_overrides.at(index);
}

size_t PrintToolRowItem::size() const
{
    return m_tool_overrides.size();
}

void PrintToolRowItem::on_colors_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    const std::vector<Domain::ColorRGB>& colors
)
{
    if (m_initialized_type == InitializedType::PrintTool) {
        update_explanation();

        if (!m_tool_overrides.empty()) {
            for (size_t index = 0; index < m_overrides.size(); ++index) {
                ToolRowOverridePtr& override = m_overrides.at(index);
                const bool is_candidate =
                    m_state->shared_context.extruder_candidates.contains(index);

                ImColor color;
                if (is_candidate) {
                    if (index < colors.size()) {
                        const Domain::ColorRGB& color_domain = colors.at(index);
                        color = {color_domain.r(), color_domain.g(), color_domain.b()};
                    } else {
                        Domain::ColorRGB color_domain;
                        ASSERT(
                            Biz::Algorithms::Color::decode_color(
                                Biz::ProjectSettingsInteractor::palette_color(index),
                                color_domain
                            )
                        );
                        color = {color_domain.r(), color_domain.g(), color_domain.b()};
                    }
                } else {
                    color =
                        m_theme->color_imgui(Platform::Color::Text, Platform::ColorGroup::Disabled);
                }

                override->color = color;
            }
            invoke_listeners<Biz::IListObserver<ToolRowOverrideGroup>>(
                [this](Biz::IListObserver<ToolRowOverrideGroup>* l)
                { l->on_updated({0, size() - 1}); }
            );
        }
    }
}

void PrintToolRowItem::on_view_will_be_reset()
{
    m_tool_overrides.clear();
}

void PrintToolRowItem::on_preset_selection_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    Biz::Preset::PresetItemType type
)
{
    if (m_project_interactor.selected_project_id() == project_id
        && m_project_interactor.selected_config_container_id() == config_container_id
        && (type == Biz::Preset::PresetItemType::PrintPreset
            || type == Biz::Preset::PresetItemType::ToolPrintPreset)
        && m_initialized_type == InitializedType::PrintTool)
    {
        presort_overrides();
    }
}

void PrintToolRowItem::on_data_update()
{
    const bool need_multiple =
        m_state->shared_context.has_multiple_extruders && !m_state->tool_overrides.empty();
    const bool need_init = m_initialized_type == InitializedType::None
        || (m_initialized_type == InitializedType::PrintOnly && need_multiple)
        || (m_initialized_type == InitializedType::PrintTool && !need_multiple);

    if (need_init) {
        if (m_initialized_type != InitializedType::None) {
            clear();
        }
        initialize();
    }

    if (m_initialized_type == InitializedType::PrintTool) {
        m_main_button->update_data(m_state);

        invoke_listeners<Biz::IListObserver<ToolRowOverrideGroup>>(
            [](Biz::IListObserver<ToolRowOverrideGroup>* l) { l->on_will_be_reset(); }
        );

        const std::unordered_set<const Domain::ConfigItem*> used_overrides{
            m_state->tool_overrides.begin(),
            m_state->tool_overrides.end()
        };

        // remove deleted overrides from groups
        for (ToolRowOverrideGroup& group : m_tool_overrides) {
            std::erase_if(
                group.first,
                [&](const ToolRowOverride* override)
                { return !used_overrides.contains(override->override_item); }
            );
        }

        // remove empty overrides group
        std::erase_if(
            m_tool_overrides,
            [](const ToolRowOverrideGroup& group) { return group.first.empty(); }
        );

        // remove deleted overrides source
        std::erase_if(
            m_overrides,
            [&](const ToolRowOverridePtr& override)
            { return !used_overrides.contains(override->override_item); }
        );

        // update values | create new ones
        const std::vector<Domain::ColorRGB> colors =
            m_project_interactor.project_settings_interactor().get_colors(
                m_project_interactor.selected_config_container_id()
            );

        size_t index = 0;
        for (const Domain::ConfigItem* tool_override : m_state->tool_overrides) {
            const bool is_candidate = m_state->shared_context.extruder_candidates.contains(index);

            ImColor color;
            if (is_candidate) {
                if (index < colors.size()) {
                    const Domain::ColorRGB& color_domain = colors.at(index);
                    color = {color_domain.r(), color_domain.g(), color_domain.b()};
                } else {
                    Domain::ColorRGB color_domain;
                    ASSERT(
                        Biz::Algorithms::Color::decode_color(
                            Biz::ProjectSettingsInteractor::palette_color(index),
                            color_domain
                        )
                    );
                    color = {color_domain.r(), color_domain.g(), color_domain.b()};
                }
            } else {
                color = m_theme->color_imgui(Platform::Color::Text, Platform::ColorGroup::Disabled);
            }

            std::vector<ToolRowOverridePtr>::iterator it = std::ranges::find_if(
                m_overrides,
                [&](const ToolRowOverridePtr& override)
                { return override->override_item == tool_override; }
            );
            if (it == m_overrides.end()) {
                m_overrides.emplace_back(
                    std::make_unique<ToolRowOverride>(
                        tool_override,
                        is_candidate,
                        index,
                        std::move(color)
                    )
                );
            } else {
                (*it)->color              = std::move(color);
                (*it)->extruder_candidate = is_candidate;
            }

            ++index;
        }

        std::unordered_set<std::string> dnd_keys;
        dnd_keys.reserve(m_overrides.size());

        std::ranges::transform(
            m_overrides,
            std::inserter(dnd_keys, dnd_keys.end()),
            [](const ToolRowOverridePtr& override) { return override->dnd_key(); }
        );
        m_tool_drop_area->set_accepted_keys(dnd_keys);

        update_group_size();

        invoke_listeners<Biz::IListObserver<ToolRowOverrideGroup>>(
            [](Biz::IListObserver<ToolRowOverrideGroup>* l) { l->on_reset(); }
        );

        update_explanation();

        // we have not yet presorted overrides
        if (m_tool_overrides.empty() && !m_overrides.empty()) {
            presort_overrides();
        }
    }

    if (m_config_row_item) {
        m_config_row_item->set_state(*m_state->print_item);
    }

    m_favorite_button->set_checked(m_state->is_favorite);
}

void PrintToolRowItem::clear()
{
    if (m_initialized_type == InitializedType::PrintOnly) {
        m_header->remove(m_config_row_item);
        m_config_row_item = nullptr;
    } else if (m_initialized_type == InitializedType::PrintTool) {
        m_tool_list_view->set_source_list(nullptr, true);
        m_column->remove(m_content);
        m_header->remove(m_main_button);
        m_content     = nullptr;
        m_main_button = nullptr;
        m_tool_overrides.clear();
    }
    m_tool_list_view   = nullptr;
    m_initialized_type = InitializedType::None;
}

void PrintToolRowItem::initialize()
{
    ASSERT(m_initialized_type == InitializedType::None);
    if (!m_state->shared_context.has_multiple_extruders || m_state->tool_overrides.empty()) {
        m_initialized_type = InitializedType::PrintOnly;

        m_config_row_item = m_header->emplace<ConfigRowItem>(
            0,
            m_index,
            *m_state->print_item,
            m_cb_setter,
            [this]() { return m_state->is_dirty(); },
            0
        );
        m_config_row_item->set_flex_grow(1.f);

        m_favorite_button->set_margin(Margins{0, 10_fpx, 0, 0});

    } else {
        m_initialized_type = InitializedType::PrintTool;

        m_favorite_button->set_margin(Margins{0, 6_fpx, 0, 0});

        m_main_button = m_header->emplace<PrintToolRowButton>(0, m_cb_setter);
        m_main_button->set_flex_grow(1.f);
        m_main_button->callbacks().checked_changed = [this](bool checked)
        { m_content->set_visible(checked); };

        m_content = m_column->emplace_back<Rectangle>();
        m_content->set_flags(ImDrawFlags_RoundCornersBottom);
        m_content->set_visible(false);
        m_content->set_orientation(Orientation::Vertical);
        m_content->set_gap(5_fpx);
        m_content->set_padding(5_fpx);
        m_content->set_fill(IM_COL32_BLACK_TRANS);
        m_content->set_border_color(
            m_theme->color_imgui(Platform::Color::Button, Platform::ColorGroup::Active)
        );
        m_content->set_border_width(1);

        ToolRowFactory factory{
            m_cb_setter,
            [this](size_t tool_index) { exclude_tool(tool_index); },
            [this](size_t dropped_tool_index, size_t index)
            { move_tool(dropped_tool_index, index); },
            !m_options.show_favorites
        };
        m_tool_list_view = m_content->emplace_back<ToolRowListView>(std::move(factory));
        m_tool_list_view->set_orientation(Orientation::Vertical);
        m_tool_list_view->set_gap(5_fpx);
        m_tool_list_view->set_source_list(this);

        if (m_options.show_explanation) {
            m_explanation_container =
                m_content->emplace_back<ExplanationContainer>(m_project_interactor);
        }

        m_tool_drop_area                           = m_content->emplace_back<ToolDropArea>();
        m_tool_drop_area->callbacks().dnd_accepted = [this](const DnDPayload& payload)
        { exclude_tool(*payload.get<size_t>("tool_index")); };
    }
}

void PrintToolRowItem::update_explanation()
{
    if (m_explanation_container) {
        m_explanation_container->update_explanation(*m_state);
    }
}

void PrintToolRowItem::exclude_tool(size_t tool_index)
{
    const ToolRowOverridePtr& override = m_overrides.at(tool_index);

    ToolRowOverrideGroup* group = find_group(override);
    if (!group || group->first.size() == 1) {
        return;
    }

    invoke_listeners<Biz::IListObserver<ToolRowOverrideGroup>>(
        [](Biz::IListObserver<ToolRowOverrideGroup>* l) { l->on_will_be_reset(); }
    );

    std::erase_if(
        group->first,
        [&](const ToolRowOverride* group_override) { return group_override == override.get(); }
    );

    m_tool_overrides.emplace_back(ToolRowOverrideGroup{ToolRowOverrides{override.get()}, 0});

    update_group_size();

    invoke_listeners<Biz::IListObserver<ToolRowOverrideGroup>>(
        [](Biz::IListObserver<ToolRowOverrideGroup>* l) { l->on_reset(); }
    );
}

void PrintToolRowItem::move_tool(size_t tool_index, size_t group_index)
{
    ToolRowOverride* override = m_overrides.at(tool_index).get();

    std::vector<ToolRowOverrideGroup>::iterator src_group_it = std::ranges::find_if(
        m_tool_overrides,
        [override](const ToolRowOverrideGroup& group)
        { return std::ranges::find(group.first, override) != group.first.end(); }
    );

    std::vector<ToolRowOverrideGroup>::iterator dst_group_it =
        m_tool_overrides.begin() + group_index;

    // if groups are same, do nothing
    if (src_group_it == dst_group_it) {
        return;
    }

    const Domain::ConfigValue& target_value = dst_group_it->first.front()->override_item->value();

    auto override_it = std::ranges::find(src_group_it->first, override);

    invoke_listeners<Biz::IListObserver<ToolRowOverrideGroup>>(
        [](Biz::IListObserver<ToolRowOverrideGroup>* l) { l->on_will_be_reset(); }
    );

    dst_group_it->first.emplace_back(*override_it);
    src_group_it->first.erase(override_it);
    if (src_group_it->first.empty()) { // remove group_if empty
        m_tool_overrides.erase(src_group_it);
    }

    sort_extruders_in_groups();
    update_group_size();

    invoke_listeners<Biz::IListObserver<ToolRowOverrideGroup>>(
        [](Biz::IListObserver<ToolRowOverrideGroup>* l) { l->on_reset(); }
    );

    if (override->override_item->value() != target_value) {
        m_cb_setter.set_item_value(*override->override_item, target_value, {override->tool_index});
    }
}

void PrintToolRowItem::presort_overrides()
{
    invoke_listeners<Biz::IListObserver<ToolRowOverrideGroup>>(
        [](Biz::IListObserver<ToolRowOverrideGroup>* l) { l->on_will_be_reset(); }
    );

    m_tool_overrides.clear();
    for (const std::unique_ptr<ToolRowOverride>& override : m_overrides) {
        const Domain::ConfigValue& value = override->override_item->value();
        auto it                          = std::ranges::find_if(
            m_tool_overrides,
            [&](const ToolRowOverrideGroup& overrides)
            { return value == overrides.first.front()->override_item->value(); }
        );

        if (it == m_tool_overrides.end()) {
            m_tool_overrides.emplace_back(
                ToolRowOverrideGroup{ToolRowOverrides{override.get()}, 0}
            );
        } else {
            it->first.emplace_back(override.get());
        }
    }

    sort_extruders_in_groups();
    update_group_size();

    invoke_listeners<Biz::IListObserver<ToolRowOverrideGroup>>(
        [](Biz::IListObserver<ToolRowOverrideGroup>* l) { l->on_reset(); }
    );
}

void PrintToolRowItem::update_group_size()
{
    std::vector<ToolRowOverrideGroup>::iterator it = std::ranges::max_element(
        m_tool_overrides,
        {},
        [](const ToolRowOverrideGroup& group) { return group.first.size(); }
    );
    const size_t largest_group_size = it == m_tool_overrides.end() ? 0 : it->first.size();

    for (ToolRowOverrideGroup& group : m_tool_overrides) {
        group.second = largest_group_size;
    }
}

void PrintToolRowItem::sort_extruders_in_groups()
{
    // sort all overrides by tool_index
    for (ToolRowOverrideGroup& group : m_tool_overrides) {
        std::ranges::sort(
            group.first,
            [](const ToolRowOverride* lhs, const ToolRowOverride* rhs)
            { return lhs->tool_index < rhs->tool_index; }
        );
    }
}

ToolRowOverrideGroup* PrintToolRowItem::find_group(const ToolRowOverridePtr& override)
{
    std::vector<ToolRowOverrideGroup>::iterator it = std::ranges::find_if(
        m_tool_overrides,
        [&](const ToolRowOverrideGroup& group)
        { return std::ranges::find(group.first, override.get()) != group.first.end(); }
    );

    return it == m_tool_overrides.end() ? nullptr : &(*it);
}

} // namespace Slic3r::App
