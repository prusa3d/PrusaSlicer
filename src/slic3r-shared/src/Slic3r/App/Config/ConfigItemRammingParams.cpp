#include "Slic3r/App/Config/ConfigItemRammingParams.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include <Slic3r/App/AppServices.hpp>
#include "Slic3r/App/IDialogManager.hpp"

#include "Slic3r/Biz/IConfigBoxSetter.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

ConfigItemRammingParams::ConfigItemRammingParams(
    size_t index,
    const Domain::ConfigItem& data,
    Biz::IConfigBoxSetter& cb_setter,
    std::vector<size_t> cbi_index
) :
    ConfigItemControl(index, data, cb_setter, cbi_index),
    LayoutButton(Biz::_u8L("Ramming settings") + "...")
{
    set_background_color(Platform::Color::Button);
    m_tooltip->set_text_wrap(true);
    m_tooltip->content_item()->set_width(350);
    set_tooltip(tooltip_text());

    on_data_update();

    callbacks().action = [this]()
    {
        const std::string init_val = m_state->get<std::string>();
        const std::string ret =
            App::AppServices::instance().dialog_manager().show_ramming_dialog(init_val);
        if (ret != init_val) {
            set_item_value(Domain::ConfigValue{ret});
        }
    };
}

void ConfigItemRammingParams::on_data_update() {}

} // namespace Slic3r::App
