#include "Slic3r/Biz/PrintToolConfigBoxInteractor.hpp"

#include "Slic3r/Biz/PrintToolConfigObservableList.hpp"

namespace Slic3r::Biz {

PrintToolConfigBoxInteractor::PrintToolConfigBoxInteractor(
    const Domain::Workbench& workbench,
    Scene::SceneInteractor& scene_interactor,
    SetAccessor& set_accessor
) :
    m_observable_list(std::make_shared<PrintToolConfigObservableList>(workbench, scene_interactor))
{
    set_accessor.set_source(m_observable_list.get());
}

const Domain::ConfigValue* PrintToolConfigBoxInteractor::find_print_value(
    const std::string& name
) const
{
    return m_observable_list->find_print_value(name);
}

const Domain::ConfigValue*
PrintToolConfigBoxInteractor::find_tool_value(const std::string& name, size_t index) const
{
    return m_observable_list->find_tool_value(name, index);
}

std::weak_ptr<PrintToolConfigObservableList> PrintToolConfigBoxInteractor::observable_list()
{
    return m_observable_list.get();
}

std::weak_ptr<const PrintToolConfigObservableList>
PrintToolConfigBoxInteractor::observable_list() const
{
    return m_observable_list.get();
}

void PrintToolConfigBoxInteractor::set_app_config_cbol(
    std::weak_ptr<ConfigBoxObservableList> app_config_cbol
)
{
    m_app_config_cbol = app_config_cbol;
    m_app_config_cbol.lock()->add_listener<IListObserver<Biz::ConfigItemContext>>(this);
    update_favorites();
}

void PrintToolConfigBoxInteractor::on_reset()
{
    update_favorites();
}

void PrintToolConfigBoxInteractor::on_updated(const IndexRange&)
{
    update_favorites();
}

void PrintToolConfigBoxInteractor::update_favorites()
{
    // Get AppConfig item
    const Domain::ConfigValue* favorites = m_app_config_cbol.lock()->find("favorite_params");
    ASSERT(favorites);

    m_observable_list->set_favorites(favorites->get<std::vector<std::string>>());
}

void PrintToolConfigBoxInteractor::SetAccessor::set_source(
    std::weak_ptr<PrintToolConfigObservableList> observable_list
)
{
    m_observable_list = observable_list;
}

void PrintToolConfigBoxInteractor::SetAccessor::set_print_value(
    const std::string& key,
    const Domain::ConfigValue& value
)
{
    m_observable_list.lock()->set_print_value(key, value);
}

void PrintToolConfigBoxInteractor::SetAccessor::set_tool_override(
    const std::string& key,
    size_t index,
    bool override
)
{
    m_observable_list.lock()->set_tool_override(key, index, override);
}

void PrintToolConfigBoxInteractor::SetAccessor::set_tool_value(
    const std::string& key,
    const std::vector<size_t>& indexes,
    const Domain::ConfigValue& value
)
{
    m_observable_list.lock()->set_tool_value(key, indexes, value);
}

void PrintToolConfigBoxInteractor::SetAccessor::set_sources(
    const Domain::SelectionId selected_project_id,
    const Domain::SelectionId selected_container_id,
    Domain::Preset::SelectedPreset& selected_preset,
    const std::vector<Domain::ConfigBox*>& tool_config_boxes,
    const Domain::ConfigBox* original_print_config_box,
    const std::vector<const Domain::ConfigBox*>& original_tool_config_boxes
)
{
    m_observable_list.lock()->set_sources(
        selected_project_id,
        selected_container_id,
        selected_preset,
        tool_config_boxes,
        original_print_config_box,
        original_tool_config_boxes
    );
}

void PrintToolConfigBoxInteractor::SetAccessor::set_from_original_print_value(
    const std::string& key
)
{
    m_observable_list.lock()->set_from_original_print_value(key);
}

void PrintToolConfigBoxInteractor::SetAccessor::set_from_original_tool_value(
    const std::string& key,
    size_t index
)
{
    m_observable_list.lock()->set_from_original_tool_value(key, index);
}

} // namespace Slic3r::Biz
