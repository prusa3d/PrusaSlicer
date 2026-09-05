#include "Slic3r/App/Config/ConfigRowItem.hpp"

#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/ToggleButton.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Config/ConfigItemControl.hpp"
#include "Slic3r/App/Config/ConfigItemSpinBox.hpp"

#include "Slic3r/Biz/IConfigBoxSetter.hpp"
#include "Slic3r/Domain/ConfigItemPredicate.hpp"
#include <Slic3r/Biz/I18N/I18N.hpp>

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

ConfigRowItem::ConfigRowItem(
    size_t index,
    const Domain::ConfigItem& data,
    Biz::IConfigBoxSetter& cb_setter,
    FnEnableRevert enable_revert_fn,
    size_t cbi_index,
    std::optional<std::string> force_label
) :
    Biz::DataObserver<Domain::ConfigItem>(index, data),
    m_cb_setter(cb_setter),
    m_cbi_index(cbi_index),
    m_force_label(force_label),
    m_enable_revert(enable_revert_fn)
{
    set_object_name("ConfigRowItem");
    set_fill(m_theme->color_imgui(Platform::Color::Transparent));
    set_border_width(2);
    set_border_color(m_theme->color_imgui(Platform::Color::Transparent));
    set_gap(5.f);

    m_left_side = emplace_back<Item>();

    m_label = m_left_side->emplace_back<Text>(m_force_label.value_or(Biz::_u8(data.def().label)));
    m_label->set_height(40);
    m_label->set_wrap_mode(Text::WrapMode::WrapElide);
    m_label->set_align({AlignH::Left, AlignV::Center});

    m_revert_button = m_left_side->emplace_back<LayoutButton>(
        std::string{},
        Render::Icon::UndoGizmo,
        Biz::_u8L("Revert to the initial profile value")
    );
    m_revert_button->set_background_color(Platform::Color::ButtonTransparent);
    m_revert_button->set_icon_tint(m_theme->color_imgui(Platform::Color::AccentTertiary));
    m_revert_button->set_self_align(YGAlignCenter);
    m_revert_button->set_content_padding(3);
    m_revert_button->set_width(20);
    m_revert_button->set_height(20);
    m_revert_button->set_flex_shrink(0.f);
    m_revert_button->callbacks().action = [this]()
    { m_cb_setter.set_from_original_value(*m_state, m_cbi_index); };

    m_label->set_width(m_force_label.has_value() ? 90 : 175);
    m_left_side->set_max_width(175);

    on_data_update();
}

void ConfigRowItem::set_label_text_color(const ImColor& color)
{
    m_label->set_text_color(color);
}

void ConfigRowItem::set_enabled_control(bool enabled)
{
    m_enabled_by_caller = enabled;
    apply_enabled_state();
}

void ConfigRowItem::apply_enabled_state()
{
    if (m_input)
        m_input->set_enabled(m_enabled_by_caller && m_enabled_by_dependency);
}

void ConfigRowItem::refresh_dependency_state()
{
    const Domain::ConfigItemLookup* lookup = m_cb_setter.item_lookup();
    if (lookup == nullptr || m_state == nullptr)
        return;

    const Domain::ConfigItemDef& def = m_state->def();
    const Domain::ConfigItemRequirement* unmet =
        Domain::first_unmet(def.requirements, *lookup);
    const bool applies = unmet == nullptr && Domain::evaluate(def.enable_if, *lookup);

    const std::string reason = unmet == nullptr ? std::string{} : Biz::_u8(unmet->reason);
    if (applies == m_enabled_by_dependency && reason == m_shown_reason)
        return;

    m_enabled_by_dependency = applies;
    m_shown_reason          = reason;
    apply_enabled_state();
    apply_label_color();
    apply_reason_text();
}

void ConfigRowItem::apply_reason_text()
{
    if (m_shown_reason.empty()) {
        if (m_reason != nullptr)
            m_reason->set_visible(false);
        return;
    }

    // Created on first need: most settings have no requirements, and an always
    // present empty label would cost a node on every row in the form.
    //
    // Placed after the input, in the row's existing horizontal flow, rather
    // than below it. Putting it below would mean making the row vertical and
    // nesting the label and input in a sub-row — a restructure of the one item
    // every settings row is built from, which is not worth doing sight unseen.
    // Only settings that declare requirements grow this text at all.
    if (m_reason == nullptr) {
        m_reason = emplace_back<Text>(m_shown_reason);
        m_reason->set_wrap_mode(Text::WrapMode::WrapElide);
        m_reason->set_flex_shrink(1.f);
        m_reason->set_max_width(260);
        m_reason->set_align({AlignH::Left, AlignV::Center});
        m_reason->set_text_color(
            m_theme->color_imgui(Platform::Color::Text, Platform::ColorGroup::Disabled)
        );
    } else {
        m_reason->set_text(m_shown_reason);
    }
    m_reason->set_visible(true);
}

