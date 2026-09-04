#include "Slic3r/App/Config/ConfigItemControl.hpp"

#include "Slic3r/App/Config/ConfigItemTextField.hpp"
#include "Slic3r/App/Config/ConfigItemCheckBox.hpp"
#include "Slic3r/App/Config/ConfigItemCheckBoxes.hpp"
#include "Slic3r/App/Config/ConfigItemColorPicker.hpp"
#include "Slic3r/App/Config/ConfigItemPoints.hpp"
#include "Slic3r/App/Config/ConfigItemComboBox.hpp"
#include "Slic3r/App/Config/ConfigItemTextFields.hpp"
#include "Slic3r/App/Config/ConfigItemSpinBox.hpp"
#include "Slic3r/App/Config/ConfigItemBedShape.hpp"
#include "Slic3r/App/Config/ConfigItemFilePicker.hpp"
#include "Slic3r/App/Config/ConfigItemSpinBoxes.hpp"
#include "Slic3r/App/Config/ConfigItemComboBoxes.hpp"
#include "Slic3r/App/Config/ConfigItemSubstitutions.hpp"
#include "Slic3r/App/Config/ConfigItemExtruderSelection.hpp"
#include "Slic3r/App/Config/ConfigItemLanguageSelection.hpp"
#include "Slic3r/App/Config/ConfigItemRammingParams.hpp"
#include "Slic3r/App/Config/ConfigItemUnitPercentage.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include <fmt/format.h>

