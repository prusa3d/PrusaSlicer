#include "Slic3r/App/Config/ConfigItemComboBox.hpp"

#include "Slic3r/Biz/IConfigBoxSetter.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include "Slic3r/App/Config/ConfigItemUtils.hpp"

#include <fmt/format.h>

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

ConfigItemComboBox::ConfigItemComboBox(
    size_t index,
    const Domain::ConfigItem& config_item,
    Biz::IConfigBoxSetter& cb_setter,
    std::vector<size_t> cbi_index
) :
    ConfigItemControl(index, config_item, cb_setter, cbi_index),
    ComboBox("ConfigItemCombo")
{
    const Domain::ConfigItemDef::GUIType gui_type = m_state->def().gui_type;
    set_editable(
        gui_type == Domain::ConfigItemDef::GUIType::f_enum_open
        || gui_type == Domain::ConfigItemDef::GUIType::i_enum_open
        || gui_type == Domain::ConfigItemDef::GUIType::s_enum_open
    );

    set_width(150);

    on_data_update();

    m_tooltip->set_text(ConfigItemUtils::config_item_tooltip(*m_state));
    m_tooltip->content_item()->set_width(350);
    m_tooltip->set_text_wrap(true);

    // TODO: The callbacks are disgusting, clean them up

    callbacks().selection_changed = [this](int selected)
    {
        std::optional<Domain::ConfigValue> value;
        const Domain::ConfigItemDef::GUIType gui_type = m_state->def().gui_type;
        if (gui_type == Domain::ConfigItemDef::GUIType::f_enum_open) {
            if (*m_state->def().type == typeid(double)) {
                value = Domain::ConfigValue{
                    std::get<double>(m_state->def().choices.at(current_index()).first)
                };
            } else if (*m_state->def().type == typeid(Domain::Percentage)) {
                value = Domain::ConfigValue{Domain::Percentage{
                    std::get<double>(m_state->def().choices.at(current_index()).first)
                }};
            } else if (*m_state->def().type == typeid(Domain::FloatOrPercentage)) {
                // Assume it is always Float :((
                value = Domain::ConfigValue{Domain::FloatOrPercentage{
                    std::get<double>(m_state->def().choices.at(current_index()).first)
                }};
            }
        } else if (gui_type == Domain::ConfigItemDef::GUIType::i_enum_open) {
            value = Domain::ConfigValue{
                std::get<int>(m_state->def().choices.at(current_index()).first)
            };
        } else if (gui_type == Domain::ConfigItemDef::GUIType::s_enum_open) {
            value = Domain::ConfigValue{
                std::get<std::string>(m_state->def().choices.at(current_index()).first)
            };
        } else if (*m_state->def().type == typeid(Domain::EnumWrapper)) {
            Domain::EnumWrapper values = m_state->get<Domain::EnumWrapper>();
            values.set_string(values.def().at(static_cast<size_t>(selected)).str_serialized);
            value = Domain::ConfigValue{values};
        }

        if (value.has_value()) {
            set_item_value(value.value());
        }
    };

    callbacks().text_edited = [this]()
    {
        std::optional<Domain::ConfigValue> value;
        const Domain::ConfigItemDef::GUIType gui_type = m_state->def().gui_type;
        // TODO: Unify this with ConfigItemTextField
        if (gui_type == Domain::ConfigItemDef::GUIType::f_enum_open) {
            if (*m_state->def().type == typeid(double)) {
                value = Domain::ConfigValue{m_double_validator->value()};
            } else if (*m_state->def().type == typeid(Domain::Percentage)) {
                value = Domain::ConfigValue{Domain::Percentage{m_double_validator->value()}};
            } else if (*m_state->def().type == typeid(Domain::FloatOrPercentage)) {
                if (m_double_validator->detected_unit()
                    && *m_double_validator->detected_unit() == "%")
                {
                    value = Domain::ConfigValue{
                        Domain::FloatOrPercentage{Domain::Percentage{m_double_validator->value()}}
                    };
                } else {
                    value =
                        Domain::ConfigValue{Domain::FloatOrPercentage{m_double_validator->value()}};
                }
            }
        } else if (gui_type == Domain::ConfigItemDef::GUIType::i_enum_open) {
            value = Domain::ConfigValue{m_int_validator->value()};
        } else if (gui_type == Domain::ConfigItemDef::GUIType::s_enum_open) {
            value = Domain::ConfigValue{current_label()};
        }

        if (value.has_value()) {
            set_item_value(value.value());
        }
    };
}

