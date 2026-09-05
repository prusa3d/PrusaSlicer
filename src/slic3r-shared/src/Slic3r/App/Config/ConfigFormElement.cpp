#include "Slic3r/App/Config/ConfigFormElement.hpp"

#include "Slic3r/Biz/ConfigBoxInteractor.hpp"
#include "Slic3r/Biz/IConfigBoxSetter.hpp"

namespace Slic3r::App {

ConfigFormElement::ConfigFormElement(const ConfigFormContext& context) : m_context(context) {}

const Domain::ConfigItem* ConfigFormElement::config_item(const std::string& key) const
{
    return m_context.cbi == nullptr ? nullptr : m_context.cbi->find_item(key);
}

bool ConfigFormElement::flag_of(const std::string& key, bool fallback) const
{
    const Domain::ConfigItem* item = config_item(key);
    if (item == nullptr || !item->holds_alternative<bool>())
        return fallback;
    return item->get<bool>();
}

void ConfigFormElement::set_flag(const std::string& key, bool value)
{
    const Domain::ConfigItem* item = config_item(key);
    if (item == nullptr || m_context.setter == nullptr)
        return;
    m_context.setter->set_item_value(*item, Domain::ConfigValue{value}, {m_context.cbi_index});
}

} // namespace Slic3r::App
