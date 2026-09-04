#include "Slic3r/App/Config/ConfigItemSpinBox.hpp"

#include "Slic3r/Biz/IConfigBoxSetter.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include "Slic3r/App/Yoga/Validator.hpp"
#include "Slic3r/App/Config/ConfigItemUtils.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

ConfigItemSpinBox::ConfigItemSpinBox(
    size_t index,
    const Domain::ConfigItem& data,
    Biz::IConfigBoxSetter& cb_setter,
    std::vector<size_t> cbi_index
) :
    ConfigItemControl(index, data, cb_setter, cbi_index),
    InputTextWithSpin(
        std::make_unique<IntValidator>(
            data.def().min.value_or(std::numeric_limits<int>::min()),
            data.def().max.value_or(std::numeric_limits<int>::max())
        )
    )
{
    set_width(80);
    m_value_validator = dynamic_cast<IntValidator*>(validator());

    callbacks().text_edited = [this]()
    {
        std::optional<Domain::ConfigValue> val;
        if (*m_state->def().type == typeid(int)) {
            val = Domain::ConfigValue{value()};
        } else if (*m_state->def().type == typeid(std::optional<int>)) {
            if (m_state->get<std::optional<int>>().has_value()) {
                val = Domain::ConfigValue{std::optional<int>(value())};
            }
        }

        if (val.has_value()) {
            set_item_value(val.value());
        }
    };

    set_tooltip(ConfigItemUtils::config_item_tooltip(*m_state));
    m_tooltip->set_text_wrap(true);
    m_tooltip->content_item()->set_width(350);

    on_data_update();
}

int ConfigItemSpinBox::value() const
{
    return m_value_validator->value();
}

void ConfigItemSpinBox::on_data_update()
{
    if (mixed()) {
        set_override_label(Biz::_u8L("Mixed"));
        set_font_type(Render::ImguiFontType::Italic);
        return;
    }
    m_value_validator->set_units(m_state->def().units);

    set_override_label({});
    set_font_type(Render::ImguiFontType::Regular);
    if (!overriden().value_or(true)) {
        update_value(*m_cbi_container.get_override_original_value(*m_state, location_index()));
    } else {
        update_value(m_state->value());
    }
}

void ConfigItemSpinBox::update_value(const Domain::ConfigValue& value)
{
    if (*m_state->def().type == typeid(int)) {
        set_text(std::to_string(value.get<int>()));
    } else if (*m_state->def().type == typeid(std::optional<int>)) {
        std::optional<int> val = value.get<std::optional<int>>();
        set_enabled(val.has_value());
        if (val.has_value()) {
            set_text(std::to_string(val.value()));
        }
    }
}

} // namespace Slic3r::App
