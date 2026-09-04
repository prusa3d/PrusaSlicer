#pragma once

#include <Slic3r/Domain/ConfigDef.hpp>

#include "Slic3r/Biz/Platform/ListenerScope.hpp"
#include "Slic3r/Biz/PrintToolItem.hpp"
#include "Slic3r/Biz/IObservableList.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Biz/ISelectedBedInstanceChangedListener.hpp"

namespace Slic3r::Domain::Preset {
struct SelectedPreset;
} // namespace Slic3r::Domain::Preset

namespace Slic3r::Biz {

class PrintToolConfigObservableList :
    public IObservableList<PrintToolItem>,
    public Scene::ISceneBedInstanceChangedListener,
    public ISelectedBedInstancesChangedListener
{
public:
    explicit PrintToolConfigObservableList(
        const Domain::Workbench& workbench,
        Scene::SceneInteractor& scene_interactor
    );

    const PrintToolItem& at(size_t index) const override;

    size_t size() const override;

    /**
     * @note this will either invoke on_reset or just data_changed
     * in order to decide which, we want to update all sources at the same time
     */
    void set_sources(
        const Domain::SelectionId selected_project_id,
        const Domain::SelectionId selected_container_id,
        Domain::Preset::SelectedPreset& selected_preset,
        const std::vector<Domain::ConfigBox*>& tool_config_boxes,
        const Domain::ConfigBox* original_print_config_box,
        const std::vector<const Domain::ConfigBox*>& original_tool_config_boxes
    );

    void set_print_value(const std::string& key, const Domain::ConfigValue& value);

    void set_tool_override(const std::string& key, size_t index, bool override);

    void set_tool_value(
        const std::string& key,
        const std::vector<size_t>& indexes,
        const Domain::ConfigValue& value
    );

    const Domain::ConfigValue* find_print_value(const std::string& name) const;

    const Domain::ConfigValue* find_tool_value(const std::string& name, size_t index) const;

    void on_bed_instance_extruder_candidates_changed(
        Domain::SelectionId project_id,
        Domain::BedRef instance,
        const std::vector<unsigned int>& extruder_candidates
    ) override;

    void on_selected_bed_instances_changed(
        Domain::SelectionId project_id,
        const Scene::BedSelection& bed_selection
    ) override;

    void set_favorites(const std::vector<std::string>& favorites);

    /**
     * @brief Check if a specific configuration key has been modified from its original value
     * @param key Configuration item key to check
     * @return true if the key's value differs from the original, false otherwise
     */
    bool is_dirty(const std::string& key) const;

    /**
     * @brief Check if a print-level configuration key has been modified
     * @param key Print configuration item key to check
     * @return true if the print key's value differs from the original, false otherwise
     */
    bool is_dirty_print(const std::string& key) const;

    /**
     * @brief Check if a tool-specific configuration key has been modified
     * @param key Tool configuration item key to check
     * @param index Tool index to check
     * @return true if the tool key's value differs from the original, false otherwise
     */
    bool is_dirty_tool(const std::string& key, size_t index) const;

    /**
     * @brief Check if any configuration value has been modified
     * @return true if any print or tool configuration differs from the original, false otherwise
     */
    bool is_dirty() const;

    /**
     * @brief Check if any print-level configuration has been modified
     * @return true if any print configuration differs from the original, false otherwise
     */
    bool is_dirty_print() const;

    /**
     * @brief Check if any configuration for a specific tool has been modified
     * @param index Tool index to check
     * @return true if any configuration for the specified tool differs from the original, false otherwise
     */
    bool is_dirty_tool(size_t index) const;

    void set_from_original_value(const std::string& key);

    /**
     * @brief Reset a print-level configuration value to its original value from the preset
     * @param key Print configuration item key to reset
     */
    void set_from_original_print_value(const std::string& key);

    /**
     * @brief Reset a tool-specific configuration value to its original value from the preset
     * @param key Tool configuration item key to reset
     * @param index Tool index to reset the value for
     */
    void set_from_original_tool_value(const std::string& key, size_t index);

private:
    using PrintToolItems = std::vector<PrintToolItem>;

    PrintToolItems::iterator find_item(const std::string& name);

    void update_items();
    void update_extruders();

private:
    const Domain::Workbench& m_workbench;
    Scene::SceneInteractor& m_scene_interactor;

    ListenerScope<
        Scene::ISceneBedInstanceChangedListener,
        Scene::SceneInteractor,
        PrintToolConfigObservableList>
        m_scene_bed_changed_listener_scope;
    ListenerScope<
        ISelectedBedInstancesChangedListener,
        Scene::SceneInteractor,
        PrintToolConfigObservableList>
        m_selected_bed_changed_listener_scope;

    Domain::SelectionId m_selected_project_id   = Domain::INVALID_ID;
    Domain::SelectionId m_selected_container_id = Domain::INVALID_ID;
    Domain::ConfigBox* m_print_config_box{nullptr};
    std::vector<Domain::ConfigBox*> m_tool_config_boxes;
    const Domain::ConfigBox* m_original_print_config_box{nullptr};
    std::vector<const Domain::ConfigBox*> m_original_tool_config_boxes;
    PrintToolItems m_items;
    std::set<unsigned> m_extruder_candidates;
    PrintToolItem::SharedContext m_print_tool_shared_context;

    std::set<std::string> m_favorites;
};

} // namespace Slic3r::Biz