namespace Slic3r::App {

Biz::ProjectInteractor* ConfigItemControl::m_project_interactor = nullptr;

ConfigItemControl::ConfigItemControl(
    size_t index,
    const Domain::ConfigItem& data,
    Biz::IConfigBoxSetter& cb_setter,
    std::vector<size_t> cbi_index
) :
    Biz::DataObserver<Domain::ConfigItem>(index, data),
    m_cbi_container(cb_setter),
    m_cbi_index(cbi_index)
{}

std::string ConfigItemControl::tooltip_text() const
{
    const Domain::ConfigItemDef& def = m_state->def();
    std::string text = fmt::format("{}\n\nParameter name: {}", Biz::_u8(def.tooltip), def.name);

    std::optional<std::string> default_val = default_value();
    if (default_val.has_value()) {
        text += "\nDefault value: " + default_val.value();
    }

    if (def.min.has_value()) {
        text += fmt::format("\nMin: {:.10g}", def.min.value());
    }
    if (def.max.has_value()) {
        text += fmt::format("\nMax: {:.10g}", def.max.value());
    }

    return text;
}

Biz::ProjectInteractor* ConfigItemControl::project_interactor() const
{
    return m_project_interactor;
}

void ConfigItemControl::set_item_value(const Domain::ConfigValue& value)
{
    m_cbi_container.set_item_value(*m_state, value, m_cbi_index);
}

int ConfigItemControl::location_index() const
{
    return m_location_index;
}

void ConfigItemControl::set_location_index(int location_index)
{
    m_location_index = location_index;
}

void ConfigItemControl::set_project_interactor(Biz::ProjectInteractor* project_interactor)
{
    m_project_interactor = project_interactor;
}

std::optional<bool> ConfigItemControl::overriden() const
{
    return m_overriden;
}

void ConfigItemControl::set_overriden(std::optional<bool> overriden)
{
    m_overriden = overriden;
}

bool ConfigItemControl::mixed() const
{
    return m_mixed;
}

void ConfigItemControl::set_mixed(bool mixed)
{
    m_mixed = mixed;
}

std::optional<std::string> ConfigItemControl::default_value() const
{
    if (!m_state->def().init_fn) {
        return {};
    }

    Domain::ConfigValue value = m_state->def().init_fn();

    if (*m_state->def().type == typeid(std::string)) {
        return m_state->value().get<std::string>();
    } else if (*m_state->def().type == typeid(double)) {
        return fmt::format("{:.10g}", m_state->value().get<double>());
    } else if (*m_state->def().type == typeid(int)) {
        return std::to_string(m_state->value().get<int>());
    } else if (*m_state->def().type == typeid(bool)) {
        return m_state->value().get<bool>() ? "true" : "false";
    } else if (*m_state->def().type == typeid(Domain::Percentage)) {
        return fmt::format("{:.10g} %", m_state->value().get<Domain::Percentage>().value);
    } else if (*m_state->def().type == typeid(Domain::FloatOrPercentage)) {
        Domain::FloatOrPercentage value = m_state->value().get<Domain::FloatOrPercentage>();
        return value.is_percentage() ? fmt::format("{:.10g} %", value.percentage().value) :
                                       fmt::format("{:.10g}", value.float_value());
    } else if (*m_state->def().type == typeid(std::vector<double>)) {
        std::vector<double> values = m_state->get<std::vector<double>>();

        fmt::memory_buffer buffer;
        buffer.push_back('[');
        for (size_t i = 0; i < values.size(); ++i) {
            if (i > 0) {
                buffer.push_back(',');
            }

            fmt::format_to(std::back_inserter(buffer), "{:.10g}", values[i]);
        }
        buffer.push_back(']');

        return fmt::to_string(buffer);

    } else if (*m_state->def().type == typeid(Domain::EnumWrapper)) {
        const Domain::EnumWrapper enum_wrapper = m_state->get<Domain::EnumWrapper>();

        return enum_wrapper.def().at(enum_wrapper.index_of_value(enum_wrapper.value())).str_ui;
    }

    return {};
}

ConfigItemControl* ConfigItemControl::config_item_control_factory(
    Yoga::Item* container,
    size_t child_index,
    size_t data_index,
    const Domain::ConfigItem& item,
    Biz::IConfigBoxSetter& cb_setter,
    std::vector<size_t> cbi_index
)
{
    ConfigItemControl* item_control = nullptr;

    switch (item.def().gui_type) {
    case Slic3r::Domain::ConfigItemDef::GUIType::textfield:
        item_control =
            container
                ->emplace<ConfigItemTextField>(child_index, data_index, item, cb_setter, cbi_index);
        break;
    case Slic3r::Domain::ConfigItemDef::GUIType::textfields:
        item_control = container->emplace<ConfigItemTextFields>(
            child_index,
            data_index,
            item,
            cb_setter,
            cbi_index
        );
        break;
    case Slic3r::Domain::ConfigItemDef::GUIType::checkbox:
        item_control =
            container
                ->emplace<ConfigItemCheckBox>(child_index, data_index, item, cb_setter, cbi_index);
        break;
    case Slic3r::Domain::ConfigItemDef::GUIType::checkboxes:
        item_control = container->emplace<ConfigItemCheckBoxes>(
            child_index,
            data_index,
            item,
            cb_setter,
            cbi_index
        );
        break;
    case Slic3r::Domain::ConfigItemDef::GUIType::f_enum_open:
    case Slic3r::Domain::ConfigItemDef::GUIType::i_enum_open:
    case Slic3r::Domain::ConfigItemDef::GUIType::s_enum_open:
    case Slic3r::Domain::ConfigItemDef::GUIType::combobox:
        item_control =
            container
                ->emplace<ConfigItemComboBox>(child_index, data_index, item, cb_setter, cbi_index);
        break;
    case Slic3r::Domain::ConfigItemDef::GUIType::extruder_selection:
        item_control = container->emplace<ConfigItemExtruderSelection>(
            child_index,
            data_index,
            item,
            cb_setter,
            cbi_index
        );
        break;
    case Slic3r::Domain::ConfigItemDef::GUIType::comboboxes:
        item_control = container->emplace<ConfigItemComboBoxes>(
            child_index,
            data_index,
            item,
            cb_setter,
            cbi_index
        );
        break;
    case Slic3r::Domain::ConfigItemDef::GUIType::points:
        item_control =
            container
                ->emplace<ConfigItemPoints>(child_index, data_index, item, cb_setter, cbi_index);
        break;
    case Slic3r::Domain::ConfigItemDef::GUIType::color:
        item_control = container->emplace<ConfigItemColorPicker>(
            child_index,
            data_index,
            item,
            cb_setter,
            cbi_index
        );
        break;
    case Slic3r::Domain::ConfigItemDef::GUIType::spinbox:
        item_control =
            container
                ->emplace<ConfigItemSpinBox>(child_index, data_index, item, cb_setter, cbi_index);
        break;
    case Slic3r::Domain::ConfigItemDef::GUIType::file_picker:
        item_control = container->emplace<ConfigItemFilePicker>(
            child_index,
            data_index,
            item,
            cb_setter,
            cbi_index
        );
        break;
    case Slic3r::Domain::ConfigItemDef::GUIType::bed_shape:
        item_control =
            container
                ->emplace<ConfigItemBedShape>(child_index, data_index, item, cb_setter, cbi_index);
        break;
    case Slic3r::Domain::ConfigItemDef::GUIType::spinboxes:
        item_control =
            container
                ->emplace<ConfigItemSpinBoxes>(child_index, data_index, item, cb_setter, cbi_index);
        break;
    case Slic3r::Domain::ConfigItemDef::GUIType::substitutions:
        item_control = container->emplace<ConfigItemSubstitutions>(
            child_index,
            data_index,
            item,
            cb_setter,
            cbi_index
        );
        break;
    case Slic3r::Domain::ConfigItemDef::GUIType::ramming_params:
        item_control = container->emplace<ConfigItemRammingParams>(
            child_index,
            data_index,
            item,
            cb_setter,
            cbi_index
        );
        break;
    case Slic3r::Domain::ConfigItemDef::GUIType::language_selection:
        item_control = container->emplace<ConfigItemLanguageSelection>(
            child_index,
            data_index,
            item,
            cb_setter,
            cbi_index
        );
        break;
    case Slic3r::Domain::ConfigItemDef::GUIType::unit_or_percentage:
        item_control = container->emplace<ConfigItemUnitPercentage>(
            child_index,
            data_index,
            item,
            cb_setter,
            cbi_index
        );
        break;
    default:
        PANIC("Unhandled GUIType", item.def().gui_type);
    }

    return item_control;
}

} // namespace Slic3r::App