void ConfigRowItem::apply_label_color()
{
    // Dim the label alongside the input. Greying only the input reads as "this
    // control is busy"; greying the pair reads as "this setting is not in play
    // right now", which is what the rule actually means. A dependency that does
    // not hold outranks the modified-value highlight: the value is still
    // modified, but saying so is noise while the setting has no effect.
    if (!m_enabled_by_dependency) {
        m_label->set_text_color(
            m_theme->color_imgui(Platform::Color::Text, Platform::ColorGroup::Disabled)
        );
        return;
    }
    m_label->set_text_color(m_theme->color_imgui(
        m_can_revert ? Platform::Color::AccentTertiary : Platform::Color::Text
    ));
}

void ConfigRowItem::render(const Yoga::Vec2f& pos, const Yoga::Vec2f& size)
{
    refresh_dependency_state();
    Rectangle::render(pos, size);
}

void ConfigRowItem::on_data_update()
{
    if (m_created_gui_type != m_state->def().gui_type) {
        // We got a new ConfigItem assigned with different GuiType
        m_created_gui_type = m_state->def().gui_type;

        if (m_input) {
            remove(m_input);
        }

        m_control = ConfigItemControl::config_item_control_factory(
            this,
            1,
            m_index,
            *m_state,
            m_cb_setter,
            {m_cbi_index}
        );

        m_input = dynamic_cast<Yoga::Item*>(m_control);
        ASSERT(m_input, "ConfigItem needs to derive from Yoga::Item");
        m_input->set_flex_grow(1);
        m_input->set_max_width(200_fpx);
        m_input->set_width(150_fpx);

        if (m_state->def().gui_type == Slic3r::Domain::ConfigItemDef::GUIType::spinbox) {
            m_config_item_spin_box = dynamic_cast<ConfigItemSpinBox*>(m_input);
        }
    }

    if (m_input && (!m_last_full_width || m_last_full_width.value() != m_state->def().full_width)) {
        if (m_state->def().full_width) {
            set_orientation(Orientation::Vertical);
            set_align_items(YGAlign::YGAlignStretch);
            m_input->set_width(YGUndefined);
            m_input->set_max_width(YGUndefined);
        } else {
            set_orientation(Orientation::Horizontal);
            set_align_items(YGAlign::YGAlignCenter);
            m_input->set_width(150_fpx);
            m_input->set_max_width(200_fpx);
        }
        m_last_full_width = m_state->def().full_width;
    }

    if (m_state->def().type != m_created_value_type) {
        // We got a new ConfigItem assigned with different value type
        m_created_value_type = m_state->def().type;
        if (*m_state->def().type == typeid(std::optional<int>)) {
            m_toggle_enable = m_left_side->emplace_back<ToggleButton>();
            m_toggle_enable->set_margin(Margins(0, 0, 5, 0));

            std::optional<int> value = m_state->value().get<std::optional<int>>();
            m_toggle_enable->set_checked(value.has_value());
            m_toggle_enable->callbacks().action = [this]()
            {
                // We are using action to make sure this callbacks comes from user
                std::optional<int> value;
                if (m_toggle_enable->checked()) {
                    value = m_config_item_spin_box->value();
                }

                m_cb_setter.set_item_value(*m_state, Domain::ConfigValue{value}, {m_cbi_index});
            };
        } else {
            if (m_toggle_enable) {
                m_left_side->remove_later(m_toggle_enable);
                m_toggle_enable = nullptr;
            }
        }
    }

    m_label->set_text(m_force_label.value_or(Biz::_u8(m_state->def().label)));

    if (*m_state->def().type == typeid(std::optional<int>)) {
        std::optional<int> value = m_state->value().get<std::optional<int>>();
        m_toggle_enable->set_checked(value.has_value());
    }

    m_control->set_state(*m_state);

    m_can_revert = m_enable_revert();
    m_revert_button->set_visible(m_can_revert);
    apply_enabled_state();
    apply_label_color();
}

void ConfigRowItem::navigate_to_item(const Domain::ConfigItem* config_item)
{
    if (m_state == config_item) {
        set_border_color(m_theme->color_imgui(Platform::Color::AccentTertiary));
    } else {
        set_border_color(m_theme->color_imgui(Platform::Color::Transparent));
    }
}

void ConfigRowItem::clear_navigation()
{
    set_border_color(IM_COL32_BLACK_TRANS);
}

} // namespace Slic3r::App
