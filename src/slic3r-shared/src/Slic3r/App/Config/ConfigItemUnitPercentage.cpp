#include "Slic3r/App/Config/ConfigItemUnitPercentage.hpp"

#include "Slic3r/Biz/IConfigBoxSetter.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include "Slic3r/App/Config/ConfigItemUtils.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Separator.hpp"
#include "Slic3r/App/Imgui/ImguiExtension.hpp"

#include <fmt/format.h>

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

ConfigItemUnitPercentage::ConfigItemUnitPercentage(
    size_t index,
    const Domain::ConfigItem& data,
    Biz::IConfigBoxSetter& cb_setter,
    std::vector<size_t> cbi_index
) :
    ConfigItemControl(index, data, cb_setter, cbi_index)
{
    m_tooltip->set_text_wrap(true);
    m_tooltip->content_item()->set_width(350);

    Rectangle* unit_rect = emplace_back<Rectangle>();
    unit_rect->set_fill(
        Imgui::adjust_brightness(m_theme->color_imgui(Platform::Color::WindowBgAlternate), 1.3)
    );
    unit_rect->set_flags(ImDrawFlags_RoundCornersRight);
    unit_rect->set_orientation(Orientation::Horizontal);
    m_unit1 = unit_rect->emplace_back<LayoutButton>("1");
    m_unit1->set_content_padding(4_fpx);
    m_unit1->label_object()->set_font_size(14_fpx);
    m_unit1->set_background_color(Platform::Color::ButtonTransparent);
    unit_rect->emplace_back<Separator>(Orientation::Vertical);
    m_unit2 = unit_rect->emplace_back<LayoutButton>("2");
    m_unit2->set_content_padding(4_fpx);
    m_unit2->label_object()->set_font_size(14_fpx);
    m_unit2->set_background_color(Platform::Color::ButtonTransparent);

    m_unit1->callbacks().action = [this]
    { set_item_value(Domain::ConfigValue{Domain::FloatOrPercentage{m_validator->value()}}); };
    m_unit2->callbacks().action = [this]
    {
        set_item_value(
            Domain::ConfigValue{Domain::FloatOrPercentage{Domain::Percentage{m_validator->value()}}}
        );
    };

    on_data_update();

    callbacks().text_edited = [this]()
    {
        std::optional<Domain::ConfigValue> value;
        const std::string value_text = text();
        if (m_validator->detected_unit() && *m_validator->detected_unit() == "%") {
            value = Domain::ConfigValue{
                Domain::FloatOrPercentage{Domain::Percentage{m_validator->value()}}
            };
        } else {
            value = Domain::ConfigValue{Domain::FloatOrPercentage{m_validator->value()}};
        }

        set_item_value(value.value());
    };
}

void ConfigItemUnitPercentage::on_data_update()
{
    if (m_last_item != m_state) {
        m_last_item = m_state;

        set_tooltip(ConfigItemUtils::config_item_tooltip(*m_state));
        m_unit1->set_label(Biz::_u8(m_state->def().units.front()));
        m_unit2->set_label(Biz::_u8(m_state->def().units.back()));

        if (*m_state->def().type == typeid(Domain::Percentage)
            || *m_state->def().type == typeid(Domain::FloatOrPercentage))
        {
            m_validator = std::make_unique<DoubleValidator>(
                m_state->def().min.value_or(std::numeric_limits<double>::lowest()),
                m_state->def().max.value_or(std::numeric_limits<double>::max())
            );
            m_validator->set_units(m_state->def().units);
            set_validator(m_validator.release());
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

void ConfigItemUnitPercentage::update_value(const Domain::ConfigValue& value)
{
    if (*m_state->def().type == typeid(Domain::FloatOrPercentage)) {
        Domain::FloatOrPercentage val = value.get<Domain::FloatOrPercentage>();
        set_text(
            val.is_percentage() ? fmt::format("{:.10g} %", val.percentage().value) :
                                  fmt::format("{:.10g}", val.float_value())
        );

        const bool unit2_detected =
            m_validator->detected_unit() && *m_validator->detected_unit() == "%";

        static const ImColor enabled_color = m_theme->color_imgui(Platform::Color::AccentSecondary);
        static const ImColor disabled_color = m_theme->color_imgui(Platform::Color::Text);

        m_unit1->set_label_color(!unit2_detected ? enabled_color : disabled_color);
        m_unit2->set_label_color(unit2_detected ? enabled_color : disabled_color);
    } else {
        PANIC("Item is used for unexpected parameter type");
    }
}

} // namespace Slic3r::App
