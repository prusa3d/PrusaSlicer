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

int ConfigFormElement::enum_of(const std::string& key, int fallback) const
{
    const Domain::ConfigItem* item = config_item(key);
    if (item == nullptr || !item->holds_alternative<Domain::EnumWrapper>())
        return fallback;
    return item->value().get<Domain::EnumWrapper>().value();
}

void ConfigFormElement::set_enum(const std::string& key, int value)
{
    const Domain::ConfigItem* item = config_item(key);
    if (item == nullptr || m_context.setter == nullptr
        || !item->holds_alternative<Domain::EnumWrapper>())
    {
        return;
    }
    // Rebuild from the current value so the enum's C++ type and its value
    // definitions carry over; the caller only knows the underlying integer.
    const Domain::EnumWrapper current = item->value().get<Domain::EnumWrapper>();
    const Domain::EnumWrapper next{value, current.type(), current.def()};
    m_context.setter->set_item_value(*item, Domain::ConfigValue{next}, {m_context.cbi_index});
}

double ConfigFormElement::percent_of(const std::string& key, double fallback) const
{
    const Domain::ConfigItem* item = config_item(key);
    if (item == nullptr || !item->holds_alternative<Domain::Percentage>())
        return fallback;
    return item->value().get<Domain::Percentage>().value;
}

void ConfigFormElement::set_percent(const std::string& key, double value)
{
    const Domain::ConfigItem* item = config_item(key);
    if (item == nullptr || m_context.setter == nullptr)
        return;
    m_context.setter->set_item_value(
        *item, Domain::ConfigValue{Domain::Percentage{value}}, {m_context.cbi_index}
    );
}

} // namespace Slic3r::App
