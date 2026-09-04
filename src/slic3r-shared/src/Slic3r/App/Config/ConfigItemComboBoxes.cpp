#include "Slic3r/App/Config/ConfigItemComboBoxes.hpp"

#include "Slic3r/Biz/IConfigBoxSetter.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include "Slic3r/App/Yoga/ComboBox.hpp"
#include "Slic3r/App/Config/ConfigItemUtils.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

ConfigItemComboBoxes::ConfigItemComboBoxes(
    size_t index,
    const Domain::ConfigItem& config_item,
    Biz::IConfigBoxSetter& cb_setter,
    std::vector<size_t> cbi_index
) :
    ConfigItemControl(index, config_item, cb_setter, cbi_index)
{
    set_width(150);
    set_orientation(Orientation::Horizontal);
    set_gap(5);

    on_data_update();
}

void ConfigItemComboBoxes::on_data_update()
{
    if (m_combo_boxes.size() != m_state->get<Domain::EnumVectorWrapper>().values().size()) {
        reconstruct_boxes();
    } else {
        update_values();
    }
}

void ConfigItemComboBoxes::reconstruct_boxes()
{
    for (size_t child_index = 0; child_index < object_count(); ++child_index) {
        remove(get_item(0));
    }
    m_combo_boxes.clear();

    const Domain::EnumVectorWrapper vector_wrapper = m_state->get<Domain::EnumVectorWrapper>();

    std::vector<std::string> items;
    items.reserve(vector_wrapper.def().size());
    std::transform(
        vector_wrapper.def().cbegin(),
        vector_wrapper.def().cend(),
        std::back_inserter(items),
        [](const Domain::EnumValueDef& value) { return Biz::_u8(value.str_ui); }
    );

    const std::string tooltip_value           = ConfigItemUtils::config_item_tooltip(*m_state);
    const std::vector<size_t> current_indexes = vector_wrapper.get_indexes();
    m_combo_boxes.reserve(current_indexes.size());
    for (int index : current_indexes) {
        ComboBox* combo = emplace_back<ComboBox>(items);
        m_combo_boxes.push_back(combo);
        combo->set_current_index(index);

        Tooltip& tooltip = combo->tooltip();
        tooltip.set_text(tooltip_value);
        tooltip.content_item()->set_width(350);
        tooltip.set_text_wrap(true);

        combo->callbacks().selection_changed = [this, combo](int index)
        {
            Domain::EnumVectorWrapper vector_wrapper = m_state->get<Domain::EnumVectorWrapper>();
            std::vector<size_t> current_indexes      = vector_wrapper.get_indexes();
            current_indexes[index_of(combo).value()] = index;

            vector_wrapper.set_indexes(current_indexes);

            set_item_value(Domain::ConfigValue{vector_wrapper});
        };
    }
}

void ConfigItemComboBoxes::update_values()
{
    const Domain::EnumVectorWrapper vector_wrapper = m_state->get<Domain::EnumVectorWrapper>();
    const std::vector<int>& current_indexes        = vector_wrapper.values();
    for (size_t i = 0; i < current_indexes.size(); ++i) {
        m_combo_boxes[i]->set_current_index(current_indexes.at(i));
    }
}
} // namespace Slic3r::App
