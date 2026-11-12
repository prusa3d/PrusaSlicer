#include "Slic3r/App/Config/ConfigItemSpinBoxes.hpp"

#include "Slic3r/Biz/IConfigBoxSetter.hpp"

#include "Slic3r/App/Yoga/Validator.hpp"
#include "Slic3r/App/Config/ConfigItemUtils.hpp"
#include "Slic3r/App/Yoga/InputTextWithSpin.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

ConfigItemSpinBoxes::ConfigItemSpinBoxes(
    size_t index,
    const Domain::ConfigItem& data,
    Biz::IConfigBoxSetter& cb_setter,
    std::vector<size_t> cbi_index
) :
    ConfigItemControl(index, data, cb_setter, cbi_index)
{
    set_orientation(Orientation::Horizontal);
    set_gap(5);

    on_data_update();
}

void ConfigItemSpinBoxes::on_data_update()
{
    if (m_boxes.size() != m_state->get<std::vector<int>>().size()) {
        reconstruct_spin_buttons();
    } else {
        update_values();
    }
}

void ConfigItemSpinBoxes::reconstruct_spin_buttons()
{
    for (size_t child_index = 0; child_index < object_count(); ++child_index) {
        remove(get_item(0));
    }
    m_boxes.clear();

    size_t size = m_state->get<std::vector<int>>().size();
    m_boxes.reserve(size);

    const int min = static_cast<int>(m_state->def().min.value_or(std::numeric_limits<int>::min()));
    const int max = static_cast<int>(m_state->def().max.value_or(std::numeric_limits<int>::max()));

    const std::string tooltip = ConfigItemUtils::config_item_tooltip(*m_state);

    for (size_t index = 0; index < size; ++index) {
        Box& box = m_boxes.emplace_back();
        InputTextWithSpin* input =
            emplace_back<InputTextWithSpin>(std::make_unique<IntValidator>(min, max));
        box.spinbox         = input;
        box.value_validator = dynamic_cast<IntValidator*>(input->validator());

        input->callbacks().text_edited = [this, index]()
        {
            std::vector<int> data = m_state->get<std::vector<int>>();
            data[index]           = m_boxes.at(index).value_validator->value();
            set_item_value(Domain::ConfigValue{data});
        };
        input->set_tooltip(tooltip);
    }

    update_values();
}

void ConfigItemSpinBoxes::update_values()
{
    const std::vector<int>& data = m_state->get<std::vector<int>>();
    for (size_t i = 0; i < data.size(); ++i) {
        // Todo: this is not right, we spinbox should accept int
        m_boxes.at(i).spinbox->set_text(std::to_string(data.at(i)));
    }
}

} // namespace Slic3r::App
