#pragma once

#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/IListObserver.hpp"
#include "Slic3r/Biz/IVirtualExtrudersChangedListener.hpp"
#include "Slic3r/Biz/VirtualExtruderInteractor.hpp"

#include <Slic3r/Biz/Platform/ListenerScope.hpp>

#include "Slic3r/App/Yoga/ComboBox.hpp"
#include "Slic3r/App/Config/ConfigItemControl.hpp"

#include <vector>

namespace Slic3r::Biz {
class IConfigBoxSetter;
} // namespace Slic3r::Biz

namespace Slic3r::App {

class ConfigItemExtruderSelection :
    public ConfigItemControl,
    public Yoga::ComboBox,
    public Biz::Preset::IPresetChangedListener,
    public Biz::IVirtualExtrudersChangedListener
{
public:
    ConfigItemExtruderSelection(
        size_t index,
        const Domain::ConfigItem& config_item,
        Biz::IConfigBoxSetter& cb_setter,
        std::vector<size_t> cbi_index
    );

    void on_preset_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        Biz::Preset::PresetItemType type
    ) override;

    void on_virtual_extruders_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id
    ) override;

    void on_config_container_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id
    ) override;

protected:
    void update_size();

    void on_data_update() override;
    void update_value(const Domain::ConfigValue& value);

private:
    /**
     * @brief Extruder id of every declared item, indexed by the position of the item.
     */
    std::vector<int> m_extruder_ids;

    Biz::ListenerScope<
        Biz::Preset::IPresetChangedListener,
        Biz::Preset::PresetInteractor,
        ConfigItemExtruderSelection>
        m_preset_changed_scope;

    Biz::ListenerScope<
        Biz::IVirtualExtrudersChangedListener,
        Biz::VirtualExtruderInteractor,
        ConfigItemExtruderSelection>
        m_virtual_extruders_changed_scope;
};

} // namespace Slic3r::App
