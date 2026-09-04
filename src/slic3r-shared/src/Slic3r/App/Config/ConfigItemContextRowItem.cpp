
#include "Slic3r/App/Config/ConfigItemContextRowItem.hpp"

#include "Slic3r/Biz/OverridableConfigBoxInteractor.hpp"
#include "Slic3r/Biz/IConfigBoxSetter.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include "Slic3r/App/Config/ConfigRowItem.hpp"
#include "Slic3r/App/Yoga/ToggleButton.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

ConfigItemContextRowItem::ConfigItemContextRowItem(
    size_t index,
    const Biz::ConfigItemContext& data,
    Biz::IConfigBoxSetter& cb_setter,
    size_t cbi_index
) :
    Biz::DataObserver<Biz::ConfigItemContext>(index, data),
    m_cb_setter(cb_setter),
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

void ConfigItemContextRowItem::navigate_to_item(const Domain::ConfigItem* config_item)
{
    m_config_row_item->navigate_to_item(config_item);
}

void ConfigItemContextRowItem::clear_navigation()
{
    m_config_row_item->clear_navigation();
}

void ConfigItemContextRowItem::on_data_update()
{
    m_config_row_item->set_state(*m_state->config_item);
}

} // namespace Slic3r::App