void ConfigItemComboBox::on_data_update()
{
    if (m_last_item != m_state) {
        m_init      = false;
        m_last_item = m_state;
    }

    // TODO: the validators gets constantly recreated, clean this up
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

void ConfigItemComboBox::update_value(const Domain::ConfigValue& value)
{
    if (!m_init) {
        initialize();
        m_init = true;
    }

    std::vector<std::string> items;
    const Domain::ConfigItemDef::GUIType gui_type = m_state->def().gui_type;
    if (gui_type == Domain::ConfigItemDef::GUIType::f_enum_open) {
        if (*m_state->def().type == typeid(Domain::Percentage)) {
            set_current_label(fmt::format("{:.10g}", value.get<Domain::Percentage>().value));
        } else if (*m_state->def().type == typeid(Domain::FloatOrPercentage)) {
            Domain::FloatOrPercentage val = value.get<Domain::FloatOrPercentage>();
            set_current_label(
                val.is_percentage() ? fmt::format("{:.10g} %", val.percentage().value) :
                                      fmt::format("{:.10g}", val.float_value())
            );

        } else if (*m_state->def().type == typeid(double)) {
            set_current_label(fmt::format("{:.10g}", value.get<double>()));
        }

    } else if (gui_type == Domain::ConfigItemDef::GUIType::i_enum_open) {
        set_current_label(std::to_string(value.get<int>()));
    } else if (gui_type == Domain::ConfigItemDef::GUIType::s_enum_open) {
        set_current_label(value.get<std::string>());
    } else if (*m_state->def().type == typeid(Domain::EnumWrapper)) {
        const Domain::EnumWrapper enum_wrapper = value.get<Domain::EnumWrapper>();
        set_current_index(enum_wrapper.index_of_value(enum_wrapper.value()));
    }
}

void ConfigItemComboBox::initialize()
{
    ASSERT(!m_init);

    auto i18n_str = [](const std::string& s, const std::string& context)
    { return context.empty() ? Biz::_u8(s) : Biz::_ctx_u8(s, context); };

    std::vector<std::string> items;
    const Domain::ConfigItemDef::GUIType gui_type = m_state->def().gui_type;
    const std::string& i18n_context               = m_state->def().i18n_context;
    if (gui_type == Domain::ConfigItemDef::GUIType::f_enum_open) {
        set_editable(true);

        m_double_validator = std::make_unique<DoubleValidator>();
        m_double_validator->set_units(m_state->def().units);
        set_validator(m_double_validator.release());

        for (const auto& choice : m_state->def().choices) {
            items.push_back(choice.second);
        }
    } else if (gui_type == Domain::ConfigItemDef::GUIType::i_enum_open) {
        set_editable(true);
        m_int_validator = std::make_unique<IntValidator>();
        m_int_validator->set_units(m_state->def().units);
        set_validator(m_int_validator.release());

        for (const auto& [v, str_ui] : m_state->def().choices) {
            items.push_back(i18n_str(str_ui, i18n_context));
        }

    } else if (gui_type == Domain::ConfigItemDef::GUIType::s_enum_open) {
        ASSERT(
            *m_state->def().type == typeid(std::string),
            "We only support std::string<->s_enum_open for now"
        );

        set_editable(true);

        for (const auto& [v, str_ui] : m_state->def().choices) {
            items.push_back(i18n_str(str_ui, i18n_context));
        }
    } else if (*m_state->def().type == typeid(Domain::EnumWrapper)) {
        set_editable(false);
        const Domain::EnumWrapper enum_wrapper = m_state->get<Domain::EnumWrapper>();

        for (const Domain::EnumValueDef& value : enum_wrapper.def()) {
            items.push_back(i18n_str(value.str_ui, i18n_context));
        }
    }

    set_items(items);
}

} // namespace Slic3r::App
