#include "Slic3r/App/Config/OverridableConfigRowItem.hpp"

#include "Slic3r/Biz/OverridableConfigBoxInteractor.hpp"
#include "Slic3r/Biz/IConfigBoxSetter.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include "Slic3r/App/Config/ConfigRowItem.hpp"
#include "Slic3r/App/Yoga/ToggleButton.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

OverridableConfigRowItem::OverridableConfigRowItem(
    size_t index,
    const Biz::OverrideItem& data,
    Biz::IConfigBoxSetter& cb_setter,
    Biz::OverridableConfigBoxInteractor& cbi,
    size_t cbi_index
) :
    Biz::DataObserver<Biz::OverrideItem>(index, data),
    m_cb_setter(cb_setter),
    m_cbi(cbi),
    m_cbi_index(cbi_index)
{
    set_orientation(Orientation::Horizontal);
    set_gap(5);

    m_config_row_item = emplace_back<ConfigRowItem>(
        0,
        *data.config_item,
        cb_setter,
        [this]() ->bool {return m_state->is_dirty(); },
        cbi_index
    );
    m_config_row_item->set_flex_grow(1);

    on_data_update();
}

void OverridableConfigRowItem::navigate_to_item(const Domain::ConfigItem* config_item)
{
    m_config_row_item->navigate_to_item(config_item);
}

void OverridableConfigRowItem::clear_navigation()
{
    m_config_row_item->clear_navigation();
}

void OverridableConfigRowItem::on_data_update()
{
    if (m_last_is_override != m_state->is_override()) {
        m_last_is_override = m_state->is_override();

        if (m_last_is_override) {
            m_override_toggle_button = emplace<ToggleButton>(0);

            m_override_toggle_button->callbacks().action = [this]
            {
                m_cb_setter.set_item_override(
                    *m_state->config_item,
                    m_override_toggle_button->checked(),
                    m_cbi_index
                );
            };
        } else {
            remove(m_override_toggle_button);
            m_override_toggle_button = nullptr;
            m_config_row_item->set_enabled_control(true);
        }
    }

    if (m_override_toggle_button) {
        m_override_toggle_button->set_checked(m_state->overriden.value());
        m_override_toggle_button->set_tooltip(
            m_state->overriden.value() ? Biz::_u8L("Disable override") :
                                         Biz::_u8L("Enable override")
        );
        m_config_row_item->set_enabled_control(m_state->overriden.value());
    }
    m_config_row_item->set_state(*m_state->config_item);
}

} // namespace Slic3r::App
