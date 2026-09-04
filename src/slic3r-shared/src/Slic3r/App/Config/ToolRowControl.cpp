#include "Slic3r/App/Config/ToolRowControl.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/IConfigBoxSetter.hpp"

#include "Slic3r/App/Config/ConfigItemControl.hpp"
#include "Slic3r/App/Config/ConfigItemUtils.hpp"
#include "Slic3r/App/ExtruderIcon.hpp"
#include "Slic3r/App/Yoga/RectangleButton.hpp"

#include <fmt/format.h>

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

class ExtruderButton : public RectangleButton
{
public:
    explicit ExtruderButton(ToolRowControl::ExtruderClickedFn clicked_fn) : m_clicked_fn(clicked_fn)
    {
        set_orientation(Orientation::Vertical);
        set_background_color(Platform::Color::ButtonTransparent);
        m_icon = emplace_back<ExtruderIcon>();
        m_icon->set_width(24_fpx);
        m_icon->set_height(24_fpx);
        set_content_padding(4_fpx);
        set_width(28_fpx);
        set_height(28_fpx);
        set_content_align_items(YGAlignCenter);
        set_object_name("ExtruderButton");

        set_draggable(true);
        set_cursor(ImGuiMouseCursor_Hand);
    }

    ExtruderIcon* icon() const
    {
        return m_icon;
    }

    void set_tool_index(size_t tool_index)
    {
        m_tool_index = tool_index;
        set_tooltip(
            fmt::format(
                fmt::runtime(Biz::_u8L("Tool {}\nDrag tool to separate into different group.")),
                m_tool_index + 1
            )
        );
    }

protected:
    void action_internal() override
    {
        m_clicked_fn(m_tool_index);
    }

private:
    ExtruderIcon* m_icon{nullptr};
    ToolRowControl::ExtruderClickedFn m_clicked_fn;
    size_t m_tool_index{0};
};

ToolRowControl::ToolRowControl(
    size_t index,
    const ToolRowOverrideGroup& data,
    Biz::IConfigBoxSetter& cb_setter,
    ExtruderClickedFn clicked_fn,
    ExtruderDroppedFn dropped_fn,
    bool small
) :
    Biz::DataObserver<ToolRowOverrideGroup>(index, data),
    m_cb_setter(cb_setter),
    m_extruder_clicked_fn(clicked_fn),
    m_extruder_dropped_fn(dropped_fn),
    m_small(small)
{
    set_orientation(Orientation::Horizontal);
    set_align_items(YGAlign::YGAlignCenter);
    set_content_justify_content(YGJustifyFlexStart);
    set_width_percent(100);
    if (!m_small) {
        set_height(28_fpx);
    }
    set_padding(Paddings{m_small ? 0 : 10_fpx, 0, 0, 0});
    set_gap(5_fpx);
    set_allow_overlap(true);
    set_droppable(true);
    set_background_color(Platform::Color::Transparent);
    set_background_color_border(m_theme->color_imgui(Platform::Color::AccentSecondary));
    set_object_name("ToolRowControl");

    m_icon_container = emplace_back<Item>();

    on_index_update();
    on_data_update();
}

void ToolRowControl::on_data_update()
{
    std::vector<size_t> indexes;

    const ToolRowOverrides& overrides = m_state->first;
    indexes.reserve(overrides.size());
    std::ranges::transform(overrides, std::back_inserter(indexes), &ToolRowOverride::tool_index);

    if (m_small) {
        m_icon_container->set_min_width(28_fpx);
        m_icon_container->set_max_width(84_fpx);
        m_icon_container->set_flex_grow(1);
        m_icon_container->set_flex_shrink(1);
        m_icon_container->set_flex_wrap(YGWrap::YGWrapWrap);
    } else {
        m_icon_container->set_flex_grow(0);
        m_icon_container->set_flex_shrink(0);
        m_icon_container->set_width(28_fpx * m_state->second);
        m_icon_container->set_max_width(YGUndefined);
        m_icon_container->set_flex_wrap(YGWrap::YGWrapNoWrap);
    }

    if (m_last_indexes != indexes) {
        m_control_gui_type = Domain::ConfigItemDef::GUIType::undefined; // reset GUI
        m_last_indexes     = std::move(indexes);
        for (AbstractButton* icon : std::as_const(m_extruders)) {
            m_icon_container->remove_later(icon);
        }
        m_extruders.clear();
        for (size_t index : std::as_const(m_last_indexes)) {
            ExtruderButton* button =
                m_icon_container->emplace_back<ExtruderButton>(m_extruder_clicked_fn);
            m_extruders.emplace_back(button);
        }
    }

    for (size_t i = 0; i < m_extruders.size(); ++i) {
        ExtruderIcon* icon              = m_extruders.at(i)->icon();
        const ToolRowOverride* override = overrides.at(i);
        icon->set_extruder_index(override->tool_index + 1);

        ImColor color = override->color;
        if (override->extruder_candidate) {
            icon->set_enabled(true);
        } else {
            icon->set_enabled(false);
            color = m_theme->color_imgui(Platform::Color::Text, Platform::ColorGroup::Disabled);
        }
        icon->set_extruder_color(color);
        m_extruders.at(i)->set_tool_index(m_last_indexes.at(i));
        m_extruders.at(i)->set_dnd_payload(
            DnDPayload{
                .type{override->dnd_key()},
                .data{{"tool_index", size_t{(m_last_indexes.at(i))}}}
            }
        );
    }

    set_accepted_keys({overrides.front()->dnd_key()});

    const Domain::ConfigItem* config_item = overrides.front()->override_item;
    if (m_control_gui_type != config_item->def().gui_type) {
        // We got a new ConfigItem assigned with different GuiType
        if (m_input) {
            m_control_gui_type = Domain::ConfigItemDef::GUIType::undefined;
            remove(m_input);
            m_input   = nullptr;
            m_control = nullptr;
        }

        m_control_gui_type = config_item->def().gui_type;
        m_control          = ConfigItemControl::config_item_control_factory(
            this,
            1,
            m_index,
            *config_item,
            m_cb_setter,
            m_last_indexes
        );

        m_input = dynamic_cast<Yoga::Item*>(m_control);
        ASSERT(m_input, "ConfigItem needs to derive from Yoga::Item");

        m_input->set_flex_grow(1);
        if (m_small) {
            m_input->set_width(100_fpx);
            m_input->set_max_width(100_fpx);
        } else {
            m_input->set_width(125_fpx);
            m_input->set_max_width(125_fpx);
        }
        m_input->set_self_align(YGAlignCenter);
    }

    ASSERT(m_control);
    m_control->set_state(*overrides.front()->override_item);
}

void ToolRowControl::dnd_accepted_internal(const Yoga::DnDPayload& dnd_payload)
{
    m_extruder_dropped_fn(*dnd_payload.get<size_t>("tool_index"), m_index);
}

void ToolRowControl::dnd_could_accept_changed_internal(bool could_accept)
{
    set_background_border_width(could_accept ? 2 : 0);
}

} // namespace Slic3r::App
