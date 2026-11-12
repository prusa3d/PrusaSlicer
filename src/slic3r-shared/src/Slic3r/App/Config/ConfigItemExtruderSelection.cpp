#include "Slic3r/App/Config/ConfigItemExtruderSelection.hpp"

#include "Slic3r/App/Yoga/ComboBox.hpp"
#include "Slic3r/Biz/IConfigBoxSetter.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Domain/VirtualExtruder.hpp"

#include <algorithm>
#include <iterator>

using Slic3r::Domain::ConfigContainer;
using Slic3r::Domain::SelectionId;
using Slic3r::Domain::VirtualExtruder;
using Slic3r::Domain::VirtualExtruders;

namespace Slic3r::App {

ConfigItemExtruderSelection::ConfigItemExtruderSelection(
    size_t index,
    const Domain::ConfigItem& config_item,
    Biz::IConfigBoxSetter& cb_setter,
    std::vector<size_t> cbi_index
) :
    ConfigItemControl(index, config_item, cb_setter, cbi_index),
    ComboBox("ConfigItemCombo"),
    m_preset_changed_scope(project_interactor()->preset_interactor(), *this),
    m_virtual_extruders_changed_scope(project_interactor()->virtual_extruder_interactor(), *this)
{
    set_width(150);

    update_size();
    on_data_update();

    m_tooltip->set_text(tooltip_text());
    m_tooltip->content_item()->set_width(350);
    m_tooltip->set_text_wrap(true);

    callbacks().selection_changed = [this](int selected)
    {
        const int selected_index = current_index();
        if (selected_index >= 0 && static_cast<size_t>(selected_index) < m_extruder_ids.size()) {
            set_item_value(Domain::ConfigValue{m_extruder_ids[selected_index]});
            update_size();
        }
    };
}

void ConfigItemExtruderSelection::on_preset_selection_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    Biz::Preset::PresetItemType type
)
{
    if (project_interactor()->selected_project_id() == project_id
        && project_interactor()->selected_config_container_id() == config_container_id
        && type == Biz::Preset::PresetItemType::PrinterPreset)
    {
        this->update_size();
        this->on_data_update();
    }
}

void ConfigItemExtruderSelection::
    on_virtual_extruders_changed(SelectionId project_id, SelectionId config_container_id)
{
    if (project_interactor()->selected_project_id() != project_id
        || project_interactor()->selected_config_container_id() != config_container_id)
    {
        return;
    }

    this->update_size();
    this->on_data_update();
}

void ConfigItemExtruderSelection::
    on_config_container_selection_changed(SelectionId project_id, SelectionId config_container_id)
{
    if (project_interactor()->selected_project_id() != project_id) {
        return;
    }

    this->update_size();
    this->on_data_update();
}

void ConfigItemExtruderSelection::update_size()
{
    if (project_interactor()->selected_config_container_id() == Domain::INVALID_ID) {
        return;
    }

    const ConfigContainer& config_container   = project_interactor()->selected_config_container();
    const VirtualExtruders& virtual_extruders = config_container.virtual_extruders();
    const size_t slot_count = config_container.selected_preset().hw_config.material_slot_count();

    m_extruder_ids.clear();
    m_extruder_ids.reserve(slot_count + 1 + virtual_extruders.size());

    std::vector<std::string> new_items;
    new_items.reserve(slot_count + 1 + virtual_extruders.size());
    for (size_t i = 0; i <= slot_count; ++i) {
        if (i == 0) {
            new_items.push_back(Biz::_u8L("Default"));
        } else {
            new_items.push_back(std::to_string(i));
        }

        m_extruder_ids.push_back(static_cast<int>(i));
    }

    for (const VirtualExtruder& virtual_extruder : virtual_extruders) {
        new_items.push_back(std::to_string(virtual_extruder.id));
        m_extruder_ids.push_back(static_cast<int>(virtual_extruder.id));
    }

    int state_value = m_state->value().get<int>();
    ASSERT(state_value >= 0);
    if (!mixed() && (std::ranges::find(m_extruder_ids, state_value) == m_extruder_ids.end())) {
        new_items.push_back(std::to_string(state_value));
        set_items(new_items);
        set_current_index(static_cast<int>(m_extruder_ids.size()));
    } else {
        set_items(new_items);
    }
}

void ConfigItemExtruderSelection::on_data_update()
{
    if (mixed()) {
        set_override_label(Biz::_u8L("Mixed"));
        set_label_font_type(Render::ImguiFontType::Italic);
        return;
    }

    set_override_label(std::string());
    set_label_font_type(Render::ImguiFontType::Regular);
    if (!overriden().value_or(true)) {
        update_value(*m_cbi_container.get_override_original_value(*m_state, location_index()));
    } else {
        update_value(m_state->value());
    }
}

void ConfigItemExtruderSelection::update_value(const Domain::ConfigValue& value)
{
    const auto it = std::ranges::find(m_extruder_ids, value.get<int>());
    if (it != m_extruder_ids.end()) {
        set_current_index(static_cast<int>(std::distance(m_extruder_ids.begin(), it)));
        return;
    }

    // A value the selected printer group does not offer is shown as a last item.
    set_current_index(static_cast<int>(m_extruder_ids.size()));
}

} // namespace Slic3r::App
