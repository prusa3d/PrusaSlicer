#include "Slic3r/App/Config/ConfigRowItem.hpp"

#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/ToggleButton.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Config/ConfigItemControl.hpp"
#include "Slic3r/App/Config/ConfigItemSpinBox.hpp"

#include "Slic3r/Biz/IConfigBoxSetter.hpp"
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
    m_input->set_enabled(enabled);
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

    const bool can_revert = m_enable_revert();
    m_revert_button->set_visible(can_revert);
    m_label->set_text_color(
        m_theme->color_imgui(can_revert ? Platform::Color::AccentTertiary : Platform::Color::Text)
    );
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
