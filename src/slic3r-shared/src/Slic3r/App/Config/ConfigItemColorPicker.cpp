#include "Slic3r/App/Config/ConfigItemColorPicker.hpp"

#include "Slic3r/Biz/IConfigBoxSetter.hpp"
#include "Slic3r/Biz/Algorithms/Color.hpp"

#include "Slic3r/App/Config/ConfigItemUtils.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

ConfigItemColorPicker::ConfigItemColorPicker(
    size_t index,
    const Domain::ConfigItem& data,
    Biz::IConfigBoxSetter& cbi_container,
    std::vector<size_t> cbi_index
) :
    ConfigItemControl(index, data, cbi_container, cbi_index)
{
    m_tooltip->set_text_wrap(true);
    m_tooltip->content_item()->set_width(350);
    set_tooltip(ConfigItemUtils::config_item_tooltip(*m_state));

    on_data_update();

    callbacks().color_edited = [this](const ImColor& color)
    {
        set_item_value(
            Domain::ConfigValue{Biz::Algorithms::Color::encode_color(
                Domain::ColorRGB(color.Value.x, color.Value.y, color.Value.z)
            )}
        );
    };
}

void ConfigItemColorPicker::on_data_update()
{
    Domain::ColorRGB color;
    if (Biz::Algorithms::Color::decode_color(m_state->get<std::string>(), color)) {
        set_color(ImColor(color.r(), color.g(), color.b()));
    } else {
        // set default color
        set_color(IM_COL32_BLACK);
    }
}

} // namespace Slic3r::App
