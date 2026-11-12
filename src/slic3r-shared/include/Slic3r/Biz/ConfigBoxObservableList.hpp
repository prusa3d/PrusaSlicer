#pragma once

#include <Slic3r/Biz/ConfigItemContext.hpp>
#include <Slic3r/Biz/IObservableList.hpp>
#include "Slic3r/Biz/ISelectedBedInstanceChangedListener.hpp"
#include "Slic3r/Biz/Slicing/SlicingInteractor.hpp"

namespace Slic3r::Biz {

namespace Slicing {
class SlicingInteractor;
} // namespace Slicing

namespace Scene {
class SceneInteractor;
}

class ConfigBoxObservableList :
    public IObservableList<ConfigItemContext>,
    public ISelectedBedInstancesChangedListener,
    public Slicing::IBackgroundProcessStateListener
{
public:
    ConfigBoxObservableList(
        Scene::SceneInteractor* scene_interctor,
        Slicing::SlicingInteractor* slicing_interactor);

    ~ConfigBoxObservableList();

    void set_config_box(
        Domain::ConfigBox* observable_config_box,
        const Domain::ConfigBox* original_config_box
    );

    /**
     * @brief Points the items to the config items of @p original_config_box without resetting
     * the list, the observers get only an update of all items.
     * @note Intended for the case when only the original config box was re-created (e.g. after
     * the presets were reloaded). Falls back to set_config_box() if the items of the observed
     * config box have changed since the last set_config_box().
     */
    void set_original_config_box(const Domain::ConfigBox* original_config_box);

    const ConfigItemContext& at(size_t index) const override;

    size_t size() const override;

    void set_value(const std::string_view key, const Domain::ConfigValue& value);

    const Domain::ConfigValue* find(const std::string& name) const;

    bool is_dirty(const std::string& key) const;
    bool is_dirty() const;

    void set_from_original_value(const std::string& key);

    void on_selected_bed_instances_changed(
        Domain::SelectionId project_id,
        const Scene::BedSelection& bed_selection) override;

    void on_background_process_state_changed(const Domain::SlicingId) override;

private:
    void update_effective_value_statuses(const Domain::SlicingId slicing_id);

    Domain::ConfigBox* m_config_box{nullptr};
    Scene::SceneInteractor* m_scene_interactor{nullptr};
    Slicing::SlicingInteractor* m_slicing_interactor{nullptr};

    using Items = std::vector<ConfigItemContext>;
    Items m_items;
};

} // namespace Slic3r::Biz
