#pragma once

#include "Slic3r/Domain/SelectionId.hpp"

#include "Slic3r/Biz/IListObserver.hpp"
#include "Slic3r/Biz/ConfigBoxObservableList.hpp"
#include <Slic3r/Biz/ConfigItemContext.hpp>

#include <vector>
#include <string>
#include <memory>

namespace Slic3r::Domain {
struct ConfigBox;
struct ConfigValue;
class Workbench;
} // namespace Slic3r::Domain

namespace Slic3r::Domain::Preset {
struct SelectedPreset;
} // namespace Slic3r::Domain::Preset

namespace Slic3r::Biz::Scene {
class SceneInteractor;
} // namespace Slic3r::Biz::Scene

namespace Slic3r::Biz {

class PrintToolConfigObservableList;

class PrintToolConfigBoxInteractor : public IListObserver<Biz::ConfigItemContext>
{
public:
    class SetAccessor
    {
    public:
        void set_source(std::weak_ptr<PrintToolConfigObservableList> observable_list);

        void set_print_value(const std::string& key, const Domain::ConfigValue& value);

        void set_tool_override(const std::string& key, size_t index, bool override);

        void set_tool_value(
            const std::string& key,
            const std::vector<size_t>& indexes,
            const Domain::ConfigValue& value
        );

        void set_sources(
            const Domain::SelectionId selected_project_id,
            const Domain::SelectionId selected_container_id,
            Domain::Preset::SelectedPreset& selected_preset,
            const std::vector<Domain::ConfigBox*>& tool_config_boxes,
            const Domain::ConfigBox* original_print_config_box,
            const std::vector<const Domain::ConfigBox*>& original_tool_config_boxes
        );

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
        std::weak_ptr<PrintToolConfigObservableList> m_observable_list;
    };

    explicit PrintToolConfigBoxInteractor(
        const Domain::Workbench& workbench,
        Scene::SceneInteractor& scene_interactor,
        SetAccessor& set_accessor
    );

    const Domain::ConfigValue* find_print_value(const std::string& name) const;

    const Domain::ConfigValue* find_tool_value(const std::string& name, size_t index) const;

    std::weak_ptr<PrintToolConfigObservableList> observable_list();

    std::weak_ptr<const PrintToolConfigObservableList> observable_list() const;

    void set_app_config_cbol(std::weak_ptr<ConfigBoxObservableList> app_config_cbol);

    // Whenewer AppConfig invokes reset (probably never)
    void on_reset() override;
    // Whenever AppConfig has updated value(s)
    void on_updated(const IndexRange&) override;

private:
    void update_favorites();

private:
    Biz::UnsharedPointer<PrintToolConfigObservableList> m_observable_list;
    std::weak_ptr<ConfigBoxObservableList> m_app_config_cbol;
};

} // namespace Slic3r::Biz
