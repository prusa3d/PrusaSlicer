#include "Slic3r/App/AppConfigInteractor.hpp"

namespace Slic3r::App {

AppConfigInteractor::AppConfigInteractor(Domain::ConfigBox* app_config_box)
{
    m_app_config_cbi = Biz::ConfigBoxInteractor(m_cbi_accessor);
    m_cbi_accessor.set_config_box(app_config_box);
}

Biz::ConfigBoxInteractor& AppConfigInteractor::app_config_cbi()
{
    return m_app_config_cbi;
}

const Domain::ConfigValue*
AppConfigInteractor::get_override_original_value(const Domain::ConfigItem& item, size_t index) const
{
    return m_app_config_cbi.find(item.name());
}

void AppConfigInteractor::set_item_value(
    const Domain::ConfigItem& item,
    const Domain::ConfigValue& value,
    const std::vector<size_t>& indexes
)
{
    set_item_value(item.name(), value);
}

void AppConfigInteractor::set_item_value(
    const std::string& item_name,
    const Domain::ConfigValue& value
)
{
    m_cbi_accessor.set_value(item_name, value);

    invoke_listeners<IAppConfigChangedListener>([&](auto* l)
                                                { l->on_app_config_changed(item_name); });
}

void AppConfigInteractor::toggle_favorite_param(const std::string& param)
{
    std::vector<std::string> favorites =
        m_app_config_cbi.find("favorite_params")->get<std::vector<std::string>>();

    if (std::erase(favorites, param) == 0) {
        favorites.push_back(param);
    }
    m_cbi_accessor.set_value("favorite_params", Domain::ConfigValue(favorites));
}

} // namespace Slic3r::App
