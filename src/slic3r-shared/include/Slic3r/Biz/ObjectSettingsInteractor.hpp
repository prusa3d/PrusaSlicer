#pragma once

#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Biz/ObjectSettingsObservableList.hpp"
#include "Slic3r/Biz/Platform/ListenerScope.hpp"

namespace Slic3r::Biz {

class IConfigBoxSetter;

namespace Scene {
class SceneInteractor;
} // namespace Scene

class ObjectSettingsInteractor :
    public Scene::ISceneSelectionChangedListener,
    public ISelectedConfigContainerChangedListener,
    public Preset::IPresetChangedListener,
    public Scene::ISceneChangedListener
{
public:
    class SetAccessor
    {
    public:
        void set_source(std::weak_ptr<ObjectSettingsObservableList> object_observable_list);

        void set_value(const std::string& key, const Domain::ConfigValue& value);
        void set_override(const std::string& key, bool enable);
        const Domain::ConfigValue*
        find_object_value(const std::string& key, size_t index = 0) const;

    private:
        std::weak_ptr<ObjectSettingsObservableList> m_object_observable_list;
    };

    explicit ObjectSettingsInteractor(
        SetAccessor& set_accessor,
        Domain::Workbench& workbench,
        Scene::SceneInteractor& scene_interactor,
        IConfigBoxSetter& preset_interactor
    );
    ~ObjectSettingsInteractor();
    ObjectSettingsInteractor(ObjectSettingsInteractor&&) = default;

    std::weak_ptr<ObjectSettingsObservableList> object_observable_list() const;

    void on_scene_selection_changed(
        Domain::SelectionId project_id,
        const Scene::ObjectSelection& selection
    ) override;

    void on_selected_config_container_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId container_id
    ) override;

    void on_preset_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        Preset::PresetItemType type
    ) override;

    void on_model_reloaded(Domain::SelectionId project_id) override;

    void on_instances_last_bed_updated(const Domain::ElementRefs& updated_instances) override;

private:
    void update_sources();
    void update_settings_from_original(Domain::ConfigBox& object_settings);

private:
    Domain::Workbench& m_workbench;
    Scene::SceneInteractor& m_scene_interactor;
    IConfigBoxSetter& m_preset_interactor;

    ListenerScope<
        Scene::ISceneSelectionChangedListener,
        Biz::Scene::SceneInteractor,
        ObjectSettingsInteractor>
        m_scene_selection_listener_scope;

    ListenerScope<
        Scene::ISceneChangedListener,
        Biz::Scene::SceneInteractor,
        ObjectSettingsInteractor>
        m_scene_changed_listener_scope;

    Domain::PrinterTechnology m_current_print_technology = Domain::PrinterTechnology::FFF;
    Scene::ObjectSelection m_current_selection;
    Domain::SelectionId m_project_id = Domain::INVALID_ID;
    UnsharedPointer<ObjectSettingsObservableList> m_object_observable_list;
};

} // namespace Slic3r::Biz
