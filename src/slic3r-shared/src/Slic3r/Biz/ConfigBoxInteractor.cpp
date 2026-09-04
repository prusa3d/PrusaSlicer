#include "Slic3r/Biz/ConfigBoxInteractor.hpp"

namespace Slic3r::Biz {

ConfigBoxInteractor::ConfigBoxInteractor() :
    m_config_box_list(std::make_shared<ConfigBoxObservableList>())
{}

ConfigBoxInteractor::ConfigBoxInteractor(SetAccessor& set_accessor) :
    ConfigBoxInteractor()
{
    set_accessor.set_source(m_config_box_list.get());
}

const Domain::ConfigValue* ConfigBoxInteractor::find(const std::string& name) const
{
    return m_config_box_list->find(name);
}

std::weak_ptr<ConfigBoxObservableList> ConfigBoxInteractor::config_box_list()
{
    return m_config_box_list.get();
}

std::weak_ptr<const ConfigBoxObservableList> ConfigBoxInteractor::config_box_list() const
{
    return m_config_box_list.get();
}

void ConfigBoxInteractor::SetAccessor::set_source(
    std::weak_ptr<ConfigBoxObservableList> config_box_list
)
{
    m_config_box_list           = config_box_list;
}

void ConfigBoxInteractor::SetAccessor::set_value(
    const std::string& key,
    const Domain::ConfigValue& value
)
{
    m_config_box_list.lock()->set_value(key, value);
}

void ConfigBoxInteractor::SetAccessor::set_config_box(
    Domain::ConfigBox* config_box,
    const Domain::ConfigBox* original_config_box
)
{
    m_config_box_list.lock()->set_config_box(config_box, original_config_box);
}

void ConfigBoxInteractor::SetAccessor::set_from_original_value(const std::string& key)
{
    m_config_box_list.lock()->set_from_original_value(key);
}

} // namespace Slic3r::Biz
