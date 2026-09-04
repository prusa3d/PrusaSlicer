#include "Slic3r/App/Config/ConfigItemPreview.hpp"

#include "Slic3r/App/Config/ConfigItemUtils.hpp"
#include "Slic3r/App/Yoga/Rectangle.hpp"
#include "Slic3r/App/Yoga/ToggleButton.hpp"
#include "Slic3r/App/Yoga/Text.hpp"

#include "Slic3r/Biz/Algorithms/Color.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

ConfigItemPreview::ConfigItemPreview() {}

void ConfigItemPreview::set_data(
    const Domain::ConfigItem& data,
    const Domain::ConfigValue& value,
    bool mixed
)
{
    if (m_last_gui_type != data.def().gui_type) {
        if (m_input_text && !mixed) {
            remove(m_input_text);
            m_input_text = nullptr;
        }

        switch (m_last_gui_type) {
        case Domain::ConfigItemDef::GUIType::undefined:
            break;
        case Domain::ConfigItemDef::GUIType::color:
            remove(m_input_color);
            m_input_color = nullptr;
            break;
        case Domain::ConfigItemDef::GUIType::checkbox:
            remove(m_input_checkbox);
            m_input_checkbox = nullptr;
            break;
        case Domain::ConfigItemDef::GUIType::textfield:
        case Domain::ConfigItemDef::GUIType::spinbox:
        case Domain::ConfigItemDef::GUIType::f_enum_open:
        case Domain::ConfigItemDef::GUIType::i_enum_open:
        case Domain::ConfigItemDef::GUIType::s_enum_open:
        case Domain::ConfigItemDef::GUIType::extruder_selection:
        case Domain::ConfigItemDef::GUIType::combobox:
            // m_input_text is already removed
            break;
        default:
            PANIC("All gui types must be explicitly handled here, you apparently missed one.");
        }
        m_last_gui_type = data.def().gui_type;

        switch (data.def().gui_type) {
        case Domain::ConfigItemDef::GUIType::undefined:
            break;
        case Domain::ConfigItemDef::GUIType::color:
            m_input_color = emplace_back<Rectangle>();
            m_input_color->set_width(30);
            m_input_color->set_height(25);
            m_input_color->set_border_width(1);
            m_input_color->set_border_color(IM_COL32_WHITE);
            break;
        case Domain::ConfigItemDef::GUIType::checkbox:
            m_input_checkbox = emplace_back<ToggleButton>();
            m_input_checkbox->set_enabled(false);
            break;
        case Domain::ConfigItemDef::GUIType::unit_or_percentage:
        case Domain::ConfigItemDef::GUIType::textfield:
        case Domain::ConfigItemDef::GUIType::spinbox:
        case Domain::ConfigItemDef::GUIType::f_enum_open:
        case Domain::ConfigItemDef::GUIType::i_enum_open:
        case Domain::ConfigItemDef::GUIType::s_enum_open:
        case Domain::ConfigItemDef::GUIType::extruder_selection:
        case Domain::ConfigItemDef::GUIType::combobox: {
            if (!m_input_text) {
                m_input_text = emplace_back<Text>(std::string{});
                m_input_text->set_font_type(m_text_font_type);
            }
            break;
        }
        default:
            PANIC("All gui types must be explicitly handled here, you apparently missed one.");
        }
    }

    if (mixed) {
        if (!m_input_text) {
            m_input_text = emplace_back<Text>(std::string{});
            m_input_text->set_font_type(m_text_font_type);
        }
        m_input_text->set_text(Biz::_u8L("Mixed"));
    } else {
        switch (data.def().gui_type) {
        case Domain::ConfigItemDef::GUIType::undefined:
            break;
        case Domain::ConfigItemDef::GUIType::color: {
            Domain::ColorRGB color;
            if (Biz::Algorithms::Color::decode_color(value.get<std::string>(), color)) {
                m_input_color->set_fill(ImColor(color.r(), color.g(), color.b()));
            }
        } break;
        case Domain::ConfigItemDef::GUIType::checkbox:
            m_input_checkbox->set_checked(value.get<bool>());
            break;
        case Domain::ConfigItemDef::GUIType::unit_or_percentage:
        case Domain::ConfigItemDef::GUIType::textfield:
        case Domain::ConfigItemDef::GUIType::spinbox:
        case Domain::ConfigItemDef::GUIType::f_enum_open:
        case Domain::ConfigItemDef::GUIType::i_enum_open:
        case Domain::ConfigItemDef::GUIType::s_enum_open:
        case Domain::ConfigItemDef::GUIType::extruder_selection:
        case Domain::ConfigItemDef::GUIType::combobox:
            m_input_text->set_text(ConfigItemUtils::config_item_to_string(data, value));
            break;
        default:
            PANIC("All gui types must be explicitly handled here, you apparently missed one.");
        }
    }
    if (m_input_color)
        m_input_color->set_visible(!mixed);
    if (m_input_checkbox)
        m_input_checkbox->set_visible(!mixed);
}

void ConfigItemPreview::set_text_font_type(Render::ImguiFontType font)
{
    m_text_font_type = font;
}

} // namespace Slic3r::App
