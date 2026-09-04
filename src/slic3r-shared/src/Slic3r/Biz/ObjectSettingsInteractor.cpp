#include "Slic3r/Biz/ObjectSettingsInteractor.hpp"
#include "Slic3r/Biz/IConfigBoxSetter.hpp"

#include "Slic3r/Biz/Scene/SceneInteractor.hpp"

#include <unordered_set>

namespace Slic3r::Biz {

ObjectSettingsInteractor::ObjectSettingsInteractor(
    SetAccessor& set_accessor,
    Domain::Workbench& workbench,
    Scene::SceneInteractor& scene_interactor,
    IConfigBoxSetter& preset_interactor
) :
    m_workbench(workbench),
    m_scene_interactor(scene_interactor),
    m_preset_interactor(preset_interactor),
    m_scene_selection_listener_scope(scene_interactor, *this),
    m_scene_changed_listener_scope(scene_interactor, *this),
    m_object_observable_list(std::make_shared<ObjectSettingsObservableList>(m_scene_interactor))
{
    set_accessor.set_source(m_object_observable_list.get());
}

ObjectSettingsInteractor::~ObjectSettingsInteractor() = default;

std::weak_ptr<ObjectSettingsObservableList> ObjectSettingsInteractor::object_observable_list() const
{
    return m_object_observable_list.get();
}

void ObjectSettingsInteractor::on_scene_selection_changed(
    Domain::SelectionId project_id,
    const Scene::ObjectSelection& selection
)
{
    m_current_selection = selection;
    m_project_id        = project_id;

    update_sources();
}

void ObjectSettingsInteractor::on_selected_config_container_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId container_id
)
{
    Domain::Project& project = m_workbench.project(project_id);
    Domain::PrinterTechnology print_technology =
        project.find_config_container(container_id)->print_technology();

    m_current_print_technology = print_technology;
    m_project_id               = project_id;
    m_current_selection        = m_scene_interactor.object_selection();
    update_sources();
}

void ObjectSettingsInteractor::on_preset_selection_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    Preset::PresetItemType type
)
{
    if (m_project_id == project_id) {
        Domain::Project& project = m_workbench.project(project_id);
        Domain::PrinterTechnology print_technology =
            project.find_config_container(config_container_id)->print_technology();

        m_current_print_technology = print_technology;

        update_sources();
    }
}

void ObjectSettingsInteractor::on_model_reloaded(Domain::SelectionId project_id)
{
    if (m_project_id == project_id) {
        update_sources();
    }
}

void ObjectSettingsInteractor::on_instances_last_bed_updated(
    const Domain::ElementRefs& updated_instances
)
{
    if (m_current_selection.mode == Scene::SelectionMode::Instance
        && std::any_of(
            updated_instances.cbegin(),
            updated_instances.cend(),
            [this](const Domain::ElementRef& ref) { return m_current_selection.is_selected(ref); }
        ))
    {
        update_sources();
    }
}

void ObjectSettingsInteractor::update_sources()
{
    std::vector<Domain::ConfigBox*> sources;

    if (m_current_selection.is_valid() && !m_current_selection.empty()) {
        Domain::Project& project = m_workbench.project(m_project_id);

        if (m_current_selection.mode == Scene::SelectionMode::Instance) {
            m_object_observable_list->set_object_settings_source(nullptr);
            for (const Domain::ElementRef& ref : std::as_const(m_current_selection.elements)) {
                if (ref.is_wipe_tower()) {
                    continue;
                }
                Domain::ModelInstance* model_instance =
                    project.find_instance_by_id(ref.object_id, ref.instance_id);

                Domain::ConfigContainer* config_container = project.find_config_container(
                    model_instance->get_last_bed().config_container_id
                );
                Domain::PrinterTechnology technology = config_container ?
                    config_container->print_technology() :
                    m_current_print_technology;

                if (technology != m_current_print_technology) {
                    // We do not allow to modify overrides for not current technology
                    // (preset interactor does not allow this)
                    continue;
                }

                Domain::ModelObject* model_object = project.find_object_by_id(ref.object_id);
                if (m_current_print_technology == Domain::PrinterTechnology::FFF) {
                    update_settings_from_original(model_object->object_settings);
                    sources.push_back(&model_object->object_settings);
                } else {
                    update_settings_from_original(model_object->object_settings_sla);
                    sources.push_back(&model_object->object_settings_sla);
                }
            }
        } else if (
            m_current_selection.mode == Scene::SelectionMode::Volume
            && m_current_print_technology == Domain::PrinterTechnology::FFF
        )
        {
            Domain::ModelObject* model_object =
                project.find_object_by_id(m_current_selection.elements.front().object_id);
            update_settings_from_original(model_object->object_settings);

            m_object_observable_list->set_object_settings_source(&model_object->object_settings);

            for (const Domain::ElementRef& ref : std::as_const(m_current_selection.elements)) {
                Domain::ModelVolume* model_volume =
                    project.find_volume_by_id(ref.object_id, ref.volume_id);
                update_settings_from_original(model_volume->volume_settings);
                sources.push_back(&model_volume->volume_settings);
            }
        }
    }

    m_object_observable_list->set_sources(sources);
}

void ObjectSettingsInteractor::update_settings_from_original(Domain::ConfigBox& settings)
{
    for (Domain::ConfigItem& item : settings.overrides.all_items()) {
        if (!settings.overrides.get(item.name()).has_value()) {
            // Restore original values only for items without overrides.
            if (const Domain::ConfigValue* value =
                    m_preset_interactor.get_override_original_value(item, 0))
            {
                item.set(*value);
            }
        }
    }
}

void ObjectSettingsInteractor::SetAccessor::set_source(
    std::weak_ptr<ObjectSettingsObservableList> object_observable_list
)
{
    m_object_observable_list = object_observable_list;
}

void ObjectSettingsInteractor::SetAccessor::set_value(
    const std::string& key,
    const Domain::ConfigValue& value
)
{
    m_object_observable_list.lock()->set_value(key, value);
}

void ObjectSettingsInteractor::SetAccessor::set_override(const std::string& key, bool enable)
{
    m_object_observable_list.lock()->set_override(key, enable);
}

const Domain::ConfigValue*
ObjectSettingsInteractor::SetAccessor::find_object_value(const std::string& key, size_t index) const
{
    return m_object_observable_list.lock()->find_object_value(key, index);
}

} // namespace Slic3r::Biz
