#include "Slic3r/App/Config/ConfigItemTextField.hpp"

#include "Slic3r/Biz/IConfigBoxSetter.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include "Slic3r/App/Config/ConfigItemUtils.hpp"

#include <boost/algorithm/string.hpp>
#include <imgui_internal.h>
#include <fmt/format.h>

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

ConfigItemTextField::ConfigItemTextField(
    size_t index,
    const Domain::ConfigItem& data,
    Biz::IConfigBoxSetter& cb_setter,
    std::vector<size_t> cbi_index
) :
    ConfigItemControl(index, data, cb_setter, cbi_index)
{
    m_tooltip->set_text_wrap(true);
    m_tooltip->content_item()->set_width(350);

    on_data_update();

    callbacks().text_edited = [this]()
    {
        std::optional<Domain::ConfigValue> value;
        if (*m_state->def().type == typeid(std::string)) {
            value = Domain::ConfigValue{text()};
        } else if (*m_state->def().type == typeid(std::vector<std::string>)) {
            std::vector<std::string> new_strings;
            boost::split(new_strings, text(), boost::is_any_of("\n"));
            std::erase_if(new_strings, [](const std::string& s) { return s.empty(); });
            value = Domain::ConfigValue{new_strings};
        } else if (*m_state->def().type == typeid(double)) {
            value = Domain::ConfigValue{m_double_validator->value()};
        } else if (*m_state->def().type == typeid(Domain::Percentage)) {
            value = Domain::ConfigValue{Domain::Percentage{m_double_validator->value()}};
        } else {
            PANIC("Item is used for unexpected parameter type");
        }
        if (value.has_value()) {
            set_item_value(value.value());
        }
    };
}

void ConfigItemTextField::on_data_update()
{
    if (m_last_item != m_state) {
        if (!m_is_multiline || m_is_multiline.value() != m_state->def().multiline) {
            if (m_state->def().multiline) {
                set_input_flags(input_flags() | ImGuiInputTextFlags_Multiline);
                set_resizable(true);
                input_text()->set_min_height(80);
            } else {
                set_input_flags(input_flags() & ~ImGuiInputTextFlags_Multiline);
                set_resizable(false);
                input_text()->set_height(YGUndefined);
                input_text()->set_min_height(0);
                input_text()->invalidate_min_size_calculation();
            }
            m_is_multiline = m_state->def().multiline;
        }

        if (!m_is_full_width || m_is_full_width.value() != m_state->def().full_width) {
            set_width(m_state->def().full_width ? YGUndefined : 80);
            m_is_full_width = m_state->def().full_width;
        }

        m_last_item = m_state;

        set_tooltip(ConfigItemUtils::config_item_tooltip(*m_state));

        if (*m_state->def().type == typeid(double)
            || *m_state->def().type == typeid(Domain::Percentage))
        {
            m_double_validator = std::make_unique<DoubleValidator>(
                m_state->def().min.value_or(std::numeric_limits<double>::lowest()),
                m_state->def().max.value_or(std::numeric_limits<double>::max())
            );
            m_double_validator->set_units(m_state->def().units);
            set_validator(m_double_validator.release());
        } else if (validator()) {
            // Remove the validator when validation is no longer required.
            set_validator(nullptr);
        }
    }

    if (mixed()) {
        set_override_label(Biz::_u8L("Mixed"));
        set_font_type(Render::ImguiFontType::Italic);
        return;
    }

    set_override_label(std::string());
    set_font_type(Render::ImguiFontType::Regular);
    if (!overriden().value_or(true)) {
        update_value(*m_cbi_container.get_override_original_value(*m_state, location_index()));
    } else {
        update_value(m_state->value());
    }
}

void ConfigItemTextField::update_value(const Domain::ConfigValue& value)
{
    if (*m_state->def().type == typeid(std::string)) {
        set_text(m_state->value().get<std::string>());
    } else if (*m_state->def().type == typeid(std::vector<std::string>)) {
        std::vector<std::string> old_strings = m_state->value().get<std::vector<std::string>>();
        std::string new_string{};
        for (const std::string& str : old_strings) {
            new_string += str + "\n";
        }
        if (!new_string.empty()) {
            new_string.pop_back();
        }
        set_text(new_string);
    } else if (*m_state->def().type == typeid(double)) {
        set_text(fmt::format("{:.10g}", m_state->value().get<double>()));
    } else if (*m_state->def().type == typeid(Domain::Percentage)) {
        set_text(fmt::format("{:.10g}", m_state->value().get<Domain::Percentage>().value));
    } else {
        PANIC("Item is used for unexpected parameter type");
    }
}

} // namespace Slic3r::App
